#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Value.h"
#include <llvm/IR/IRBuilder.h>

#include <clang-c/Index.h>

#include <iostream>
#include <memory>
#include <tuple>
#include <vector>
#include <map>
#include <algorithm>

#include "types.hpp"

class Function {
    LLVMContext &context;
    llvm::Module &module;
    llvm::Function *function;

    std::unique_ptr<FunctionType> type;
    std::string name;
    std::vector<std::string> argumentNames;
public:

    Function(LLVMContext &context, llvm::Module &module,
             std::string name, std::vector<std::tuple<std::string, LanguageType *>> arguments,
             LanguageType *returnType)
        : context(context), module(module), name(name) {

        std::vector<LanguageType *> argumentTypes;
        for (auto &&[nm, tp] : arguments) {
            argumentNames.push_back(nm);
            argumentTypes.push_back(tp);
        }
        type = std::make_unique<FunctionType>(context, argumentTypes, returnType);

        auto funcType = (llvm::FunctionType*)type->llvmType();
        function =
            llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name.data(), module);
    }

    llvm::Function *llvmFunction() {
        return function;
    }
};

struct CodegenContext {
    LLVMContext &context;
    llvm::Module &module;

    std::map<std::string, std::unique_ptr<LanguageType>> types;
};

LanguageType *clangToLanguageType(CodegenContext &codegenCont, CXType clangTp) {
    auto &context = codegenCont.context;

    auto typeSpelling = clang_getTypeSpelling(clangTp);
    auto typeName = std::string((char*)typeSpelling.data);
    clang_disposeString(typeSpelling);

    if (codegenCont.types.contains(typeName))
        return codegenCont.types[typeName].get();

    std::unique_ptr<LanguageType> result;
    switch (clangTp.kind) {
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Short:
    case CXType_Int128:
        result = std::make_unique<IntegerType>(context, clang_Type_getSizeOf(clangTp), true);
        break;
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
    case CXType_UShort:
    case CXType_UInt128:
        result = std::make_unique<IntegerType>(context, clang_Type_getSizeOf(clangTp), false);
        break;
    case CXType_Float:
        result = std::make_unique<FloatType>(context, FloatType::Bits::Float);
        break;
    case CXType_Double:
        result = std::make_unique<FloatType>(context, FloatType::Bits::Double);
        break;
    case CXType_SChar:
    case CXType_UChar:
    case CXType_Char_S:
    case CXType_Char_U:
        result = std::make_unique<CharType>(context);
        break;
    case CXType_Void:
        result = std::make_unique<VoidType>(context);
        break;
    case CXType_Pointer:
    case CXType_ConstantArray:
    case CXType_IncompleteArray: {
        auto internalType = clangTp.kind == CXType_Pointer
            ? clang_getPointeeType(clangTp)
            : clang_getArrayElementType(clangTp);

        auto pointee = clangToLanguageType(codegenCont, internalType);
        if (pointee == nullptr) {
            return nullptr;
        }
        if (dynamic_cast<VoidType *>(pointee) != nullptr) {
            // Synthesize a type if needed
            if (!codegenCont.types.contains("void_internal")) {
                codegenCont.types.emplace("void_internal", std::make_unique<IntegerType>(context, 8, true));
            }

            pointee = codegenCont.types["void_internal"].get();
        }

        result = std::make_unique<PointerType>(context, pointee);
        break;
    }
    case CXType_Typedef: {
        auto aliasTo = clangToLanguageType(codegenCont, clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(clangTp)));
        if (aliasTo == nullptr)
            return nullptr;

        result = std::make_unique<AliasType>(context, aliasTo);
        break;
    }
    case CXType_Elaborated: {
        auto namedType = clang_Type_getNamedType(clangTp);

        if (namedType.kind == CXType_Record) {
            struct VisitData {
                CodegenContext &codegenContext;
                std::vector<std::tuple<std::string, LanguageType *>> fields;
            };
            VisitData data = { .codegenContext = codegenCont };
            clang_Type_visitFields(
                namedType,
                [](CXCursor cursor, CXClientData client_data) {
                    VisitData *visitData = (VisitData*)client_data;

                    auto type = clang_getCursorType(cursor);
                    auto fieldNameC = clang_getCursorSpelling(cursor);
                    auto fieldName = std::string((char*)fieldNameC.data);
                    clang_disposeString(fieldNameC);

                    visitData->fields.emplace_back(fieldName, clangToLanguageType(visitData->codegenContext, type));

                    return CXVisit_Continue;
                },
                &data
            );

            auto spelling = clang_getCursorDisplayName(clang_getTypeDeclaration(namedType));
            std::string name((char*)spelling.data);
            clang_disposeString(spelling);

            result = std::make_unique<StructType>(context, name, data.fields);
            break;
        } else {
            auto spelling = clang_getTypeKindSpelling(namedType.kind);
            auto kindName = std::string((char*)spelling.data);
            clang_disposeString(spelling);

            std::cout << "Unknown elaborate type: " << kindName << std::endl;

            return nullptr;
        }
    }
    default:
        auto typeName = clang_getTypeSpelling(clangTp);
        std::cout << "Unknown type: " << std::string((char*)typeName.data) << " ";
        clang_disposeString(typeName);

        auto spelling = clang_getTypeKindSpelling(clangTp.kind);
        auto kindName = std::string((char*)spelling.data);
        clang_disposeString(spelling);

        std::cout << "kind " << kindName << std::endl;

        return nullptr;
    }

    codegenCont.types[typeName] = std::move(result);
    return codegenCont.types[typeName].get();
}

