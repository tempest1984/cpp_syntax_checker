// parser_name.cpp - the undeclared-type check.
//
// The parser keeps a table of every type declared in the file (typeNames_). That table
// already existed to resolve `a * b;`, but nothing ever asked whether a name used as a
// type was in it, so `Student s;` parsed happily with no class Student anywhere. This
// file closes that gap: parseTypeSpecifier() records each unqualified name it reads as a
// type, and resolveTypeUses() checks the whole list once the file has been read.
//
// Checking at the end rather than at the point of use matters, because a member function
// body may legally mention a type that the class declares further down:
//
//     struct S { void f() { Nested n; } struct Nested {}; };
//
// Headers are the one thing the parser cannot see through, so the check steps aside when
// it would have to guess: see noteTypeUse() and scanIncludes() below.
#include "parser.hpp"

// Names from the standard library that routinely appear unqualified, through
// `using namespace std;` or `using std::string;`. They are only trusted when the file
// includes at least one system header, so a file with no #include at all is still
// checked strictly.
const std::unordered_set<std::string>& Parser::libraryTypeNames() {
    static const std::unordered_set<std::string> names = {
        // containers and friends
        "string", "wstring", "u16string", "u32string", "string_view", "wstring_view",
        "vector", "array", "deque", "list", "forward_list", "queue", "stack",
        "priority_queue", "map", "multimap", "set", "multiset", "unordered_map",
        "unordered_multimap", "unordered_set", "unordered_multiset", "bitset", "valarray",
        "pair", "tuple", "initializer_list", "optional", "variant", "any",
        // memory and callables
        "unique_ptr", "shared_ptr", "weak_ptr", "auto_ptr", "allocator", "function",
        "reference_wrapper", "hash", "less", "greater",
        // streams
        "ostream", "istream", "iostream", "ofstream", "ifstream", "fstream",
        "stringstream", "istringstream", "ostringstream", "streambuf", "streamsize",
        "wostream", "wistream", "ios", "ios_base", "iterator", "ptrdiff_t",
        // errors
        "exception", "bad_alloc", "bad_cast", "runtime_error", "logic_error",
        "out_of_range", "invalid_argument", "length_error", "domain_error",
        "range_error", "overflow_error", "underflow_error", "error_code", "system_error",
        // threads, time, numerics
        "thread", "mutex", "recursive_mutex", "timed_mutex", "lock_guard", "unique_lock",
        "scoped_lock", "condition_variable", "atomic", "future", "promise",
        "packaged_task", "complex", "regex", "smatch", "cmatch", "random_device",
        "mt19937", "mt19937_64", "nullptr_t",
        // C library types
        "size_t", "ssize_t", "intptr_t", "uintptr_t", "intmax_t", "uintmax_t",
        "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
        "uint64_t", "int_least8_t", "int_least16_t", "int_least32_t", "int_least64_t",
        "uint_least8_t", "uint_least16_t", "uint_least32_t", "uint_least64_t",
        "int_fast8_t", "int_fast16_t", "int_fast32_t", "int_fast64_t",
        "uint_fast8_t", "uint_fast16_t", "uint_fast32_t", "uint_fast64_t",
        "FILE", "va_list", "time_t", "clock_t", "tm", "div_t", "ldiv_t", "jmp_buf",
        "wchar_t", "wint_t", "fpos_t", "sig_atomic_t", "max_align_t", "byte",
    };
    return names;
}

// Classifies the #include directives once, before parsing. A quoted include names a
// header belonging to this project, and the parser has no way to know which types it
// declares, so the check is disabled rather than reporting names that are perfectly
// well declared one file away.
void Parser::scanIncludes() {
    for (const Token& tok : t_) {
        if (tok.kind != Tok::Preprocessor) continue;
        const size_t hash = tok.text.find('#');
        if (hash == std::string::npos) continue;
        size_t i = tok.text.find_first_not_of(" \t", hash + 1);
        if (i == std::string::npos || tok.text.compare(i, 7, "include") != 0) continue;
        const size_t open = tok.text.find_first_of("<\"", i + 7);
        if (open == std::string::npos) continue;
        if (tok.text[open] == '"')
            opaqueHeaders_ = true;
        else
            systemHeaders_ = true;
    }
}

// Called by parseTypeSpecifier() for every name it consumes as a type.
//
// Only a plain unqualified name is recorded. A qualified name (`std::string`,
// `shapes::Point`, `T::value_type`) is left alone because resolving it would mean
// tracking namespaces and dependent names, and guessing wrong would mean rejecting
// correct code. An elaborated specifier (`struct Node* next;`) is left alone too: in
// C++ it declares the type rather than merely using it.
void Parser::noteTypeUse(size_t nameTok, const std::string& spelling, bool elaborated) {
    if (!checkUndeclared_ || opaqueHeaders_ || elaborated) return;
    if (at(nameTok).kind != Tok::Identifier) return;
    if (spelling.find("::") != std::string::npos) return;  // qualified: not ours to judge
    // A name carrying template arguments is where the declaration/expression ambiguity
    // lives: `x<y> z;` is read as a declaration here, but `(x < y) > z;` is just as
    // valid a reading, and only a real compiler knows which. The parser does not claim
    // `x` is a type, so it does not complain that `x` was never declared either. This
    // mirrors how `a * b;` is left as a multiplication when `a` is not a known type.
    if (spelling.find('<') != std::string::npos) return;
    typeUses_.push_back({at(nameTok).text, at(nameTok).line, at(nameTok).col});
}

// Run once, after the whole translation unit has been parsed.
void Parser::resolveTypeUses() {
    if (!checkUndeclared_ || opaqueHeaders_) return;
    for (const TypeUse& use : typeUses_) {
        if (typeNames_.count(use.name)) continue;
        if (systemHeaders_ && libraryTypeNames().count(use.name)) continue;
        diag_.report(use.line, use.col, "unknown type name '" + use.name + "'");
    }
}
