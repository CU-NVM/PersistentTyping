#include <iostream>
#include <stdexcept>
#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/transaction.hpp>
#include <unistd.h>
#include <sys/wait.h>


using namespace pmem::obj;

#define POOL_PATH "/mnt/pmem-emu/ex2.pool"
#define POOL_SIZE (1024 * 1024 * 32)  // 8

struct Node {
    p<int> value;
    persistent_ptr<Node> next;
};

struct Stack {
    persistent_ptr<Node> top;
    p<int> size;

    void push(pool_base &pop, int val) {
        transaction::run(pop, [&] {
            auto node = make_persistent<Node>();
            node->value = val;
            node->next = top;
            top = node;
            size= size +1;
        });
    }

    int pop(pool_base &pop) {
        int val;
        transaction::run(pop, [&] {
            if(size == 0) { 
                throw std::runtime_error("Empty stack");;
            }
            val= top->value;
            auto old_top = top;
            top = top->next;
            delete_persistent<Node>(old_top);
            size = size -1;
        });
        return val;   // The member functions always need a return value so you can't just convert everything to transactions
    }
    void crash_push(pool_base &pop, int val) {
        pid_t pid = fork();
        if (pid == 0) {
            transaction::run(pop, [&] {
                auto node = make_persistent<Node>();
                node->value = val;
                node->next = top;
                top = node;
                size = size + 1;
                abort();   // crash with everything written but not committed
            });
        } else {
            wait(nullptr);
        }
    }
    void print() {
        auto cur = top;
        std::cout << "stack (top -> bottom): ";
        while (cur != nullptr) {
            std::cout << cur->value << " ";
            cur = cur->next;
        }        
        std::cout << std::endl;
    }
};  

struct root {
    persistent_ptr<Stack> stack;
};



int main() {
    /*On recovery run this*/
    auto pop = pool<root>::open(POOL_PATH, "ex2");
    pop.root()->stack->print();
    pop.close();
//     pool<root> pop;

//     if(access(POOL_PATH, F_OK) == 0) {
//         pop = pool<root>::open(POOL_PATH, "ex2");
//     }else{
//         pop = pool<root>::create(POOL_PATH, "ex2", POOL_SIZE, 0666);
//     }

//     auto r = pop.root();

//     if(r->stack == nullptr) {
//         transaction::run (pop, [&] {
//             r->stack = make_persistent<Stack>();
//         });
//     }

//    r->stack->print();

//     // push two values normally
//     r->stack->push(pop, 10);
//     r->stack->push(pop, 20);
//     std::cout << "before crash:\n";
//     r->stack->print();

//     // crash mid-push of 999
//     r->stack->crash_push(pop, 999);
//     std::cout << "after crash (parent sees):\n";
//     r->stack->print();

//     pop.close();
    return 0;
}

