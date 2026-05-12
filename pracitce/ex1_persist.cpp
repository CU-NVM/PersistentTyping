#include<iostream>
#include<libpmemobj++/make_persistent.hpp>
#include<libpmemobj++/persistent_ptr.hpp>
#include<libpmemobj++/pool.hpp>
#include <libpmemobj++/p.hpp>
#include<libpmemobj++/transaction.hpp>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>

using namespace pmem::obj;

#define POOL_PATH "/mnt/pmem-emu/ex1.pool"
#define POOL_SIZE (1024 *1024* 8)  // 8MB minimum


struct root {
    p<int> counter;
};

int main() {
    pool<root> pop;
    
    if(std::filesystem::exists(POOL_PATH)) {
        pop= pool<root>::open(POOL_PATH, "ex1");
    }
    else {
        pop = pool<root>::create(POOL_PATH, "ex1", POOL_SIZE, 0666);
    }

    auto r = pop.root();

    // transaction::run(pop, [&] {
    //     // if(r->counter = nullptr) 
    //     //     r->counter = make_persistent<int>(0);
    //     r->counter = r->counter + 1;
    // });

    

    // pid_t pid=fork();
    // if(pid==0) {
    //     transaction::run(pop, [&] {
    //         r->counter = r->counter + 999;
    //         abort();
    //     });
    // } else {
    //     wait(nullptr);
    // }
    std::cout << "Counter value: " << r->counter << std::endl;
    pop.close();
    return 0;
}