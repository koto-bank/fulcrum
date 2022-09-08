#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <clang-c/Index.h>

#include "fmt/color.h"

#include <iostream>
#include <memory>
#include <tuple>
#include <vector>
#include <map>
#include <algorithm>
#include <concepts>

#include "parse_context.hpp"
#include "types.hpp"
#include "expressions.hpp"
#include "codegen_context.hpp"

std::string cxToString(CXString &&str) {
    auto res = std::string((char*)str.data);
    clang_disposeString(str);

    return res;
}

LanguageType *clangToLanguageType(CodegenContext &codegenCont, CXType clangTp) {
    std::function<std::string(CXType)>actualTpName = [&actualTpName](CXType clangTp) {
        if (clangTp.kind == CXType_Elaborated) {
            auto namedType = clang_Type_getNamedType(clangTp);
            auto name = cxToString(clang_getCursorDisplayName(clang_getTypeDeclaration(namedType)));
            if (name != "") {
                return name;
            } else {
                return cxToString(clang_getTypeSpelling(clang_Type_getNamedType(clangTp)));
            }
        } else if (clangTp.kind == CXType_Pointer) {
            return actualTpName(clang_getPointeeType(clangTp)) + "*";
        }

        return cxToString(clang_getTypeSpelling(clangTp));
    };
    auto typeName = actualTpName(clangTp);

    if (codegenCont.types.contains(typeName))
        return codegenCont.types[typeName].get();

    std::unique_ptr<LanguageType> result;
    switch (clangTp.kind) {
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Short:
    case CXType_Int128:
        result = std::make_unique<IntegerType>(codegenCont, clang_Type_getSizeOf(clangTp) * 8, true);
        break;
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
    case CXType_UShort:
    case CXType_UInt128:
        result = std::make_unique<IntegerType>(codegenCont, clang_Type_getSizeOf(clangTp) * 8, false);
        break;
    case CXType_Float:
        result = std::make_unique<FloatType>(codegenCont, FloatType::Bits::Float);
        break;
    case CXType_Double:
        result = std::make_unique<FloatType>(codegenCont, FloatType::Bits::Double);
        break;
    case CXType_SChar:
    case CXType_UChar:
    case CXType_Char_S:
    case CXType_Char_U:
        result = std::make_unique<CharType>(codegenCont);
        break;
    case CXType_Void:
        result = std::make_unique<VoidType>(codegenCont);
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
                codegenCont.types.emplace("void_internal", std::make_unique<IntegerType>(codegenCont, 8, true));
            }

            pointee = codegenCont.types["void_internal"].get();
        }

        result = std::make_unique<PointerType>(codegenCont, pointee);
        break;
    }
    case CXType_Typedef: {
        auto aliasTo = clangToLanguageType(codegenCont, clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(clangTp)));
        if (aliasTo == nullptr)
            return nullptr;

        result = std::make_unique<AliasType>(codegenCont, typeName, aliasTo);
        break;
    }
    case CXType_Elaborated: {
        auto namedType = clang_Type_getNamedType(clangTp);

        if (namedType.kind == CXType_Record) {
            // Create and insert the type early, in case the type is recursive
            result = std::make_unique<StructType>(codegenCont, typeName, decltype(StructType::fields){}, true);
            codegenCont.types[typeName] = std::move(result);

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
                    auto fieldName = cxToString(clang_getCursorSpelling(cursor));

                    visitData->fields.emplace_back(fieldName, clangToLanguageType(visitData->codegenContext, type));

                    return CXVisit_Continue;
                },
                &data
            );
            // Now insert the fields into the struct
            ((StructType*)codegenCont.types[typeName].get())->fields =
                std::move(data.fields);

            break;
        } else if (namedType.kind == CXType_Enum) {
            auto enumDecl = clang_getTypeDeclaration(namedType);

            auto enumName = cxToString(clang_getTypeSpelling(namedType));

            auto enumIntType = (IntegerType*)clangToLanguageType(codegenCont, clang_getEnumDeclIntegerType(enumDecl));
            result = std::make_unique<AliasType>(codegenCont, enumName, enumIntType);

            struct EnumParseContext {
                CodegenContext &codegenCont;
                AliasType *enumIntType;
            };
            EnumParseContext parseContext = { .codegenCont = codegenCont, .enumIntType = (AliasType*)result.get() };

            clang_visitChildren(
                enumDecl,
                [](CXCursor c, CXCursor parent, CXClientData client_data_) {
                    EnumParseContext *parseContext = (EnumParseContext*)client_data_;

                    auto variantName = cxToString(clang_getCursorSpelling(c));
                    VariableDefinition varDef(variantName, parseContext->enumIntType);

                    auto intType = (IntegerType*)parseContext->enumIntType->aliasTo;
                    llvm::Constant *numberConstant = intType->isSigned
                        ? llvm::ConstantInt::getSigned(intType->llvmType(), clang_getEnumConstantDeclValue(c))
                        : llvm::ConstantInt::get(intType->llvmType(), clang_getEnumConstantDeclUnsignedValue(c));

                    auto llvmGlobal = new llvm::GlobalVariable(
                        parseContext->codegenCont.module,
                        intType->llvmType(),
                        false,
                        llvm::GlobalVariable::PrivateLinkage,
                        numberConstant,
                        variantName
                    );
                    varDef.value = llvmGlobal;

                    parseContext->codegenCont.globalVariables.emplace(variantName, varDef);

                    return CXChildVisit_Continue;
                },
                &parseContext
            );

            break;
        } else {
            auto kindName = cxToString(clang_getTypeKindSpelling(namedType.kind));
            std::cout << "Unknown elaborate type: " << kindName << std::endl;

            return nullptr;
        }
    }
    case CXType_FunctionProto: {
        auto resType = clang_getResultType(clangTp);
        LanguageType *returnType = clangToLanguageType(codegenCont, resType);

        bool unknownType = returnType == nullptr;

        std::vector<LanguageType *> arguments;
        for (auto i = 0; i < clang_getNumArgTypes(clangTp); i++) {
            auto argType = clang_getArgType(clangTp, i);

            auto langArgType = clangToLanguageType(codegenCont, argType);
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
        result = std::make_unique<FunctionType>(codegenCont, std::move(arguments), returnType);

        break;
    }
    default:
        std::cout << "Unknown type: " << cxToString(clang_getTypeSpelling(clangTp)) << " ";
        std::cout << "kind " << cxToString(clang_getTypeKindSpelling(clangTp.kind)) << std::endl;

        return nullptr;
    }

    // Check if the type hasn't been added early
    if (!codegenCont.types.contains(typeName))
        codegenCont.types[typeName] = std::move(result);

    return codegenCont.types[typeName].get();
}

