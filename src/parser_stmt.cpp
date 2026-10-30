// parser_stmt.cpp - statements.
//
//   statement := compound | if | switch | while | do-while | for | range-for | return
//              | break | continue | goto | try-catch | label | declaration | expression ';'
#include "parser.hpp"

NodePtr Parser::parseStatement() {
    DepthGuard guard(*this);
    if (tooDeep()) return makeNode("Error", "", cur().line);
    const int line = cur().line;

    if (cur().kind == Tok::Preprocessor) {
        auto n = makeNode("Preprocessor", cur().text, line);
        advance();
        return n;
    }
    if (isP("{")) return parseCompound();
    if (acceptP(";")) return makeNode("EmptyStatement", "", line);

    if (cur().kind == Tok::Keyword) {
        const std::string kw = cur().text;
        if (kw == "if") return parseIf();
        if (kw == "switch") return parseSwitch();
        if (kw == "while") return parseWhile();
        if (kw == "do") return parseDo();
        if (kw == "for") return parseFor();
        if (kw == "return") return parseReturn();
        if (kw == "try") return parseTry();
        if (kw == "break" || kw == "continue" || kw == "goto") return parseJump(kw.c_str());
        if (kw == "case" || kw == "default") return parseLabel();
    }
    if (isId() && tokP(pos_ + 1, ":")) return parseLabel();  // goto label

    if (isDeclarationStart()) return parseDeclaration(Scope::Block, "");

    auto n = makeNode("ExpressionStatement", "", line);
    n->add(parseExpression());
    expectSemi("expression");
    return n;
}

// { statement* }   A missing '}' is reported on the last line that was read.
NodePtr Parser::parseCompound() {
    const int openLine = cur().line;
    auto block = makeNode("CompoundStatement", "", openLine);
    if (!expectP("{", "to start a block")) return block;
    while (!isP("}") && !atEof()) {
        const size_t before = pos_;
        block->add(parseStatement());
        if (pos_ == before) {  // no progress: skip the offending token so we cannot loop forever
            errorHere("unexpected '" + cur().text + "'");
            advance();
        }
    }
    if (!acceptP("}")) errorAfterPrev("expected '}' to close block opened at line " + std::to_string(openLine));
    return block;
}

// case expr:  |  default:  |  label:
NodePtr Parser::parseLabel() {
    const int line = cur().line;
    if (acceptK("case")) {
        auto n = makeNode("CaseLabel", "", line);
        n->add(parseConditional());
        expectP(":", "after case label");
        return n;
    }
    if (acceptK("default")) {
        expectP(":", "after 'default'");
        return makeNode("DefaultLabel", "", line);
    }
    auto n = makeNode("Label", cur().text, line);
    advance();  // identifier
    advance();  // ':'
    return n;
}

// Condition of if/switch/while: an expression, a declaration (`if (auto p = f())`) or a
// C++17 init-statement followed by a condition (`if (int x = f(); x > 0)`).
NodePtr Parser::parseCondition() {
    const int line = cur().line;
    auto cond = makeNode("Condition", "", line);
    if (isDeclarationStart()) {
        NodePtr decl = parseSimpleDeclaration(Scope::Block, "", false);
        if (acceptP(";")) {
            auto init = makeNode("InitStatement", "", line);
            init->add(std::move(decl));
            cond->add(std::move(init));
            if (!isP(")")) cond->add(parseExpression());
        } else {
            cond->add(std::move(decl));
        }
        return cond;
    }
    NodePtr e = parseExpression();
    if (acceptP(";")) {
        auto init = makeNode("InitStatement", "", line);
        auto stmt = makeNode("ExpressionStatement", "", line);
        stmt->add(std::move(e));
        init->add(std::move(stmt));
        cond->add(std::move(init));
        if (!isP(")")) cond->add(parseExpression());
    } else {
        cond->add(std::move(e));
    }
    return cond;
}

NodePtr Parser::parseIf() {
    const int line = cur().line;
    advance();  // if
    auto n = makeNode("IfStmt", "", line);
    if (acceptK("constexpr")) n->text = "constexpr";
    expectP("(", "after 'if'");
    n->add(parseCondition());
    expectP(")", "to close 'if' condition");
    auto thenBranch = makeNode("Then", "", cur().line);
    thenBranch->add(parseStatement());
    n->add(std::move(thenBranch));
    if (isK("else")) {
        const int elseLine = cur().line;
        advance();
        auto elseBranch = makeNode("Else", "", elseLine);
        elseBranch->add(parseStatement());  // `else if` simply nests another IfStmt
        n->add(std::move(elseBranch));
    }
    return n;
}

