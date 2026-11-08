#include "rt.hpp"

persistent<persistent<int>*> buf;        // one slot that holds an array
persistent<persistent<int>*> rows[2];    // each element holds an array

int main() {
    buf = new persistent<int>[3];
    for (int i = 0; i < 2; i++) rows[i] = new persistent<int>[4];
    pmem::obj::transaction::run(pmem_pool(), [&] { buf[2] += 1; rows[1][3] += 1; });
    std::cout << "buf " << buf[2] << " row " << rows[1][3] << "\n";
    std::cout << "objs " << object_count() << "\n";
}
