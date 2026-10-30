// lexer.cpp - hand-written scanner (maximal munch for operators).
#include "lexer.hpp"

#include <cctype>
#include <unordered_map>

const char* tokName(Tok kind) {
    switch (kind) {
        case Tok::Identifier: return "IDENT";
        case Tok::Keyword: return "KEYWORD";
        case Tok::IntLiteral: return "INT";
        case Tok::FloatLiteral: return "FLOAT";
        case Tok::CharLiteral: return "CHAR";
        case Tok::StringLiteral: return "STRING";
        case Tok::Preprocessor: return "PREPROC";
        case Tok::Punct: return "PUNCT";
        case Tok::Eof: return "EOF";
        case Tok::Invalid: return "INVALID";
    }
    return "?";
}

bool isKeyword(const std::string& word) {
    static const std::unordered_set<std::string> keywords = {
        "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char",
        "char16_t", "char32_t", "class", "const", "const_cast", "constexpr", "continue",
        "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if",
        "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "nullptr",
        "operator", "private", "protected", "public", "register", "reinterpret_cast",
        "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch", "template", "this", "thread_local", "throw", "true", "try",
        "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
        "volatile", "wchar_t", "while"};
    return keywords.count(word) != 0;
}

namespace {

// `and`, `or`, `not` ... are alternative spellings of operators.
const std::unordered_map<std::string, std::string>& alternativeTokens() {
    static const std::unordered_map<std::string, std::string> table = {
        {"and", "&&"}, {"or", "||"},   {"not", "!"},     {"xor", "^"},
        {"bitand", "&"}, {"bitor", "|"}, {"compl", "~"}, {"not_eq", "!="},
        {"and_eq", "&="}, {"or_eq", "|="}, {"xor_eq", "^="}};
    return table;
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;  // UTF-8 identifiers
}
bool isIdentChar(char c) { return isIdentStart(c) || std::isdigit(static_cast<unsigned char>(c)); }

class Lexer {
public:
    Lexer(const std::string& source, Diagnostics& diag) : s_(source), diag_(diag) {}
    std::vector<Token> run();

private:
    const std::string& s_;
    Diagnostics& diag_;
    size_t i_ = 0;
    int line_ = 1;
    int col_ = 1;
    bool lineStart_ = true;  // only blanks/comments seen so far on this line
    std::vector<Token> out_;

    bool eof() const { return i_ >= s_.size(); }
    char cur(size_t ahead = 0) const { return i_ + ahead < s_.size() ? s_[i_ + ahead] : '\0'; }
    void bump() {
        if (eof()) return;
        if (s_[i_] == '\n') {
            ++line_;
            col_ = 1;
            lineStart_ = true;
        } else {
            ++col_;
        }
        ++i_;
    }
    void emit(Tok kind, std::string text, int line, int col) {
        out_.push_back(Token{kind, std::move(text), line, col});
        lineStart_ = false;
    }

    void skipTrivia();
    void lexPreprocessor();
    void lexIdentifier();
    void lexNumber();
    void lexQuoted(char quote, const std::string& prefix, int line, int col);
    void lexRawString(const std::string& prefix, int line, int col);
    void lexPunct();
};

void Lexer::skipTrivia() {
    while (!eof()) {
        const char c = cur();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            bump();
        } else if (c == '/' && cur(1) == '/') {
            while (!eof() && cur() != '\n') bump();
        } else if (c == '/' && cur(1) == '*') {
            const int startLine = line_, startCol = col_;
            bump();
            bump();
            bool closed = false;
            while (!eof()) {
                if (cur() == '*' && cur(1) == '/') {
                    bump();
                    bump();
                    closed = true;
                    break;
                }
                bump();
            }
            if (!closed) diag_.report(startLine, startCol, "unterminated comment");
        } else {
            break;
        }
    }
}

