#include <functional>
#include <map>
#include <string>
#include <vector>
using Map = std::map<std::string, std::vector<int>>;
using Cube = std::vector<std::vector<std::vector<int>>>;
std::function<int(const std::string&, double)> callback;
struct Point { int x, y; };
int main() {
    auto p = std::pair<int, std::string>{1, "a"};
    auto q = Point{1, 2};
    auto v = std::make_shared<std::vector<int>>();
    double{1.5};
    const char* s = R"1234567890123456(ok)1234567890123456";
    return 0;
}
