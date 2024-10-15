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
                "Include path '{}' does not exist or is not a directory", includePath.string()
                );
            continue;
        }

        targetPath = includePath / importName;
        // std::cout << "Checking " << targetPath << "\n";
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

template <typename T>
using ImportedModules = std::map<std::string, T>;
using FulcrumImports = ImportedModules<FulcrumModule>;
using CImports = ImportedModules<CModule>;

void processImportsRecursive(FulcrumModule &fulcrumModule,
                             Compiler& compiler,
                             FulcrumImports &fulcrumModules,
                             CImports &CModules) {
    for (auto &import : fulcrumModule.imports) {
        if (import.isCImport) {
            auto targetPath = tryToFindImport(import.target, compiler);
            if (targetPath == std::nullopt) throw CodegenError(fmt::format("Failed to find C include '{}'", import.target));

            auto cImport = targetPath.value();
            HeaderParser headerParser;
            auto res = headerParser.parseHeader(cImport, compiler);
            if (!res) {
                return;
            }
            auto module = headerParser.takeModule();
            CModules.emplace(import.nickname, std::move(module));
        } else {
            if (fulcrumModules.contains(import.target)) {
                continue;
            }
            auto maybeTargetPath = tryToFindImport(import.target, compiler);
            if (maybeTargetPath == std::nullopt) throw CodegenError(fmt::format("Failed to find module '{}'", import.target));

            auto targetPath = maybeTargetPath.value();
            Parser parser;
            auto res = parser.parse(targetPath);
            if (res == false) {
                parser.dumpErrors();
                throw CodegenError(
                    fmt::format("Could not parse module {} ({})", import.target, targetPath.string())
                    );
            }
            SemanticAnalyzer sem;
            res = sem.run(parser);
            if (res == false) {
                sem.dumpErrors();
                throw CodegenError(
                    fmt::format("Could not parse module {} ({})", import.target, targetPath.string())
                    );
            }
            auto moduleAST = sem.takeModule();
            if (moduleAST.name != import.target)
                throw CodegenError(fmt::format(
                                       "Module was imported as {}, but the name declared in the module was {}", import.target,
                                       moduleAST.name
                                       ));

            auto &emplaced = fulcrumModules.emplace(import.nickname, std::move(moduleAST)).first->second;
            processImportsRecursive(emplaced, compiler, fulcrumModules, CModules);
        }
    }
}

std::tuple<FulcrumImports, CImports>
processImports(FulcrumModule &module, Compiler& compiler) {
    FulcrumImports fulcrumModules;
    CImports CModules;
    processImportsRecursive(module, compiler, fulcrumModules, CModules);
    return { std::move(fulcrumModules), std::move(CModules) };
}

template <typename T>
bool importNamed(const std::string &nick, T &&namedThing, std::vector<T> &importTo) {
    if (nick.empty() == false) {
        namedThing.name = fmt::format("{}.{}", nick, namedThing.name);
    }
    if (std::find_if(importTo.begin(), importTo.end(),
                     [&namedThing](const auto &fn) { return fn.name == namedThing.name; })
        != importTo.end()) {
        std::cerr << fmt::format("Duplicate import name {}\n", namedThing.name);
        return false;
    }
    importTo.push_back(std::move(namedThing));
    return true;
}

void importNames(FulcrumModule &importTo, FulcrumImports &fulcrumImports, CImports &CImports) {
    for (auto &[nick, m] : fulcrumImports) {
        for (auto &&f : m.functions) {
            importNamed<FunctionNode>(nick, std::move(f), importTo.functions);
        }
        for (auto &&s : m.structs) {
            importNamed<StructNode>(nick, std::move(s), importTo.structs);
        }
        for (auto &&t : m.typeAliases) {
            importNamed<TypeAliasNode>(nick, std::move(t), importTo.typeAliases);
        }
        for (auto &&v : m.globalVariables) {
            importNamed<VariableDeclarationNode>(nick, std::move(v), importTo.globalVariables);
        }
    }

    for (auto &[nick, m] : CImports) {
        for (auto &&f : m.functions) {
            importNamed<FunctionNode>(nick, std::move(f), importTo.functions);
        }
        for (auto &&s : m.structs) {
            importNamed<StructNode>(nick, std::move(s), importTo.structs);
        }
        for (auto &&t : m.typeAliases) {
            importNamed<TypeAliasNode>(nick, std::move(t), importTo.typeAliases);
        }
        for (auto &&v : m.globalVariables) {
            importNamed<VariableDeclarationNode>(nick, std::move(v), importTo.globalVariables);
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

    FulcrumImports importedFulcrumModules;
    CImports importedCModules;
    try {
        std::tie(importedFulcrumModules, importedCModules) = processImports(moduleAST, *this);
    } catch (const CodegenError &err) {
        std::cout << fmt::format(
                "{}\n{}", fmt::styled("Errors:", fmt::fg(fmt::color::red) | fmt::emphasis::bold), err.whatIndented(4)
            ) << std::endl;
        return 1;
    }

    // TODO: better error collection, recovery and reporting
    try {
        for (auto &[name, ast] : importedFulcrumModules) {
            std::cout << fmt::format("Compiling {}", name) << std::endl;

            importNames(ast, importedFulcrumModules, importedCModules);
            codegenCont.generate(std::move(ast));
        }

        // hack for __uint128_t on ARM64
        codegenCont.emplaceType<IntegerType>("__uint128_t", 128, false);
        for (auto &[name, ast] : importedCModules) {
            std::cout << fmt::format("Compiling {}", name) << std::endl;
            codegenCont.generate(std::move(ast));
        }

    } catch (const CodegenError &err) {
        std::cout << fmt::format(
                "{}\n{}", fmt::styled("Errors:", fmt::fg(fmt::color::red) | fmt::emphasis::bold), err.whatIndented(4)
            ) << std::endl;
        return 1;
    }

    importNames(moduleAST, importedFulcrumModules, importedCModules);

    codegenCont.generate(std::move(moduleAST));
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
