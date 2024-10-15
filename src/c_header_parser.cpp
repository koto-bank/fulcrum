#include <iostream>
#include <map>
#include <optional>

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Support/Program.h>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/FrontendOptions.h>
#include <clang/Lex/LiteralSupport.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Tooling/Syntax/Tokens.h>

#include <fmt/format.h>

#include "assert.hpp"
#include "ast_nodes.hpp"
#include "c_header_parser.hpp"
#include "codegen_context.hpp"
#include "compiler.hpp"
#include "types.hpp"

namespace {
void disableUnusedCompilerOptions(clang::CompilerInvocation &ci) {
    ci.getFrontendOpts().DisableFree = false;
    ci.getLangOpts().CommentOpts.ParseAllComments = true;
    ci.getLangOpts().RetainCommentsFromSystemHeaders = true;

    // Disable "clang -verify" diagnostics
    ci.getDiagnosticOpts().VerifyDiagnostics = false;
    ci.getDiagnosticOpts().ShowColors = false;

    // Disable any dependency outputting, we don't want to generate files or write
    // to stdout/stderr.
    ci.getDependencyOutputOpts().ShowIncludesDest = clang::ShowIncludesDestination::None;
    ci.getDependencyOutputOpts().OutputFile.clear();
    ci.getDependencyOutputOpts().HeaderIncludeOutputFile.clear();
    ci.getDependencyOutputOpts().DOTOutputFile.clear();
    ci.getDependencyOutputOpts().ModuleDependencyOutputDir.clear();

    // Disable any pch generation/usage operations. Since serialized preamble
    // format is unstable, using an incompatible one might result in unexpected
    // behaviours, including crashes.

    ci.getPreprocessorOpts().PrecompiledPreambleBytes = {0, false};
    ci.getPreprocessorOpts().PCHThroughHeader.clear();
    ci.getPreprocessorOpts().PCHWithHdrStop = false;
    ci.getPreprocessorOpts().PCHWithHdrStopCreate = false;

    // Don't crash on `#pragma clang __debug parser_crash`
    ci.getPreprocessorOpts().DisablePragmaDebugCrash = true;

    // Always default to raw container format as we don't registry any other
    // and clang dies when faced with unknown formats.
    ci.getHeaderSearchOpts().ModuleFormat =
        clang::PCHContainerOperations().getRawReader().getFormats().front().str();

    ci.getFrontendOpts().Plugins.clear();
    ci.getFrontendOpts().AddPluginActions.clear();
    ci.getFrontendOpts().PluginArgs.clear();
    ci.getFrontendOpts().ProgramAction = clang::frontend::ParseSyntaxOnly;
    ci.getFrontendOpts().ActionName.clear();

    // These options mostly affect codegen, and aren't relevant to us.
    // And clang will die immediately when these files are not existed.
    // Disable these uninteresting options.
    ci.getLangOpts().NoSanitizeFiles.clear();
    ci.getLangOpts().XRayAttrListFiles.clear();
    ci.getLangOpts().ProfileListFiles.clear();
    ci.getLangOpts().XRayAlwaysInstrumentFiles.clear();
    ci.getLangOpts().XRayNeverInstrumentFiles.clear();
}

bool isInsideMainFile(clang::SourceLocation loc, const clang::SourceManager &sm) {
    if (!loc.isValid()) {
        return false;
    }

    clang::FileID fileId = sm.getFileID(sm.getExpansionLoc(loc));
    return fileId == sm.getMainFileID();
}

class DeclarationCollector : public clang::ASTConsumer {
public:
    DeclarationCollector(std::vector<clang::Decl *> &topLevelDecls)
        : topLevelDecls(topLevelDecls) {}

    bool HandleTopLevelDecl(clang::DeclGroupRef dg) override {
        for (clang::Decl *d : dg) {
            auto &sm = d->getASTContext().getSourceManager();
            if (!isInsideMainFile(d->getLocation(), sm)) {
                continue;
            }

            topLevelDecls.push_back(d);
        }
        return true;
    }

private:
    std::vector<clang::Decl *> &topLevelDecls;
};

class FrontendAction : public clang::SyntaxOnlyAction {
public:
    std::vector<clang::Decl *> takeTopLevelDecls() {
        return std::move(topLevelDecls);
    }

protected:
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &, llvm::StringRef) override {
        return std::make_unique<DeclarationCollector>(topLevelDecls);
    }

private:
    std::vector<clang::Decl *> topLevelDecls;
};

class CollectMacros : public clang::PPCallbacks {
public:
    CollectMacros(clang::Preprocessor &pp)
        : pp(pp) {}

