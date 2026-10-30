// ast.hpp - a small generic syntax tree: every node has a kind, a text and children.
#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    std::string kind;
    std::string text;
    int line = 0;
    std::vector<NodePtr> kids;

    Node(std::string k, std::string t, int l) : kind(std::move(k)), text(std::move(t)), line(l) {}

    void add(NodePtr child) {
        if (child) kids.push_back(std::move(child));
    }
};

inline NodePtr makeNode(std::string kind, std::string text, int line) {
    return std::make_unique<Node>(std::move(kind), std::move(text), line);
}

// Prints the tree with box-drawing characters, one node per line.
inline void printTree(const Node& n, std::ostream& os, const std::string& prefix = "",
                      bool isLast = true, bool isRoot = true) {
    if (isRoot)
        os << n.kind;
    else
        os << prefix << (isLast ? "└─ " : "├─ ") << n.kind;
    if (!n.text.empty()) os << ' ' << n.text;
    os << "  [line " << n.line << "]\n";
    const std::string childPrefix = isRoot ? "" : prefix + (isLast ? "   " : "│  ");
    for (size_t i = 0; i < n.kids.size(); ++i)
        printTree(*n.kids[i], os, childPrefix, i + 1 == n.kids.size(), false);
}
