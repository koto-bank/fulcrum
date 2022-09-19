#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>

#include <llvm/MC/TargetRegistry.h>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>

#include <clang-c/Index.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Interpreter/Interpreter.h>

#include "fmt/color.h"

#include "args.hxx"

#include <algorithm>
#include <concepts>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "codegen_context.hpp"
#include "expressions.hpp"
#include "parse_context.hpp"
#include "types.hpp"

std::string cxToString(CXString &&str) {
    auto res = std::string((char *)str.data);
    clang_disposeString(str);

    return res;
}

struct ParseHeaderContext {
    std::unique_ptr<ModuleNode> module;
    CodegenContext &codegenContext;

    ParseHeaderContext(std::string name, CodegenContext &parentContext)
        : codegenContext(parentContext),
          interp(createClangInterpreter()) {
        module = std::make_unique<ModuleNode>(name);
    }

    std::string getAnonName(CXCursor cur) {
        auto usr = cxToString(clang_getCursorUSR(cur));
        if (anonymousNumbers.contains(usr)) {
            auto n = anonymousNumbers.size();
            anonymousNumbers.emplace(usr, n);
        }

        return fmt::format("anon{}", anonymousNumbers[usr]);
    }

    void includeHeader(const std::string &name) {
        auto tuOrErr = interp->Parse(std::string(fmt::format(R"(#include <{}>)", name)));
        auto isOk = bool(tuOrErr);
        if (!isOk) return;
        llvm::handleAllErrors(interp->Execute(tuOrErr.get()));
    }

    std::unique_ptr<VariableDeclarationNode> evalMacro(const std::string &name) {
        auto varName = fmt::format("macro_{}", name);
        auto tuOrErr
            = interp->Parse(std::string(fmt::format(R"(extern "C" constexpr decltype(auto) {} = {};)", varName, name)));
        auto isOk = bool(tuOrErr);
        if (!isOk) return nullptr;

        clang::VarDecl *decl = nullptr;
        for (auto toplevel : tuOrErr->TUPart->decls()) {
            if (llvm::isa<clang::LinkageSpecDecl>(toplevel)) {
                auto linkageSpec = llvm::dyn_cast<clang::LinkageSpecDecl>(toplevel);
                for (auto varDecl_ : linkageSpec->decls()) {
                    if (llvm::isa<clang::VarDecl>(varDecl_)) {
                        decl = llvm::dyn_cast<clang::VarDecl>(varDecl_);
                        goto doneSearching;
                    }
                }
            }
        }
    doneSearching:
        if (decl == nullptr) tuOrErr->TUPart->dump();
        auto declType = decl->getType();

        auto llvmVar = tuOrErr->TheModule.get()->getGlobalVariable(varName);
        auto varDef = std::make_unique<VariableDeclarationNode>(name);
        if (declType->isIntegerType()) {
            auto varType = llvmVar->getValueType();

            bool isSigned = declType->isSignedIntegerType();
            auto valType = ASTBuiltinType(fmt::format("{}{}", isSigned ? "i" : "u", varType->getIntegerBitWidth()));

            auto initializer = llvm::dyn_cast<llvm::ConstantInt>(llvmVar->getInitializer());

            varDef->type = std::make_unique<ASTBuiltinType>("i64");
            if (isSigned)
                varDef->initialValue = std::make_unique<ConstantIntNode>(valType, initializer->getSExtValue());
            else
                varDef->initialValue = std::make_unique<ConstantIntNode>(valType, initializer->getZExtValue());
            return varDef;
        } else if (llvmVar->getType()->isArrayTy()) {
            auto varInitializer = llvm::dyn_cast<llvm::ConstantDataArray>(llvmVar->getInitializer());

            varDef->type = std::make_unique<ASTBuiltinType>("str");
            varDef->initialValue = std::make_unique<ConstantStringNode>(varInitializer->getAsString().str());
        } else {
            return nullptr;
        }

        llvm::handleAllErrors(interp->Execute(tuOrErr.get()));

        return varDef;
    }

private:
    std::unique_ptr<clang::Interpreter> interp;
    std::map<std::string, int> anonymousNumbers;

