// diag.hpp - collects syntax errors found by the lexer and the parser.
#pragma once

#include <set>
#include <string>
#include <vector>

struct Diagnostic {
    int line;
    int col;
    std::string message;
};

class Diagnostics {
public:
    // Records an error. Only the first error on any given line is kept, so a
    // single mistake never floods the output with follow-on errors.
    void report(int line, int col, const std::string& message) {
        if (lines_.insert(line).second) items_.push_back({line, col, message});
    }

    bool hasErrors() const { return !items_.empty(); }
    const std::vector<Diagnostic>& items() const { return items_; }

private:
    std::set<int> lines_;
    std::vector<Diagnostic> items_;
};
