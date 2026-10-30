// parser.cpp - token helpers, error reporting and the lookahead heuristics that
// resolve C++'s context-sensitive ambiguities (declaration vs expression, template
// argument lists vs less-than, casts vs parenthesised expressions).
#include "parser.hpp"

Parser::Parser(std::vector<Token> tokens, Diagnostics& diag) : t_(std::move(tokens)), diag_(diag) {
    if (t_.empty() || t_.back().kind != Tok::Eof)
        t_.push_back(Token{Tok::Eof, "", t_.empty() ? 1 : t_.back().line, 1});
    scanIncludes();
}

// ---------------------------------------------------------------------------------------
// Token helpers
// ---------------------------------------------------------------------------------------
const Token& Parser::cur() const { return t_[pos_]; }
const Token& Parser::at(size_t p) const { return t_[p < t_.size() ? p : t_.size() - 1]; }
const Token& Parser::prev() const { return t_[pos_ > 0 ? pos_ - 1 : 0]; }
bool Parser::atEof() const { return cur().kind == Tok::Eof; }
bool Parser::isP(const char* s) const { return cur().kind == Tok::Punct && cur().text == s; }
bool Parser::isK(const char* s) const { return cur().kind == Tok::Keyword && cur().text == s; }
bool Parser::isId() const { return cur().kind == Tok::Identifier; }
bool Parser::isIdent(const char* s) const { return isId() && cur().text == s; }
bool Parser::tokP(size_t p, const char* s) const { return at(p).kind == Tok::Punct && at(p).text == s; }
bool Parser::tokK(size_t p, const char* s) const { return at(p).kind == Tok::Keyword && at(p).text == s; }
void Parser::advance() {
    if (!atEof()) ++pos_;
}

bool Parser::acceptP(const char* s) {
    if (!isP(s)) return false;
    advance();
    return true;
}
bool Parser::acceptK(const char* s) {
    if (!isK(s)) return false;
    advance();
    return true;
}

// A missing token is reported on the line of the token BEFORE it, because that is where
// the programmer forgot to type it (e.g. the ';' missing at the end of line 3).
bool Parser::expectP(const char* s, const char* context) {
    if (acceptP(s)) return true;
    errorAfterPrev(std::string("expected '") + s + "' " + context);
    return false;
}
void Parser::expectSemi(const char* context) {
    if (!acceptP(";")) errorAfterPrev(std::string("expected ';' after ") + context);
}
std::string Parser::expectIdentifier(const char* context) {
    if (isId()) {
        std::string name = cur().text;
        advance();
        return name;
    }
    errorHere(std::string("expected identifier ") + context);
    return "";
}

void Parser::errorHere(const std::string& msg) {
    const Token& k = atEof() ? prev() : cur();
    diag_.report(k.line, k.col, msg);
}
void Parser::errorAfterPrev(const std::string& msg) {
    const Token& k = pos_ > 0 ? prev() : cur();
    diag_.report(k.line, k.col, msg);
}

bool Parser::tooDeep() {
    if (depth_ <= 200) return false;
    errorHere("nesting is too deep");
    pos_ = t_.size() - 1;  // jump to EOF so that every loop unwinds
    return true;
}

// Joins tokens [from, to) into readable text: "unsigned long", "std::vector<int>".
std::string Parser::spell(size_t from, size_t to) const {
    auto wordy = [](const Token& k) {
        return k.kind == Tok::Identifier || k.kind == Tok::Keyword || k.kind == Tok::IntLiteral ||
               k.kind == Tok::FloatLiteral;
    };
    std::string out;
    for (size_t i = from; i < to && i < t_.size(); ++i) {
        if (i > from && ((wordy(t_[i - 1]) && wordy(t_[i])) || t_[i - 1].text == ",")) out += ' ';
        out += t_[i].text;
    }
    return out;
}

