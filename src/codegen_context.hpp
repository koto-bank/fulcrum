#pragma once

#include <map>

#include "llvm/IR/Function.h"

#include "types.hpp"

struct Expression;

class Function {
    LLVMContext &context;
    llvm::Module &module;
    llvm::Function *function;

    std::unique_ptr<FunctionType> type;
    std::string name;
    std::vector<std::string> argumentNames;
    std::vector<std::unique_ptr<Expression>> body;
public:
    bool isPublic;

    Function(LLVMContext &context, llvm::Module &module,
             const std::string &name, const std::vector<std::tuple<std::string, LanguageType *>> &arguments,
             LanguageType *returnType, std::vector<std::unique_ptr<Expression>> &&body,
             bool isPublic)
        : context(context), module(module), name(name), body(std::move(body)), isPublic(isPublic) {

        std::vector<LanguageType *> argumentTypes;
        for (auto &&[nm, tp] : arguments) {
            argumentNames.push_back(nm);
            argumentTypes.push_back(tp);
        }
        type = std::make_unique<FunctionType>(context, argumentTypes, returnType);

        auto funcType = (llvm::FunctionType*)type->llvmType();
        function =
            llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name.data(), module);

        for (auto i = 0; i < function->arg_size(); i++)
            function->getArg(i)->setName(argumentNames[i]);
    }

    const std::string &getName() const { return name; }
    FunctionType *functionType() { return type.get(); }
    llvm::Function *llvmFunction() { return function; }

    std::string dump();
};

struct CodegenContext {
    LLVMContext &context;
    llvm::Module &module;

    std::map<std::string, std::unique_ptr<LanguageType>> types;
    std::map<std::string, std::unique_ptr<CustomType>> unresolvedCustomTypes;

    std::map<std::string, Function> functions;
};
