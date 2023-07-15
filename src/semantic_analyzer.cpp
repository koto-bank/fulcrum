#include <fmt/format.h>

#include "token.hpp"
#include "semantic_analyzer.hpp"

#include <stdio.h>

namespace {
void printError(const std::string &fileName, const SemanticAnalyzer::Error &e) {
    std::cerr << fmt::format("{}:{}:{}: error: {}\n", fileName, e.line, e.col, e.error);
    if (!e.sourceLine.empty()) {
        std::cerr << e.sourceLine << '\n';
    }
}

std::optional<std::string> getSymbol(const Parser::Expression &expr) {
    if (expr.token == nullptr) {
        return std::nullopt;
    }
    if (expr.token->type != token::Type::Symbol) {
        return std::nullopt;
    }
    return expr.token->as<token::Symbol>()->symbol;
}

std::optional<std::unique_ptr<ASTBuiltinType>> parseIntType(const std::string &type) {
    fc_assert(type[0] == 'u' || type[0] == 'i');

    if (type.size() == 1) {
        return std::nullopt;
    }

    bool isSigned;
    if (type[0] == 'u') {
        isSigned = false;
    } else {
        isSigned = true;
    }

    if (type[1] < '1' || type[1] > '9') {
        return std::nullopt;
    }
    uint32_t bits = type[1] - '0';
    for (auto i = 2u; i < type.size(); i++) {
        if (type[i] < '0' || type[i] > '9') {
            return std::nullopt;
        }
        bits = bits * 10 + type[i] - '0';
    }
    // why 999?
    if (bits > 999) {
        return std::nullopt;
    }
    return std::make_unique<ASTIntegerType>(type, isSigned, bits);
}

std::string makeIntTypeSignature(const token::IntegerLiteral &token) {
    return fmt::format("{}{}", token.isSigned ? 'i' : 'u', token.bits);
}

template <typename T>
std::optional<T> getInteger(const Parser::Expression &expr) {
    if (expr.token == nullptr) {
        return std::nullopt;
    }
    if (expr.token->type != token::Type::IntegerLiteral) {
        return std::nullopt;
    }
    auto intLiteral = expr.token->as<token::IntegerLiteral>();
    if (intLiteral->isSigned) {
        return static_cast<T>(std::get<int64_t>(intLiteral->value));
    } else {
        return static_cast<T>(std::get<uint64_t>(intLiteral->value));
    }
}
}

bool SemanticAnalyzer::run(Parser &_parser) {
    parser = &_parser;

    auto tree = parser->releaseSyntaxTree();
    if (tree->children.empty()) {
        errors.push_back({ "", "empty syntax tree, nothing to parse", 0u, 0u });
        return false;
    }

    const auto &moduleForm = tree->children[0];
    auto res = parseModuleDefinition(moduleForm);
    for (auto i = 1u; i < tree->children.size(); i++) {
        res = parseToplevelForm(tree->children[i]) && res;
    }
    return res;
}

bool SemanticAnalyzer::parseModuleDefinition(const Parser::Expression &moduleForm) {
    if (moduleForm.token != nullptr || moduleForm.children.size() == 0) {
        reportError(moduleForm.token, "expected module definition");
        return false;
    }
    auto symbol = getSymbol(moduleForm.children[0]);
    if (symbol == std::nullopt || symbol != "module") {
        reportError(moduleForm.children[0].token, "\"module\" expected");
        return false;
    }

    if (moduleForm.children.size() == 1) {
        reportError(moduleForm.children[0].token, "module name expected");
        return false;
    }
    auto name = getSymbol(moduleForm.children[0]);
    if (symbol == std::nullopt) {
        reportError(moduleForm.children[1].token, "module name expected");
        return false;
    }
    module = std::make_unique<ModuleNode>(name.value());

    // get imports
    auto res = true;
    for (auto i = 2u; i < moduleForm.children.size(); i++) {
        res = parseImport(moduleForm.children[i]) && res;
    }

    return res;
}

