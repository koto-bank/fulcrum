#include <iostream>
#include <map>
#include <optional>

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileUtilities.h>
#include <llvm/Support/Program.h>

#include <clang/Frontend/CompilerInstance.h>
#include <clang/Interpreter/Interpreter.h>

#include <fmt/format.h>

#include "assert.hpp"
#include "c_header_parser.hpp"
#include "codegen_context.hpp"
#include "parse_context.hpp"
#include "types.hpp"

namespace {
std::string cxToString(CXString &&str) {
    auto res = std::string(clang_getCString(str));
    clang_disposeString(str);
    return res;
}

std::string getSourceFileName(const CXCursor cur) {
    auto loc = clang_getCursorLocation(cur);
    CXFile fileName;
    clang_getSpellingLocation(loc, &fileName, nullptr, nullptr, nullptr);
    auto fileNameStr = clang_File_tryGetRealPathName(fileName);
    if (clang_getCString(fileNameStr) == nullptr) {
        fileNameStr = clang_getFileName(fileName);
    }
    auto name = cxToString(std::move(fileNameStr));
    std::replace(name.begin(), name.end(), '/', '_');
    std::replace(name.begin(), name.end(), '.', '_');
    return name;
}
}

std::string HeaderParser::getAnonName(const CXCursor cur) {
    auto usr = cxToString(clang_getCursorUSR(cur));
    if (anonymousNumbers.contains(usr)) {
        auto n = anonymousNumbers.size();
        anonymousNumbers.emplace(usr, n);
    }
    return fmt::format("anon{}", anonymousNumbers[usr]);
}

