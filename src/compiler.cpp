#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>

#include <llvm/MC/TargetRegistry.h>

#include <llvm/Pass.h>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <fmt/color.h>

#include <args.hxx>

#include <algorithm>
#include <concepts>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "ast_type_storage.hpp"
#include "c_header_parser.hpp"
#include "c_module.hpp"
#include "codegen_context.hpp"
#include "compiler.hpp"
#include "expressions.hpp"
#include "parser.hpp"
#include "semantic_analyzer.hpp"
#include "types.hpp"

namespace {
std::optional<std::filesystem::path> tryToFindImport(const std::string &importName, Compiler& compiler) {
    namespace fs = std::filesystem;

    fs::path targetPath;
    bool found = false;
    for (auto inclDir = compiler.includeDirectories.rbegin();
         inclDir != compiler.includeDirectories.rend(); inclDir++) {
        fs::path includePath(*inclDir);
        includePath.make_preferred();

        if (!fs::is_directory(includePath.string())) {
            std::cout << fmt::format(
                "Include path '{}' does not exist or is not a directory\n", includePath.string()
                );
            continue;
        }

        targetPath = includePath / importName;
        if (fs::is_regular_file(targetPath)) {
            found = true;
            break;
        }
    }

    if (found) {
        return targetPath;
    } else {
        return std::nullopt;
    }
}

using ImportedModules = std::unordered_map<std::string, FulcrumModule>;

template <typename T>
bool importNamed(const std::string &nick, T &&namedThing, std::vector<T> &importTo) {
    if (std::find_if(importTo.begin(), importTo.end(),
                     [&namedThing](const auto &fn) { return fn.name == namedThing.name; })
        != importTo.end()) {
        std::cerr << fmt::format("Duplicate import name {}\n", namedThing.name.join());
        return false;
    }
    if (!nick.empty()) {
        namedThing.name.add(nick);
    }
    importTo.push_back(std::move(namedThing));
    return true;
}

void processImports(FulcrumModule &importTo, Compiler& compiler, ASTTypeStorage &typeStorage) {
    for (auto &importFrom : importTo.imports) {
        if (importFrom.isCImport) {
            auto targetPath = tryToFindImport(importFrom.target, compiler);
            if (targetPath == std::nullopt) throw CodegenError(fmt::format("Failed to find C include '{}'", importFrom.target));

            auto cImport = targetPath.value();
            HeaderParser headerParser;
            auto res = headerParser.parseHeader(cImport, compiler, cImport.stem());
            if (!res) {
                return;
            }
            auto m = headerParser.takeModule();
            auto nick = importFrom.nickname;
            if (nick.empty()) {
                nick = m.name;
            }

            m.types.addNick(nick);
            typeStorage.merge(std::move(m.types));

            fc_assert(!nick.empty());
            for (auto &&f : m.functions) {
                importNamed(nick, std::move(f), importTo.functions);
            }
            for (auto &&s : m.structs) {
                importNamed(nick, std::move(s), importTo.structs);
            }
            for (auto &&t : m.typeAliases) {
                importNamed(nick, std::move(t), importTo.typeAliases);
            }
            for (auto &&v : m.globalVariables) {
                importNamed(nick, std::move(v), importTo.globalVariables);
            }
        } else {
            auto maybeTargetPath = tryToFindImport(importFrom.target, compiler);
            if (maybeTargetPath == std::nullopt) {
                // TODO: replace with std::expected
                throw CodegenError(fmt::format("Failed to find module '{}'", importFrom.target));
            }

            auto targetPath = maybeTargetPath.value();
            Parser parser;
            auto res = parser.parse(targetPath);
            if (res == false) {
                parser.dumpErrors();
                throw CodegenError(
                    fmt::format("Could not parse module {} ({})", importFrom.target, targetPath.string())
                    );
            }
            SemanticAnalyzer sem;
            res = sem.run(parser);
            if (res == false) {
                sem.dumpErrors();
                throw CodegenError(
                    fmt::format("Could not parse module {} ({})", importFrom.target, targetPath.string())
                    );
            }
            auto m = sem.takeModule();
            if (m.name != importFrom.target) {
                throw CodegenError(fmt::format(
                                       "Module was imported as {}, but the name declared in the module was {}", importFrom.target,
                                       m.name
                                       ));
            }

            auto nick = importFrom.nickname;
            if (nick.empty()) {
                nick = m.name;
            }
            fc_assert(!nick.empty());

            for (auto &&f : m.functions) {
                importNamed(nick, std::move(f), importTo.functions);
            }
            for (auto &&s : m.structs) {
                importNamed(nick, std::move(s), importTo.structs);
            }
            for (auto &&t : m.typeAliases) {
                importNamed(nick, std::move(t), importTo.typeAliases);
            }
            for (auto &&v : m.globalVariables) {
                importNamed(nick, std::move(v), importTo.globalVariables);
            }

            m.types.addNick(nick);
            typeStorage.merge(std::move(m.types));
        }
    }
}

} // namespace