    static std::unique_ptr<clang::Interpreter> createClangInterpreter() {
        std::string clangPath = llvm::sys::findProgramByName("clang").get();
        std::vector<llvm::StringRef> PrintResourceDirArgs{ clangPath, "-print-resource-dir" };
        llvm::SmallString<64> OutputFile;
        llvm::sys::fs::createTemporaryFile("print-resource-dir-output", "", OutputFile);
        llvm::FileRemover OutputRemover(OutputFile.c_str());
        llvm::Optional<llvm::StringRef> Redirects[] = { llvm::None, llvm::StringRef(OutputFile), llvm::None };
        llvm::sys::ExecuteAndWait(clangPath, PrintResourceDirArgs, {}, Redirects);

        auto OutputBuf = llvm::MemoryBuffer::getFile(OutputFile.c_str());
        llvm::StringRef Output = OutputBuf.get()->getBuffer().rtrim('\n');
        auto clangInc = fmt::format("-I{}/include", Output);

        std::vector<const char *> clangArgs{ "-Xclang", "-emit-llvm-only", clangInc.data() };

        auto instOrErr = clang::IncrementalCompilerBuilder::create(clangArgs);
        auto inst = std::move(instOrErr.get());

        auto ignoring = std::make_unique<clang::IgnoringDiagConsumer>();
        inst->getDiagnostics().setClient(ignoring.release(), true);

        std::unique_ptr<clang::Interpreter> interp;
        llvm::handleAllErrors(clang::Interpreter::create(std::move(inst)).moveInto(interp));

        return interp;
    }
};

std::unique_ptr<ASTType> clangToASTType(ParseHeaderContext &context, CXType clangTp) {
    auto qualType = clang::QualType::getFromOpaquePtr(clangTp.data[0]);

    // FIXME: this is very bad
    qualType = qualType.getAtomicUnqualifiedType();
    auto typeName = qualType.getAsString();
    if (qualType.getTypePtr()->isElaboratedTypeSpecifier()) {
        auto prevName = typeName;

        typeName = qualType.getTypePtr()->getAsTagDecl()->getDeclName().getAsString();
        if (typeName == "") typeName = prevName;
    }

    std::unique_ptr<ASTType> result;
    switch (clangTp.kind) {
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Short:
    case CXType_Int128: {
        auto size = clang_Type_getSizeOf(clangTp) * 8;
        auto signature = "i" + std::to_string(size);
        context.codegenContext.ensureType<IntegerType>(signature, size, true);

        result = std::make_unique<ASTBuiltinType>(signature);
        break;
    }
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
    case CXType_UShort:
    case CXType_UInt128: {
        auto size = clang_Type_getSizeOf(clangTp) * 8;
        auto signature = "u" + std::to_string(size);
        context.codegenContext.ensureType<IntegerType>(signature, size, false);

        result = std::make_unique<ASTBuiltinType>(signature);
        break;
    }
    case CXType_Float:
        result = std::make_unique<ASTBuiltinType>("f32");
        break;
    case CXType_Double:
        result = std::make_unique<ASTBuiltinType>("f64");
        break;
    case CXType_SChar:
    case CXType_UChar:
    case CXType_Char_S:
    case CXType_Char_U:
        result = std::make_unique<ASTBuiltinType>("char");
        break;
    case CXType_Void:
        result = std::make_unique<ASTBuiltinType>("void");
        break;
    case CXType_Pointer:
    case CXType_ConstantArray:
    case CXType_IncompleteArray: {
        auto internalType
            = clangTp.kind == CXType_Pointer ? clang_getPointeeType(clangTp) : clang_getArrayElementType(clangTp);

        auto pointee = clangToASTType(context, internalType);
        if (pointee == nullptr) return nullptr;

        auto maybeBuiltin = dynamic_cast<ASTBuiltinType *>(pointee.get());
        if (maybeBuiltin != nullptr && maybeBuiltin->builtinName == "void") {
            // Replace void pointer with i8*

            context.codegenContext.ensureType<IntegerType>("i8", 8, true);
            pointee = std::make_unique<ASTBuiltinType>("i8");
        }

        result = std::make_unique<ASTPointerType>(std::move(pointee));
        break;
    }
    case CXType_Typedef: {
        auto aliasTo = clangToASTType(context, clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(clangTp)));
        if (aliasTo == nullptr) return nullptr;

        result = std::make_unique<ASTNamedType>(typeName);
        break;
    }
    case CXType_Elaborated:
    case CXType_Record:
    case CXType_Enum: {
        auto namedType = clangTp.kind == CXType_Elaborated ? clang_Type_getNamedType(clangTp) : clangTp;

        if (namedType.kind == CXType_Record) {
            auto decl = clang_getTypeDeclaration(namedType);
            if (clang_Cursor_isAnonymous(decl)) typeName = context.getAnonName(decl);

            result = std::make_unique<ASTNamedType>(typeName);
        } else if (namedType.kind == CXType_Enum) {
            auto enumDecl = clang_getTypeDeclaration(namedType);

            result = clangToASTType(context, clang_getEnumDeclIntegerType(enumDecl));
        } else {
            auto kindName = cxToString(clang_getTypeKindSpelling(namedType.kind));
            std::cout << "Unknown elaborate type: " << kindName << std::endl;

            return nullptr;
        }

        break;
    }
    case CXType_FunctionProto: {
        auto resType = clang_getResultType(clangTp);
        auto returnType = clangToASTType(context, resType);

        bool unknownType = returnType == nullptr;

        ASTFunctionType::Args arguments;
        for (auto i = 0; i < clang_getNumArgTypes(clangTp); i++) {
            auto argType = clang_getArgType(clangTp, i);

            auto langArgType = clangToASTType(context, argType);
            if (langArgType == nullptr) {
                unknownType = true;
                break;
            }

            arguments.emplace_back(std::move(langArgType));
        }
        if (unknownType) {
            std::cout << "Skipped " << typeName << std::endl;
            return nullptr;
        }
        result = std::make_unique<ASTFunctionType>(std::move(arguments), std::move(returnType));

        break;
    }
    default:
        std::cout << "Unknown type: " << cxToString(clang_getTypeSpelling(clangTp)) << " ";
        std::cout << "kind " << cxToString(clang_getTypeKindSpelling(clangTp.kind)) << std::endl;

        return nullptr;
    }

    return result;
}

