#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <clang-c/Index.h>

#include <iostream>
#include <memory>
#include <tuple>
#include <vector>
#include <map>
#include <algorithm>
#include <concepts>

#include "parsing_types.hpp"
#include "types.hpp"
#include "expressions.hpp"
#include "codegen_context.hpp"

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
        result = std::make_unique<IntegerType>(context, clang_Type_getSizeOf(clangTp) * 8, true);
        break;
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
    case CXType_UShort:
    case CXType_UInt128:
        result = std::make_unique<IntegerType>(context, clang_Type_getSizeOf(clangTp) * 8, false);
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

        result = std::make_unique<AliasType>(context, typeName, aliasTo);
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

            result = std::make_unique<StructType>(context, name, data.fields, true);
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

void parseHeader(CodegenContext &codegenCont, std::string path) {
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
                bool variadic = clang_isFunctionTypeVariadic(funcType);

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

                client_data->functions.emplace(funcName, Function(client_data->context, client_data->module, funcName, arguments, returnType));
                llvm::Function *llvmFnc = client_data->functions.at(funcName).llvmFunction();
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
        &codegenCont
    );

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
}

int main() {
    LLVMContext context;
    llvm::IRBuilder<> builder(context);

    llvm::Module module("main", context);

    CodegenContext codegenCont = { .context = context, .module = module };

    //parseHeader(codegenCont, "/usr/include/stdio.h");

    codegenCont.types.emplace("f32", std::make_unique<FloatType>(codegenCont.context, FloatType::Bits::Float));
    codegenCont.types.emplace("f64", std::make_unique<FloatType>(codegenCont.context, FloatType::Bits::Double));
    codegenCont.types.emplace("void", std::make_unique<VoidType>(codegenCont.context));
    codegenCont.types.emplace("bool", std::make_unique<BoolType>(codegenCont.context));
    codegenCont.types.emplace("i32",  std::make_unique<IntegerType>(codegenCont.context, 32, true));
    codegenCont.types.emplace("u32",  std::make_unique<IntegerType>(codegenCont.context, 32, false));

    parse(&codegenCont);

    //IntegerConstant x(codegenCont.types["int"].get(), 10l);

        /*
    llvm::FunctionType *type = llvm::FunctionType::get(Type::getInt32Ty(context), std::vector<Type*>(), false);
    llvm::Function *f = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "main", module);
    llvm::BasicBlock *bb = llvm::BasicBlock::Create(context, "enter", f);

    builder.SetInsertPoint(bb);

    auto &puts = codegenCont.functions.at("puts");
    auto strToPut = StringConstant(module, codegenCont.types.at("char *").get(), "A string to print");
    auto &charType = codegenCont.types.at("char");
    auto &intType = codegenCont.types.at("int");

    builder.CreateCall((llvm::FunctionType*)puts.functionType()->llvmType(), puts.llvmFunction(), { strToPut.llvmValue() });
    */

    /*
    auto &rand = codegenCont.functions.at("rand");
    auto &srand = codegenCont.functions.at("srand");

    std::vector<llvm::Value *> srandArgs = { IntegerConstant(srand.functionType()->arguments[0], 100l).llvmValue(), str.llvmValue() };
    builder.CreateCall(srand.llvmFunction()->getFunctionType(), srand.llvmFunction(), srandArgs);

    std::vector<llvm::Value *> args;
    auto res = builder.CreateCall(rand.llvmFunction()->getFunctionType(), rand.llvmFunction(), args, "result");
    */

    //builder.CreateRet(x.llvmValue());

    //llvm::errs() << module;

    // Object file generation
    /*

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();

    auto targetTriple = llvm::sys::getDefaultTargetTriple();
    std::string err;
    auto target = llvm::TargetRegistry::lookupTarget(targetTriple, err);
    if (!target) {
        llvm::errs() << err;
        return 1;
    }

    llvm::TargetOptions options;
    auto rm = llvm::Optional<llvm::Reloc::Model>();
    auto targetMachine = target->createTargetMachine(targetTriple, "generic", "", options, rm);
    module.setDataLayout(targetMachine->createDataLayout());
    module.setTargetTriple(targetTriple);

    auto filename = "output.o";
    std::error_code EC;
    llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);

    llvm::legacy::PassManager passManager;
    targetMachine->addPassesToEmitFile(passManager, dest, nullptr, llvm::CGFT_ObjectFile);
    passManager.run(module);
    dest.flush();
    */

    return 0;
}