    void MacroDefined(const clang::Token &macroName, const clang::MacroDirective *md) override {
        if (isInsideMainFile(macroName.getLocation(), pp.getSourceManager())) {
            add(macroName, md);
        }
    }

    using MacroDefinitions = std::vector<std::pair<std::string, const clang::MacroDirective *>>;

    MacroDefinitions takeMacroDefinitions() {
        return std::move(macroDefinitions);
    }

private:
    void add(const clang::Token &macroName, const clang::MacroDirective *md) {
        auto name = macroName.getIdentifierInfo()->getName();
        macroDefinitions.push_back({ name.str(), md });
    }

    MacroDefinitions macroDefinitions;
    clang::Preprocessor &pp;
};

uint32_t processParsedMacros(ASTTypeStorage &typeStorage, CModule &cModule, CollectMacros &macroCollector, clang::CompilerInstance &ci) {
    uint32_t macroCount = 0u;
    for (auto &[macroName, md] : macroCollector.takeMacroDefinitions()) {
        const auto *mi = md->getMacroInfo();
        if (mi->isObjectLike()) {
            const auto &toks = mi->tokens();

            for (auto &tok : toks) {
                if (!clang::tok::isStringLiteral(tok.getKind())) {
                    break;
                }

                // special case! all strings
                clang::StringLiteralParser sp(toks, ci.getPreprocessor());
                if (sp.hadError) {
                    std::cerr << "Failed to parse string value of macro define " << macroName << '\n';
                    break;
                }
                // TODO: wide strings, some other errors, idk, looks kinda brittle

                auto varVal = VariableDeclarationNode(std::move(macroName), typeStorage.getType<ASTNamedType>("str"));
                varVal.initialValue = std::make_unique<ConstantStringNode>(sp.GetString().str());
                cModule.globalVariables.push_back(std::move(varVal));
                macroCount++;

                goto nextToken;
            }

            if (toks.size() != 1) {
                continue; // TODO: support more complex macro evaluation
            }
            auto tok = toks[0];
            switch (tok.getKind()) {
            case clang::tok::numeric_constant: {
                auto &pp = ci.getPreprocessor();
                llvm::SmallString<8> buf;
                bool numberInvalid = false;
                llvm::StringRef spelling = pp.getSpelling(tok, buf, &numberInvalid);
                if (numberInvalid) {
                    continue;
                }
                clang::NumericLiteralParser litParser(spelling,
                                                      tok.getLocation(),
                                                      pp.getSourceManager(),
                                                      pp.getLangOpts(),
                                                      pp.getTargetInfo(),
                                                      pp.getDiagnostics());
                if (litParser.hadError) {
                    std::cerr << "Failed to parse numeric value of macro define " << macroName <<  "\n";
                    continue;
                }
                if (litParser.isIntegerLiteral()) {
                    llvm::APInt val(64, 0);
                    litParser.GetIntegerValue(val);
                    auto varVal = VariableDeclarationNode(std::move(macroName), typeStorage.getType<ASTIntegerType>(!litParser.isUnsigned, 64));
                    varVal.initialValue = std::make_unique<ConstantIntNode>(typeStorage.getType<ASTIntegerType>(!litParser.isUnsigned, 64), val.getLimitedValue());
                    cModule.globalVariables.push_back(std::move(varVal));
                    macroCount++;
                } else if (litParser.isFloatingLiteral()) {
                    llvm::APFloat val(0.0f);
                    litParser.GetFloatValue(val);
                    // TODO: how to do double literals?
                    auto varVal = VariableDeclarationNode(std::move(macroName), typeStorage.getType<ASTFloatType>(32));
                    varVal.initialValue = std::make_unique<ConstantFloatNode>(typeStorage.getType<ASTFloatType>(32), val.convertToFloat());
                    cModule.globalVariables.push_back(std::move(varVal));
                    macroCount++;
                }

                break;
            }
            default:
                // unsupported, skipping
                break;
            }
        }
        nextToken:
    }
    return macroCount;
}
}

