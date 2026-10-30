// parser_expr.cpp - expressions: precedence climbing over the C++ operator table.
//
//   expression  := assignment (',' assignment)*
//   assignment  := throw-expr | conditional (assign-op assignment)?          right-assoc
//   conditional := binary ('?' expression ':' assignment)?
//   binary      := unary (bin-op unary)*      levels 1..11:  || && | ^ & ==!= <> <<>> +- */% .*->*
//   unary       := prefix-op unary | sizeof | new | delete | (type) unary | postfix
//   postfix     := primary ( call | index | . member | -> member | ++ | -- )*
//   primary     := literal | this | id-expression | ( expression ) | lambda | cast | {init-list}
#include "parser.hpp"

int Parser::binaryPrecedence(const Token& k) {
    if (k.kind != Tok::Punct) return 0;
    const std::string& s = k.text;
    if (s == "||") return 1;
    if (s == "&&") return 2;
    if (s == "|") return 3;
    if (s == "^") return 4;
    if (s == "&") return 5;
    if (s == "==" || s == "!=") return 6;
    if (s == "<" || s == ">" || s == "<=" || s == ">=") return 7;
    if (s == "<<" || s == ">>") return 8;
    if (s == "+" || s == "-") return 9;
    if (s == "*" || s == "/" || s == "%") return 10;
    if (s == ".*" || s == "->*") return 11;
    return 0;
}

NodePtr Parser::parseExpression() {
    const int line = cur().line;
    NodePtr first = parseAssignment();
    if (!isP(",")) return first;
    auto seq = makeNode("CommaExpr", "", line);
    seq->add(std::move(first));
    while (acceptP(",")) seq->add(parseAssignment());
    return seq;
}

NodePtr Parser::parseAssignment() {
    DepthGuard guard(*this);
    if (tooDeep()) return makeNode("Error", "", cur().line);
    const int line = cur().line;
    if (isK("throw")) {
        advance();
        auto n = makeNode("ThrowExpr", "", line);
        if (!isP(";") && !isP(")") && !isP(",") && !isP("}") && !atEof()) n->add(parseAssignment());
        return n;
    }
    NodePtr lhs = parseConditional();
    static const char* const ops[] = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
    for (const char* op : ops) {
        if (!isP(op)) continue;
        const int opLine = cur().line;
        advance();
        auto n = makeNode("AssignExpr", op, opLine);
        n->add(std::move(lhs));
        if (isP("{"))
            n->add(parseBracedInit());
        else
            n->add(parseAssignment());
        return n;
    }
    return lhs;
}

NodePtr Parser::parseConditional() {
    NodePtr cond = parseBinary(1);
    if (!isP("?")) return cond;
    const int line = cur().line;
    advance();
    auto n = makeNode("ConditionalExpr", "", line);
    n->add(std::move(cond));
    n->add(parseExpression());
    expectP(":", "in conditional expression");
    n->add(parseAssignment());
    return n;
}

// Precedence climbing: all binary operators here are left-associative.
NodePtr Parser::parseBinary(int minPrec) {
    NodePtr lhs = parseUnary();
    while (true) {
        const int prec = binaryPrecedence(cur());
        if (prec == 0 || prec < minPrec) break;
        const std::string op = cur().text;
        const int line = cur().line;
        advance();
        NodePtr rhs = parseBinary(prec + 1);
        auto n = makeNode("BinaryExpr", op, line);
        n->add(std::move(lhs));
        n->add(std::move(rhs));
        lhs = std::move(n);
    }
    return lhs;
}

