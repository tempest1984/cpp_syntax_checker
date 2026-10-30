// A system header does not excuse an unknown type, but it does make the standard
// library names available unqualified.
#include <iostream>
#include <string>
using namespace std;

int main() {
    string name = "ok";
    size_t n = name.size();
    Student s;
    cout << n << endl;
    return 0;
}
