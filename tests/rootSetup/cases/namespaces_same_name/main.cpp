#include "rt.hpp"

namespace a { persistent<int> x = 1; }
namespace b { persistent<int> x = 2; }

int main() {
    pmem::obj::transaction::run(pmem_pool(), [&] { a::x += 1; b::x += 10; });
    std::cout << "a " << a::x << " b " << b::x << "\n";
}
