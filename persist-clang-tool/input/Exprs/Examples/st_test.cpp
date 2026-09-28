#include <iostream>

#include <stdio.h>
#include <numa.h>
#include <chrono>

#ifdef UMF
    #include "umf_numa_allocator.hpp"
#endif
using namespace std;

int duration = 60;
int64_t umfOps = 0;
int64_t regularOps = 0;

void numa_test(){
    for(int i = 0; i < 100*1024*1024; i+=4096){
        void* ptr = numa_alloc_onnode(1 * sizeof(char), 0);
        if(ptr == nullptr){
            cout<<"allocation failed\n";
            throw std::bad_alloc();
        }
        ptr = (void*)'a';
        numa_free(ptr, 1 * sizeof(char));
    }
}

#ifdef UMF
    void umf_test(){
        auto start = std::chrono::high_resolution_clock::now();
        while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start).count() < duration){
            void* ptr = umf_alloc(0, 1 * sizeof(char), 1);
            umfOps++;
            if(ptr == nullptr){
                cout<<"allocation failed\n";
                throw std::bad_alloc();
            }
            *((char*)ptr) = 'a';
            umf_free(0, ptr);
            umfOps++;
        }
    }
#endif

void regular_test(){
    auto start = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start).count() < duration){          
        void* ptr = malloc(1 * sizeof(char));
        regularOps++;
        if(ptr == nullptr){
            cout<<"allocation failed\n";
            throw std::bad_alloc();
        }
        // ((char*)ptr)[i] = 'a';
        free(ptr);
        regularOps++;
    }
}

int main (int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <umf/regular>\n";
        return 1;
    }

    std::string argument = argv[1];
    if (argument == "umf") {
        std::cout << "Running numa allocation using umf.\n";
        #ifdef UMF
            umf_test();
        #endif
        std::cout << "umfOps: " << umfOps << "\n";

    } else if (argument == "regular") {
        std::cout << "Running regular allocation using malloc.\n";
        regular_test();
        std::cout << "regularOps: " << regularOps << "\n";
        
    } else {
        std::cout << "Unknown option: " << argument << "\n";
    }
}