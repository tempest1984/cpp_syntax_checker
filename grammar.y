/* ==========================================================================
 * grammar.y - Bison / Yacc Grammar Specification for C++ Parser
 *
 * This file defines the context-free grammar (BNF productions) for the C++17
 * language subset supported by cpp_parser.
 *
 * HOW THIS RELATES TO THE IMPLEMENTATION:
 * ---------------------------------------
 * In a recursive-descent parser (like src/parser_*.cpp), every non-terminal
 * grammar rule corresponds to a C++ member function:
 *
 *    Grammar Rule (.y)                 Recursive Descent Function (.cpp)
 *   -------------------               -----------------------------------
 *    translation_unit            ->    Parser::parseTranslationUnit() [parser_decl.cpp]
 *    declaration                 ->    Parser::parseDeclaration()     [parser_decl.cpp]
 *    class_specifier             ->    Parser::parseClass()           [parser_decl.cpp]
 *    enum_specifier              ->    Parser::parseEnum()            [parser_decl.cpp]
 *    statement                   ->    Parser::parseStatement()       [parser_stmt.cpp]
 *    compound_statement          ->    Parser::parseCompound()        [parser_stmt.cpp]
 *    selection_statement (if)    ->    Parser::parseIf()              [parser_stmt.cpp]
 *    iteration_statement (while) ->    Parser::parseWhile()           [parser_stmt.cpp]
 *    iteration_statement (for)   ->    Parser::parseFor()             [parser_stmt.cpp]
 *    jump_statement (return)     ->    Parser::parseReturn()          [parser_stmt.cpp]
 *    expression                  ->    Parser::parseExpression()      [parser_expr.cpp]
 *    assignment_expression       ->    Parser::parseAssignment()      [parser_expr.cpp]
 *    conditional_expression      ->    Parser::parseConditional()     [parser_expr.cpp]
 *    binary_expression           ->    Parser::parseBinary()          [parser_expr.cpp]
 *    unary_expression            ->    Parser::parseUnary()           [parser_expr.cpp]
 *    postfix_expression          ->    Parser::parsePostfix()         [parser_expr.cpp]
 *    type_specifier              ->    Parser::parseTypeSpecifier()   [parser_type.cpp]
 *    declarator                  ->    Parser::parseDeclarator()      [parser_type.cpp]
 *
 * Rather than writing parsing functions manually, a parser generator (Bison/Yacc)
 * reads this .y file and automatically generates the complete parsing engine
 * (the function `yyparse()`) as an LALR(1) / GLR state machine!
 * ========================================================================== */

%{
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include "ast.hpp"

/* Forward declarations for parser-lexer interaction */
int yylex();
void yyerror(const char* s);

/* The root AST node populated by the parser */
extern NodePtr g_ast_root;
%}

/* Enable rich error messages and location tracking in Bison */
%define parse.error verbose
%locations

/* Semantic value union: types that grammar symbols ($$, $1, $2...) can hold */
%union {
    Node* node;
    std::string* str;
    int token_val;
}

/* ==========================================================================
 * TOKEN DEFINITIONS (Terminals produced by the Lexer)
 * Matches src/token.hpp and src/lexer.cpp
 * ========================================================================== */

/* Literals and Identifiers */
%token <str> IDENTIFIER      "identifier"
%token <str> TYPE_NAME       "type-name"
%token <str> INT_LITERAL     "integer-literal"
%token <str> FLOAT_LITERAL   "floating-literal"
%token <str> CHAR_LITERAL    "character-literal"
%token <str> STRING_LITERAL  "string-literal"
%token <str> PREPROCESSOR    "preprocessor-directive"

/* Keywords */
%token AUTO BOOL BREAK CASE CATCH CHAR CHAR16_T CHAR32_T CLASS CONST
%token CONST_CAST CONSTEXPR CONTINUE DECLTYPE DEFAULT DELETE DO DOUBLE
%token DYNAMIC_CAST ELSE ENUM EXPLICIT EXPORT EXTERN FALSE FLOAT FOR
%token FRIEND GOTO IF INLINE INT LONG MUTABLE NAMESPACE NEW NOEXCEPT
%token NULLPTR OPERATOR PRIVATE PROTECTED PUBLIC REGISTER REINTERPRET_CAST
%token RETURN SHORT SIGNED SIZEOF STATIC STATIC_ASSERT STATIC_CAST STRUCT
%token SWITCH TEMPLATE THIS THREAD_LOCAL THROW TRUE TRY TYPEDEF TYPEID
%token TYPENAME UNION UNSIGNED USING VIRTUAL VOID VOLATILE WCHAR_T WHILE
%token OVERRIDE FINAL