NodePtr Parser::parseUnary() {
    DepthGuard guard(*this);
    if (tooDeep()) return makeNode("Error", "", cur().line);
    const int line = cur().line;

    if (cur().kind == Tok::Punct) {
        const std::string s = cur().text;
        if (s == "+" || s == "-" || s == "!" || s == "~" || s == "*" || s == "&" || s == "++" || s == "--") {
            advance();
            auto n = makeNode("UnaryExpr", s, line);
            n->add(parseUnary());
            return n;
        }
        if (s == "(" && looksLikeCast()) {  // C-style cast
            advance();
            NodePtr type = parseTypeName();
            expectP(")", "to close cast");
            auto n = makeNode("CastExpr", "(" + type->text + ")", line);
            n->add(parseUnary());
            return n;
        }
    }

    if (cur().kind == Tok::Keyword) {
        const std::string kw = cur().text;
        if (kw == "sizeof" || kw == "alignof") {
            advance();
            const std::string kind = kw == "sizeof" ? "SizeofExpr" : "AlignofExpr";
            if (kw == "sizeof" && isP("...")) {  // sizeof...(Pack)
                advance();
                auto n = makeNode(kind, "...", line);
                expectP("(", "after 'sizeof...'");
                n->add(makeNode("Identifier", expectIdentifier("in sizeof..."), line));
                expectP(")", "to close 'sizeof...'");
                return n;
            }
            if (isP("(") && startsType(pos_ + 1)) {
                advance();
                NodePtr type = parseTypeName();
                expectP(")", ("to close '" + kw + "'").c_str());
                return makeNode(kind, type->text, line);
            }
            auto n = makeNode(kind, "", line);
            n->add(parseUnary());
            return n;
        }
        if (kw == "new") return parseNew();
        if (kw == "delete") {
            advance();
            const bool array = isP("[") && tokP(pos_ + 1, "]");
            if (array) {
                advance();
                advance();
            }
            auto n = makeNode("DeleteExpr", array ? "[]" : "", line);
            n->add(parseUnary());
            return n;
        }
    }
    return parsePostfix();
}

NodePtr Parser::parseNew() {
    const int line = cur().line;
    advance();  // 'new'
    auto n = makeNode("NewExpr", "", line);
    if (isP("(") && !startsType(pos_ + 1)) {  // placement arguments: new (buffer) T
        advance();
        auto placement = makeNode("Placement", "", line);
        parseArguments(*placement, ")");
        expectP(")", "to close placement arguments");
        n->add(std::move(placement));
    }
    bool parenthesised = false;
    if (isP("(") && startsType(pos_ + 1)) {  // new (int)
        advance();
        parenthesised = true;
    }
    NodePtr type = parseTypeSpecifier();
    while (isP("*") || isP("&") || isCvKw(cur())) {
        type->text += cur().text;
        advance();
    }
    n->text = type->text;
    n->add(std::move(type));
    if (parenthesised) expectP(")", "to close type in new-expression");
    while (isP("[")) {  // array new
        const int bracketLine = cur().line;
        advance();
        auto size = makeNode("ArraySize", "", bracketLine);
        if (!isP("]")) size->add(parseExpression());
        expectP("]", "to close array size");
        n->add(std::move(size));
    }
    if (isP("(")) {
        advance();
        auto args = makeNode("Arguments", "", line);
        parseArguments(*args, ")");
        expectP(")", "to close constructor arguments");
        n->add(std::move(args));
    } else if (isP("{")) {
        n->add(parseBracedInit());
    }
    return n;
}