bool HeaderParser::parseHeader(const std::string &path, const Compiler &compiler) {
    clang::CreateInvocationOptions ciOpts;
    ciOpts.VFS = nullptr;
    ciOpts.CC1Args = nullptr;
    ciOpts.RecoverOnError = true;

    ciOpts.Diags = nullptr; // TODO: implement our diagnostics consumer,
    // because the docs say: Receives diagnostics encountered while parsing command-line flags.
    //                       If not provided, these are printed to stderr.
    // clang::CompilerInstance::createDiagnostics(new clang::DiagnosticOptions, &OurDerivedDiagnosticsConsumer, true);
    ciOpts.ProbePrecompiled = false;

    std::vector<const char *> argStrs = { "clang" };
    std::vector<std::string> includes;
    for (auto &include : compiler.includeDirectories) {
        includes.push_back(fmt::format("-I{}", include.string()));
        argStrs.push_back(includes.back().c_str());
    }
    argStrs.push_back(path.c_str());
    std::unique_ptr<clang::CompilerInvocation> ci = clang::createInvocation(argStrs, ciOpts);

    disableUnusedCompilerOptions(*ci);

    // Read file to be processed
    auto fs = llvm::vfs::createPhysicalFileSystem();
    auto contents = fs->getBufferForFile(path).get()->getBuffer().str();
    auto buffer = llvm::MemoryBuffer::getMemBufferCopy(contents, path);

    ci->getPreprocessorOpts().addRemappedFile(
        ci->getFrontendOpts().Inputs[0].getFile(), buffer.get());

    if (ci->getFrontendOpts().Inputs.size() > 0) {
        auto lang = ci->getFrontendOpts().Inputs[0].getKind().getLanguage();
        if (lang != clang::Language::C) {
            std::cout << "Fulcrum only supports C includes\n";
            return false;
        }
    }

    std::unique_ptr<clang::CompilerInstance> clang = std::make_unique<clang::CompilerInstance>();
    clang::IgnoringDiagConsumer dropDiags; // we probably _should_ report malformed headers

    clang->setInvocation(std::move(ci));
    clang->createDiagnostics(&dropDiags, false);
    clang->createFileManager();
    clang->createTarget();

    auto action = std::make_unique<FrontendAction>();
    const clang::FrontendInputFile &mainInput = clang->getFrontendOpts().Inputs[0];
    if (!action->BeginSourceFile(*clang, mainInput)) {
        std::cout << fmt::format("BeginSourceFile() failed when building AST for {}\n",
                                 std::string(mainInput.getFile()));
        return false;
    }

    auto &pp = clang->getPreprocessor();
    auto macroCollector = std::make_unique<CollectMacros>(pp);
    auto *macroCollectorPtr = macroCollector.get();
    pp.addPPCallbacks(std::move(macroCollector));

    if (llvm::Error err = action->Execute()) {
        std::cout << fmt::format("Execute() failed when building AST for {}: {}\n",
                                 std::string(mainInput.getFile()),
                                 llvm::toString(std::move(err)));
    }

    auto macroNum = processParsedMacros(cModule.types, cModule, *macroCollectorPtr, *clang);

    std::vector<clang::Decl *> parsedDecls = action->takeTopLevelDecls();
    std::cout << fmt::format("Parsed {} decls from file {}\n", parsedDecls.size() + macroNum,  path);
    for (auto d : parsedDecls) {
        if (const clang::TypedefDecl *td = llvm::dyn_cast<clang::TypedefDecl>(d)) {
            std::cout << "Typedef  " << td->getDeclName().getAsString() << std::endl;
        } else if (const clang::RecordDecl *rd = llvm::dyn_cast<clang::RecordDecl>(d)) {
            // struct/class
            std::cout << "Record  " << rd->getDeclName().getAsString() << std::endl;
        } else if (const clang::EnumDecl *ed = llvm::dyn_cast<clang::EnumDecl>(d)) {
            std::cout << "Enum  " << ed->getDeclName().getAsString() << std::endl;
            // enum
            // also need to collect corresponding EnumConstantDecl to fill values
        } else if (const clang::EnumConstantDecl *ecd = llvm::dyn_cast<clang::EnumConstantDecl>(d)) {
            auto tt = (clang::TagType *)ecd->getType().getTypePtr();
            std::cout << "Enum  " << tt->getDecl()->getDeclName().getAsString() << " " << ecd->getDeclName().getAsString() << " " << ecd->getValue().getLimitedValue() << std::endl;
        } else if (const clang::FunctionDecl *fd = llvm::dyn_cast<clang::FunctionDecl>(d)) {
            // function
            std::cout << "Fn  " << fd->getDeclName().getAsString() << std::endl;
        } else if (const clang::VarDecl *vd = llvm::dyn_cast<clang::VarDecl>(d)) {
            // global var
            std::cout << "Var  " << vd->getDeclName().getAsString() << std::endl;
        } else {
            // ignore
        }
    }

    (void) buffer.release(); // TODO: do we really need to do this?
    return true;
}

CModule HeaderParser::takeModule() {
    return std::move(cModule);
}

HeaderParser::~HeaderParser() = default;
