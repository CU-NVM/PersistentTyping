// heap_arrays.cpp
//
// Pointer-to-fundamental fields holding heap arrays: `int* data` and
// `char* name`, filled with new[]. grow() allocates a bigger array into a
// local, copies, delete[]s the old one and stores the new one, so locals
// initialised from `new` and from a field read must both point into pmem.

#include "persistenttype.hpp"
#include "harness.hpp"

class Buffer {
public:
    int*  data;
    int   len;
    char* name;
    Buffer(int n) : len(n) {
        data = new int[n];
        for (int i = 0; i < n; i++) data[i] = i * i;
        name = new char[4];
        name[0] = 'b'; name[1] = 'u'; name[2] = 'f'; name[3] = '\0';
        harness::crash_point_ctor();
    }
    void grow() {
        int* bigger = new int[len * 2];
        for (int i = 0; i < len; i++) bigger[i] = data[i];
        for (int i = len; i < len * 2; i++) bigger[i] = -i;
        delete[] data;
        data = bigger;
        len = len * 2;
    }
    int sum() const {
        int s = 0;
        int* p = data;
        for (int i = 0; i < len; i++) s += p[i];
        return s;
    }
    void print() const {
        std::cout << "STATE len=" << len << " data=";
        for (int i = 0; i < len; i++) std::cout << data[i] << " ";
        std::cout << "sum=" << sum() << " name=";
        for (int i = 0; name[i]; i++) std::cout << name[i];
        std::cout << "\n";
    }
};

class Holder {
public:
    Buffer* b = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return b != nullptr; }
    void make() {
        b = new Buffer(3);
        b->grow();
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { b->print(); }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