bool SemanticAnalyzer::parseImport(const Parser::Expression &form) {
    if (form.token != nullptr || form.children.size() == 0) {
        reportError(form.token, "expected import definition");
        return false;
    }

    auto symbol = getSymbol(form.children[0]);
    if (symbol == std::nullopt || symbol != "import") {
        reportError(form.children[0].token, "\"import\" expected");
        return false;
    }

    ModuleNode::Import import;
    bool targetSet = false;
    for(auto next = form.children.begin() + 1; next != form.children.end(); next++) {
        if (next->token == nullptr) {
            reportError(next->token, "expected keyword, Fulcrum module name or C header filename");
            return false;
        }
        switch (next->token->type) {
        case token::Type::Symbol: {
            if (targetSet) {
                reportError(next->token, "multiple import targets not allowed");
                return false;
            }
            auto symbol = getSymbol(*next);
            import.target = symbol.value();
            break;
        }
        case token::Type::StringLiteral: {
            if (targetSet) {
                reportError(next->token, "multiple import targets not allowed");
                return false;
            }
            auto symbol = next->token->as<token::StringLiteral>();
            import.target = symbol->contents;
            break;
        }
        case token::Type::Keyword: {
            auto kw = next->token->as<token::Keyword>();
            import.keywords.push_back(kw->name);
            break;
        }
        default:
            reportError(next->token, "multiple import targets not allowed");
            return false;
        }
    }
    module->imports.push_back(std::move(import));
    return true;
}

bool SemanticAnalyzer::parseToplevelForm(const Parser::Expression &form) {
    if (form.token != nullptr || form.children.size() == 0) {
        reportError(form.token, "expected function or structure definition");
        return false;
    }
    auto symbol = getSymbol(form.children[0]);
    if (symbol == std::nullopt) {
        reportError(form.children[0].token, "expected fn, fn-, struct, or struct-");
        return false;
    }
    if (symbol == "fn" || symbol == "fn-") {
        return parseFunctionDefinition(form);
    } else if (symbol == "struct" || symbol == "struct-") {
        return parseStructureDefinition(form);
    }
    reportError(form.children[0].token, "expected fn, fn-, struct, or struct-");
    return false;
}

bool SemanticAnalyzer::parseFunctionDefinition(const Parser::Expression &form) {
    if (form.children.size() == 1) {
        reportError(form.children[0].token, "expected function name, return type, argument list and body");
        return false;
    }

    auto fn = std::make_unique<FunctionNode>();
    auto type = getSymbol(form.children[0]);
    if (type == std::nullopt
        || !(type.value() == "fn"
             || type.value() == "fn-")) {
        reportError(form.children[0].token, "expected fn or fn-");
        return false;
    }
    if (type == "fn") {
        fn->isPublic = true;
    } else if (type == "fn-") {
        fn->isPublic = false;
    }

    auto name = getSymbol(form.children[1]);
    if (name == std::nullopt) {
        reportError(form.children[1].token, "function name must be a symbol");
        return false;
    }
    fn->name = name.value();

    if (form.children.size() == 2) {
        reportError(form.children[1].token, "expected return type, argument list and body");
        return false;
    }
    auto retType = parseType(form.children[2]);
    if (retType == std::nullopt) {
        return false;
    }
    fn->returnType = std::move(retType.value());

    if (form.children.size() == 3) {
        reportError(form.children[2].token, "expected argument list and body");
        return false;
    }

    auto argList = parseArgList(form.children[3]);
    if (argList == std::nullopt) {
        return false;
    }
    fn->arguments = std::move(argList.value());

    if (form.children.size() == 4) {
        reportError(form.children[3].token, "expected function body");
        return false;
    }

    for (auto i = 4u; i < form.children.size(); i++) {
        auto bodyForm = parseBodyForm(form.children[i]);
        if (bodyForm == std::nullopt) {
            return false;
        }
        fn->body.push_back(std::move(bodyForm.value()));
    }

    module->functions.push_back(std::move(fn));
    return true;
}