void parseHeader(CodegenContext &codegenCont, std::string path) {
    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index,
        path.data(),
        nullptr, 0, nullptr, 0, CXTranslationUnit_DetailedPreprocessingRecord
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
                auto funcName = cxToString(clang_getCursorSpelling(c));

                // Skip internal functions
                if (funcName.starts_with("__")) return CXChildVisit_Continue;

                //std::cout << "Function decl: " << funcName << std::endl;

                auto funcType = clang_getCursorType(c);
                bool variadic = clang_isFunctionTypeVariadic(funcType);

                auto resType = clang_getResultType(funcType);
                LanguageType *returnType = clangToLanguageType(*client_data, resType);

                bool unknownType = returnType == nullptr;
                Function::Args arguments;

                for (auto i = 0; i < clang_getNumArgTypes(funcType); i++) {
                    auto argCursor = clang_Cursor_getArgument(c, i);

                    auto argName = cxToString(clang_getCursorSpelling(argCursor));

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

                client_data->emplaceFn(funcName, arguments, returnType, {}, true);

                return CXChildVisit_Continue;
            }
            case CXCursor_MacroDefinition: {
                auto name = cxToString(clang_getCursorSpelling(c));
                // Skip internals
                if (name.starts_with("__")) return CXChildVisit_Continue;

                auto tu = clang_Cursor_getTranslationUnit(c);
                auto extent = clang_getCursorExtent(c);

                std::string macroContents;
                CXToken *tokens;
                unsigned count;
                clang_tokenize(tu, extent, &tokens, &count);

                if (count == 2) {
                    auto macroStr = cxToString(clang_getTokenSpelling(tu, tokens[1]));
                    try {
                        auto parsed = std::stoll(macroStr, nullptr, 0);

                        VariableDefinition varDef(name, client_data->getType("i32"));

                        auto llvmGlobal = new llvm::GlobalVariable(
                            client_data->module,
                            varDef.type->llvmType(),
                            false,
                            llvm::GlobalVariable::PrivateLinkage,
                            llvm::ConstantInt::get(client_data->getType("i32")->llvmType(), (int64_t)parsed),
                            name
                        );
                        varDef.value = llvmGlobal;

                        client_data->globalVariables.emplace(name, varDef);
                    } catch (std::exception &) {
                        // Not a number, it seems
                    }
                }
                clang_disposeTokens(tu, tokens, count);

                break;
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

    llvm::TargetMachine *targetMachine;
    {
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
        targetMachine = target->createTargetMachine(targetTriple, "generic", "", options, rm);
        module.setDataLayout(targetMachine->createDataLayout());
        module.setTargetTriple(targetTriple);
    }

    CodegenContext codegenCont = { .context = context, .module = module };

    codegenCont.types.emplace("f32", std::make_unique<FloatType>(codegenCont, FloatType::Bits::Float));
    codegenCont.types.emplace("f64", std::make_unique<FloatType>(codegenCont, FloatType::Bits::Double));
    codegenCont.types.emplace("void", std::make_unique<VoidType>(codegenCont));
    codegenCont.types.emplace("bool", std::make_unique<BoolType>(codegenCont));
    codegenCont.types.emplace("i32",  std::make_unique<IntegerType>(codegenCont, 32, true));
    codegenCont.types.emplace("u32",  std::make_unique<IntegerType>(codegenCont, 32, false));

    if (!parse(&codegenCont)) {
        std::cout << "-----xxxxxx parsing failure xxxxxx-----\n";
        return 1;
    }

    /*
    for (auto &import : codegenCont.imports) {
        if (std::find(import.keywords.begin(), import.keywords.end(), "c") != import.keywords.end()) {
            parseHeader(codegenCont, import.target);
        }
    }

    std::cout << "Module " << codegenCont.moduleName << std::endl;
    std::cout << "Imports: " << std::endl;
    for (const auto &i : codegenCont.imports) {
        std::cout << i.target;
        if (!i.keywords.empty()) {
            std::cout << " keywords:";
        }

        for (const auto &kw : i.keywords) {
            std::cout << ' ' << kw;
        }
        std::cout << std::endl;
    }
    std::cout << codegenCont.functions.at("main").dump() << std::endl;
    */

    // Now that all types are known, function declrations can actually be generated
    for (auto &[name, knownFn] : codegenCont.functions) {
        knownFn.generateDeclaration();
    }

    ExpressionGenContext exprGenContext = {
        .builder = builder,
        .codegenContext = codegenCont
    };
    for (auto &[name, f] : codegenCont.functions) {
        try {
            exprGenContext.function = &f;
            f.generateBody(exprGenContext);
        } catch (const CodegenError &err) {
            std::cout << fmt::format(
                "{}\n{}",
                fmt::styled("Errors:", fmt::fg(fmt::color::red) | fmt::emphasis::bold),
                err.whatIndented(4)
            ) << std::endl;

            return 1;
        }
    }

    //IntegerConstant x(codegenCont.types["int"].get(), 10l);

    /*
    {
        std::vector<std::tuple<std::string, LanguageType *>> testArgs {
            { "x", codegenCont.types["i32"].get() }
        };
        Function testF(context, module, "test", std::move(testArgs), codegenCont.types["i32"].get());
        llvm::BasicBlock *bb = llvm::BasicBlock::Create(context, "enter", testF.llvmFunction());

        builder.SetInsertPoint(bb);
        builder.CreateRet(testF.llvmFunction()->getArg(0));

        codegenCont.functions.emplace("test", std::move(testF));
    }

    std::vector<std::tuple<std::string, LanguageType *>> mainArgs;
    Function mainF(context, module, "main", std::move(mainArgs), codegenCont.types["i32"].get());

    llvm::BasicBlock *bb = llvm::BasicBlock::Create(context, "enter", mainF.llvmFunction());

    builder.SetInsertPoint(bb);
    std::vector<std::unique_ptr<Expression>> fCallArgs;
    fCallArgs.push_back(std::make_unique<IntegerConstant>(codegenCont.types["i32"].get(), 123l));

    auto fCall = FunctionCall(codegenCont, "test", std::move(fCallArgs));
    builder.CreateRet(fCall.llvmValue(builder));
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

    llvm::errs() << module;

    if (llvm::verifyModule(module, &llvm::errs())) {
        // Exit early if there's an error
        return 1;
    }

    // Object file generation

    auto filename = "output.o";
    std::error_code EC;
    llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);

    llvm::legacy::PassManager passManager;
    targetMachine->addPassesToEmitFile(passManager, dest, nullptr, llvm::CGFT_ObjectFile);
    passManager.run(module);
    dest.flush();

    return 0;
}
