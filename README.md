# C++ Parser

A hand-written lexer and recursive-descent parser for a large subset of ISO C++17, written in C++17.
It reads a `.cpp` file and either builds a syntax tree or reports **the line number of every syntax error**,
plus any name that is used as a type but never declared.

## Build and run

```bash
cmake -S . -B build && cmake --build build
./build/cpp_parser samples/errors.cpp
```

Output for `samples/errors.cpp` (the example from the slides):

```
Error at line 3
Error at line 8
Error at line 12
3 errors found
```

| Option     | Effect                                                        |
|------------|---------------------------------------------------------------|
| `--ast`    | print the syntax tree when the file has no errors            |
| `--tokens` | print the token stream (line:col, kind, text)                |
| `-v`       | add column and a short description to each error             |
| `--no-undeclared` | do not report names used as a type but never declared |
| `-`        | read the source from standard input                          |

Exit status: `0` no errors, `1` syntax errors, `2` usage or I/O problem.
Run the tests with `tests/run_tests.sh build/cpp_parser` (or `ctest` inside `build/`).

## Source layout

| File                    | Role                                                                  |
|-------------------------|-----------------------------------------------------------------------|
| `grammar.y`             | Bison/Yacc grammar specification (formal BNF rules & AST actions)     |
| `src/token.hpp`         | token kinds                                                           |
| `src/lexer.cpp`         | scanner: comments, preprocessor lines, literals, maximal-munch operators |
| `src/ast.hpp`           | generic syntax tree node and tree printer                             |
| `src/diag.hpp`          | error collection (one error kept per line)                            |
| `src/parser.hpp/.cpp`   | parser class, token helpers, lookahead heuristics                     |
| `src/parser_decl.cpp`   | namespaces, using, typedef, templates, classes, enums, functions      |
| `src/parser_type.cpp`   | types, declarators, parameters, function tails, template arguments    |
| `src/parser_name.cpp`   | the undeclared-type check and its header heuristics                   |
| `src/parser_stmt.cpp`   | statements                                                            |
| `src/parser_expr.cpp`   | expressions (precedence climbing)                                     |
| `src/main.cpp`          | command line driver                                                   |

## Grammar covered

* **Lexical:** identifiers, all C++17 keywords, alternative operator spellings (`and`, `or`, `not`, ...),
  decimal/hex/binary/octal/float literals with suffixes and `'` separators, char/string/raw string literals with
  encoding prefixes, `//` and `/* */` comments, preprocessor directives (including line continuations).
* **Declarations:** variables, arrays, pointers, references, function pointers, structured bindings, functions
  (default arguments, `const`/`noexcept`/`override`/`final`, trailing return types, `= 0/default/delete`),
  constructors (with member initializer lists), destructors, conversion operators, operator overloads,
  classes/structs/unions (inheritance, access specifiers, bit-fields, friends, nested types), enums and scoped enums,
  namespaces (nested, aliases, `using`), `typedef`, alias declarations, `static_assert`, `extern "C"`,
  class/function/alias templates (type, non-type, template-template and variadic parameters).
* **Statements:** blocks, `if`/`else` (with C++17 init-statement and `constexpr`), `switch`/`case`/`default`,
  `for`, range-`for`, `while`, `do-while`, `break`, `continue`, `goto`/labels, `return`, `try`/`catch`.
* **Expressions:** all binary/unary/postfix operators with correct precedence and associativity, `?:`, assignment
  operators, `sizeof`/`alignof`, `new`/`delete`, C-style and named casts, function-style casts, lambdas (captures,
  `mutable`, trailing return), brace initializers, `throw`, `typeid`, `noexcept`, template arguments in expressions.

## Undeclared types

The parser keeps a table of every type the file declares (classes, structs, unions, enums,
typedefs, alias declarations and template parameters). A name used as a type is checked
against that table, so

```cpp
int main() {
    Student s;   // Error at line 2: unknown type name 'Student'
    return 0;
}
```

is reported even though it is perfectly well-formed as a sequence of tokens. Base class
names, parameter types, return types and member types are all checked the same way.

The check runs after the whole file has been read, not at the point of use, so a member
function body may mention a type the class declares further down:

```cpp
struct S {
    void f() { Nested n; }   // fine: Nested is declared below
    struct Nested {};
};
```

Headers are the one thing the parser cannot see through, so it only reports a name when it
can be sure:

* `#include "my_header.h"` — a header belonging to this project, whose contents are unknown.
  The check is switched off for the whole file; a name declared one file away is not an error.