// Consumes one '>' that closes a template argument list. A '>>' (or '>=', '>>=') token is
// split in two so that  vector<vector<int>>  parses.
bool Parser::closeAngle() {
    if (isP(">")) {
        advance();
        return true;
    }
    if (isP(">>") || isP(">=") || isP(">>=")) {
        Token rest = cur();
        rest.text = rest.text.substr(1);
        rest.col += 1;
        t_[pos_].text = ">";
        t_.insert(t_.begin() + static_cast<std::ptrdiff_t>(pos_) + 1, rest);
        advance();
        return true;
    }
    errorAfterPrev("expected '>' to close template argument list");
    return false;
}

// ---------------------------------------------------------------------------------------
// Classification of keywords
// ---------------------------------------------------------------------------------------
bool Parser::isBuiltinTypeKw(const Token& k) {
    if (k.kind != Tok::Keyword) return false;
    static const std::unordered_set<std::string> kws = {"int",    "char",     "bool",     "short",    "long",
                                                        "signed", "unsigned", "float",    "double",   "void",
                                                        "wchar_t", "char16_t", "char32_t", "auto"};
    return kws.count(k.text) != 0;
}
bool Parser::isCvKw(const Token& k) {
    return k.kind == Tok::Keyword && (k.text == "const" || k.text == "volatile");
}
bool Parser::isStorageKw(const Token& k) {
    if (k.kind != Tok::Keyword) return false;
    static const std::unordered_set<std::string> kws = {"static",    "extern",  "inline",   "virtual",  "explicit",
                                                        "constexpr", "friend",  "mutable",  "thread_local", "register"};
    return kws.count(k.text) != 0;
}

// ---------------------------------------------------------------------------------------
// Lookahead (never consumes tokens)
// ---------------------------------------------------------------------------------------

// `p` is the index of a '<'. Returns the index just after the matching '>' or npos when
// the tokens cannot be a template argument list (e.g. `i < n;`).
size_t Parser::skipTemplateArgs(size_t p) const {
    if (!tokP(p, "<")) return npos;
    int angle = 0, paren = 0, bracket = 0, brace = 0;
    for (; p < t_.size(); ++p) {
        const Token& k = t_[p];
        if (k.kind == Tok::Eof) return npos;
        if (k.kind != Tok::Punct) continue;
        const std::string& s = k.text;
        const bool top = paren == 0 && bracket == 0 && brace == 0;
        if (s == "(") {
            ++paren;
        } else if (s == ")") {
            if (paren == 0) return npos;
            --paren;
        } else if (s == "[") {
            ++bracket;
        } else if (s == "]") {
            if (bracket == 0) return npos;
            --bracket;
        } else if (s == "{") {
            ++brace;
        } else if (s == "}") {
            if (brace == 0) return npos;
            --brace;
        } else if (s == ";") {
            return npos;
        } else if (top && s == "<") {
            ++angle;
        } else if (top && s == ">") {
            if (--angle == 0) return p + 1;
        } else if (top && s == ">>") {
            // '>>' is two closing '>'. When only one is needed here, the second one closes the
            // enclosing list, so the list ends in the middle of this token: return the index of
            // the '>>' itself so callers see that a '>' still follows (closeAngle splits it later).
            if (angle == 1) return p;
            angle -= 2;
            if (angle == 0) return p + 1;
        }
    }
    return npos;
}