/* Multi-character operators and punctuators */
%token SCOPE         "::"
%token ELLIPSIS      "..."
%token ARROW         "->"
%token ARROW_STAR    "->*"
%token DOT_STAR      ".*"
%token INC           "++"
%token DEC           "--"
%token SHL           "<<"
%token SHR           ">>"
%token LE            "<="
%token GE            ">="
%token EQ            "=="
%token NE            "!="
%token AND           "&&"
%token OR            "||"
%token ADD_ASSIGN    "+="
%token SUB_ASSIGN    "-="
%token MUL_ASSIGN    "*="
%token DIV_ASSIGN    "/="
%token MOD_ASSIGN    "%="
%token SHL_ASSIGN    "<<="
%token SHR_ASSIGN    ">>="
%token AND_ASSIGN    "&="
%token XOR_ASSIGN    "^="
%token OR_ASSIGN     "|="

/* ==========================================================================
 * PRECEDENCE & ASSOCIATIVITY RULES
 * Resolves ambiguities in expressions and dangling-else statements.
 * Ordered from lowest precedence to highest precedence.
 * ========================================================================== */

/* Dangling else resolution: shift 'else' rather than reduce */
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

/* Expression operator precedence levels (matches Parser::binaryPrecedence) */
%left ','
%right '=' ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN SHL_ASSIGN SHR_ASSIGN AND_ASSIGN XOR_ASSIGN OR_ASSIGN
%right '?' ':'
%left OR
%left AND
%left '|'
%left '^'
%left '&'
%left EQ NE
%left '<' '>' LE GE
%left SHL SHR
%left '+' '-'
%left '*' '/' '%'
%left DOT_STAR ARROW_STAR
%right PREFIX_OP SIZEOF ALIGNOF NEW DELETE THROW
%left POSTFIX_OP INC DEC '.' ARROW '[' ']' '(' ')'
%left SCOPE

/* Non-terminal types */
%type <node> translation_unit declaration_seq declaration
%type <node> simple_declaration function_definition
%type <node> namespace_definition using_declaration typedef_declaration
%type <node> template_declaration class_specifier enum_specifier
%type <node> static_assert_declaration linkage_specification
%type <node> statement compound_statement selection_statement
%type <node> iteration_statement jump_statement labeled_statement
%type <node> expression_statement declaration_statement try_block
%type <node> condition expression assignment_expression conditional_expression
%type <node> binary_expression unary_expression postfix_expression primary_expression
%type <node> type_specifier declarator init_declarator init_declarator_list
%type <node> parameter_list parameter_declaration parameter_declaration_list
%type <node> braced_init_list initializer initializer_opt

%start translation_unit

%%

/* ==========================================================================
 * 1. TRANSLATION UNIT & DECLARATIONS
 * Maps to: Parser::parseTranslationUnit(), Parser::parseDeclarationSeq(),
 *          Parser::parseDeclaration() in src/parser_decl.cpp
 * ========================================================================== */

translation_unit
    : declaration_seq {
        $$ = $1;
        // g_ast_root.reset($$);
    }
    | /* empty */ {
        $$ = makeNode("TranslationUnit", "", 1).release();
        // g_ast_root.reset($$);
    }
    ;