NodePtr Parser::parsePostfix() {
    NodePtr e = parsePrimary();
    while (true) {
        const int line = cur().line;
        if (isP("(")) {
            advance();
            auto call = makeNode("CallExpr", "", line);
            call->add(std::move(e));
            parseArguments(*call, ")");
            expectP(")", "to close call");
            e = std::move(call);
        } else if (isP("[")) {
            advance();
            auto index = makeNode("IndexExpr", "", line);
            index->add(std::move(e));
            if (isP("{"))
                index->add(parseBracedInit());
            else
                index->add(parseExpression());
            expectP("]", "to close subscript");
            e = std::move(index);
        } else if (isP(".") || isP("->")) {
            const std::string op = cur().text;
            advance();
            acceptK("template");
            std::string member;
            auto access = makeNode("MemberExpr", "", line);
            access->add(std::move(e));
            if (isP("~")) {
                member = "~";
                advance();
            }
            if (isK("operator")) {
                member += parseOperatorName();
            } else if (isId()) {
                member += cur().text;
                advance();
                if (isP("<") && looksLikeTemplateArgs(pos_)) {
                    const size_t start = pos_;
                    NodePtr args = parseTemplateArgs();
                    member += spell(start, pos_);
                    access->add(std::move(args));
                }
            } else {
                errorHere("expected member name after '" + op + "'");
            }
            access->text = op + member;
            e = std::move(access);
        } else if (isP("++") || isP("--")) {
            auto post = makeNode("PostfixExpr", cur().text, line);
            advance();
            post->add(std::move(e));
            e = std::move(post);
        } else {
            break;
        }
    }
    return e;
}

NodePtr Parser::parsePrimary() {
    const int line = cur().line;
    const Token& k = cur();

    switch (k.kind) {
        case Tok::IntLiteral: {
            auto n = makeNode("IntLiteral", k.text, line);
            advance();
            return n;
        }
        case Tok::FloatLiteral: {
            auto n = makeNode("FloatLiteral", k.text, line);
            advance();
            return n;
        }
        case Tok::CharLiteral: {
            auto n = makeNode("CharLiteral", k.text, line);
            advance();
            return n;
        }
        case Tok::StringLiteral: {
            std::string text = k.text;
            advance();
            while (cur().kind == Tok::StringLiteral) {  // adjacent literals concatenate
                text += " " + cur().text;
                advance();
            }
            return makeNode("StringLiteral", text, line);
        }
        case Tok::Identifier:
            return parseIdExpression();
        case Tok::Keyword: {
            const std::string kw = k.text;
            if (kw == "true" || kw == "false") {
                advance();
                return makeNode("BoolLiteral", kw, line);
            }
            if (kw == "nullptr") {
                advance();
                return makeNode("NullptrLiteral", kw, line);
            }
            if (kw == "this") {
                advance();
                return makeNode("This", "", line);
            }
            if (kw == "static_cast" || kw == "dynamic_cast" || kw == "const_cast" || kw == "reinterpret_cast") {
                advance();
                expectP("<", "after cast keyword");
                NodePtr type = parseTypeName();
                closeAngle();
                expectP("(", "after cast");
                auto n = makeNode("CastExpr", kw + "<" + type->text + ">", line);
                n->add(parseExpression());
                expectP(")", "to close cast");
                return n;
            }
            if (kw == "typeid") {
                advance();
                expectP("(", "after 'typeid'");
                auto n = makeNode("TypeidExpr", "", line);
                if (startsType(pos_))
                    n->add(parseTypeName());
                else
                    n->add(parseExpression());
                expectP(")", "to close 'typeid'");
                return n;
            }
            if (kw == "noexcept") {  // noexcept(expr) operator
                advance();
                auto n = makeNode("NoexceptExpr", "", line);
                expectP("(", "after 'noexcept'");
                n->add(parseExpression());
                expectP(")", "to close 'noexcept'");
                return n;
            }
            if (kw == "operator") return makeNode("Identifier", parseOperatorName(), line);
            if (isBuiltinTypeKw(k)) {  // int(3), double{1.5}, unsigned(x)
                NodePtr type = parseTypeSpecifier();
                auto n = makeNode("ConstructExpr", type->text, line);
                if (isP("(")) {
                    advance();
                    parseArguments(*n, ")");
                    expectP(")", "to close constructor call");
                } else if (isP("{")) {
                    n->add(parseBracedInit());
                } else {
                    errorAfterPrev("expected '(' or '{' after type name");
                }
                return n;
            }
            break;
        }
        case Tok::Punct: {
            if (k.text == "(") {
                advance();
                auto n = makeNode("ParenExpr", "", line);
                n->add(parseExpression());
                expectP(")", "to close '('");
                return n;
            }
            if (k.text == "[") return parseLambda();
            if (k.text == "{") return parseBracedInit();
            if (k.text == "::") return parseIdExpression();
            break;
        }
        default:
            break;
    }
    errorHere("expected an expression but found '" + cur().text + "'");
    return makeNode("Error", "", line);
}

