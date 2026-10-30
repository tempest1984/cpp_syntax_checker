// valid.cpp - a program that uses most of the grammar the parser understands.
// It must parse with "No errors found".
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define SQUARE(x) ((x) * (x))
#define MAX_SIZE 100

using namespace std;
using std::string;
namespace geo = std;

constexpr int kMax = 100;
const double PI = 3.14159;
static_assert(kMax > 0, "kMax must be positive");

typedef unsigned long size_type;
typedef int (*BinaryOp)(int, int);
using Callback = void (*)(int);
template <typename T> using Vec = std::vector<T>;

extern "C" {
int c_function(int x);
}

namespace shapes {

enum class Color : int { Red, Green = 5, Blue };
enum Level { Low, High };

struct Point {
    int x;
    int y;
    Point(int px = 0, int py = 0) : x(px), y(py) {}
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

struct Flags {
    unsigned int visible : 1;
    unsigned int mode : 3;
};

class Shape {
public:
    Shape() = default;
    virtual ~Shape() = default;
    virtual double area() const = 0;
    virtual string name() const { return "shape"; }
    static int count;

protected:
    int id_ = 0;
};

class Circle : public Shape {
public:
    explicit Circle(double r) : radius_(r) { ++count; }
    double area() const override { return PI * radius_ * radius_; }
    string name() const override { return "circle"; }
    Circle& operator+=(const Circle& other) {
        radius_ += other.radius_;
        return *this;
    }
    friend std::ostream& operator<<(std::ostream& os, const Circle& c);

private:
    double radius_;
};

int Shape::count = 0;

}  // namespace shapes

template <typename T, int N = 4>
class Stack {
public:
    void push(const T& value) { items_.push_back(value); }
    T pop() {
        T top = items_.back();
        items_.pop_back();
        return top;
    }
    bool empty() const noexcept { return items_.empty(); }
    int capacity() const { return N; }

private:
    std::vector<T> items_;
};

template <typename T>
T maxOf(T a, T b) {
    return a > b ? a : b;
}

template <typename... Args>
int countArgs(Args... args) {
    return sizeof...(Args);
}

inline int twice(int x) { return x * 2; }
auto sum(int a, int b) -> int { return a + b; }
int apply(BinaryOp op, int a, int b) { return op(a, b); }
void greet(const std::string& name, int times = 1);

int global_counter = 0;
const char* names[] = {"alpha", "beta", "gamma"};
int matrix[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};

std::pair<int, int> divide(int a, int b) { return {a / b, a % b}; }

int main() {
    // declarations and initialisation
    int a = 5, b = 0x1F, c = 0b1010;
    long long big = 1'000'000LL << 4;
    double d = 3.14e-2, e = .5;
    float f = 2.5f;
    char ch = '\n';
    bool ok = true && !false || nullptr == nullptr;
    string s = "hello" " world";
    string raw = R"(raw "text")";
    int* p = new int(5);
    int* arr = new int[10];
    std::vector<int> v{1, 2, 3};
    std::vector<std::vector<int>> grid(3, std::vector<int>(3, 0));
    std::map<std::string, int> table;
    shapes::Point origin = {0, 0};
    shapes::Point pt{3, 4};
    shapes::Circle circle(2.0);
    Vec<int> numbers = {4, 5, 6};
    int (*op)(int, int) = &twice2;
    auto [q, r] = divide(7, 2);

    // expressions
    a += b * c - (a << 2) / 3 % 2;
    a = b = c;
    b = (int)d + static_cast<int>(f) + sizeof(int);
    char* raw_ptr = reinterpret_cast<char*>(p);
    ok = a < b && b >= c || (a != c) ^ (b == 0);
    a = ok ? 1 : (b > 2 ? 2 : 3);
    a = ~b & 0xFF | c;
    ++a;
    b--;
    p[0] = *p + arr[1] - SQUARE(a);
    d = pt.x + origin.y + circle.area();
    delete p;
    delete[] arr;

    // lambdas
    auto square = [](int x) { return x * x; };
    auto add = [&](int x, int y) -> int { return x + y + a; };
    auto counter = [n = 0]() mutable { return ++n; };

    // control flow
    if (a > 3) {
        a = a * 2 + 1;
    } else if (a < 0) {
        a = -a;
    } else {
        a = 0;
    }

    if (auto it = table.find("key"); it != table.end()) {
        it->second += 1;
    }

    switch (a) {
        case 1:
        case 2:
            b = 10;
            break;
        case shapes::Low:
            break;
        default:
            b = 20;
    }

    for (int i = 0, j = 10; i < j; ++i, --j) {
        if (i == 3) continue;
        a += i;
    }
    for (const auto& item : v) {
        std::cout << item << std::endl;
    }
    for (auto& [key, value] : table) {
        value = key.size();
    }
    while (a > 0) {
        --a;
    }
    do {
        ++a;
    } while (a < 5);

    // exceptions
    try {
        if (a == 0) throw std::runtime_error("zero");
        throw;
    } catch (const std::exception& ex) {
        std::cout << ex.what() << "\n";
    } catch (...) {
        return 1;
    }

    goto done;
done:
    return 0;
}

void greet(const std::string& name, int times) {
    for (int i = 0; i < times; ++i) std::cout << "Hello, " << name << "!\n";
}