* `#include <vector>` — the standard library. Names such as `string`, `vector` and `size_t`
  are accepted unqualified (for `using namespace std;`), but any other unknown name is still
  reported.
* No `#include` at all — every name used as a type must be declared in the file.

Three further cases are deliberately left alone, because the parser is guessing rather than
reading a type:

* **Qualified names** (`std::string`, `ns::Foo`, `T::value_type`) — resolving these means
  tracking namespaces and dependent names, and a wrong guess rejects correct code.
* **Names with template arguments** (`x<y> z;`) — this is read as a declaration, but
  `(x < y) > z;` is an equally valid reading, so no claim is made about `x`.
* **Elaborated specifiers** (`struct Node* next;`) — in C++ these declare the type rather
  than look it up.

`--no-undeclared` turns the check off and restores pure syntax checking.

## How C++'s ambiguities are resolved

* `a * b;` is a declaration only when `a` was declared as a type earlier (the parser keeps a table of type names).
* `<` starts template arguments in a type context, or in an expression only when the contents look like types
  and a `(`, `{` or `::` follows.
* `>>` is split into two `>` tokens while closing nested template argument lists.
* `Foo x(1);` (variable) versus `Foo x(int);` (function) is decided by the first token inside the parentheses.

## Error handling

A missing token is reported on the line of the token before it (the `;` missing on line 3 is reported on line 3).
A missing `}` is reported on the last line that was read. The parser never stops at the first error: it keeps
going, guarantees progress on every loop, and keeps only the first error per line so one mistake does not create
a flood of follow-on messages. Recursion depth is capped, so pathological input cannot crash it.

## Limitations

This is a syntax checker with one semantic check (undeclared types), not a full compiler front end:
there is no macro expansion or `#if` evaluation, no overload resolution or type checking of expressions,
and a few rare constructs are not supported (pointer-to-member declarators, function-try-blocks,
C++20 concepts/coroutines/modules).

## Formal Grammar (`grammar.y`) and Parser Generation

The project includes `grammar.y`, a Bison / Yacc specification representing the grammar rules of this C++ subset.

### Mapping: Grammar Productions to Recursive-Descent Functions

| Grammar Production (`grammar.y`) | Recursive Descent Function (`src/`) |
|-----------------------------------|--------------------------------------|
| `translation_unit`                | `Parser::parseTranslationUnit()` (`src/parser_decl.cpp`) |
| `declaration`                     | `Parser::parseDeclaration()` (`src/parser_decl.cpp`) |
| `class_specifier`                 | `Parser::parseClass()` (`src/parser_decl.cpp`) |
| `enum_specifier`                  | `Parser::parseEnum()` (`src/parser_decl.cpp`) |
| `statement`                       | `Parser::parseStatement()` (`src/parser_stmt.cpp`) |
| `compound_statement`              | `Parser::parseCompound()` (`src/parser_stmt.cpp`) |
| `selection_statement` (`if`)      | `Parser::parseIf()` (`src/parser_stmt.cpp`) |
| `iteration_statement` (`while`)   | `Parser::parseWhile()` (`src/parser_stmt.cpp`) |
| `iteration_statement` (`for`)     | `Parser::parseFor()` (`src/parser_stmt.cpp`) |
| `jump_statement` (`return`)       | `Parser::parseReturn()` (`src/parser_stmt.cpp`) |
| `expression`                      | `Parser::parseExpression()` (`src/parser_expr.cpp`) |
| `assignment_expression`           | `Parser::parseAssignment()` (`src/parser_expr.cpp`) |
| `conditional_expression`          | `Parser::parseConditional()` (`src/parser_expr.cpp`) |
| `binary_expression`               | `Parser::parseBinary()` (`src/parser_expr.cpp`) |
| `unary_expression`                | `Parser::parseUnary()` (`src/parser_expr.cpp`) |
| `postfix_expression`              | `Parser::parsePostfix()` (`src/parser_expr.cpp`) |
| `type_specifier`                  | `Parser::parseTypeSpecifier()` (`src/parser_type.cpp`) |
| `declarator`                      | `Parser::parseDeclarator()` (`src/parser_type.cpp`) |

### Using Bison instead of handwritten functions

To generate the parser engine directly from `grammar.y` with Bison:
```bash
bison -d -o src/parser_yacc.tab.cpp grammar.y
```
This generates `int yyparse()`, which replaces handwritten parsing functions with a table-driven LALR(1) state machine.

