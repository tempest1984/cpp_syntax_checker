// parser_type.cpp - types, declarators, parameter lists and function tails.
//
//   type-specifier := (cv | builtin-type | decltype(expr) | qualified-name)+
//   qualified-name := '::'? name template-args? ('::' name template-args?)*
//   declarator     := ptr-op* (name | '(' '*' name ')' params | '[' ids ']') array-bound*
//   parameters     := '(' (type declarator ('=' expr)? (',' ...)* )? ')'
#include "parser.hpp"

// ---------------------------------------------------------------------------------------
// Names and template arguments
// ---------------------------------------------------------------------------------------

// a::b::c<int>::d   (`exprContext` decides whether '<' may start template arguments)
std::string Parser::parseQualifiedName(Node* holder, bool exprContext) {
    std::string text;
    if (isP("::")) {
        text += "::";
        advance();
    }
    while (true) {
        acceptK("template");  // dependent names:  T::template rebind<U>
        if (isP("~")) {
            text += "~";
            advance();
        }
        if (isK("operator")) {
            text += parseOperatorName();
            break;
        }
        if (!isId()) {
            errorHere("expected an identifier in name");
            break;
        }
        text += cur().text;
        advance();
        if (isP("<") && (!exprContext || looksLikeTemplateArgs(pos_))) {
            const size_t start = pos_;
            NodePtr args = parseTemplateArgs();
            text += spell(start, pos_);
            if (holder) holder->add(std::move(args));
        }
        if (isP("::") && (at(pos_ + 1).kind == Tok::Identifier || tokK(pos_ + 1, "template") ||
                          tokP(pos_ + 1, "~") || tokK(pos_ + 1, "operator"))) {
            text += "::";
            advance();
            continue;
        }
        break;
    }
    return text;
}

// operator+   operator()   operator[]   operator new[]   operator bool
std::string Parser::parseOperatorName() {
    advance();  // 'operator'
    std::string s = "operator";
    if (isP("(") && tokP(pos_ + 1, ")")) {
        s += "()";
        advance();
        advance();
    } else if (isP("[") && tokP(pos_ + 1, "]")) {
        s += "[]";
        advance();
        advance();
    } else if (isK("new") || isK("delete")) {
        s += " " + cur().text;
        advance();
        if (isP("[") && tokP(pos_ + 1, "]")) {
            s += "[]";
            advance();
            advance();
        }
    } else if (cur().kind == Tok::Punct) {
        s += cur().text;
        advance();
    } else {  // conversion function: operator bool()
        s += " " + parseTypeName()->text;
    }
    return s;
}

// < arg, arg, ... >   where each argument is a type or a constant expression
NodePtr Parser::parseTemplateArgs() {
    const int line = cur().line;
    auto n = makeNode("TemplateArgs", "", line);
    if (!expectP("<", "to open template arguments")) return n;
    if (!isP(">") && !isP(">>")) {
        do {
            const size_t before = pos_;
            if (startsType(pos_) || isTemplateTypeArg(pos_)) {
                NodePtr type = parseTypeName();
                if (isP("(")) {  // function type: std::function<void(int)>
                    type->add(parseParameterList());
                    type->text += "(...)";
                }
                n->add(std::move(type));
            } else {
                n->add(parseBinary(9));  // above shift level so that '>' closes the list
            }
            if (isP("...")) advance();
            if (pos_ == before) break;
        } while (acceptP(","));
    }
    closeAngle();
    return n;
}

// ---------------------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------------------

// const unsigned long | std::vector<int> | decltype(x) | struct Foo | typename T::type
NodePtr Parser::parseTypeSpecifier() {
    const int line = cur().line;
    const size_t start = pos_;
    auto type = makeNode("Type", "", line);
    bool sawBase = false;
    while (true) {
        if (isCvKw(cur())) {
            advance();
            continue;
        }
        if (isBuiltinTypeKw(cur())) {
            sawBase = true;
            advance();
            continue;
        }
        if (isK("decltype")) {
            advance();
            expectP("(", "after 'decltype'");
            type->add(parseExpression());
            expectP(")", "to close 'decltype'");
            sawBase = true;
            continue;
        }
        if (!sawBase) {
            // `struct Node* p;` and `typename T::type x;` introduce or defer the name
            // rather than looking it up, so they are not recorded as uses.
            bool elaborated = false;
            if (isK("struct") || isK("class") || isK("union") || isK("enum") || isK("typename")) {
                elaborated = true;
                advance();
            }
            if (isId() || isP("::")) {
                const size_t nameTok = pos_;
                const std::string name = parseQualifiedName(type.get(), false);
                noteTypeUse(nameTok, name, elaborated);
                sawBase = true;
                continue;
            }
        }
        break;
    }
    if (!sawBase) errorHere("expected a type name but found '" + cur().text + "'");
    type->text = spell(start, pos_);
    return type;
}

