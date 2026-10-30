// parser.hpp - recursive-descent parser for a large subset of ISO C++17.
//
// Grammar overview (see the .cpp files for the exact rules):
//   translation-unit  := declaration*
//   declaration       := preprocessor | namespace | using | typedef | template | class | enum
//                      | static_assert | linkage-spec | simple-declaration
//   statement         := compound | if | switch | while | do | for | range-for | return | break
//                      | continue | goto | try | label | declaration | expression ';'
//   expression        := precedence climbing over the 17 C++ precedence levels
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "ast.hpp"
#include "diag.hpp"
#include "token.hpp"

class Parser {
public:
    Parser(std::vector<Token> tokens, Diagnostics& diag);

    // Turns the undeclared-type check on or off (on by default). See resolveTypeUses().
    void setUndeclaredTypeCheck(bool on) { checkUndeclared_ = on; }

    // Parses the whole token stream. Never throws; errors go to the Diagnostics object.
    NodePtr parseTranslationUnit();

private:
    enum class Scope { Global, Namespace, Class, Block };
    static constexpr size_t npos = static_cast<size_t>(-1);

    std::vector<Token> t_;
    size_t pos_ = 0;
    Diagnostics& diag_;
    std::unordered_set<std::string> typeNames_;  // classes, enums, typedefs, aliases, template parameters
    int depth_ = 0;                              // recursion guard

    // ---- undeclared-type check -------------------------------------------------------
    // Every unqualified name used as a type is recorded here and checked against
    // typeNames_ once the whole file has been read, so a type may be declared after
    // the member function body that uses it.
    struct TypeUse {
        std::string name;
        int line;
        int col;
    };
    std::vector<TypeUse> typeUses_;
    bool checkUndeclared_ = true;   // --no-undeclared turns this off
    bool opaqueHeaders_ = false;    // a #include "..." whose contents we cannot see
    bool systemHeaders_ = false;    // a #include <...>: assume the standard library

    void scanIncludes();
    void noteTypeUse(size_t nameTok, const std::string& spelling, bool elaborated);
    void resolveTypeUses();
    static const std::unordered_set<std::string>& libraryTypeNames();

    struct DepthGuard {
        explicit DepthGuard(Parser& p) : p_(p) { ++p_.depth_; }
        ~DepthGuard() { --p_.depth_; }
        Parser& p_;
    };
    bool tooDeep();

    // ---- token helpers (parser.cpp) ----------------------------------------------------
    const Token& cur() const;
    const Token& at(size_t p) const;
    const Token& prev() const;
    bool atEof() const;
    bool isP(const char* s) const;
    bool isK(const char* s) const;
    bool isId() const;
    bool isIdent(const char* s) const;  // contextual keywords: override, final
    bool tokP(size_t p, const char* s) const;
    bool tokK(size_t p, const char* s) const;
    void advance();
    bool acceptP(const char* s);
    bool acceptK(const char* s);
    bool expectP(const char* s, const char* context);
    void expectSemi(const char* context);
    std::string expectIdentifier(const char* context);
    void errorHere(const std::string& msg);
    void errorAfterPrev(const std::string& msg);
    std::string spell(size_t from, size_t to) const;
    bool closeAngle();

    // ---- lookahead helpers (parser.cpp) ------------------------------------------------
    static bool isBuiltinTypeKw(const Token& k);
    static bool isCvKw(const Token& k);
    static bool isStorageKw(const Token& k);
    size_t skipTemplateArgs(size_t p) const;
    size_t skipTypeName(size_t p) const;
    bool startsType(size_t p) const;
    bool looksLikeDeclaration(size_t p) const;
    bool isDeclarationStart() const;
    bool looksLikeTemplateArgs(size_t p) const;
    bool isTemplateTypeArg(size_t p) const;
    bool looksLikeCast() const;
    bool isRangeFor() const;
    bool isConstructorLike(const std::string& className) const;
    bool parenIsParameterList() const;

    // ---- declarations (parser_decl.cpp) ------------------------------------------------
    void parseDeclarationSeq(Node& parent, Scope scope, const std::string& className);
    NodePtr parseDeclaration(Scope scope, const std::string& className);
    NodePtr parseNamespace();
    NodePtr parseUsing();
    NodePtr parseTypedef(Scope scope, const std::string& className);
    NodePtr parseTemplate(Scope scope, const std::string& className);
    NodePtr parseClass();
    NodePtr parseEnum();
    NodePtr parseStaticAssert();
    NodePtr parseLinkageSpec(Scope scope, const std::string& className);
    NodePtr finishTypeDeclaration(NodePtr typeNode, Scope scope, const std::string& className);
    NodePtr parseSimpleDeclaration(Scope scope, const std::string& className, bool needSemi);
    void parseInitDeclaratorList(Node& decl, Scope scope, bool typedefMode, bool needSemi);
    void parseFunctionRest(Node& fn);
    void parseCtorInitList(Node& fn);
    NodePtr parseParameterList();
    NodePtr parseDeclarator(bool abstractOk, std::string& nameOut);
    void parseInitializer(Node& decl);
    NodePtr parseTypeSpecifier();
    NodePtr parseTypeName();
    NodePtr parseTemplateArgs();
    std::string parseQualifiedName(Node* holder, bool exprContext);
    std::string parseOperatorName();

    // ---- statements (parser_stmt.cpp) --------------------------------------------------
    NodePtr parseStatement();
    NodePtr parseCompound();
    NodePtr parseIf();
    NodePtr parseSwitch();
    NodePtr parseWhile();
    NodePtr parseDo();
    NodePtr parseFor();
    NodePtr parseReturn();
    NodePtr parseTry();
    NodePtr parseCondition();
    NodePtr parseJump(const char* keyword);
    NodePtr parseLabel();

    // ---- expressions (parser_expr.cpp) -------------------------------------------------
    NodePtr parseExpression();
    NodePtr parseAssignment();
    NodePtr parseConditional();
    NodePtr parseBinary(int minPrec);
    NodePtr parseUnary();
    NodePtr parsePostfix();
    NodePtr parsePrimary();
    NodePtr parseIdExpression();
    NodePtr parseLambda();
    NodePtr parseNew();
    NodePtr parseBracedInit();
    void parseArguments(Node& call, const char* closer);
    static int binaryPrecedence(const Token& k);
};