int Compiler::run(int argc, char *argv[]) {
    args::ArgumentParser argParser("fulcrum");
    args::Positional<std::string> fileArg(argParser, "file", "The file to compile");
    args::ValueFlagList<std::string> includeArg(
        argParser, "path", "Directories to search modules in. Searched from last to first", { 'I', "include" }
    );
    args::HelpFlag helpArg(argParser, "help", "Display help", { 'h', "help" });
    args::Flag picArg(argParser, "pic", "Create a dynamically linked position independent object", { "pic" });

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

    CodegenContext codegenCont("main", context);

    llvm::TargetMachine *targetMachine;
    {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();

        auto targetTriple = llvm::sys::getDefaultTargetTriple();
        std::string err;
        auto target = llvm::TargetRegistry::lookupTarget(targetTriple, err);
        if (!target) {
            llvm::errs() << err;
            return 1;
        }

        llvm::TargetOptions options;
        auto rm = std::optional<llvm::Reloc::Model>();
        if (picArg) {
            rm = llvm::Reloc::Model::PIC_;
        }
        targetMachine = target->createTargetMachine(targetTriple, "generic", "", options, rm);
        codegenCont.module.setDataLayout(targetMachine->createDataLayout());
        codegenCont.module.setTargetTriple(targetTriple);
    }

    if (includeArg) {
        namespace fs = std::filesystem;
        for (auto &path : includeArg.Get()) {
            auto absPath = fs::absolute(fs::path(path));
            includeDirectories.push_back(absPath);
        }
    }

    FulcrumModule moduleAST;
    Parser parser;
    bool res = false;
    if (!fileArg) {
        res = parser.parse();
    } else {
        res = parser.parse(std::filesystem::path(fileArg.Get()));
    }

    if (!res) {
        std::cerr << fmt::format("Failed to parse module {}\n", fileArg.Get());
        parser.dumpErrors();
        return 1;
    }

    SemanticAnalyzer sem;
    res = sem.run(parser);
    if (res == false) {
        std::cerr << fmt::format("Failed to perform semantic analysis of module {}\n", fileArg.Get());
        sem.dumpErrors();
        return 1;
    }
    moduleAST = sem.takeModule();

    // At this point we have correctly parsed AST, annotated with types
    ASTTypeStorage typeStorage;
    processImports(moduleAST, *this, typeStorage);

    codegenCont.generate(std::move(moduleAST));
    ExpressionGenContext exprGenContext{ .builder = builder, .codegenContext = codegenCont };
    for (auto &[name, fn] : codegenCont.namedFunctions) {
        try {
            if (fn == nullptr) { continue; } // why?
            exprGenContext.function = fn.get();
            fn->generateBody(exprGenContext);
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
        std::cerr << "Error occured\n";
        return 1;
    }

    // Object file generation

    auto filename = "output.o";
    std::error_code EC;
    llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);

    llvm::legacy::PassManager passManager;
    targetMachine->addPassesToEmitFile(passManager, dest, nullptr, llvm::CodeGenFileType::ObjectFile);
    passManager.run(codegenCont.module);
    dest.flush();

    return 0;
}
