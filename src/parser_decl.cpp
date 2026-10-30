// parser_decl.cpp - declarations.
//
//   declaration := preprocessor | ';' | access-specifier ':' | namespace | using | typedef
//                | template-declaration | class | enum | static_assert | extern "C" ...
//                | simple-declaration
//   simple-declaration := specifiers? type init-declarator (',' init-declarator)* ';'
//                       | ctor/dtor/conversion  function-rest
#include "parser.hpp"

NodePtr Parser::parseTranslationUnit() {
    auto root = makeNode("TranslationUnit", "", 1);
    parseDeclarationSeq(*root, Scope::Global, "");
    resolveTypeUses();  // every name used as a type must have been declared somewhere
    return root;
}

// Parses declarations until EOF, or until the '}' that closes a namespace/class body.
void Parser::parseDeclarationSeq(Node& parent, Scope scope, const std::string& className) {
    while (!atEof()) {
        if (scope != Scope::Global && isP("}")) break;
        const size_t before = pos_;
        parent.add(parseDeclaration(scope, className));
        if (pos_ == before) {  // no progress: skip the offending token
            errorHere("unexpected '" + cur().text + "'");
            advance();
        }
    }
}

NodePtr Parser::parseDeclaration(Scope scope, const std::string& className) {
    DepthGuard guard(*this);
    if (tooDeep()) return makeNode("Error", "", cur().line);
    const int line = cur().line;

    if (cur().kind == Tok::Preprocessor) {
        auto n = makeNode("Preprocessor", cur().text, line);
        advance();
        return n;
    }
    if (acceptP(";")) return makeNode("EmptyDeclaration", "", line);
    if (scope == Scope::Class && (isK("public") || isK("private") || isK("protected"))) {
        auto n = makeNode("AccessSpecifier", cur().text, line);
        advance();
        expectP(":", "after access specifier");
        return n;
    }

    if (cur().kind == Tok::Keyword) {
        const std::string kw = cur().text;
        if (kw == "namespace") return parseNamespace();
        if (kw == "inline" && tokK(pos_ + 1, "namespace")) {
            advance();
            return parseNamespace();
        }
        if (kw == "using") return parseUsing();
        if (kw == "typedef") return parseTypedef(scope, className);
        if (kw == "template") return parseTemplate(scope, className);
        if (kw == "static_assert") return parseStaticAssert();
        if (kw == "extern" && at(pos_ + 1).kind == Tok::StringLiteral) return parseLinkageSpec(scope, className);
        if (kw == "class" || kw == "struct" || kw == "union")
            return finishTypeDeclaration(parseClass(), scope, className);
        if (kw == "enum") return finishTypeDeclaration(parseEnum(), scope, className);
    }
    return parseSimpleDeclaration(scope, className, true);
}

// namespace a::b { ... }   |   namespace alias = a::b;
NodePtr Parser::parseNamespace() {
    const int line = cur().line;
    advance();  // namespace
    auto n = makeNode("NamespaceDecl", "", line);
    std::string name;
    if (isId()) {
        name = cur().text;
        advance();
        while (isP("::") && at(pos_ + 1).kind == Tok::Identifier) {
            advance();
            name += "::" + cur().text;
            advance();
        }
    }
    n->text = name;
    if (acceptP("=")) {
        n->kind = "NamespaceAlias";
        n->add(makeNode("Identifier", parseQualifiedName(nullptr, false), line));
        expectSemi("namespace alias");
        return n;
    }
    if (!expectP("{", "after namespace name")) return n;
    parseDeclarationSeq(*n, Scope::Namespace, "");
    if (!acceptP("}")) errorAfterPrev("expected '}' to close namespace opened at line " + std::to_string(line));
    return n;
}

// using namespace std;  |  using Alias = T;  |  using std::string;
NodePtr Parser::parseUsing() {
    const int line = cur().line;
    advance();  // using
    if (acceptK("namespace")) {
        auto n = makeNode("UsingDirective", "", line);
        n->text = parseQualifiedName(nullptr, false);
        expectSemi("using-directive");
        return n;
    }
    if (isId() && tokP(pos_ + 1, "=")) {
        auto n = makeNode("UsingAlias", cur().text, line);
        typeNames_.insert(cur().text);
        advance();
        advance();
        n->add(parseTypeName());
        expectSemi("alias declaration");
        return n;
    }
    auto n = makeNode("UsingDeclaration", "", line);
    acceptK("typename");
    n->text = parseQualifiedName(nullptr, false);
    // `using std::string;` may name a type or a function; assume it could be a type so
    // that a later `string s;` is not reported as unknown.
    const size_t lastSep = n->text.rfind("::");
    if (lastSep != std::string::npos) typeNames_.insert(n->text.substr(lastSep + 2));
    expectSemi("using-declaration");
    return n;
}

