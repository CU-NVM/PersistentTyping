#include "../persistentLib/persistenttype.hpp"
#include <cstdlib>
#include <string>
#include <iostream>

using namespace pmem::obj;

struct Root {
   pmem_ptr<persistent<int>> counter;
};

int main(int argc, char* argv[]) {
    bool crash_mode = (argc > 1 && std::string(argv[1])=="--crash");
    Root* root = pmem_root<Root>();
    auto counter = pmem_get_or_create<persistent<int>>(root->counter, 0);
    std::cout << "before: "<<*counter << std::endl;
    transaction::run(pmem_pool(), [&]{
        *counter = *counter + 1;
        if (crash_mode) {
            std::cerr << "Crashing now...\n";
            std::abort();
        }
    });
    std::cout << "After: " << *counter << std::endl;
}