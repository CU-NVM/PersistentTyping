// field_arrays.cpp
//
// Fixed-size array fields: a 2-D int array, an int array with an in-class
// initialiser, a char array, a double array, and an array of class pointers
// with one null slot. Each becomes an array of persistent<...> elements.

#include "persistenttype.hpp"
#include "harness.hpp"

class Node {
public:
    int v;
    Node(int v) : v(v) {}
};

class Grid {
public:
    int    cells[2][3];
    int    recent[4] = {1, 2, 3, 4};
    char   tag[5];
    double weights[2];
    Node*  slots[3];
    Grid(int base) {
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++) cells[r][c] = base + r * 10 + c;
        const char* s = "grid";
        for (int i = 0; i < 5; i++) tag[i] = s[i];
        weights[0] = 0.5;
        weights[1] = weights[0] * 3;
        for (int i = 0; i < 3; i++) slots[i] = (i == 1) ? nullptr : new Node(base + i);
        recent[2] += 100;
        harness::crash_point_ctor();
    }
    void print() const {
        std::cout << "STATE cells=";
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++) std::cout << cells[r][c] << " ";
        std::cout << "recent=";
        for (int i = 0; i < 4; i++) std::cout << recent[i] << " ";
        std::cout << "tag=";
        for (int i = 0; tag[i]; i++) std::cout << tag[i];
        std::cout << " weights=" << weights[0] << "," << weights[1] << " slots=";
        for (int i = 0; i < 3; i++) {
            if (slots[i]) std::cout << slots[i]->v << " ";
            else          std::cout << "null ";
        }
        std::cout << "\n";
    }
};

class Holder {
public:
    Grid* g = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return g != nullptr; }
    void make() {
        g = new Grid(7);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { g->print(); }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
