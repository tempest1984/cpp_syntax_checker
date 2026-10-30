// main.cpp - command line driver.
//
//   cpp_parser [--ast] [--tokens] [-v] <file.cpp | ->
//
// Default output for a file with syntax errors is only the line number of each error:
//   Error at line 3
//   Error at line 8
//   3 errors found
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include "lexer.hpp"
#include "parser.hpp"

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options] <file.cpp | ->\n"
              << "  --ast      print the syntax tree when the file has no errors\n"
              << "  --tokens   print the token stream\n"
              << "  -v         also print column and a short description of every error\n"
              << "  --no-undeclared  do not report names used as a type but never declared\n"
              << "  -h         show this help\n"
              << "Exit status: 0 = no errors, 1 = syntax errors, 2 = usage / I/O problem\n";
}

int main(int argc, char** argv) {
    bool showAst = false, showTokens = false, verbose = false, checkUndeclared = true;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--ast") {
            showAst = true;
        } else if (arg == "--tokens") {
            showTokens = true;
        } else if (arg == "-v") {
            verbose = true;
        } else if (arg == "--no-undeclared") {
            checkUndeclared = false;
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else if (arg.size() > 1 && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            usage(argv[0]);
            return 2;
        } else {
            path = arg;
        }
    }
    if (path.empty()) {
        usage(argv[0]);
        return 2;
    }

    std::stringstream buffer;
    if (path == "-") {
        buffer << std::cin.rdbuf();
    } else {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::cerr << "cannot open '" << path << "'\n";
            return 2;
        }
        buffer << in.rdbuf();
    }

    Diagnostics diag;
    std::vector<Token> tokens = tokenize(buffer.str(), diag);
    if (showTokens) {
        for (const Token& t : tokens)
            std::cout << t.line << ':' << t.col << "\t" << tokName(t.kind) << "\t" << t.text << "\n";
    }

    Parser parser(tokens, diag);
    parser.setUndeclaredTypeCheck(checkUndeclared);
    NodePtr tree = parser.parseTranslationUnit();

    if (diag.hasErrors()) {
        std::vector<Diagnostic> errors = diag.items();
        std::stable_sort(errors.begin(), errors.end(),
                         [](const Diagnostic& a, const Diagnostic& b) { return a.line < b.line; });
        for (const Diagnostic& e : errors) {
            std::cout << "Error at line " << e.line;
            if (verbose) std::cout << " (col " << e.col << "): " << e.message;
            std::cout << "\n";
        }
        std::cout << errors.size() << (errors.size() == 1 ? " error" : " errors") << " found\n";
        return 1;
    }

    std::cout << "No errors found\n";
    if (showAst) printTree(*tree, std::cout);
    return 0;
}