// A complete type-id as used in casts, sizeof, template arguments: type + '*' '&' '[]'
NodePtr Parser::parseTypeName() {
    NodePtr t = parseTypeSpecifier();
    std::string extra;
    while (isP("*") || isP("&") || isP("&&") || isCvKw(cur())) {
        extra += (isCvKw(cur()) ? " " : "") + cur().text;
        advance();
    }
    while (isP("[")) {
        advance();
        if (!isP("]")) parseConditional();
        expectP("]", "to close array type");
        extra += "[]";
    }
    if (isP("(") && tokP(pos_ + 1, "*")) {  // function pointer type: void (*)(int)
        advance();
        std::string inner;
        while (isP("*") || isP("&")) {
            inner += cur().text;
            advance();
        }
        expectP(")", "to close function pointer type");
        if (isP("(")) t->add(parseParameterList());
        extra += "(" + inner + ")(...)";
    }
    t->text += extra;
    return t;
}

// ---------------------------------------------------------------------------------------
// Declarators
// ---------------------------------------------------------------------------------------

// *const &name[3]   (*callback)(int)   [a, b]   ...  The name is returned through nameOut.
NodePtr Parser::parseDeclarator(bool abstractOk, std::string& nameOut) {
    const int line = cur().line;
    auto d = makeNode("Declarator", "", line);
    std::string ptrs;
    while (true) {
        if (isP("*") || isP("&") || isP("&&")) {
            ptrs += cur().text;
            advance();
        } else if (isCvKw(cur())) {
            ptrs += " " + cur().text + " ";
            advance();
        } else {
            break;
        }
    }

    std::string name;
    if (isP("(") && tokP(pos_ + 1, "*")) {  // function pointer: (*name)(params)
        advance();
        std::string inner;
        while (isP("*") || isP("&")) {
            inner += cur().text;
            advance();
        }
        if (isId()) {
            name = cur().text;
            advance();
        }
        while (isP("[")) {  // array of function pointers
            advance();
            if (!isP("]")) parseConditional();
            expectP("]", "to close array bound");
        }
        expectP(")", "to close function pointer declarator");
        d->kind = "FunctionPointerDeclarator";
        if (isP("("))
            d->add(parseParameterList());
        else
            errorAfterPrev("expected '(' for function pointer parameters");
        nameOut = name;
        d->text = ptrs + "(" + inner + name + ")";
        return d;
    }
    if (isK("operator")) {
        name = parseOperatorName();
    } else if (isId() || isP("::") || isP("~")) {
        name = parseQualifiedName(d.get(), false);
    } else if (isP("[") && !abstractOk) {  // structured binding: auto [a, b]
        advance();
        d->kind = "StructuredBinding";
        name = "[";
        do {
            const size_t before = pos_;
            const std::string id = expectIdentifier("in structured binding");
            name += (name.size() > 1 ? ", " : "") + id;
            if (pos_ == before) break;
        } while (acceptP(","));
        name += "]";
        expectP("]", "to close structured binding");
    } else if (!abstractOk) {
        errorHere("expected a declarator name but found '" + cur().text + "'");
    }

    while (isP("[")) {  // array bounds
        const int bracketLine = cur().line;
        advance();
        auto bound = makeNode("ArrayBound", "", bracketLine);
        if (!isP("]")) bound->add(parseConditional());
        expectP("]", "to close array bound");
        d->add(std::move(bound));
    }
    nameOut = name;
    d->text = ptrs + name;
    return d;
}