std::optional<ArgList> SemanticAnalyzer::parseArgList(const Parser::Expression &form) {
    // ((name-1 type) (name-2 type) ... )
    if (form.token != nullptr) {
        reportError(form.token, "expected list");
        return std::nullopt;
    }

    ArgList args;
    for (const auto &argDecl : form.children) {
        if (argDecl.token != nullptr) {
            reportError(argDecl.token, "expected argument declaration");
            return std::nullopt;
        }
        if (argDecl.children.size() == 0) {
            // TODO: proper line and column when issue #20 is compeled
            reportError(form.token, "empty argument declaration in argument list");
            return std::nullopt;
        }
        if (argDecl.children.size() != 2) {
            reportError(form.token, "argument declaration must consist of symbol and type");
            return std::nullopt;
        }
        const auto &argForm = argDecl.children[0];
        auto argName = getSymbol(argForm);
        if (argName == std::nullopt) {
            reportError(form.token, "argument name must be a symbol");
            return std::nullopt;
        }

        auto argType = parseType(argDecl.children[1]);
        if (argType == std::nullopt) {
            return std::nullopt;
        }

        args.push_back({ argName.value(), std::move(argType.value()) });
    }
    return args;
}

std::optional<std::unique_ptr<ASTNode>> SemanticAnalyzer::parseBodyForm(const Parser::Expression &form) {
    // what can be in a body (toplevel): VarDeclaration | FnCall
    if (form.token != nullptr) {
        reportError(form.token, "function body expression must be a list");
        return std::nullopt;
    }

    if (form.token != nullptr) {
        reportError(form.token, "function body must consist of lists");
        return std::nullopt;
    }
    // identify toplevel form
    if (form.children.size() == 0) {
        reportError(form.token, "empty toplevel epxression");
        return std::nullopt;
    }

    const auto &c = form.children[0];
    auto sym = getSymbol(c);
    if (sym == std::nullopt) {
        reportError(c.token, "expected symbol");
        return std::nullopt;
    }

    if (sym.value() == "var") {
        auto varDecl = parseVariableDeclaraion(form);
        if (varDecl == std::nullopt) {
            return std::nullopt;
        }
        return std::move(varDecl.value());
    } else {
        auto fnCall = parseFunctionCall(form);
        if (fnCall == std::nullopt) {
            return std::nullopt;
        }
        return std::move(fnCall.value());
    }
}

std::optional<std::unique_ptr<FunctionCallNode>> SemanticAnalyzer::parseFunctionCall(const Parser::Expression &form) {
    if (form.token != nullptr) {
        reportError(form.token, "function call must be a list");
        return std::nullopt;
    }

    if (form.children.size() == 0) {
        reportError(form.token, "function call cannot be an empty list");
        return std::nullopt;
    }

    const auto &nameExpr = form.children[0];
    auto name = getSymbol(nameExpr);
    if (name == std::nullopt) {
        reportError(form.token, "function name must be a symbol");
        return std::nullopt;
    }

    auto fnCall = std::make_unique<FunctionCallNode>(name.value());
    for (auto i = 1u; i < form.children.size(); i++) {
        auto arg = parseArgExpression(form.children[i]);
        if (arg == std::nullopt) {
            return std::nullopt;
        }
        fnCall->args.push_back(std::move(arg.value()));
    }
    return fnCall;
}

std::optional<std::unique_ptr<VarDeclarationNode>>
SemanticAnalyzer::parseVariableDeclaraion(const Parser::Expression &form) {
    if (form.token != nullptr) {
        reportError(form.token, "variable declaration must be a list");
        return std::nullopt;
    }

    if (form.children.size() != 3 && form.children.size() != 4) {
        reportError(form.token, "variable declaration must specify variable name and type and optional initial value");
        return std::nullopt;
    }

    auto varName = getSymbol(form.children[1]);
    if (varName == std::nullopt) {
        reportError(form.children[1].token, "variable name must be a symbol");
        return std::nullopt;
    }

    const auto &typeForm = form.children[2];
    auto type = parseType(typeForm);
    if (type == std::nullopt) {
        return std::nullopt;
    }

    if (form.children.size() == 4) {
        auto initialValue = parseArgExpression(form.children[3]);
        if (initialValue != std::nullopt) {
            auto res = std::make_unique<VarDeclarationNode>(varName.value(), std::move(type.value()));
            res->initialValue = std::move(initialValue.value());
            return res;
        } else {
            return std::nullopt;
        }
    } else {
        return std::make_unique<VarDeclarationNode>(varName.value(), std::move(type.value()));
    }
}

