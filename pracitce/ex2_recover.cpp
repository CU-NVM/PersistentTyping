#include <iostream>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/transaction.hpp>
#include <unistd.h>

using namespace pmem::obj;

#define POOL_PATH "/mnt/pmem-emu/ex2.pool"

struct Node { p<int> value; persistent_ptr<Node> next; };
struct Stack {
    persistent_ptr<Node> top;
    p<int> size;
    void print() {
        auto cur = top;
        std::cout << "stack (top -> bottom): ";
        while (cur) { std::cout << cur->value << " "; cur = cur->next; }
        std::cout << "(size=" << size << ")\n";
    }
};
struct root { persistent_ptr<Stack> stack; };

int main() {
    auto pop = pool<root>::open(POOL_PATH, "ex2");
    pop.root()->stack->print();
    pop.close();
}
