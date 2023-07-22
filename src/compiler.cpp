#include <llvm/ADT/Optional.h>

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

#include <fmt/color.h>

#include <args.hxx>

#include <algorithm>
#include <concepts>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "c_header_parser.hpp"
#include "codegen_context.hpp"
#include "expressions.hpp"
#include "parser.hpp"
#include "semantic_analyzer.hpp"
#include "types.hpp"

namespace {
std::optional<std::filesystem::path> tryToFindImport(const std::string &importName, CodegenContext& codegenCont) {
    namespace fs = std::filesystem;

    fs::path targetPath;
    bool found = false;
    for (auto inclDir = codegenCont.includeDirectories.rbegin();
         inclDir != codegenCont.includeDirectories.rend(); inclDir++) {
        fs::path includePath(*inclDir);
        includePath.make_preferred();

        if (!fs::is_directory(includePath.string())) {
            std::cout << fmt::format(
                "Include path '{}' does not exist or is not a directory", includePath.string()
                );
            continue;
        }

        targetPath = includePath / importName;
        std::cout << "Checking " << targetPath << "\n";
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

void processImportsRecursive(const std::vector<ModuleNode::Import> &moduleImports,
                             CodegenContext& codegenCont,
                             std::map<std::string, std::unique_ptr<ModuleNode>> &includedModules) {
    for (auto &import : moduleImports) {
        if (includedModules.contains(import.target)) continue;

        if (std::find(import.keywords.begin(), import.keywords.end(), "c") != import.keywords.end()) {
            auto targetPath = tryToFindImport(import.target, codegenCont);
            if (targetPath == std::nullopt) throw CodegenError(fmt::format("Failed to find C include '{}'", import.target));

            auto cImport = targetPath.value();
            auto headerParser = HeaderParser(codegenCont);
            auto res = headerParser.parseHeader(cImport);
            if (!res) {
                return;
            }
            includedModules.emplace(import.target, headerParser.releaseModule());
        } else {

            auto maybeTargetPath = tryToFindImport(import.target, codegenCont);
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
            auto moduleAST = sem.releaseModule();
            if (moduleAST->name != import.target)
                throw CodegenError(fmt::format(
                                       "Module was imported as {}, but the name declared in the module was {}", import.target,
                                       moduleAST->name
                                       ));

            auto &emplaced = includedModules.emplace(import.target, std::move(moduleAST)).first->second;
            processImportsRecursive(emplaced->imports, codegenCont, includedModules);
        }
    }
}

std::map<std::string, std::unique_ptr<ModuleNode>>
processImports(std::vector<ModuleNode::Import> moduleImports, CodegenContext& codegenCont) {
    std::map<std::string, std::unique_ptr<ModuleNode>> includedModules;
    processImportsRecursive(moduleImports, codegenCont, includedModules);
    return includedModules;
}

void importNames(ModuleNode *importTo, std::map<std::string, std::unique_ptr<ModuleNode>> &includedModules) {
    for (auto &import : importTo->imports) {
        // TODO: actually add import names, for now everything is imported
        auto &importedAST = includedModules[import.target];
        for (auto &[basename, fullname] : importedAST->allNames()) {
            importTo->importName(basename, fullname);
        }
    }
}
}

namespace compiler {
int run(int argc, char *argv[]) {
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

    context.setOpaquePointers(true);

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
            codegenCont.includeDirectories.push_back(absPath);
        }
    }

    std::unique_ptr<ModuleNode> moduleAST;
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
    moduleAST = sem.releaseModule();

    std::map<std::string, std::unique_ptr<ModuleNode>> includedModules;
    try {
        includedModules = processImports(moduleAST->imports, codegenCont);
    } catch (const CodegenError &err) {
        std::cout << fmt::format(
                "{}\n{}", fmt::styled("Errors:", fmt::fg(fmt::color::red) | fmt::emphasis::bold), err.whatIndented(4)
            ) << std::endl;
        return 1;
    }

    // TODO: better error collection, recovery and reporting
    try {
        for (auto &[name, ast] : includedModules) {
            std::cout << fmt::format("Compiling {}", name) << std::endl;

            importNames(ast.get(), includedModules);
            codegenCont.generate(std::move(ast));
        }

    } catch (const CodegenError &err) {
        std::cout << fmt::format(
                "{}\n{}", fmt::styled("Errors:", fmt::fg(fmt::color::red) | fmt::emphasis::bold), err.whatIndented(4)
            ) << std::endl;
        return 1;
    }

    importNames(moduleAST.get(), includedModules);
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
}