// Skips a type: cv-qualifiers, builtin type words, or a (qualified, templated) name.
// Returns the index after it, or npos when no type starts at `p`.
size_t Parser::skipTypeName(size_t p) const {
    bool sawBase = false, sawName = false;
    while (true) {
        const Token& k = at(p);
        if (k.kind == Tok::Keyword) {
            if (isCvKw(k)) {
                ++p;
            } else if (isBuiltinTypeKw(k)) {
                if (sawName) break;
                sawBase = true;
                ++p;
            } else if (k.text == "struct" || k.text == "class" || k.text == "union" || k.text == "enum" ||
                       k.text == "typename") {
                ++p;
            } else if (k.text == "decltype") {
                if (!tokP(p + 1, "(")) return npos;
                int depth = 0;
                ++p;
                do {
                    if (tokP(p, "(")) ++depth;
                    if (tokP(p, ")")) --depth;
                    ++p;
                } while (depth > 0 && at(p).kind != Tok::Eof);
                sawBase = true;
            } else {
                break;
            }
            continue;
        }
        if (!sawBase && !sawName && (k.kind == Tok::Identifier || tokP(p, "::"))) {
            if (tokP(p, "::")) ++p;
            if (at(p).kind != Tok::Identifier) return npos;
            while (at(p).kind == Tok::Identifier) {
                ++p;
                if (tokP(p, "<")) {
                    const size_t e = skipTemplateArgs(p);
                    if (e != npos) p = e;
                }
                if (tokP(p, "::") && (at(p + 1).kind == Tok::Identifier || tokK(p + 1, "template"))) {
                    ++p;
                    if (tokK(p, "template")) ++p;
                    continue;
                }
                break;
            }
            sawName = true;
            continue;
        }
        break;
    }
    return (sawBase || sawName) ? p : npos;
}

// Can a type start at token `p`? Unknown plain identifiers are NOT types, which is how
// `a * b;` is read as a multiplication unless `a` was declared as a type earlier.
bool Parser::startsType(size_t p) const {
    const Token& k = at(p);
    if (k.kind == Tok::Keyword) {
        return isBuiltinTypeKw(k) || isCvKw(k) || k.text == "struct" || k.text == "class" || k.text == "union" ||
               k.text == "enum" || k.text == "typename" || k.text == "decltype";
    }
    if (k.kind == Tok::Identifier && typeNames_.count(k.text)) return true;
    if (k.kind == Tok::Identifier || tokP(p, "::")) {
        const size_t q = skipTypeName(p);
        if (q == npos || (k.kind == Tok::Identifier && q == p + 1)) return false;
        const Token& f = at(q);
        if (f.kind == Tok::Identifier) return true;
        return f.kind == Tok::Punct &&
               (f.text == ")" || f.text == "," || f.text == "*" || f.text == "&" || f.text == "&&" || f.text == "...");
    }
    return false;
}

// Statement-level test: type, optional pointer/reference operators, a declarator name and a
// token that can follow a declarator.
bool Parser::looksLikeDeclaration(size_t p) const {
    const Token& k = at(p);
    if (k.kind == Tok::Keyword) return startsType(p);
    const size_t q = skipTypeName(p);
    if (q == npos) return false;
    const bool unknownSimple = k.kind == Tok::Identifier && typeNames_.count(k.text) == 0 && q == p + 1;
    size_t r = q;
    bool sawPtr = false;
    while (tokP(r, "*") || tokP(r, "&") || tokP(r, "&&") || isCvKw(at(r))) {
        ++r;
        sawPtr = true;
    }
    if (unknownSimple && sawPtr) return false;  // `a * b;` is a multiplication
    if (at(r).kind != Tok::Identifier) return false;
    const Token& f = at(r + 1);
    return f.kind == Tok::Punct && (f.text == "=" || f.text == ";" || f.text == "," || f.text == "(" ||
                                    f.text == "{" || f.text == "[" || f.text == ":");
}

bool Parser::isDeclarationStart() const {
    const Token& k = cur();
    if (k.kind == Tok::Keyword) {
        if (isBuiltinTypeKw(k)) {  // `double{1.5};` is a functional cast, not a declaration
            const size_t q = skipTypeName(pos_);
            if (q != npos && tokP(q, "{")) return false;
        }
        return isStorageKw(k) || k.text == "typedef" || k.text == "using" || k.text == "static_assert" ||
               startsType(pos_);
    }
    if (k.kind == Tok::Identifier || isP("::")) return looksLikeDeclaration(pos_);
    return false;
}

