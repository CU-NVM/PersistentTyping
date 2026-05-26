/*! \file main.cpp
 * \brief Test Suite for classic Data Structures.
 * \author Nii Mante
 * \date 10/28/2012
 *
 */

#include "Stack.hpp"
#include <iostream>

#include "../../persistentLib/persistenttype.hpp"

using namespace pmem::obj;
int main(int argc, char* argv[]) {
	
	bool crash_mode = (argc > 1 && std::string(argv[1]) == "--crash");
	persistent<Stack>* stack = new persistent<Stack>();
    
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