NodePtr Parser::parseSwitch() {
    const int line = cur().line;
    advance();  // switch
    auto n = makeNode("SwitchStmt", "", line);
    expectP("(", "after 'switch'");
    n->add(parseCondition());
    expectP(")", "to close 'switch' condition");
    n->add(parseStatement());
    return n;
}

NodePtr Parser::parseWhile() {
    const int line = cur().line;
    advance();  // while
    auto n = makeNode("WhileStmt", "", line);
    expectP("(", "after 'while'");
    n->add(parseCondition());
    expectP(")", "to close 'while' condition");
    n->add(parseStatement());
    return n;
}

NodePtr Parser::parseDo() {
    const int line = cur().line;
    advance();  // do
    auto n = makeNode("DoWhileStmt", "", line);
    n->add(parseStatement());
    if (!acceptK("while")) errorAfterPrev("expected 'while' after the body of 'do'");
    expectP("(", "after 'while'");
    n->add(parseExpression());
    expectP(")", "to close 'do-while' condition");
    expectSemi("'do-while' statement");
    return n;
}

NodePtr Parser::parseFor() {
    const int line = cur().line;
    advance();  // for
    expectP("(", "after 'for'");

    if (isRangeFor()) {  // for (decl : range)
        auto n = makeNode("RangeForStmt", "", line);
        n->add(parseSimpleDeclaration(Scope::Block, "", false));
        expectP(":", "in range-based 'for'");
        if (isP("{"))
            n->add(parseBracedInit());
        else
            n->add(parseExpression());
        expectP(")", "to close 'for'");
        n->add(parseStatement());
        return n;
    }

    auto n = makeNode("ForStmt", "", line);
    auto init = makeNode("Init", "", cur().line);
    if (isP(";")) {
        advance();
    } else if (isDeclarationStart()) {
        init->add(parseSimpleDeclaration(Scope::Block, "", true));  // consumes the ';'
    } else {
        auto stmt = makeNode("ExpressionStatement", "", cur().line);
        stmt->add(parseExpression());
        expectSemi("'for' initializer");
        init->add(std::move(stmt));
    }
    n->add(std::move(init));

    auto cond = makeNode("Condition", "", cur().line);
    if (!isP(";")) cond->add(parseExpression());
    expectP(";", "after 'for' condition");
    n->add(std::move(cond));

    auto step = makeNode("Increment", "", cur().line);
    if (!isP(")")) step->add(parseExpression());
    n->add(std::move(step));

    expectP(")", "to close 'for'");
    n->add(parseStatement());
    return n;
}

NodePtr Parser::parseReturn() {
    const int line = cur().line;
    advance();  // return
    auto n = makeNode("ReturnStmt", "", line);
    if (!isP(";")) {
        if (isP("{"))
            n->add(parseBracedInit());
        else
            n->add(parseExpression());
    }
    expectSemi("return statement");
    return n;
}

NodePtr Parser::parseJump(const char* keyword) {
    const int line = cur().line;
    advance();
    const std::string kw = keyword;
    if (kw == "goto") {
        auto n = makeNode("GotoStmt", expectIdentifier("after 'goto'"), line);
        expectSemi("'goto'");
        return n;
    }
    auto n = makeNode(kw == "break" ? "BreakStmt" : "ContinueStmt", "", line);
    expectSemi(("'" + kw + "'").c_str());
    return n;
}

// try { ... } catch (T e) { ... } catch (...) { ... }
NodePtr Parser::parseTry() {
    const int line = cur().line;
    advance();  // try
    auto n = makeNode("TryStmt", "", line);
    n->add(parseCompound());
    if (!isK("catch")) errorAfterPrev("expected 'catch' after try block");
    while (isK("catch")) {
        const int catchLine = cur().line;
        advance();
        auto handler = makeNode("CatchClause", "", catchLine);
        expectP("(", "after 'catch'");
        if (isP("...")) {
            advance();
            handler->text = "...";
        } else if (!isP(")")) {
            handler->add(parseTypeSpecifier());
            std::string name;
            handler->add(parseDeclarator(true, name));
            handler->text = name;
        }
        expectP(")", "to close catch declaration");
        handler->add(parseCompound());
        n->add(std::move(handler));
    }
    return n;
}
