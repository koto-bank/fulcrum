#include <cerrno>
#include <optional>

#include <fmt/format.h>

#include "assert.hpp"
#include "lexer.hpp"
#include "utils.hpp"

namespace {
bool isDelimiter(char c) {
    return std::isspace(c) || c == '(' || c == ')';
}

bool isNumber(const std::string &str) {
    fc_assert(!str.empty());
    return std::isdigit(str[0])
        || (str.size() > 1
            && std::isdigit(str[1])
            && (str[0] == '-' || str[0] == '+'));
}

void escapeChar(char c, std::string& str) {
    if (std::isdigit(c)) {
        str += (c - '0');
        return;
    }
    switch (c) {
    case 'n':
        str += '\n';
        break;
    case 't':
        str += '\t';
        break;
    default:
        str += c;
    }
}
}

template <typename T, typename ...Args>
void Lexer::pushToken(Args &&...args) {
    auto token = std::make_unique<T>(std::forward<Args>(args)...);
    token->line = line;
    token->col = col;
    tokens.push_back(std::move(token));
}

template <uint32_t code, typename T, typename ...Args>
void Lexer::pushError(Args &&...args) {
    auto token = std::make_unique<token::Error>(code,
                                                std::make_unique<T>(std::forward<Args>(args)...));
    token->line = line;
    token->col = col;
    tokens.push_back(std::move(token));
}

template <uint32_t code>
void Lexer::pushError() {
    auto token = std::make_unique<token::Error>(code, nullptr);
    token->line = line;
    token->col = col;
    tokens.push_back(std::move(token));
}

Lexer::Tokens Lexer::lex(std::istream *stream) {
    char c = 0;
    while (stream->get(c)) {
        if (utils::isLineBreak(c)) {
            if (inString) {
                if (escape) {
                    escape = false;
                    escapeChar(c, currentTokenStr);
                    continue;
                }
                pushError<token::Error::LineBreakInString, token::StringLiteral>(currentTokenStr);
                inString = false;
                currentTokenStr.clear();
            } else if (inCharLiteral) {
                pushError<token::Error::LineBreakInChar>();
                inCharLiteral = false;
                currentTokenStr.clear();
            } else if (inComment) {
                inComment = false;
            } else {
                // id or something idk, lines end all the time
                pushToken();
            }

            col = 0;
            line++;
            continue;
        } else {
            col++;
        }

        if (inComment) {
            continue;
        } else if (inString) {
            if (escape) {
                escape = false;
                escapeChar(c, currentTokenStr);
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                fc_assert(inString);
                inString = false;
                pushToken<token::StringLiteral>(currentTokenStr);
                currentTokenStr.clear();
            } else {
                currentTokenStr += c;
            }
        } else if (inCharLiteral) {
            if (escape) {
                escape = false;
                escapeChar(c, currentTokenStr);
            } else if (c == '\\') {
                escape = true;
            } else if (c == '\'') {
                fc_assert(inCharLiteral);
                inCharLiteral = false;
                if (currentTokenStr.size() > 1) {
                    pushError<token::Error::CharLiteralTooLong>();
                } else {
                    fc_assert(!currentTokenStr.empty());
                    pushToken<token::CharLiteral>(currentTokenStr[0]);
                }
                currentTokenStr.clear();
            } else {
                currentTokenStr += c;
            }
        } else if (c == '\'') {
            fc_assert(!inCharLiteral);
            inCharLiteral = true;
            pushToken();
        } else if (c == '"') {
            fc_assert(!inString);
            inString = true;
            pushToken();
        } else if (c == ';') {
            inComment = true;
            pushToken();
        } else if (isDelimiter(c)) {
            pushToken();
            if (c == '(') {
                pushToken<token::LParen>();
            } else if (c == ')') {
                pushToken<token::RParen>();
            }
        } else {
            currentTokenStr += c;
        }
    }
    fc_assert(stream->eof());
    if (inString) {
        pushError<token::Error::EOFInString>();
    } else if (inCharLiteral) {
        pushError<token::Error::EOFInChar>();
    } else if (!currentTokenStr.empty()) {
        pushToken();
        pushToken<token::EndOfFile>();
    } else {
        pushToken<token::EndOfFile>();
    }

    return std::move(tokens);
}

