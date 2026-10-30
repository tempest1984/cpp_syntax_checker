// token.hpp - token kinds produced by the lexer.
#pragma once

#include <string>
#include <unordered_set>

enum class Tok {
    Identifier,
    Keyword,
    IntLiteral,
    FloatLiteral,
    CharLiteral,
    StringLiteral,
    Preprocessor,  // a whole directive line such as  #include <vector>
    Punct,         // operators and punctuators, text holds the spelling
    Eof,
    Invalid
};

struct Token {
    Tok kind = Tok::Eof;
    std::string text;
    int line = 1;
    int col = 1;
};

const char* tokName(Tok kind);
bool isKeyword(const std::string& word);