std::optional<std::unique_ptr<ASTNode>> SemanticAnalyzer::parseArgExpression(const Parser::Expression &form) {
    // atom: Literal Variable
    // list: FnCall
    if (form.token != nullptr) {
        switch (form.token->type) {
        case token::Type::BooleanLiteral: {
            auto t = form.token->as<token::BooleanLiteral>();
            return std::make_unique<ConstantBoolNode>(t->value);
        }
        case token::Type::IntegerLiteral: {
            auto t = form.token->as<token::IntegerLiteral>();
            auto type = ASTIntegerType(makeIntTypeSignature(*t), t->isSigned, t->bits);
            if (t->isSigned) {
                return std::make_unique<ConstantIntNode>(type, std::get<int64_t>(t->value));
            } else {
                return std::make_unique<ConstantIntNode>(type, std::get<uint64_t>(t->value));
            }
        }
        case token::Type::CharLiteral: {
            // TODO: oops, missing
        }
        case token::Type::FloatLiteral: {
            // TODO: not implemented
        }
        case token::Type::StringLiteral: {
            auto s = form.token->as<token::StringLiteral>();
            return std::make_unique<ConstantStringNode>(s->contents);
        }
        case token::Type::Symbol: {
            // variable access
            auto sym = form.token->as<token::Symbol>();
            return std::make_unique<VarAccessNode>(sym->symbol);
        }
        default:
            reportError(form.token, "unexpected token");
            return std::nullopt;
        }
    } else {
        // list
        if (form.children.size() == 0) {
            reportError(form.token, "unexpected empty list");
            return std::nullopt;
        }
        const auto &symForm = form.children[0];
        auto sym = getSymbol(symForm);
        if (sym == std::nullopt) {
            reportError(symForm.token, "expected function name");
            return std::nullopt;
        }

        // special cases
        if (sym == "deref") {
            if (form.children.size() == 1) {
                reportError(symForm.token, "dereference must have a target");
                return std::nullopt;
            }

            if (form.children.size() > 2) {
                reportError(symForm.token, "dereference must have exactly one target");
                return std::nullopt;
            }

            auto target = parseArgExpression(form.children[1]);
            if (target == std::nullopt) {
                return std::nullopt;
            }
            return std::make_unique<DereferenceNode>(std::move(target.value()));
        } else if (sym == "cast") {
            if (form.children.size() < 3) {
                reportError(symForm.token, "cast must have type and target");
                return std::nullopt;
            }

            if (form.children.size() > 3) {
                reportError(symForm.token, "expected exactly two arguments for cast: type and target");
                return std::nullopt;
            }

            auto type = parseType(form.children[1]);
            if (type == std::nullopt) {
                return std::nullopt;
            }

            auto target = parseArgExpression(form.children[2]);
            if (target == std::nullopt) {
                return std::nullopt;
            }

            return std::make_unique<CastNode>(std::move(type.value()), std::move(target.value()));
        } else if (sym == "size-of") {
            if (form.children.size() != 2) {
                reportError(symForm.token, "size-of must have exaclty one argument");
                return std::nullopt;
            }

            auto type = parseType(form.children[1]);
            if (type == std::nullopt) {
                // TODO: support arrays
                return std::nullopt;
            }

            return std::make_unique<SizeofNode>(std::move(type.value()));
        } else {
            auto fnCall = std::make_unique<FunctionCallNode>(sym.value());
            for (auto i = 1u; i < form.children.size(); i++) {
                auto arg = parseArgExpression(form.children[i]);
                if (arg == std::nullopt) {
                    return std::nullopt;
                }
                fnCall->args.push_back(std::move(arg.value()));
            }
            return fnCall;
        }
    }
}