// typedef unsigned long size_type;   typedef struct { int x; } Point;
NodePtr Parser::parseTypedef(Scope scope, const std::string&) {
    const int line = cur().line;
    advance();  // typedef
    auto decl = makeNode("TypedefDecl", "", line);
    if (isK("class") || isK("struct") || isK("union"))
        decl->add(parseClass());
    else if (isK("enum"))
        decl->add(parseEnum());
    else
        decl->add(parseTypeSpecifier());
    parseInitDeclaratorList(*decl, scope, true, true);
    return decl;
}

// template <typename T, int N = 3, class... Ts> declaration
NodePtr Parser::parseTemplate(Scope scope, const std::string& className) {
    const int line = cur().line;
    advance();  // template
    auto n = makeNode("TemplateDecl", "", line);
    auto params = makeNode("TemplateParams", "", line);
    if (!expectP("<", "after 'template'")) return n;
    if (!isP(">")) {
        do {
            const size_t before = pos_;
            const int paramLine = cur().line;
            if (isK("template")) {  // template template parameter
                advance();
                const size_t end = skipTemplateArgs(pos_);
                if (end == npos) {
                    errorHere("malformed template template parameter");
                    break;
                }
                pos_ = end;
                if (isK("class") || isK("typename")) advance();
                std::string name;
                if (isId()) {
                    name = cur().text;
                    typeNames_.insert(name);
                    advance();
                }
                params->add(makeNode("TemplateTemplateParam", name, paramLine));
            } else if (isK("typename") || isK("class")) {
                advance();
                const bool pack = acceptP("...");
                std::string name;
                if (isId()) {
                    name = cur().text;
                    typeNames_.insert(name);
                    advance();
                }
                auto p = makeNode("TypeParam", name + (pack ? "..." : ""), paramLine);
                if (acceptP("=")) p->add(parseTypeName());
                params->add(std::move(p));
            } else {  // non-type parameter: int N = 3
                auto p = makeNode("NonTypeParam", "", paramLine);
                p->add(parseTypeSpecifier());
                std::string name;
                p->add(parseDeclarator(true, name));
                p->text = name;
                if (acceptP("=")) p->add(parseBinary(9));
                params->add(std::move(p));
            }
            if (pos_ == before) break;
        } while (acceptP(","));
    }
    closeAngle();
    n->add(std::move(params));
    n->add(parseDeclaration(scope, className));
    return n;
}

// class|struct|union Name [final] [: bases] [{ members }]
NodePtr Parser::parseClass() {
    const int line = cur().line;
    const std::string key = cur().text;
    advance();
    auto n = makeNode(key == "class" ? "ClassDecl" : key == "struct" ? "StructDecl" : "UnionDecl", "", line);
    std::string simpleName;
    if (isId()) {
        simpleName = cur().text;
        typeNames_.insert(simpleName);  // usable as a type inside its own body
        n->text = parseQualifiedName(n.get(), false);
    }
    if (isIdent("final")) advance();

    if (isP(":")) {  // base-specifier list
        advance();
        auto bases = makeNode("BaseClasses", "", cur().line);
        do {
            const size_t before = pos_;
            std::string access;
            while (isK("virtual") || isK("public") || isK("protected") || isK("private")) {
                access += cur().text + " ";
                advance();
            }
            auto base = makeNode("Base", "", cur().line);
            const size_t baseTok = pos_;
            const std::string baseName = parseQualifiedName(base.get(), false);
            noteTypeUse(baseTok, baseName, false);  // a base class is always a type
            base->text = access + baseName;
            if (isP("...")) advance();
            bases->add(std::move(base));
            if (pos_ == before) break;
        } while (acceptP(","));
        n->add(std::move(bases));
    }

    if (isP("{")) {
        advance();
        parseDeclarationSeq(*n, Scope::Class, simpleName);
        if (!acceptP("}"))
            errorAfterPrev("expected '}' to close " + key + " opened at line " + std::to_string(line));
    }
    return n;
}

// enum [class|struct] Name [: type] { A, B = 2, }
NodePtr Parser::parseEnum() {
    const int line = cur().line;
    advance();  // enum
    auto n = makeNode("EnumDecl", "", line);
    if (isK("class") || isK("struct")) {
        n->kind = "ScopedEnumDecl";
        advance();
    }
    if (isId()) {
        n->text = cur().text;
        typeNames_.insert(cur().text);
        advance();
    }
    if (acceptP(":")) n->add(parseTypeName());
    if (isP("{")) {
        advance();
        while (!isP("}") && !atEof()) {
            if (!isId()) {
                errorHere("expected an enumerator name but found '" + cur().text + "'");
                break;
            }
            auto e = makeNode("Enumerator", cur().text, cur().line);
            advance();
            if (acceptP("=")) e->add(parseConditional());
            n->add(std::move(e));
            if (!acceptP(",")) break;
        }
        expectP("}", "to close enum");
    }
    return n;
}

// static_assert(cond, "message");
NodePtr Parser::parseStaticAssert() {
    const int line = cur().line;
    advance();
    auto n = makeNode("StaticAssert", "", line);
    expectP("(", "after 'static_assert'");
    n->add(parseConditional());
    if (acceptP(",")) n->add(parseAssignment());
    expectP(")", "to close 'static_assert'");
    expectSemi("'static_assert'");
    return n;
}