NodePtr Parser::parseIdExpression() {
    const int line = cur().line;
    auto id = makeNode("Identifier", "", line);
    id->text = parseQualifiedName(id.get(), true);
    // Point{1, 2}   ns::Point{1, 2}   std::pair<int, int>{1, 2}
    const bool hasTemplateArgs = !id->kids.empty();
    const size_t colons = id->text.rfind("::");
    const std::string lastPart = colons == std::string::npos ? id->text : id->text.substr(colons + 2);
    if (isP("{") && !id->text.empty() && (hasTemplateArgs || typeNames_.count(lastPart))) {
        auto n = makeNode("ConstructExpr", id->text, line);
        n->add(parseBracedInit());
        return n;
    }
    return id;
}

// [captures](params) mutable noexcept -> ret { body }
NodePtr Parser::parseLambda() {
    const int line = cur().line;
    advance();  // '['
    auto n = makeNode("LambdaExpr", "", line);
    auto captures = makeNode("Captures", "", line);
    while (!isP("]") && !atEof()) {
        std::string item;
        NodePtr init;
        if (isP("=")) {
            item = "=";
            advance();
        } else {
            if (isP("&")) {
                item = "&";
                advance();
            }
            if (isK("this")) {
                item += "this";
                advance();
            } else if (isP("*") && tokK(pos_ + 1, "this")) {
                item += "*this";
                advance();
                advance();
            } else if (isId()) {
                item += cur().text;
                advance();
                if (isP("=")) {  // init-capture: [x = expr]
                    advance();
                    init = parseAssignment();
                }
            }
        }
        if (isP("...")) advance();
        if (item.empty()) {
            errorHere("invalid lambda capture");
            break;
        }
        auto cap = makeNode("Capture", item, line);
        cap->add(std::move(init));
        captures->add(std::move(cap));
        if (!acceptP(",")) break;
    }
    expectP("]", "to close lambda capture list");
    n->add(std::move(captures));
    if (isP("(")) n->add(parseParameterList());
    while (isK("mutable") || isK("constexpr") || isK("noexcept")) {
        const bool isNoexcept = isK("noexcept");
        advance();
        if (isNoexcept && isP("(")) {
            advance();
            parseExpression();
            expectP(")", "to close 'noexcept'");
        }
    }
    if (acceptP("->")) n->add(parseTypeName());
    n->add(parseCompound());
    return n;
}

// { a, b, {c, d}, }
NodePtr Parser::parseBracedInit() {
    DepthGuard guard(*this);
    if (tooDeep()) return makeNode("Error", "", cur().line);
    const int line = cur().line;
    auto n = makeNode("InitializerList", "", line);
    if (!expectP("{", "to start initializer list")) return n;
    while (!isP("}") && !atEof()) {
        const size_t before = pos_;
        if (isP("{"))
            n->add(parseBracedInit());
        else
            n->add(parseAssignment());
        if (pos_ == before || !acceptP(",")) break;
    }
    expectP("}", "to close initializer list");
    return n;
}

// Comma-separated call arguments; the caller consumes the closing token.
void Parser::parseArguments(Node& call, const char* closer) {
    if (isP(closer)) return;
    do {
        const size_t before = pos_;
        if (isP("{"))
            call.add(parseBracedInit());
        else
            call.add(parseAssignment());
        if (isP("...")) advance();  // pack expansion
        if (pos_ == before) break;
    } while (acceptP(","));
}