// ( parameter, parameter, ... )
NodePtr Parser::parseParameterList() {
    const int line = cur().line;
    auto params = makeNode("Parameters", "", line);
    if (!expectP("(", "to open parameter list")) return params;
    if (!isP(")")) {
        do {
            const size_t before = pos_;
            if (isP("...")) {  // C-style variadic
                advance();
                params->add(makeNode("Ellipsis", "...", line));
                break;
            }
            auto p = makeNode("Parameter", "", cur().line);
            p->add(parseTypeSpecifier());
            acceptP("...");  // parameter pack: Args... args
            std::string name;
            NodePtr d = parseDeclarator(true, name);
            p->text = name;
            if (isP("...")) advance();  // parameter pack
            if (acceptP("=")) d->add(parseAssignment());  // default argument
            p->add(std::move(d));
            params->add(std::move(p));
            if (pos_ == before) break;
        } while (acceptP(","));
    }
    expectP(")", "to close parameter list");
    return params;
}

// After the declarator name of a function, at '(': parameters, qualifiers, trailing return
// type, constructor initializers, then a body, '= 0/default/delete' or ';'.
void Parser::parseFunctionRest(Node& fn) {
    const int line = cur().line;
    fn.add(parseParameterList());

    std::string quals;
    while (true) {
        if (isCvKw(cur())) {
            quals += (quals.empty() ? "" : " ") + cur().text;
            advance();
        } else if (isP("&") || isP("&&")) {
            quals += (quals.empty() ? "" : " ") + cur().text;
            advance();
        } else if (isK("noexcept")) {
            quals += (quals.empty() ? "" : " ") + std::string("noexcept");
            advance();
            if (isP("(")) {
                advance();
                parseConditional();
                expectP(")", "to close 'noexcept'");
            }
        } else if (isIdent("override") || isIdent("final")) {
            quals += (quals.empty() ? "" : " ") + cur().text;
            advance();
        } else if (isK("throw") && tokP(pos_ + 1, "(")) {  // dynamic exception specification
            advance();
            advance();
            while (!isP(")") && !atEof()) advance();
            expectP(")", "to close exception specification");
        } else {
            break;
        }
    }
    if (acceptP("->")) {  // trailing return type
        fn.add(parseTypeName());
        while (isIdent("override") || isIdent("final")) {
            quals += (quals.empty() ? "" : " ") + cur().text;
            advance();
        }
    }
    if (!quals.empty()) fn.add(makeNode("Qualifiers", quals, line));

    if (isP(":")) parseCtorInitList(fn);

    if (isP("=")) {  // = 0;  = default;  = delete;
        advance();
        if (cur().kind == Tok::IntLiteral || isK("default") || isK("delete")) {
            fn.add(makeNode("Specifier", cur().text, cur().line));
            advance();
        } else {
            errorHere("expected '0', 'default' or 'delete'");
        }
        expectSemi("function declaration");
    } else if (isP("{")) {
        fn.add(parseCompound());
    } else {
        expectSemi("function declaration");
    }
}

// : base(1), member{2}, other(x)
void Parser::parseCtorInitList(Node& fn) {
    const int line = cur().line;
    advance();  // ':'
    auto list = makeNode("MemberInitializers", "", line);
    do {
        const size_t before = pos_;
        auto init = makeNode("MemberInitializer", "", cur().line);
        init->text = parseQualifiedName(init.get(), false);
        if (isP("(")) {
            advance();
            parseArguments(*init, ")");
            expectP(")", "to close member initializer");
        } else if (isP("{")) {
            init->add(parseBracedInit());
        } else {
            errorAfterPrev("expected '(' or '{' in member initializer");
        }
        if (isP("...")) advance();
        list->add(std::move(init));
        if (pos_ == before) break;
    } while (acceptP(","));
    fn.add(std::move(list));
}

// = expr | = {..} | {..} | (args)
void Parser::parseInitializer(Node& d) {
    if (acceptP("=")) {
        if (isP("{"))
            d.add(parseBracedInit());
        else
            d.add(parseAssignment());
    } else if (isP("{")) {
        d.add(parseBracedInit());
    } else if (isP("(")) {
        advance();
        auto args = makeNode("ConstructorArguments", "", cur().line);
        parseArguments(*args, ")");
        expectP(")", "to close initializer");
        d.add(std::move(args));
    }
}
