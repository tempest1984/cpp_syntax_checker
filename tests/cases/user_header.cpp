// The parser cannot see inside "student.h", so Student may well be declared there.
// The undeclared-type check steps aside rather than guessing; the syntax error on the
// line below it is still reported.
#include "student.h"

int main() {
    Student s;
    int total = (5 + 3;
    return 0;
}
