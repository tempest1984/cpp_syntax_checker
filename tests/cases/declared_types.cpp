// Every type here is declared somewhere in the file, including one used by a member
// function before the class declares it, so nothing may be reported.
struct Node;

enum Level { Low, High };
typedef unsigned long ulong_t;
using Alias = Level;

class Base {
public:
    virtual ~Base() = default;
};

class Derived : public Base {
public:
    void f() { Nested n; (void)n; }
    struct Nested { int a; };
};

struct List {
    Node* head;
    ulong_t size;
};

struct Node { int value; };

Derived makeDerived();
void consume(const Derived& d, Base* b);

int main() {
    Derived d;
    Level l = Low;
    Alias a = High;
    List list;
    consume(d, &d);
    return 0;
}