void Lexer::pushToken() {
    if (currentTokenStr.empty()) {
        return;
    }
    if (isNumber(currentTokenStr)) {
        tokens.push_back(readNumber());
    } else {
        if (currentTokenStr == "true") {
            pushToken<token::BooleanLiteral>(true);
        } else if (currentTokenStr == "false") {
            pushToken<token::BooleanLiteral>(false);
        } else if (currentTokenStr[0] == ':') {
            pushToken<token::Keyword>(currentTokenStr);
        } else {
            pushToken<token::Symbol>(currentTokenStr);
        }
    }
    currentTokenStr.clear();
}

std::unique_ptr<token::Token> Lexer::readNumber() {

    // Integers
    errno = 0;
    char* end = nullptr;
    const auto strEnd = &*currentTokenStr.end();
    auto intRes = std::strtoll(currentTokenStr.data(), &end, 0);
    if (errno == ERANGE) {
        return std::make_unique<token::Error>(token::Error::IntegerOverflow, nullptr);
    }

    if (end == strEnd) {
        return std::make_unique<token::IntegerLiteral>(static_cast<int64_t>(intRes), 32u);
    } else if (*end == '.' || *end == 'f' || *end == 'p') {
        // 'p' for hex exponent in floats
        goto extractFloat;
    } else {
        // try to extract signedness and bits
        bool isSigned = true;
        auto bits = 32u;
        if (*end == 'u') {
            isSigned = false;
        } else if (*end == 'i') {
            isSigned = true;
        } else {
            // Anything other than 'u' or 'i' immediately after the number is not allowed
            return std::make_unique<token::Error>(token::Error::IllFormedInteger, nullptr);
        }

        end++;
        if (end != strEnd) {
            char* bitsEnd = nullptr;
            bits = std::strtol(end, &bitsEnd, 0);
            fc_assert(bitsEnd == strEnd || isDelimiter(*bitsEnd));
            if (bits > 128u || bits <= 0) {
                if (isSigned) {
                    return std::make_unique<token::Error>(token::Error::IntegerBadBitSize,
                                                          std::make_unique<token::IntegerLiteral>(static_cast<int64_t>(intRes), bits));
                } else {
                    return std::make_unique<token::Error>(token::Error::IntegerBadBitSize,
                                                          std::make_unique<token::IntegerLiteral>(static_cast<uint64_t>(intRes), bits));
                }
            }
        }
        if (isSigned) {
            return std::make_unique<token::IntegerLiteral>(static_cast<int64_t>(intRes), bits);
        } else {
            return std::make_unique<token::IntegerLiteral>(static_cast<uint64_t>(intRes), bits);
        }
    }

    // Floats
extractFloat:
    errno = 0;
    end = nullptr;
    auto floatRes = std::strtod(currentTokenStr.data(), &end);
    if (errno == ERANGE) {
        return std::make_unique<token::Error>(token::Error::FloatOverflow, nullptr);
    }

    if (end == &*currentTokenStr.end()) {
        return std::make_unique<token::FloatLiteral>(floatRes, 64u);
    } else if (*end != 'f') {
        // Anything other than 'f32' or 'f64' is not allowed
        return std::make_unique<token::Error>(token::Error::IllFormedFloat, nullptr);
    } else {
        end++;
        char* bitsEnd = nullptr;
        auto bits = std::strtol(end, &bitsEnd, 0);
        fc_assert(bitsEnd == strEnd || isDelimiter(*bitsEnd));
        if (bits != 32 && bits != 64) {
            return std::make_unique<token::Error>(token::Error::FloatBadBitSize,
                                                  std::make_unique<token::FloatLiteral>(floatRes, bits));
        }
        return std::make_unique<token::FloatLiteral>(floatRes, bits);
    }

    return std::make_unique<token::Error>(token::Error::NumericValueParsingFailed, nullptr);
}