std::unique_ptr<ParseHeaderContext> parseHeader(std::string path, CodegenContext &parentContext) {
    auto parseHeaderContext = std::make_unique<ParseHeaderContext>(path, parentContext);
    parseHeaderContext->includeHeader(path);

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index, path.data(), nullptr, 0, nullptr, 0, CXTranslationUnit_DetailedPreprocessingRecord
    );
    if (unit == nullptr) {
        std::cout << "Could not parse " << path;
        return nullptr;
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(
        cursor,
        [](CXCursor c, CXCursor /*parent*/, CXClientData client_data_) {
            auto parseHeaderContext = (ParseHeaderContext *)client_data_;
            auto cursorKind = clang_getCursorKind(c);

            switch (cursorKind) {
            case CXCursor_FunctionDecl: {
                auto funcName = cxToString(clang_getCursorSpelling(c));

                // Skip internal functions
                if (funcName.starts_with("__")) return CXChildVisit_Continue;

                if (std::find_if(
                        parseHeaderContext->module->functions.begin(), parseHeaderContext->module->functions.end(),
                        [&funcName](auto &node) { return node->name == funcName; }
                    )
                    != parseHeaderContext->module->functions.end())
                    break;

                // std::cout << "Function decl: " << funcName << std::endl;

                auto funcType = clang_getCursorType(c);
                [[maybe_unused]] bool variadic = clang_isFunctionTypeVariadic(funcType);

                auto resType = clang_getResultType(funcType);
                auto returnType = clangToASTType(*parseHeaderContext, resType);

                bool unknownType = returnType == nullptr;
                FunctionNode::Args arguments;

                for (auto i = 0; i < clang_getNumArgTypes(funcType); i++) {
                    auto argCursor = clang_Cursor_getArgument(c, i);

                    auto argName = cxToString(clang_getCursorSpelling(argCursor));

                    auto argType = clang_getCursorType(argCursor);
                    auto langArgType = clangToASTType(*parseHeaderContext, argType);
                    if (langArgType == nullptr) {
                        unknownType = true;
                        break;
                    }

                    arguments.emplace_back(argName, std::move(langArgType));
                }
                if (unknownType) {
                    std::cout << "Skipped " << funcName << std::endl;
                    return CXChildVisit_Continue;
                }

                parseHeaderContext->module->functions.push_back(std::make_unique<FunctionNode>(
                    funcName, std::move(arguments), std::move(returnType), FunctionNode::Body{}, true
                ));

                return CXChildVisit_Continue;
            }
            case CXCursor_MacroDefinition: {
                auto name = cxToString(clang_getCursorSpelling(c));

                if (clang_Cursor_isMacroFunctionLike(c)) return CXChildVisit_Continue;

                if (std::find_if(
                        parseHeaderContext->module->globalVariables.begin(),
                        parseHeaderContext->module->globalVariables.end(),
                        [&name](auto &node) { return node->name == name; }
                    )
                    != parseHeaderContext->module->globalVariables.end())
                    break;

                auto loc = clang_getCursorLocation(c);
                CXFile locFile;
                unsigned int file, col, offset;
                clang_getSpellingLocation(loc, &locFile, &file, &col, &offset);

                // Skip internals
                if (clang_getFileName(locFile).data == nullptr) return CXChildVisit_Continue;

                auto varVal = parseHeaderContext->evalMacro(name);
                if (varVal != nullptr) parseHeaderContext->module->globalVariables.push_back(std::move(varVal));

                break;
            }
            case CXCursor_TypedefDecl: {
                auto name = cxToString(clang_getCursorSpelling(c));

                if (std::find_if(
                        parseHeaderContext->module->structs.begin(), parseHeaderContext->module->structs.end(),
                        [&name](auto &node) { return node->name == name; }
                    )
                    != parseHeaderContext->module->structs.end())
                    break;

                auto aliasTo = clangToASTType(*parseHeaderContext, clang_getTypedefDeclUnderlyingType(c));
                if (aliasTo == nullptr) break;

                parseHeaderContext->module->aliases.push_back(std::make_unique<AliasNode>(name, std::move(aliasTo)));

                break;
            }
            case CXCursor_UnionDecl: {
                auto unionType = clang_getCursorType(c);

                auto unionName = cxToString(clang_getCursorDisplayName(c));
                if (unionName == "") unionName = cxToString(clang_getTypeSpelling(unionType));
                if (clang_Cursor_isAnonymous(c)) unionName = parseHeaderContext->getAnonName(c);

                if (std::find_if(
                        parseHeaderContext->module->structs.begin(), parseHeaderContext->module->structs.end(),
                        [&unionName](auto &node) { return node->name == unionName; }
                    )
                    != parseHeaderContext->module->structs.end())
                    break;

                struct VisitData {
                    ParseHeaderContext &parseHeaderContext;

                    long long biggestSize = 0;
                    CXType biggestType;
                };
                VisitData data = { .parseHeaderContext = *parseHeaderContext };
                clang_Type_visitFields(
                    unionType,
                    [](CXCursor cursor, CXClientData client_data) {
                        VisitData *visitData = (VisitData *)client_data;

                        auto type = clang_getCursorType(cursor);
                        auto typeSize = clang_Type_getSizeOf(type);
                        if (typeSize > visitData->biggestSize) {
                            visitData->biggestSize = typeSize;
                            visitData->biggestType = type;
                        }

                        return CXVisit_Continue;
                    },
                    &data
                );

                auto biggestType = clangToASTType(*parseHeaderContext, data.biggestType);

                StructNode::Fields fields;
                fields.emplace_back("anon", std::move(biggestType));

                parseHeaderContext->module->structs.emplace_back(
                    std::make_unique<StructNode>(unionName, std::move(fields), true)
                );

                break;
            }
            case CXCursor_StructDecl: {
                auto structType = clang_getCursorType(c);
                auto structName = cxToString(clang_getCursorDisplayName(c));
                if (structName == "") structName = cxToString(clang_getTypeSpelling(structType));

                if (std::find_if(
                        parseHeaderContext->module->structs.begin(), parseHeaderContext->module->structs.end(),
                        [&structName](auto &node) { return node->name == structName; }
                    )
                    != parseHeaderContext->module->structs.end())
                    break;

                struct VisitData {
                    ParseHeaderContext &parseHeaderContext;
                    StructNode::Fields fields;
                };
                VisitData data = { .parseHeaderContext = *parseHeaderContext };
                clang_Type_visitFields(
                    structType,
                    [](CXCursor cursor, CXClientData client_data) {
                        VisitData *visitData = (VisitData *)client_data;

                        auto type = clang_getCursorType(cursor);
                        auto fieldName = cxToString(clang_getCursorSpelling(cursor));

                        visitData->fields.emplace_back(fieldName, clangToASTType(visitData->parseHeaderContext, type));

                        return CXVisit_Continue;
                    },
                    &data
                );
                parseHeaderContext->module->structs.emplace_back(
                    std::make_unique<StructNode>(structName, std::move(data.fields), true)
                );

                break;
            }
            case CXCursor_EnumDecl: {
                auto namedType = clang_getCursorType(c);
                auto enumName = cxToString(clang_getTypeSpelling(namedType));

                auto enumIntType = clangToASTType(*parseHeaderContext, clang_getEnumDeclIntegerType(c));

                struct EnumParseContext {
                    std::string enumTypeName;
                    ParseHeaderContext &parseHeaderContext;
                };
                EnumParseContext parseContext = { .enumTypeName = ((ASTBuiltinType *)enumIntType.get())->builtinName,
                                                  .parseHeaderContext = *parseHeaderContext };

                clang_visitChildren(
                    c,
                    [](CXCursor c, CXCursor /*parent*/, CXClientData client_data_) {
                        EnumParseContext *parseContext = (EnumParseContext *)client_data_;

                        auto variantName = cxToString(clang_getCursorSpelling(c));
                        if (std::find_if(
                                parseContext->parseHeaderContext.module->globalVariables.begin(),
                                parseContext->parseHeaderContext.module->globalVariables.end(),
                                [&variantName](auto &node) { return node->name == variantName; }
                            )
                            != parseContext->parseHeaderContext.module->globalVariables.end())
                            return CXChildVisit_Break;

                        auto varDef = std::make_unique<VariableDeclarationNode>(variantName);
                        varDef->type = std::make_unique<ASTBuiltinType>(parseContext->enumTypeName);

                        if (parseContext->enumTypeName[0] == 'i')
                            varDef->initialValue = std::make_unique<ConstantIntNode>(
                                ASTBuiltinType(parseContext->enumTypeName), (int64_t)clang_getEnumConstantDeclValue(c)
                            );
                        else
                            varDef->initialValue = std::make_unique<ConstantIntNode>(
                                ASTBuiltinType(parseContext->enumTypeName),
                                (uint64_t)clang_getEnumConstantDeclUnsignedValue(c)
                            );
                        parseContext->parseHeaderContext.module->globalVariables.push_back(std::move(varDef));

                        return CXChildVisit_Continue;
                    },
                    &parseContext
                );
            }
            default:
                break;
            };


            return CXChildVisit_Recurse;
        },
        parseHeaderContext.get()
    );

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);

    return parseHeaderContext;
}