std::optional<std::unique_ptr<ASTType>> SemanticAnalyzer::parseType(const Parser::Expression &form) {
// can be ptr, array, basic or custom:
// ptr: (ptr Type)
// array: (array 10 Type)
// basic: f32, f64, bool, void, i|u[1-999]
// custom: Symbol
    if (form.token != nullptr) {
        // basic or custom
        auto sym = getSymbol(form);
        if (sym == std::nullopt) {
            reportError(form.token, "only symbols allowed as type names");
            return std::nullopt;
        }

        fc_assert(!sym.value().empty());
        if (sym == "f32") {
            return std::make_unique<ASTBuiltinType>("f32");
        } else if (sym == "f64") {
            return std::make_unique<ASTBuiltinType>("f64");
        } else if (sym == "void") {
            return std::make_unique<ASTBuiltinType>("void");
        } else if (sym == "bool") {
            return std::make_unique<ASTBuiltinType>("bool");
        } else if (sym.value()[0] == 'i' || sym.value()[0] == 'u') {
            auto intType = parseIntType(sym.value());
            if (intType != std::nullopt) {
                return intType;
            }
            // Then it's smth like u666-MyType, that is, a custom type
        }
        return std::make_unique<ASTNamedType>(sym.value());
    } else {
        // ptr or array
        if (form.children.empty()) {
            reportError(form.token, "expected type");
            return std::nullopt;
        }

        const auto &kind = form.children[0];
        if (kind.token == nullptr) {
            reportError(kind.token, "unexpected form in type declaration");
            return std::nullopt;
        }

        auto sym = getSymbol(kind);
        if (sym == std::nullopt) {
            reportError(kind.token, "unexpected type specifier");
            return std::nullopt;
        }

        if (sym == "ptr") {
            // (ptr Type)
            if (form.children.size() != 2) {
                reportError(kind.token, "malformed pointer type declaration");
                return std::nullopt;
            }

            auto pointeeType = parseType(form.children[1]);
            if (pointeeType == std::nullopt) {
                // Error has already been reported in parseType
                return std::nullopt;
            }

            return std::make_unique<ASTPointerType>(std::move(pointeeType.value()));
        } else if (sym == "array") {
            // (array IntLiteral Type)
            if (form.children.size() != 3) {
                reportError(kind.token, "malformed array declaration");
                return std::nullopt;
            }

            const auto &sizeExpr = form.children[1];
            auto size = getInteger<size_t>(sizeExpr);
            if (size == std::nullopt) {
                reportError(sizeExpr.token, "malformed array declaration");
                return std::nullopt;
            }

            auto targetType = parseType(form.children[2]);
            if (targetType == std::nullopt) {
                return std::nullopt;
            }

            return std::make_unique<ASTArrayType>(std::move(targetType.value()), size.value());
        } else {
            reportError(kind.token, "unknown compound type");
            return std::nullopt;
        }
    }
}

bool SemanticAnalyzer::parseStructureDefinition(const Parser::Expression &) {
    // TODO
    return true;
}

void SemanticAnalyzer::reportError(const std::unique_ptr<token::Token> &token, const std::string &errorMsg) {
    auto line = 0u;
    auto col = 0u;
    if (token != nullptr) {
        line = token->line;
        col = token->col;
    }
    errors.push_back({ "", errorMsg, line, col });
}

std::vector<SemanticAnalyzer::Error> SemanticAnalyzer::getErrors() const {
    return errors;
}

void SemanticAnalyzer::dumpErrors() const {
    for (const auto &e : errors) {
        printError(parser->currentFileName, e);
    }
}

std::unique_ptr<ModuleNode> SemanticAnalyzer::releaseModule() {
    return std::move(module);
}