// extern "C" { ... }   |   extern "C" declaration
NodePtr Parser::parseLinkageSpec(Scope scope, const std::string& className) {
    const int line = cur().line;
    advance();  // extern
    auto n = makeNode("LinkageSpec", cur().text, line);
    advance();  // "C"
    if (acceptP("{")) {
        parseDeclarationSeq(*n, Scope::Namespace, "");
        if (!acceptP("}")) errorAfterPrev("expected '}' to close linkage block opened at line " + std::to_string(line));
    } else {
        n->add(parseDeclaration(scope, className));
    }
    return n;
}

// After a class or enum: `;`  or  declarators for variables of that type (`} obj;`).
NodePtr Parser::finishTypeDeclaration(NodePtr typeNode, Scope scope, const std::string&) {
    const int line = typeNode->line;
    if (acceptP(";")) return typeNode;
    if (!(isId() || isP("*") || isP("&") || isP("&&"))) {
        expectSemi("type definition");
        return typeNode;
    }
    auto decl = makeNode("Declaration", "", line);
    decl->add(std::move(typeNode));
    parseInitDeclaratorList(*decl, scope, false, true);
    return decl;
}

// Does a constructor, destructor or conversion operator (no return type) start here?
bool Parser::isConstructorLike(const std::string& className) const {
    if (isK("operator")) return true;
    if (isP("~")) return at(pos_ + 1).kind == Tok::Identifier && tokP(pos_ + 2, "(");
    if (!isId()) return false;
    if (!className.empty() && cur().text == className && tokP(pos_ + 1, "(")) return true;

    // Out-of-line definitions:  A::A(   A<T>::A(   A::~A(
    const size_t q = skipTypeName(pos_);
    if (q == npos) return false;
    if (tokP(q, "::") && tokP(q + 1, "~") && at(q + 2).kind == Tok::Identifier && tokP(q + 3, "(")) return true;
    if (!tokP(q, "(") || q < pos_ + 3 || !tokP(q - 2, "::")) return false;
    size_t k = q - 3;  // last token of the class-name part
    if (tokP(k, ">") || tokP(k, ">>")) {
        int depth = 0;
        while (k > pos_) {
            if (tokP(k, ">")) {
                ++depth;
            } else if (tokP(k, ">>")) {
                depth += 2;
            } else if (tokP(k, "<") && --depth <= 0) {
                --k;
                break;
            }
            --k;
        }
    }
    return at(k).kind == Tok::Identifier && at(k).text == at(q - 1).text;
}

// specifiers? type declarator-list ';'   (variables, functions, members, parameters' cousins)
NodePtr Parser::parseSimpleDeclaration(Scope scope, const std::string& className, bool needSemi) {
    const int line = cur().line;
    auto decl = makeNode("Declaration", "", line);

    std::string specs;
    while (isStorageKw(cur())) {
        specs += (specs.empty() ? "" : " ") + cur().text;
        advance();
    }
    if (!specs.empty()) decl->add(makeNode("Specifiers", specs, line));

    if (isConstructorLike(className)) {  // no return type
        std::string name;
        NodePtr d = parseDeclarator(true, name);
        const bool dtor = name.find('~') != std::string::npos;
        decl->kind = dtor ? "DestructorDecl" : (name.rfind("operator", 0) == 0 ? "ConversionDecl" : "ConstructorDecl");
        decl->text = name;
        if (!isP("(")) {
            errorAfterPrev("expected '(' after '" + name + "'");
            return decl;
        }
        parseFunctionRest(*decl);
        return decl;
    }

    decl->add(parseTypeSpecifier());
    if (decl->kids.back()->text.empty()) return decl;  // no type: the error is already reported
    if (needSemi && acceptP(";")) return decl;  // e.g. `friend class X;`
    parseInitDeclaratorList(*decl, scope, false, needSemi);
    return decl;
}

// declarator ('=' init)? (',' declarator ('=' init)?)*  ';'
// A declarator followed by a parameter list turns the whole declaration into a function.
void Parser::parseInitDeclaratorList(Node& decl, Scope scope, bool typedefMode, bool needSemi) {
    bool first = true;
    do {
        const size_t before = pos_;
        std::string name;
        NodePtr d = parseDeclarator(false, name);
        if (name.empty()) return;  // "expected a declarator name" was already reported
        if (typedefMode) typeNames_.insert(name);

        if (first && !typedefMode && isP("(") && (scope == Scope::Class || parenIsParameterList())) {
            decl.kind = "FunctionDecl";
            decl.text = name;
            for (auto& kid : d->kids)  // template arguments of a qualified name, array bounds
                decl.add(std::move(kid));
            parseFunctionRest(decl);
            return;  // the function's own body or ';' ends the declaration
        }

        if (scope == Scope::Class && isP(":")) {  // bit-field
            advance();
            d->add(parseConditional());
        }
        parseInitializer(*d);
        decl.add(std::move(d));
        first = false;
        if (pos_ == before) break;
    } while (acceptP(","));
    if (needSemi) expectSemi("declaration");
}