int main(int argc, char *argv[]) {
    args::ArgumentParser argParser("fulcrum");
    args::Positional<std::string> fileArg(argParser, "file", "The file to compile");
    args::ValueFlagList<std::string> includeArg(
        argParser, "path", "Directories to search modules in. Searched from last to first", { 'I', "include" }
    );
    args::HelpFlag helpArg(argParser, "help", "Display help", { 'h', "help" });
    try {
        argParser.ParseCLI(argc, argv);
    } catch (const args::Help &) {
        std::cout << argParser;
        return 0;
    } catch (const args::Error &e) {
        std::cerr << e.what() << std::endl;
        std::cerr << argParser;
        return 1;
    }

    llvm::LLVMContext context;
    llvm::IRBuilder<> builder(context);

    context.enableOpaquePointers();

    CodegenContext codegenCont("main", context);

    llvm::TargetMachine *targetMachine;
    {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();

        auto targetTriple = llvm::sys::getDefaultTargetTriple();
        std::string err;
        auto target = llvm::TargetRegistry::lookupTarget(targetTriple, err);
        if (!target) {
            llvm::errs() << err;
            return 1;
        }

        llvm::TargetOptions options;
        auto rm = llvm::Optional<llvm::Reloc::Model>();
        targetMachine = target->createTargetMachine(targetTriple, "generic", "", options, rm);
        codegenCont.module.setDataLayout(targetMachine->createDataLayout());
        codegenCont.module.setTargetTriple(targetTriple);
    }

    if (includeArg) {
        namespace fs = std::filesystem;
        for (auto &path : includeArg.Get()) {
            auto absPath = fs::absolute(fs::path(path));
            codegenCont.includeDirectories.push_back(absPath);
        }
    }

    std::unique_ptr<ModuleNode> moduleAST;
    if (!fileArg) {
        moduleAST = parse(&codegenCont, &std::cin);
    } else {
        std::ifstream file(fileArg.Get());
        if (!file.is_open()) {
            std::cout << fmt::format("Could not open {}", std::string(fileArg.Get()));
            return 1;
        }

        moduleAST = parse(&codegenCont, &file);
    }

    if (moduleAST == nullptr) {
        std::cout << "-----xxxxxx parsing failure xxxxxx-----\n";
        return 1;
    }

    std::map<std::string, std::unique_ptr<ModuleNode>> includedModules;
    std::function<void(std::vector<ModuleNode::Import>)> processImports
        = [&](std::vector<ModuleNode::Import> moduleImports) {
              for (auto &import : moduleImports) {
                  if (includedModules.contains(import.target)) continue;

                  if (std::find(import.keywords.begin(), import.keywords.end(), "c") != import.keywords.end()) {
                      auto resultContext = parseHeader(import.target, codegenCont);
                      includedModules.emplace(import.target, std::move(resultContext->module));
                  } else {
                      namespace fs = std::filesystem;

                      fs::path targetPath;
                      bool found = false;
                      for (auto inclDir = codegenCont.includeDirectories.rbegin();
                           inclDir != codegenCont.includeDirectories.rend(); inclDir++) {
                          fs::path includePath(*inclDir);
                          includePath.make_preferred();

                          if (!fs::is_directory(includePath.string())) {
                              std::cout << fmt::format(
                                  "Include path {} does not exist or is not a directory", includePath.string()
                              );
                              continue;
                          }

                          auto targetPathStr = import.target;
                          std::replace(targetPathStr.begin(), targetPathStr.end(), '.', fs::path::preferred_separator);
                          targetPath = includePath / targetPathStr;
                          targetPath.replace_extension(".fl");
                          if (fs::is_regular_file(targetPath)) {
                              found = true;
                              break;
                          }
                      }
                      if (!found) throw CodegenError(fmt::format("Could not find the module {}", import.target));

                      std::ifstream file(targetPath);
                      auto moduleAST = parse(&codegenCont, &file);
                      if (moduleAST == nullptr)
                          throw CodegenError(
                              fmt::format("Could not parse module {} ({})", import.target, targetPath.string())
                          );
                      if (moduleAST->name != import.target)
                          throw CodegenError(fmt::format(
                              "Module was imported as {}, but the name declared in the module was {}", import.target,
                              moduleAST->name
                          ));

                      auto &emplaced = includedModules.emplace(import.target, std::move(moduleAST)).first->second;
                      processImports(emplaced->imports);
                  }
              }
          };
    auto importNames = [&includedModules](ModuleNode *importTo) {
        for (auto &import : importTo->imports) {
            // TODO: actually add import names, for now everything is imported
            auto &importedAST = includedModules[import.target];
            for (auto &[basename, fullname] : importedAST->allNames()) {
                importTo->importName(basename, fullname);
            }
        }
    };

    processImports(moduleAST->imports);
    for (auto &[name, ast] : includedModules) {
        std::cout << fmt::format("Compiling {}", name) << std::endl;

        importNames(ast.get());
        ast->generate(codegenCont);
    }
    importNames(moduleAST.get());
    moduleAST->generate(codegenCont);

    ExpressionGenContext exprGenContext{ .builder = builder, .codegenContext = codegenCont };
    for (auto &[name, named] : codegenCont.names) {
        try {
            auto namedF = named->as<NamedFunctionValue>();
            if (namedF == nullptr) continue;
            auto f = namedF->value.get();

            exprGenContext.function = f;
            f->generateBody(exprGenContext);
        } catch (const CodegenError &err) {
            std::cout << fmt::format(
                "{}\n{}", fmt::styled("Errors:", fmt::fg(fmt::color::red) | fmt::emphasis::bold), err.whatIndented(4)
            ) << std::endl;

            return 1;
        }
    }

    llvm::errs() << codegenCont.module;

    if (llvm::verifyModule(codegenCont.module, &llvm::errs())) {
        // Exit early if there's an error
        return 1;
    }

    // Object file generation

    auto filename = "output.o";
    std::error_code EC;
    llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);

    llvm::legacy::PassManager passManager;
    targetMachine->addPassesToEmitFile(passManager, dest, nullptr, llvm::CGFT_ObjectFile);
    passManager.run(codegenCont.module);
    dest.flush();

    return 0;
}