void Lexer::lexPreprocessor() {
    const int startLine = line_, startCol = col_;
    std::string text;
    while (!eof()) {
        const char c = cur();
        if (c == '\\' && (cur(1) == '\n' || (cur(1) == '\r' && cur(2) == '\n'))) {
            bump();                      // backslash
            if (cur() == '\r') bump();
            bump();                      // newline: directive continues on the next line
            text += ' ';
            continue;
        }
        if (c == '\n') break;
        if (c == '/' && cur(1) == '/') {  // trailing comment
            while (!eof() && cur() != '\n') bump();
            break;
        }
        if (c == '/' && cur(1) == '*') {  // block comment inside a directive
            bump();
            bump();
            while (!eof() && !(cur() == '*' && cur(1) == '/')) bump();
            if (!eof()) {
                bump();
                bump();
            }
            text += ' ';
            continue;
        }
        if (c != '\r') text.push_back(c);
        bump();
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    emit(Tok::Preprocessor, text, startLine, startCol);
}

void Lexer::lexIdentifier() {
    const int startLine = line_, startCol = col_;
    const size_t start = i_;
    while (isIdentChar(cur())) bump();
    const std::string word = s_.substr(start, i_ - start);

    // String and character literals may carry an encoding prefix: L"x", u8"x", R"(x)".
    static const std::unordered_set<std::string> prefixes = {"L", "u", "U", "u8", "R", "LR", "uR", "UR", "u8R"};
    if ((cur() == '"' || cur() == '\'') && prefixes.count(word)) {
        const bool raw = word.back() == 'R';
        if (raw && cur() == '"') {
            lexRawString(word, startLine, startCol);
            return;
        }
        if (!raw) {
            lexQuoted(cur(), word, startLine, startCol);
            return;
        }
    }

    const auto alt = alternativeTokens().find(word);
    if (alt != alternativeTokens().end()) {
        emit(Tok::Punct, alt->second, startLine, startCol);
        return;
    }
    emit(isKeyword(word) ? Tok::Keyword : Tok::Identifier, word, startLine, startCol);
}

void Lexer::lexNumber() {
    const int startLine = line_, startCol = col_;
    const size_t start = i_;
    const bool hex = cur() == '0' && (cur(1) == 'x' || cur(1) == 'X');
    const bool bin = cur() == '0' && (cur(1) == 'b' || cur(1) == 'B');
    bool isFloat = false;
    if (hex || bin) {
        bump();
        bump();
    }
    while (true) {
        const char c = cur();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            const bool exponent = hex ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E');
            bump();
            if (exponent) {
                isFloat = true;
                if (cur() == '+' || cur() == '-') bump();
            }
        } else if (c == '.' && !isIdentStart(cur(1)) && cur(1) != '.') {
            isFloat = true;
            bump();
        } else if (c == '\'' && std::isalnum(static_cast<unsigned char>(cur(1)))) {
            bump();  // digit separator: 1'000'000
        } else {
            break;
        }
    }
    const std::string text = s_.substr(start, i_ - start);
    if ((hex || bin) && text.size() == 2) diag_.report(startLine, startCol, "invalid numeric literal");
    emit(isFloat ? Tok::FloatLiteral : Tok::IntLiteral, text, startLine, startCol);
}

void Lexer::lexQuoted(char quote, const std::string& prefix, int startLine, int startCol) {
    std::string text = prefix;
    text.push_back(quote);
    bump();  // opening quote
    bool closed = false;
    while (!eof() && cur() != '\n') {
        const char c = cur();
        if (c == '\\') {  // escape sequence
            text.push_back(c);
            bump();
            if (!eof() && cur() != '\n') {
                text.push_back(cur());
                bump();
            }
            continue;
        }
        text.push_back(c);
        bump();
        if (c == quote) {
            closed = true;
            break;
        }
    }
    if (!closed)
        diag_.report(startLine, startCol,
                     quote == '"' ? "unterminated string literal" : "unterminated character literal");
    emit(quote == '"' ? Tok::StringLiteral : Tok::CharLiteral, text, startLine, startCol);
}

void Lexer::lexRawString(const std::string& prefix, int startLine, int startCol) {
    std::string text = prefix + "\"";
    bump();  // opening quote
    std::string delim;
    while (!eof() && cur() != '(' && cur() != '\n' && delim.size() < 16) {  // at most 16 delimiter characters
        delim.push_back(cur());
        text.push_back(cur());
        bump();
    }
    if (cur() != '(') {
        diag_.report(startLine, startCol, "invalid raw string literal");
        emit(Tok::StringLiteral, text, startLine, startCol);
        return;
    }
    text.push_back('(');
    bump();
    const std::string terminator = ")" + delim + "\"";
    bool closed = false;
    while (!eof()) {
        if (s_.compare(i_, terminator.size(), terminator) == 0) {
            for (size_t k = 0; k < terminator.size(); ++k) {
                text.push_back(cur());
                bump();
            }
            closed = true;
            break;
        }
        text.push_back(cur());
        bump();
    }
    if (!closed) diag_.report(startLine, startCol, "unterminated raw string literal");
    emit(Tok::StringLiteral, text, startLine, startCol);
}

void Lexer::lexPunct() {
    static const char* const three[] = {"<<=", ">>=", "...", "->*"};
    static const char* const two[] = {"::", "->", ".*", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
                                      "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##"};
    static const std::string one = "{}[]();:?,.+-*/%^&|~!=<>#";
    const int startLine = line_, startCol = col_;

    for (const char* p : three) {
        if (s_.compare(i_, 3, p) == 0) {
            emit(Tok::Punct, p, startLine, startCol);
            bump(); bump(); bump();
            return;
        }
    }
    for (const char* p : two) {
        if (s_.compare(i_, 2, p) == 0) {
            emit(Tok::Punct, p, startLine, startCol);
            bump(); bump();
            return;
        }
    }
    if (one.find(cur()) != std::string::npos) {
        emit(Tok::Punct, std::string(1, cur()), startLine, startCol);
        bump();
        return;
    }
    diag_.report(startLine, startCol, std::string("unexpected character '") + cur() + "'");
    emit(Tok::Invalid, std::string(1, cur()), startLine, startCol);
    bump();
}

std::vector<Token> Lexer::run() {
    while (true) {
        skipTrivia();
        if (eof()) break;
        const char c = cur();
        if (c == '#' && lineStart_)
            lexPreprocessor();
        else if (isIdentStart(c))
            lexIdentifier();
        else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(cur(1)))))
            lexNumber();
        else if (c == '"' || c == '\'')
            lexQuoted(c, "", line_, col_);
        else
            lexPunct();
    }
    out_.push_back(Token{Tok::Eof, "", line_, col_});
    return std::move(out_);
}

}  // namespace

std::vector<Token> tokenize(const std::string& source, Diagnostics& diag) {
    return Lexer(source, diag).run();
}