// Expression context: `p` is a '<' right after a name. Treat it as a template argument
// list only when the contents look like types and a call/brace/scope follows.
bool Parser::looksLikeTemplateArgs(size_t p) const {
    const size_t e = skipTemplateArgs(p);
    if (e == npos) return false;
    if (!(tokP(e, "(") || tokP(e, "{") || tokP(e, "::"))) return false;
    const Token& first = at(p + 1);
    if (first.kind == Tok::Keyword) return isBuiltinTypeKw(first) || isCvKw(first) || first.text == "struct" || first.text == "class";
    if (first.kind == Tok::Identifier) return typeNames_.count(first.text) != 0 || tokP(p + 2, "::");
    return first.kind == Tok::IntLiteral;
}

// Inside template arguments: a qualified, templated or pointer name that ends the argument
// (`std::string>`, `ns::Foo*,`, `std::vector<int>>`) is a type even when it was never declared.
bool Parser::isTemplateTypeArg(size_t p) const {
    if (!(at(p).kind == Tok::Identifier || tokP(p, "::"))) return false;
    size_t q = skipTypeName(p);
    if (q == npos) return false;
    while (tokP(q, "*") || tokP(q, "&") || tokP(q, "&&") || isCvKw(at(q))) ++q;
    if (q <= p + 1) return false;  // a lone unknown identifier may be a constant: array<int, N>
    return tokP(q, ",") || tokP(q, ">") || tokP(q, ">>") || tokP(q, "...") || tokP(q, "(");
}

// At '(' : is this a C-style cast such as (int)x or (Foo*)p ?
bool Parser::looksLikeCast() const {
    if (!isP("(") || !startsType(pos_ + 1)) return false;
    size_t p = skipTypeName(pos_ + 1);
    if (p == npos) return false;
    while (tokP(p, "*") || tokP(p, "&") || tokP(p, "&&") || isCvKw(at(p))) ++p;
    if (!tokP(p, ")")) return false;
    const Token& n = at(p + 1);
    const bool builtin = isBuiltinTypeKw(at(pos_ + 1));
    switch (n.kind) {
        case Tok::Identifier:
        case Tok::IntLiteral:
        case Tok::FloatLiteral:
        case Tok::CharLiteral:
        case Tok::StringLiteral:
            return true;
        case Tok::Keyword:
            return n.text == "this" || n.text == "true" || n.text == "false" || n.text == "nullptr" ||
                   n.text == "sizeof" || n.text == "new" || n.text.find("_cast") != std::string::npos ||
                   isBuiltinTypeKw(n);
        case Tok::Punct:
            if (n.text == "(" || n.text == "!" || n.text == "~") return true;
            return builtin && (n.text == "-" || n.text == "+" || n.text == "*" || n.text == "&");
        default:
            return false;
    }
}

// Called just after the '(' of a for statement: is it  for (decl : range) ?
bool Parser::isRangeFor() const {
    int depth = 0, ternary = 0;
    for (size_t p = pos_; p < t_.size(); ++p) {
        const Token& k = t_[p];
        if (k.kind == Tok::Eof) return false;
        if (k.kind != Tok::Punct) continue;
        const std::string& s = k.text;
        if (s == "(" || s == "[" || s == "{") {
            ++depth;
        } else if (s == ")" || s == "]" || s == "}") {
            if (depth == 0) return false;
            --depth;
        } else if (depth == 0 && s == ";") {
            return false;
        } else if (depth == 0 && s == "?") {
            ++ternary;
        } else if (depth == 0 && s == ":") {
            if (ternary == 0) return true;
            --ternary;
        }
    }
    return false;
}

// At '(' after a declarator name: parameter list (function) or constructor arguments (variable)?
bool Parser::parenIsParameterList() const {
    return tokP(pos_ + 1, ")") || tokP(pos_ + 1, "...") || startsType(pos_ + 1);
}
