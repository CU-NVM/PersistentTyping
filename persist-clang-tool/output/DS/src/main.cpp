/*! \file main.cpp
 * \brief Test Suite for classic Data Structures.
 * \author Nii Mante
 * \date 10/28/2012
 *
 */

#include "Stack.hpp"
#include <iostream>

#include "persistenttype.hpp"

using namespace pmem::obj;
struct __pers_root {
    pmem_ptr<persistent<Stack>> stack;
};

__pers_root* __root = pmem_root<__pers_root>();

int main(int argc, char* argv[]) {
	
	bool crash_mode = (argc > 1 && std::string(argv[1]) == "--crash");
	persistent<Stack>* stack = pmem_get_or_create<persistent<Stack>>(__root->stack);
    
    std::cout << "Before :\n";
    stack->print();

    if(crash_mode) { 
        transaction::run(pmem_pool(), [&]{
            stack->push(10);
            stack->push(20);
            stack->push(30);
            std::cerr<< "Crashing now...\n";
            std::abort();
        });
    } else {
        transaction::run(pmem_pool(), [&]{
            for(int i = 1; i <= 3; i++){
                stack->push(i*10);
            }
        });
    }

    std::cout << "After :\n";
    stack->print();

    return 0;
}