declaration_seq
    : declaration {
        $$ = makeNode("TranslationUnit", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    | declaration_seq declaration {
        $$ = $1;
        $$->add(NodePtr($2));
    }
    ;

declaration
    : simple_declaration ';'                { $$ = $1; }
    | function_definition                   { $$ = $1; }
    | class_specifier ';'                   { $$ = $1; }
    | enum_specifier ';'                    { $$ = $1; }
    | namespace_definition                  { $$ = $1; }
    | using_declaration                     { $$ = $1; }
    | typedef_declaration                   { $$ = $1; }
    | template_declaration                  { $$ = $1; }
    | static_assert_declaration             { $$ = $1; }
    | linkage_specification                 { $$ = $1; }
    | PREPROCESSOR {
        $$ = makeNode("Preprocessor", *$1, @1.first_line).release();
        delete $1;
    }
    | ';' {
        $$ = makeNode("EmptyDeclaration", "", @1.first_line).release();
    }
    ;

/* Namespace declarations: namespace A { ... } or namespace Alias = A::B; */
namespace_definition
    : NAMESPACE IDENTIFIER '{' declaration_seq '}' {
        $$ = makeNode("NamespaceDecl", *$2, @1.first_line).release();
        $$->add(NodePtr($4));
        delete $2;
    }
    | NAMESPACE '{' declaration_seq '}' {
        $$ = makeNode("NamespaceDecl", "<anonymous>", @1.first_line).release();
        $$->add(NodePtr($3));
    }
    | NAMESPACE IDENTIFIER '=' qualified_id ';' {
        $$ = makeNode("NamespaceAlias", *$2, @1.first_line).release();
        delete $2;
    }
    | INLINE NAMESPACE IDENTIFIER '{' declaration_seq '}' {
        $$ = makeNode("NamespaceDecl", "inline " + *$3, @1.first_line).release();
        $$->add(NodePtr($5));
        delete $3;
    }
    ;

/* Using declarations and type aliases: using std::string; using T = int; */
using_declaration
    : USING NAMESPACE qualified_id ';' {
        $$ = makeNode("UsingDirective", "", @1.first_line).release();
    }
    | USING IDENTIFIER '=' type_specifier ';' {
        $$ = makeNode("UsingAlias", *$2, @1.first_line).release();
        $$->add(NodePtr($4));
        delete $2;
    }
    | USING qualified_id ';' {
        $$ = makeNode("UsingDeclaration", "", @1.first_line).release();
    }
    | USING TYPENAME qualified_id ';' {
        $$ = makeNode("UsingDeclaration", "typename", @1.first_line).release();
    }
    ;

/* Typedef declarations: typedef int Int32; */
typedef_declaration
    : TYPEDEF type_specifier init_declarator_list ';' {
        $$ = makeNode("TypedefDecl", "", @1.first_line).release();
        $$->add(NodePtr($2));
        $$->add(NodePtr($3));
    }
    ;

/* Templates: template <typename T> class Foo; */
template_declaration
    : TEMPLATE '<' template_parameter_list '>' declaration {
        $$ = makeNode("TemplateDecl", "", @1.first_line).release();
        $$->add(NodePtr($5));
    }
    ;

template_parameter_list
    : template_parameter
    | template_parameter_list ',' template_parameter
    ;

template_parameter
    : TYPENAME identifier_opt
    | CLASS identifier_opt
    | TYPENAME ELLIPSIS identifier_opt
    | CLASS ELLIPSIS identifier_opt
    | type_specifier declarator
    ;

/* Static assert */
static_assert_declaration
    : STATIC_ASSERT '(' expression ',' STRING_LITERAL ')' ';' {
        $$ = makeNode("StaticAssertDecl", *$5, @1.first_line).release();
        $$->add(NodePtr($3));
        delete $5;
    }
    ;

/* Extern "C" linkage */
linkage_specification
    : EXTERN STRING_LITERAL '{' declaration_seq '}' {
        $$ = makeNode("LinkageSpec", *$2, @1.first_line).release();
        $$->add(NodePtr($4));
        delete $2;
    }
    | EXTERN STRING_LITERAL declaration {
        $$ = makeNode("LinkageSpec", *$2, @1.first_line).release();
        $$->add(NodePtr($3));
        delete $2;
    }
    ;

/* ==========================================================================
 * 2. CLASSES AND STRUCTS
 * Maps to: Parser::parseClass() in src/parser_decl.cpp
 * ========================================================================== */

class_specifier
    : class_head '{' member_specification_opt '}' {
        $$ = $1;
        if ($3) $$->add(NodePtr($3));
    }
    ;

class_head
    : class_key identifier_opt final_opt base_clause_opt {
        $$ = makeNode("ClassDecl", "", @1.first_line).release();
    }
    ;

class_key
    : CLASS | STRUCT | UNION
    ;

final_opt
    : /* empty */
    | FINAL
    ;

base_clause_opt
    : /* empty */
    | ':' base_specifier_list
    ;

base_specifier_list
    : base_specifier
    | base_specifier_list ',' base_specifier
    ;

base_specifier
    : access_specifier_opt qualified_id
    | VIRTUAL access_specifier_opt qualified_id
    | access_specifier_opt VIRTUAL qualified_id
    ;

access_specifier_opt
    : /* empty */
    | PUBLIC | PROTECTED | PRIVATE
    ;

member_specification_opt
    : /* empty */                           { $$ = nullptr; }
    | member_specification                  { $$ = $1; }
    ;

member_specification
    : member_declaration {
        $$ = makeNode("MemberList", "", @1.first_line).release();
        if ($1) $$->add(NodePtr($1));
    }
    | member_specification member_declaration {
        $$ = $1;
        if ($2) $$->add(NodePtr($2));
    }
    ;

member_declaration
    : declaration                           { $$ = $1; }
    | access_specifier ':' {
        $$ = makeNode("AccessSpecifier", "", @1.first_line).release();
    }
    ;

access_specifier
    : PUBLIC | PROTECTED | PRIVATE
    ;

/* ==========================================================================
 * 3. ENUMS
 * Maps to: Parser::parseEnum() in src/parser_decl.cpp
 * ========================================================================== */

enum_specifier
    : enum_head '{' enumerator_list_opt '}' {
        $$ = $1;
    }
    ;

enum_head
    : ENUM enum_key_opt identifier_opt enum_base_opt {
        $$ = makeNode("EnumDecl", "", @1.first_line).release();
    }
    ;

enum_key_opt
    : /* empty */
    | CLASS | STRUCT
    ;

enum_base_opt
    : /* empty */
    | ':' type_specifier
    ;

enumerator_list_opt
    : /* empty */
    | enumerator_list
    | enumerator_list ','
    ;

enumerator_list
    : enumerator
    | enumerator_list ',' enumerator
    ;

enumerator
    : IDENTIFIER {
        delete $1;
    }
    | IDENTIFIER '=' expression {
        delete $1;
    }
    ;

/* ==========================================================================
 * 4. STATEMENTS
 * Maps to: Parser::parseStatement() in src/parser_stmt.cpp
 * ========================================================================== */

statement
    : labeled_statement                     { $$ = $1; }
    | compound_statement                    { $$ = $1; }
    | selection_statement                   { $$ = $1; }
    | iteration_statement                   { $$ = $1; }
    | jump_statement                        { $$ = $1; }
    | declaration_statement                 { $$ = $1; }
    | expression_statement                  { $$ = $1; }
    | try_block                             { $$ = $1; }
    | ';' {
        $$ = makeNode("EmptyStatement", "", @1.first_line).release();
    }
    ;

/* Compound statement (block): { stmt1; stmt2; } */
compound_statement
    : '{' statement_seq_opt '}' {
        $$ = $2;
    }
    ;

statement_seq_opt
    : /* empty */ {
        $$ = makeNode("CompoundStatement", "", @$.first_line).release();
    }
    | statement_seq_opt statement {
        $$ = $1;
        $$->add(NodePtr($2));
    }
    ;

/* Selection statements: if, if-else, switch
 * Maps to: Parser::parseIf(), Parser::parseSwitch() */
selection_statement
    : IF '(' condition ')' statement %prec LOWER_THAN_ELSE {
        $$ = makeNode("IfStmt", "", @1.first_line).release();
        $$->add(NodePtr($3));
        auto thenBranch = makeNode("Then", "", @5.first_line);
        thenBranch->add(NodePtr($5));
        $$->add(std::move(thenBranch));
    }
    | IF '(' condition ')' statement ELSE statement {
        $$ = makeNode("IfStmt", "", @1.first_line).release();
        $$->add(NodePtr($3));
        auto thenBranch = makeNode("Then", "", @5.first_line);
        thenBranch->add(NodePtr($5));
        $$->add(std::move(thenBranch));
        auto elseBranch = makeNode("Else", "", @7.first_line);
        elseBranch->add(NodePtr($7));
        $$->add(std::move(elseBranch));
    }
    | IF CONSTEXPR '(' condition ')' statement %prec LOWER_THAN_ELSE {
        $$ = makeNode("IfStmt", "constexpr", @1.first_line).release();
        $$->add(NodePtr($4));
        auto thenBranch = makeNode("Then", "", @6.first_line);
        thenBranch->add(NodePtr($6));
        $$->add(std::move(thenBranch));
    }
    | IF CONSTEXPR '(' condition ')' statement ELSE statement {
        $$ = makeNode("IfStmt", "constexpr", @1.first_line).release();
        $$->add(NodePtr($4));
        auto thenBranch = makeNode("Then", "", @6.first_line);
        thenBranch->add(NodePtr($6));
        $$->add(std::move(thenBranch));
        auto elseBranch = makeNode("Else", "", @8.first_line);
        elseBranch->add(NodePtr($8));
        $$->add(std::move(elseBranch));
    }
    | SWITCH '(' condition ')' statement {
        $$ = makeNode("SwitchStmt", "", @1.first_line).release();
        $$->add(NodePtr($3));
        $$->add(NodePtr($5));
    }
    ;

condition
    : expression {
        $$ = makeNode("Condition", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    | simple_declaration {
        $$ = makeNode("Condition", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    ;

/* Iteration statements: while, do-while, for, range-for
 * Maps to: Parser::parseWhile(), Parser::parseDo(), Parser::parseFor() */
iteration_statement
    : WHILE '(' condition ')' statement {
        $$ = makeNode("WhileStmt", "", @1.first_line).release();
        $$->add(NodePtr($3));
        $$->add(NodePtr($5));
    }
    | DO statement WHILE '(' expression ')' ';' {
        $$ = makeNode("DoWhileStmt", "", @1.first_line).release();
        $$->add(NodePtr($2));
        $$->add(NodePtr($5));
    }
    | FOR '(' for_init_statement expression_opt ';' expression_opt ')' statement {
        $$ = makeNode("ForStmt", "", @1.first_line).release();
        $$->add(NodePtr($3));
        if ($4) $$->add(NodePtr($4));
        if ($6) $$->add(NodePtr($6));
        $$->add(NodePtr($8));
    }
    | FOR '(' type_specifier declarator ':' expression ')' statement {
        $$ = makeNode("RangeForStmt", "", @1.first_line).release();
        $$->add(NodePtr($3));
        $$->add(NodePtr($4));
        $$->add(NodePtr($6));
        $$->add(NodePtr($8));
    }
    ;

for_init_statement
    : expression_statement                  { $$ = $1; }
    | simple_declaration ';'                { $$ = $1; }
    | ';' {
        $$ = makeNode("EmptyStatement", "", @1.first_line).release();
    }
    ;

/* Jump statements: break, continue, return, goto
 * Maps to: Parser::parseReturn(), Parser::parseJump() */
jump_statement
    : BREAK ';' {
        $$ = makeNode("BreakStmt", "", @1.first_line).release();
    }
    | CONTINUE ';' {
        $$ = makeNode("ContinueStmt", "", @1.first_line).release();
    }
    | RETURN expression_opt ';' {
        $$ = makeNode("ReturnStmt", "", @1.first_line).release();
        if ($2) $$->add(NodePtr($2));
    }
    | RETURN braced_init_list ';' {
        $$ = makeNode("ReturnStmt", "", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | GOTO IDENTIFIER ';' {
        $$ = makeNode("GotoStmt", *$2, @1.first_line).release();
        delete $2;
    }
    ;

/* Labeled statements: case, default, label:
 * Maps to: Parser::parseLabel() */
labeled_statement
    : CASE conditional_expression ':' statement {
        $$ = makeNode("CaseLabel", "", @1.first_line).release();
        $$->add(NodePtr($2));
        $$->add(NodePtr($4));
    }
    | DEFAULT ':' statement {
        $$ = makeNode("DefaultLabel", "", @1.first_line).release();
        $$->add(NodePtr($3));
    }
    | IDENTIFIER ':' statement {
        $$ = makeNode("Label", *$1, @1.first_line).release();
        $$->add(NodePtr($3));
        delete $1;
    }
    ;

expression_statement
    : expression ';' {
        $$ = makeNode("ExpressionStatement", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    ;

declaration_statement
    : simple_declaration ';'                { $$ = $1; }
    ;

/* Try / Catch blocks: try { ... } catch (T e) { ... }
 * Maps to: Parser::parseTry() */
try_block
    : TRY compound_statement handler_seq {
        $$ = makeNode("TryStmt", "", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    ;

handler_seq
    : handler
    | handler_seq handler
    ;

handler
    : CATCH '(' exception_declaration ')' compound_statement
    ;

exception_declaration
    : type_specifier declarator
    | type_specifier
    | ELLIPSIS
    ;

/* ==========================================================================
 * 5. EXPRESSIONS
 * Maps to: Parser::parseExpression(), Parser::parseAssignment(),
 *          Parser::parseConditional(), Parser::parseBinary(),
 *          Parser::parseUnary(), Parser::parsePostfix(), Parser::parsePrimary()
 *          in src/parser_expr.cpp
 * ========================================================================== */

expression_opt
    : /* empty */                           { $$ = nullptr; }
    | expression                            { $$ = $1; }
    ;

/* Comma expression */
expression
    : assignment_expression                 { $$ = $1; }
    | expression ',' assignment_expression {
        $$ = makeNode("CommaExpr", "", @1.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($3));
    }
    ;

/* Assignment & throw expressions */
assignment_expression
    : conditional_expression                { $$ = $1; }
    | unary_expression assignment_operator assignment_expression {
        $$ = makeNode("AssignExpr", "", @2.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($3));
    }
    | THROW assignment_expression_opt {
        $$ = makeNode("ThrowExpr", "", @1.first_line).release();
        if ($2) $$->add(NodePtr($2));
    }
    ;

assignment_expression_opt
    : /* empty */                           { $$ = nullptr; }
    | assignment_expression                 { $$ = $1; }
    ;

assignment_operator
    : '=' | ADD_ASSIGN | SUB_ASSIGN | MUL_ASSIGN | DIV_ASSIGN | MOD_ASSIGN
    | SHL_ASSIGN | SHR_ASSIGN | AND_ASSIGN | XOR_ASSIGN | OR_ASSIGN
    ;

/* Conditional (ternary) expression: a ? b : c */
conditional_expression
    : binary_expression                     { $$ = $1; }
    | binary_expression '?' expression ':' assignment_expression {
        $$ = makeNode("ConditionalExpr", "", @1.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($3));
        $$->add(NodePtr($5));
    }
    ;

/* Binary operators (levels 1-11 handled by Bison precedence directives) */
binary_expression
    : unary_expression                      { $$ = $1; }
    | binary_expression OR binary_expression {
        $$ = makeNode("BinaryExpr", "||", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression AND binary_expression {
        $$ = makeNode("BinaryExpr", "&&", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '|' binary_expression {
        $$ = makeNode("BinaryExpr", "|", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '^' binary_expression {
        $$ = makeNode("BinaryExpr", "^", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '&' binary_expression {
        $$ = makeNode("BinaryExpr", "&", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression EQ binary_expression {
        $$ = makeNode("BinaryExpr", "==", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression NE binary_expression {
        $$ = makeNode("BinaryExpr", "!=", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '<' binary_expression {
        $$ = makeNode("BinaryExpr", "<", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '>' binary_expression {
        $$ = makeNode("BinaryExpr", ">", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression LE binary_expression {
        $$ = makeNode("BinaryExpr", "<=", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression GE binary_expression {
        $$ = makeNode("BinaryExpr", ">=", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression SHL binary_expression {
        $$ = makeNode("BinaryExpr", "<<", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression SHR binary_expression {
        $$ = makeNode("BinaryExpr", ">>", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '+' binary_expression {
        $$ = makeNode("BinaryExpr", "+", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '-' binary_expression {
        $$ = makeNode("BinaryExpr", "-", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '*' binary_expression {
        $$ = makeNode("BinaryExpr", "*", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '/' binary_expression {
        $$ = makeNode("BinaryExpr", "/", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression '%' binary_expression {
        $$ = makeNode("BinaryExpr", "%", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression DOT_STAR binary_expression {
        $$ = makeNode("BinaryExpr", ".*", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    | binary_expression ARROW_STAR binary_expression {
        $$ = makeNode("BinaryExpr", "->*", @2.first_line).release();
        $$->add(NodePtr($1)); $$->add(NodePtr($3));
    }
    ;

/* Unary expressions: prefix ++/--, sizeof, new, delete, casts */
unary_expression
    : postfix_expression                   { $$ = $1; }
    | INC unary_expression {
        $$ = makeNode("UnaryExpr", "++", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | DEC unary_expression {
        $$ = makeNode("UnaryExpr", "--", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | '+' unary_expression %prec PREFIX_OP {
        $$ = makeNode("UnaryExpr", "+", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | '-' unary_expression %prec PREFIX_OP {
        $$ = makeNode("UnaryExpr", "-", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | '!' unary_expression {
        $$ = makeNode("UnaryExpr", "!", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | '~' unary_expression {
        $$ = makeNode("UnaryExpr", "~", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | '*' unary_expression %prec PREFIX_OP {
        $$ = makeNode("UnaryExpr", "*", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | '&' unary_expression %prec PREFIX_OP {
        $$ = makeNode("UnaryExpr", "&", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | SIZEOF unary_expression {
        $$ = makeNode("SizeofExpr", "", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | SIZEOF '(' type_specifier ')' {
        $$ = makeNode("SizeofExpr", "", @1.first_line).release();
        $$->add(NodePtr($3));
    }
    | ALIGNOF '(' type_specifier ')' {
        $$ = makeNode("AlignofExpr", "", @1.first_line).release();
        $$->add(NodePtr($3));
    }
    | NEW type_specifier new_initializer_opt {
        $$ = makeNode("NewExpr", "", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | DELETE unary_expression {
        $$ = makeNode("DeleteExpr", "", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    | DELETE '[' ']' unary_expression {
        $$ = makeNode("DeleteExpr", "[]", @1.first_line).release();
        $$->add(NodePtr($4));
    }
    | '(' type_specifier ')' unary_expression %prec PREFIX_OP {
        $$ = makeNode("CastExpr", "c-style", @1.first_line).release();
        $$->add(NodePtr($2));
        $$->add(NodePtr($4));
    }
    ;

new_initializer_opt
    : /* empty */
    | '(' expression_list_opt ')'
    | braced_init_list
    ;

/* Postfix expressions: calls, subscript, member access, postfix ++/-- */
postfix_expression
    : primary_expression                    { $$ = $1; }
    | postfix_expression '[' expression ']' {
        $$ = makeNode("IndexExpr", "", @2.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($3));
    }
    | postfix_expression '(' expression_list_opt ')' {
        $$ = makeNode("CallExpr", "", @2.first_line).release();
        $$->add(NodePtr($1));
        if ($3) $$->add(NodePtr($3));
    }
    | postfix_expression '.' IDENTIFIER {
        $$ = makeNode("MemberExpr", "." + *$3, @2.first_line).release();
        $$->add(NodePtr($1));
        delete $3;
    }
    | postfix_expression ARROW IDENTIFIER {
        $$ = makeNode("MemberExpr", "->" + *$3, @2.first_line).release();
        $$->add(NodePtr($1));
        delete $3;
    }
    | postfix_expression INC {
        $$ = makeNode("PostfixExpr", "++", @2.first_line).release();
        $$->add(NodePtr($1));
    }
    | postfix_expression DEC {
        $$ = makeNode("PostfixExpr", "--", @2.first_line).release();
        $$->add(NodePtr($1));
    }
    ;

expression_list_opt
    : /* empty */                           { $$ = nullptr; }
    | expression_list                       { $$ = $1; }
    ;

expression_list
    : assignment_expression {
        $$ = makeNode("Arguments", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    | expression_list ',' assignment_expression {
        $$ = $1;
        $$->add(NodePtr($3));
    }
    ;

/* Primary expressions: literals, this, variables, parenthesized expressions */
primary_expression
    : INT_LITERAL {
        $$ = makeNode("IntLiteral", *$1, @1.first_line).release();
        delete $1;
    }
    | FLOAT_LITERAL {
        $$ = makeNode("FloatLiteral", *$1, @1.first_line).release();
        delete $1;
    }
    | CHAR_LITERAL {
        $$ = makeNode("CharLiteral", *$1, @1.first_line).release();
        delete $1;
    }
    | STRING_LITERAL {
        $$ = makeNode("StringLiteral", *$1, @1.first_line).release();
        delete $1;
    }
    | TRUE {
        $$ = makeNode("BoolLiteral", "true", @1.first_line).release();
    }
    | FALSE {
        $$ = makeNode("BoolLiteral", "false", @1.first_line).release();
    }
    | NULLPTR {
        $$ = makeNode("NullptrLiteral", "nullptr", @1.first_line).release();
    }
    | THIS {
        $$ = makeNode("This", "", @1.first_line).release();
    }
    | qualified_id {
        $$ = makeNode("IdExpr", "", @1.first_line).release();
    }
    | '(' expression ')' {
        $$ = $2;
    }
    | braced_init_list                      { $$ = $1; }
    | cpp_cast_expression                   { $$ = $1; }
    ;

/* C++ named casts: static_cast<T>(e), dynamic_cast<T>(e), etc. */
cpp_cast_expression
    : STATIC_CAST '<' type_specifier '>' '(' expression ')' {
        $$ = makeNode("CastExpr", "static_cast", @1.first_line).release();
        $$->add(NodePtr($3));
        $$->add(NodePtr($6));
    }
    | DYNAMIC_CAST '<' type_specifier '>' '(' expression ')' {
        $$ = makeNode("CastExpr", "dynamic_cast", @1.first_line).release();
        $$->add(NodePtr($3));
        $$->add(NodePtr($6));
    }
    | CONST_CAST '<' type_specifier '>' '(' expression ')' {
        $$ = makeNode("CastExpr", "const_cast", @1.first_line).release();
        $$->add(NodePtr($3));
        $$->add(NodePtr($6));
    }
    | REINTERPRET_CAST '<' type_specifier '>' '(' expression ')' {
        $$ = makeNode("CastExpr", "reinterpret_cast", @1.first_line).release();
        $$->add(NodePtr($3));
        $$->add(NodePtr($6));
    }
    ;

/* Initializer lists: { 1, 2, 3 } */
braced_init_list
    : '{' initializer_list_opt '}' {
        $$ = makeNode("BracedInit", "", @1.first_line).release();
    }
    ;

initializer_list_opt
    : /* empty */
    | initializer_list
    | initializer_list ','
    ;

initializer_list
    : initializer_clause
    | initializer_list ',' initializer_clause
    ;

initializer_clause
    : assignment_expression
    | braced_init_list
    ;

/* ==========================================================================
 * 6. TYPES AND DECLARATORS
 * Maps to: Parser::parseTypeSpecifier(), Parser::parseDeclarator(),
 *          Parser::parseParameterList() in src/parser_type.cpp
 * ========================================================================== */

simple_declaration
    : type_specifier init_declarator_list {
        $$ = makeNode("Declaration", "", @1.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($2));
    }
    | type_specifier {
        $$ = makeNode("Declaration", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    ;

init_declarator_list
    : init_declarator {
        $$ = makeNode("InitDeclaratorList", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    | init_declarator_list ',' init_declarator {
        $$ = $1;
        $$->add(NodePtr($3));
    }
    ;

init_declarator
    : declarator initializer_opt {
        $$ = makeNode("InitDeclarator", "", @1.first_line).release();
        $$->add(NodePtr($1));
        if ($2) $$->add(NodePtr($2));
    }
    ;

initializer_opt
    : /* empty */                           { $$ = nullptr; }
    | initializer                           { $$ = $1; }
    ;

initializer
    : '=' initializer_clause                { $$ = $2; }
    | '(' expression_list ')'               { $$ = $2; }
    | braced_init_list                      { $$ = $1; }
    ;

/* Type specifiers: cv-qualifiers, fundamental types, elaborated types */
type_specifier
    : trailing_type_specifier               { $$ = $1; }
    | CONST type_specifier                  { $$ = $2; }
    | VOLATILE type_specifier               { $$ = $2; }
    | CONSTEXPR type_specifier              { $$ = $2; }
    ;

trailing_type_specifier
    : simple_type_specifier {
        $$ = makeNode("TypeSpecifier", "", @1.first_line).release();
    }
    | class_specifier                       { $$ = $1; }
    | enum_specifier                        { $$ = $1; }
    ;

simple_type_specifier
    : CHAR | CHAR16_T | CHAR32_T | WCHAR_T | BOOL | SHORT | INT | LONG
    | SIGNED | UNSIGNED | FLOAT | DOUBLE | VOID | AUTO
    | qualified_id
    | DECLTYPE '(' expression ')'
    ;

/* Declarator: pointers, references, names, array bounds */
declarator
    : ptr_declarator                        { $$ = $1; }
    ;

ptr_declarator
    : noptr_declarator                      { $$ = $1; }
    | ptr_operator ptr_declarator {
        $$ = makeNode("Pointer", "", @1.first_line).release();
        $$->add(NodePtr($2));
    }
    ;

noptr_declarator
    : declarator_id {
        $$ = makeNode("Declarator", "", @1.first_line).release();
    }
    | noptr_declarator parameters_and_qualifiers {
        $$ = makeNode("FunctionDeclarator", "", @1.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($2));
    }
    | noptr_declarator '[' expression_opt ']' {
        $$ = makeNode("ArrayDeclarator", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    | '(' ptr_declarator ')'                { $$ = $2; }
    ;

ptr_operator
    : '*' cv_qualifier_seq_opt
    | '&'
    | AND /* && (rvalue reference) */
    ;

cv_qualifier_seq_opt
    : /* empty */
    | cv_qualifier_seq_opt CONST
    | cv_qualifier_seq_opt VOLATILE
    ;

declarator_id
    : qualified_id
    ;

/* Parameters list: (int a, double b = 1.0) */
parameters_and_qualifiers
    : '(' parameter_declaration_clause ')' cv_qualifier_seq_opt noexcept_specifier_opt {
        $$ = $2;
    }
    ;

parameter_declaration_clause
    : /* empty */ {
        $$ = makeNode("ParameterList", "", @$.first_line).release();
    }
    | parameter_declaration_list {
        $$ = $1;
    }
    | parameter_declaration_list ',' ELLIPSIS {
        $$ = $1;
    }
    ;

parameter_declaration_list
    : parameter_declaration {
        $$ = makeNode("ParameterList", "", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    | parameter_declaration_list ',' parameter_declaration {
        $$ = $1;
        $$->add(NodePtr($3));
    }
    ;

parameter_declaration
    : type_specifier declarator {
        $$ = makeNode("Parameter", "", @1.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($2));
    }
    | type_specifier declarator '=' initializer_clause {
        $$ = makeNode("Parameter", "default-arg", @1.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($2));
        $$->add(NodePtr($4));
    }
    | type_specifier {
        $$ = makeNode("Parameter", "abstract", @1.first_line).release();
        $$->add(NodePtr($1));
    }
    ;

noexcept_specifier_opt
    : /* empty */
    | NOEXCEPT
    | NOEXCEPT '(' expression ')'
    ;

/* Function definition: int foo(int x) { return x + 1; } */
function_definition
    : type_specifier declarator function_body {
        $$ = makeNode("FunctionDef", "", @1.first_line).release();
        $$->add(NodePtr($1));
        $$->add(NodePtr($2));
        $$->add(NodePtr($3));
    }
    ;

function_body
    : ctor_initializer_opt compound_statement { $$ = $2; }
    | '=' DEFAULT ';'                         { $$ = makeNode("DefaultFn", "", @1.first_line).release(); }
    | '=' DELETE ';'                          { $$ = makeNode("DeleteFn", "", @1.first_line).release(); }
    | '=' INT_LITERAL ';' /* pure virtual = 0 */ {
        $$ = makeNode("PureVirtualFn", "0", @1.first_line).release();
        delete $2;
    }
    ;

ctor_initializer_opt
    : /* empty */
    | ':' mem_initializer_list
    ;

mem_initializer_list
    : mem_initializer
    | mem_initializer_list ',' mem_initializer
    ;

mem_initializer
    : qualified_id '(' expression_list_opt ')'
    | qualified_id braced_init_list
    ;

/* Qualified identifiers: std::vector, Foo::Bar */
qualified_id
    : nested_name_specifier_opt unqualified_id
    ;

nested_name_specifier_opt
    : /* empty */
    | nested_name_specifier
    ;

nested_name_specifier
    : type_or_namespace_name SCOPE
    | nested_name_specifier type_or_namespace_name SCOPE
    | SCOPE
    ;

type_or_namespace_name
    : IDENTIFIER                            { delete $1; }
    | TYPE_NAME                             { delete $1; }
    ;

unqualified_id
    : IDENTIFIER                            { delete $1; }
    | '~' IDENTIFIER                        { delete $2; }
    | OPERATOR operator_symbol
    ;

operator_symbol
    : '+' | '-' | '*' | '/' | '%' | '^' | '&' | '|' | '~' | '!'
    | '=' | '<' | '>' | ADD_ASSIGN | SUB_ASSIGN | MUL_ASSIGN | DIV_ASSIGN
    | MOD_ASSIGN | XOR_ASSIGN | AND_ASSIGN | OR_ASSIGN | SHL | SHR
    | SHL_ASSIGN | SHR_ASSIGN | EQ | NE | LE | GE | AND | OR | INC | DEC
    | ',' | ARROW_STAR | ARROW | '(' ')' | '[' ']'
    ;

identifier_opt
    : /* empty */
    | IDENTIFIER                            { delete $1; }
    ;

%%

/* ==========================================================================
 * Epilogue: Support Functions
 * ========================================================================== */

NodePtr g_ast_root = nullptr;

void yyerror(const char* s) {
    fprintf(stderr, "Grammar Error at line %d: %s\n", yylloc.first_line, s);
}