std::unique_ptr<ASTType> HeaderParser::clangToASTType(CXType clangTp) {
    auto qualType = clang::QualType::getFromOpaquePtr(clangTp.data[0]);

    // FIXME: this is very bad
    qualType = qualType.getAtomicUnqualifiedType();
    auto typeName = qualType.getAsString();
    if (qualType.getTypePtr()->isElaboratedTypeSpecifier()) {
        auto prevName = typeName;

        typeName = qualType.getTypePtr()->getAsTagDecl()->getDeclName().getAsString();
        if (typeName == "") typeName = prevName;
    }

    auto clangTypedefToASTType = [this](const std::string &typeName, CXType clangTp)
        -> std::unique_ptr<ASTType> {
        auto typeDecl = clang_getTypeDeclaration(clangTp);
        auto underlying = clang_getTypedefDeclUnderlyingType(typeDecl);
        auto aliasTo = clangToASTType(underlying);
        if (aliasTo == nullptr) {
            return nullptr;
        }
        // Special cases:
        if (typeName == "__builtin_va_list") {
            return std::make_unique<ASTBuiltinType>(VAType::Signature);
        }
        return std::make_unique<ASTNamedType>(typeName);
    };

    std::unique_ptr<ASTType> result;
    switch (clangTp.kind) {
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Short:
    case CXType_Int128: {
        auto size = clang_Type_getSizeOf(clangTp) * 8;
        auto signature = "i" + std::to_string(size);
        codegenContext.ensureType<IntegerType>(signature, size, true);

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
        codegenContext.ensureType<IntegerType>(signature, size, false);

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
    case CXType_Char_S:
        result = std::make_unique<ASTBuiltinType>("i8");
        break;
    case CXType_UChar:
    case CXType_Char_U:
        result = std::make_unique<ASTBuiltinType>("u8");
        break;
    case CXType_Void:
        result = std::make_unique<ASTBuiltinType>("void");
        break;
    case CXType_Pointer:
    case CXType_ConstantArray:
    case CXType_IncompleteArray: {
        auto internalType
            = clangTp.kind == CXType_Pointer ? clang_getPointeeType(clangTp) : clang_getArrayElementType(clangTp);

        auto pointee = clangToASTType(internalType);
        if (pointee == nullptr) {
            return nullptr;
        }

        auto maybeBuiltin = dynamic_cast<ASTBuiltinType *>(pointee.get());
        if (maybeBuiltin != nullptr && maybeBuiltin->builtinName == "void") {
            // Replace void pointer with i8*

            codegenContext.ensureType<IntegerType>("i8", 8, true);
            pointee = std::make_unique<ASTBuiltinType>("i8");
        } else if (maybeBuiltin != nullptr && maybeBuiltin->builtinName == "i8") {
            result = std::make_unique<ASTBuiltinType>("str");
            break;
        }

        result = std::make_unique<ASTPointerType>(std::move(pointee));
        break;
    }
    case CXType_Typedef: {
        result = clangTypedefToASTType(typeName, clangTp);
        break;
    }
    case CXType_Elaborated:
    case CXType_Record:
    case CXType_Enum: {
        auto namedType = clangTp.kind == CXType_Elaborated ? clang_Type_getNamedType(clangTp) : clangTp;

        if (namedType.kind == CXType_Record) {
            auto decl = clang_getTypeDeclaration(namedType);
            if (clang_Cursor_isAnonymous(decl)) typeName = getAnonName(decl);

            result = std::make_unique<ASTNamedType>(typeName);
        } else if (namedType.kind == CXType_Enum) {
            auto enumDecl = clang_getTypeDeclaration(namedType);

            result = clangToASTType(clang_getEnumDeclIntegerType(enumDecl));
        } else if (namedType.kind == CXType_Typedef) {
            result = clangTypedefToASTType(typeName, namedType);
        } else {
            auto kindName = cxToString(clang_getTypeKindSpelling(namedType.kind));
            std::cout << "Unknown elaborate type: " << kindName << std::endl;

            return nullptr;
        }

        break;
    }
    case CXType_FunctionProto: {
        auto resType = clang_getResultType(clangTp);
        auto returnType = clangToASTType(resType);

        bool unknownType = false;
        if (returnType == nullptr) {
            unknownType = true;
        }

        ASTFunctionType::Args arguments;
        for (auto i = 0; i < clang_getNumArgTypes(clangTp); i++) {
            auto argType = clang_getArgType(clangTp, i);

            auto langArgType = clangToASTType(argType);
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

HeaderParser::HeaderParser(CodegenContext &parentContext)
    : codegenContext(parentContext),
      interp(createClangInterpreter()) {
}

HeaderParser::~HeaderParser() = default;

void HeaderParser::includeHeader(const std::string &name) {
    auto tuOrErr = interp->Parse(std::string(fmt::format(R"(#include <{}>)", name)));
    auto isOk = bool(tuOrErr);
    if (!isOk) return;
    llvm::handleAllErrors(interp->Execute(tuOrErr.get()));
}

std::unique_ptr<VarDeclarationNode> HeaderParser::evalMacro(const std::string &name) {
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
    auto declType = decl->getType().getNonReferenceType();

    auto llvmVar = tuOrErr->TheModule.get()->getGlobalVariable(varName);
    if (declType->isIntegerType()) {
        auto varType = llvmVar->getValueType();

        bool isSigned = declType->isSignedIntegerType();
        auto valType = std::make_unique<ASTIntegerType>(isSigned, varType->getIntegerBitWidth());

        auto initializer = llvm::dyn_cast<llvm::ConstantInt>(llvmVar->getInitializer());

        auto varDef = std::make_unique<VarDeclarationNode>(name, std::make_unique<ASTBuiltinType>("i64"));
        if (isSigned)
            varDef->initialValue = std::make_unique<ConstantIntNode>(std::move(valType), initializer->getSExtValue());
        else
            varDef->initialValue = std::make_unique<ConstantIntNode>(std::move(valType), initializer->getZExtValue());
        llvm::handleAllErrors(interp->Execute(tuOrErr.get()));
        return varDef;
    } else if (declType->isConstantArrayType() && declType->getPointeeOrArrayElementType()->isAnyCharacterType()) {
        auto varInitializer = llvm::dyn_cast<llvm::ConstantDataArray>(llvmVar->getInitializer()->getOperand(0));

        auto varDef = std::make_unique<VarDeclarationNode>(name, std::make_unique<ASTBuiltinType>("str"));
        llvm::handleAllErrors(interp->Execute(tuOrErr.get()));
        varDef->initialValue = std::make_unique<ConstantStringNode>(varInitializer->getAsString().str());
        return varDef;
    } else {
        llvm::handleAllErrors(interp->Execute(tuOrErr.get()));
        return nullptr;
    }
}

std::unique_ptr<clang::Interpreter> HeaderParser::createClangInterpreter() const {
    std::string clangPath = llvm::sys::findProgramByName("clang").get();
    std::vector<llvm::StringRef> PrintResourceDirArgs{ clangPath, "-print-resource-dir" };
    llvm::SmallString<64> OutputFile;
    llvm::sys::fs::createTemporaryFile("print-resource-dir-output", "", OutputFile);
    llvm::FileRemover OutputRemover(OutputFile.c_str());
    std::optional<llvm::StringRef> Redirects[] = { std::nullopt, llvm::StringRef(OutputFile), std::nullopt };
    llvm::sys::ExecuteAndWait(clangPath, PrintResourceDirArgs, {}, Redirects);

    auto OutputBuf = llvm::MemoryBuffer::getFile(OutputFile.c_str());
    llvm::StringRef Output = OutputBuf.get()->getBuffer().rtrim('\n');
    auto clangInc = fmt::format("-I{}/include", Output.str());

    std::vector<const char *> clangArgs{ "-Xclang", "-emit-llvm-only", clangInc.data() };

    auto compBuilder = clang::IncrementalCompilerBuilder();
    compBuilder.SetCompilerArgs(clangArgs);
    auto instOrErr = compBuilder.CreateCpp();
    auto inst = std::move(instOrErr.get());

    auto ignoring = std::make_unique<clang::IgnoringDiagConsumer>();
    inst->getDiagnostics().setClient(ignoring.release(), true);

    std::unique_ptr<clang::Interpreter> interp;
    llvm::handleAllErrors(clang::Interpreter::create(std::move(inst)).moveInto(interp));

    return interp;
}

bool HeaderParser::parseHeader(const std::string &path) {
    fc_assert(module == nullptr);
    module = std::make_unique<CModule>();

    // What does it do?
    //includeHeader(path);

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit;
    auto code = clang_parseTranslationUnit2(
        index, path.data(), nullptr, 0, nullptr, 0, CXTranslationUnit_DetailedPreprocessingRecord, &unit
        );

    if (code != 0u) {
        std::cout << "Could not parse " << path << " error code: " << code << "\n";
        return false;
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(
        cursor,
        [](CXCursor c, CXCursor /*parent*/, CXClientData client_data_) {
            auto parseHeaderContext = (HeaderParser *)client_data_;
            auto cursorKind = clang_getCursorKind(c);

            switch (cursorKind) {
            case CXCursor_FunctionDecl: {
                auto funcName = cxToString(clang_getCursorSpelling(c));

                // Skip internal functions
                if (funcName.starts_with("__")) return CXChildVisit_Continue;

                if (std::find_if(
                        parseHeaderContext->module->functions.begin(), parseHeaderContext->module->functions.end(),
                        [&funcName](const auto &node) { return node.second->name == funcName; }
                    )
                    != parseHeaderContext->module->functions.end())
                    break;

                //std::cout << "File: " << getSourceFileName(c) << " Function decl: " << funcName << std::endl;

                auto funcType = clang_getCursorType(c);
                bool variadic = clang_isFunctionTypeVariadic(funcType);

                auto resType = clang_getResultType(funcType);
                auto returnType = parseHeaderContext->clangToASTType(resType);

                bool unknownType = false;
                if (returnType == nullptr) {
                    unknownType = true;
                }

                ArgList arguments;
                for (auto i = 0; i < clang_getNumArgTypes(funcType); i++) {
                    auto argCursor = clang_Cursor_getArgument(c, i);

                    auto argName = cxToString(clang_getCursorSpelling(argCursor));

                    auto argType = clang_getCursorType(argCursor);
                    auto langArgType = parseHeaderContext->clangToASTType(argType);
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

                parseHeaderContext->module->functions.push_back({ getSourceFileName(c),
                        std::make_unique<FunctionNode>(
                            funcName,
                            std::move(arguments),
                            std::move(returnType),
                            FunctionNode::Body{},
                            true,
                            variadic) });

                return CXChildVisit_Continue;
            }
            case CXCursor_MacroDefinition: {
                auto name = cxToString(clang_getCursorSpelling(c));

                if (clang_Cursor_isMacroFunctionLike(c)) return CXChildVisit_Continue;

                if (std::find_if(
                        parseHeaderContext->module->globalVariables.begin(),
                        parseHeaderContext->module->globalVariables.end(),
                        [&name](const auto &node) { return node.second->name == name; }
                    )
                    != parseHeaderContext->module->globalVariables.end())
                    break;

                auto loc = clang_getCursorLocation(c);
                CXFile locFile;
                unsigned int file, col, offset;
                clang_getSpellingLocation(loc, &locFile, &file, &col, &offset);

                // Skip internals
                if (clang_getFileName(locFile).data == nullptr || name.starts_with("_")) return CXChildVisit_Continue;

                auto varVal = parseHeaderContext->evalMacro(name);
                if (varVal != nullptr) parseHeaderContext->module->globalVariables.push_back({ getSourceFileName(c), std::move(varVal) });
                break;
            }
            case CXCursor_TypedefDecl: {
                auto name = cxToString(clang_getCursorSpelling(c));

                if (std::find_if(
                        parseHeaderContext->module->structs.begin(), parseHeaderContext->module->structs.end(),
                        [&name](const auto &node) { return node.second->name == name; }
                        )
                    != parseHeaderContext->module->structs.end())
                    break;

                auto aliasTo = parseHeaderContext->clangToASTType(clang_getTypedefDeclUnderlyingType(c));
                if (aliasTo == nullptr) break;

                parseHeaderContext->module->aliases.push_back({ getSourceFileName(c), std::make_unique<AliasNode>(name, std::move(aliasTo)) });
                break;
            }
            case CXCursor_UnionDecl: {
                auto unionType = clang_getCursorType(c);

                auto unionName = cxToString(clang_getCursorDisplayName(c));
                if (unionName == "") unionName = cxToString(clang_getTypeSpelling(unionType));
                if (clang_Cursor_isAnonymous(c)) unionName = parseHeaderContext->getAnonName(c);

                if (std::find_if(
                        parseHeaderContext->module->structs.begin(), parseHeaderContext->module->structs.end(),
                        [&unionName](auto &node) { return node.second->name == unionName; }
                    )
                    != parseHeaderContext->module->structs.end())
                    break;

                struct VisitData {
                    HeaderParser &parseHeaderContext;

                    long long biggestSize = 0;
                    StructNode::Fields fields;
                };
                VisitData data = { .parseHeaderContext = *parseHeaderContext,
                    .fields = {} };
                clang_Type_visitFields(
                    unionType,
                    [](CXCursor cursor, CXClientData client_data) {
                        VisitData *visitData = (VisitData *)client_data;

                        auto type = clang_getCursorType(cursor);
                        auto typeSize = clang_Type_getSizeOf(type);
                        if (typeSize > visitData->biggestSize) visitData->biggestSize = typeSize;

                        visitData->fields.emplace_back(
                            cxToString(clang_getCursorSpelling(cursor)),
                            visitData->parseHeaderContext.clangToASTType(type)
                        );

                        return CXVisit_Continue;
                    },
                    &data
                );

                auto unionNode = std::make_unique<UnionNode>(unionName, std::move(data.fields), true);
                unionNode->biggestSize = data.biggestSize;

                parseHeaderContext->module->structs.push_back({ getSourceFileName(c), std::move(unionNode) });
                break;
            }
            case CXCursor_StructDecl: {
                auto structType = clang_getCursorType(c);
                auto structName = cxToString(clang_getCursorDisplayName(c));
                if (structName == "") structName = cxToString(clang_getTypeSpelling(structType));

                if (std::find_if(
                        parseHeaderContext->module->structs.begin(), parseHeaderContext->module->structs.end(),
                        [&structName](auto &node) { return node.second->name == structName; }
                    )
                    != parseHeaderContext->module->structs.end())
                    break;

                struct VisitData {
                    HeaderParser &parseHeaderContext;
                    StructNode::Fields fields;
                };
                VisitData data = { .parseHeaderContext = *parseHeaderContext,
                    .fields = {} };
                clang_Type_visitFields(
                    structType,
                    [](CXCursor cursor, CXClientData client_data) {
                        VisitData *visitData = (VisitData *)client_data;

                        auto type = clang_getCursorType(cursor);
                        auto fieldName = cxToString(clang_getCursorSpelling(cursor));

                        visitData->fields.emplace_back(fieldName, visitData->parseHeaderContext.clangToASTType(type));

                        return CXVisit_Continue;
                    },
                    &data
                );
                parseHeaderContext->module->structs.emplace_back(getSourceFileName(c),
                                                                 std::make_unique<StructNode>(structName, std::move(data.fields), true)
                );

                break;
            }
            case CXCursor_EnumDecl: {
                auto namedType = clang_getCursorType(c);
                auto enumName = cxToString(clang_getTypeSpelling(namedType));

                auto enumIntType = parseHeaderContext->clangToASTType(clang_getEnumDeclIntegerType(c));

                struct EnumParseContext {
                    std::string enumTypeName;
                    HeaderParser &parseHeaderContext;
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
                                [&variantName](auto &node) { return node.second->name == variantName; }
                            )
                            != parseContext->parseHeaderContext.module->globalVariables.end())
                            return CXChildVisit_Break;

                        auto varDef = std::make_unique<VarDeclarationNode>(variantName,
                                                                                std::make_unique<ASTBuiltinType>(parseContext->enumTypeName));
                        if (parseContext->enumTypeName[0] == 'i')
                            varDef->initialValue = std::make_unique<ConstantIntNode>(
                                std::make_unique<ASTBuiltinType>(parseContext->enumTypeName),
                                (int64_t)clang_getEnumConstantDeclValue(c)
                                );
                        else
                            varDef->initialValue = std::make_unique<ConstantIntNode>(
                                std::make_unique<ASTBuiltinType>(parseContext->enumTypeName),
                                (uint64_t)clang_getEnumConstantDeclUnsignedValue(c)
                                );
                        parseContext->parseHeaderContext.module->globalVariables.push_back({ getSourceFileName(c), std::move(varDef) });

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
        this
    );

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);

    return true;
}

std::unique_ptr<CModule> HeaderParser::releaseModule() {
    return std::move(module);
}
