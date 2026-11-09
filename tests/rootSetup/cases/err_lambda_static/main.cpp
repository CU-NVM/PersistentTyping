#include "rt.hpp"

int main() {
    auto next = [] {
        static persistent<int> calls = 0;   // a lambda's type has no stable name
        return 0;
    };
    return next();
}