void parseHeader(LLVMContext &context, llvm::Module &module, std::string path) {
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index,
        path.data(),
        nullptr, 0, nullptr, 0, CXTranslationUnit_None
    );
    if (unit == nullptr) {
        std::cout << "Could not parse " << path;
        return;
    }

    CodegenContext data = { .context = context, .module = module };

    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(
        cursor,
        [](CXCursor c, CXCursor parent, CXClientData client_data_) {
            auto client_data = (CodegenContext *)client_data_;
            auto cursorKind = clang_getCursorKind(c);

            switch (cursorKind) {
            case CXCursor_FunctionDecl: {
                auto str = clang_getCursorSpelling(c);
                auto funcName = std::string((char*)str.data);
                clang_disposeString(str);

                // Skip internal functions
                if (funcName.starts_with("__")) return CXChildVisit_Continue;

                //std::cout << "Function decl: " << funcName << std::endl;

                auto funcType = clang_getCursorType(c);

                auto resType = clang_getResultType(funcType);
                LanguageType *returnType = clangToLanguageType(*client_data, resType);

                bool unknownType = returnType == nullptr;
                std::vector<std::tuple<std::string, LanguageType *>> arguments;

                for (auto i = 0; i < clang_getNumArgTypes(funcType); i++) {
                    auto argCursor = clang_Cursor_getArgument(c, i);

                    auto argNameC = clang_getCursorSpelling(argCursor);
                    auto argName = std::string((char*)argNameC.data);
                    clang_disposeString(argNameC);

                    auto argType = clang_getCursorType(argCursor);
                    auto langArgType = clangToLanguageType(*client_data, argType);
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

                auto fnc = Function(client_data->context, client_data->module, funcName, arguments, returnType);
                llvm::Function *llvmFnc = fnc.llvmFunction();
                for (auto i = 0; i < llvmFnc->arg_size(); i++) {
                    auto &[name, _] = arguments[i];

                    llvmFnc->getArg(i)->setName(name);
                }

                return CXChildVisit_Continue;
            }
            default: break;
            };


            return CXChildVisit_Recurse;
        },
        &data
    );

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
}

int main() {
    LLVMContext context;
    llvm::IRBuilder<> builder(context);

    llvm::Module module("main", context);

    parseHeader(context, module, "/usr/include/stdlib.h");

    llvm::FunctionType *type = llvm::FunctionType::get(Type::getVoidTy(context), std::vector<Type*>(), false);
    llvm::Function *f = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "main", module);
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(context, "enter", f);

    builder.SetInsertPoint(bb);
    builder.CreateRetVoid();

    llvm::errs() << module;

    return 0;
}
