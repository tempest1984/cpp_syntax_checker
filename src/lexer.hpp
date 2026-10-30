// lexer.hpp - turns C++ source text into a vector of tokens.
#pragma once

#include <string>
#include <vector>

#include "diag.hpp"
#include "token.hpp"

// Always ends the returned vector with a Tok::Eof token.
// Lexical problems (unterminated strings/comments, stray characters) go to `diag`.
std::vector<Token> tokenize(const std::string& source, Diagnostics& diag);
