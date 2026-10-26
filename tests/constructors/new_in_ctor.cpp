// new_in_ctor.cpp
//
// A constructor that allocates: one child in the initialiser list, two more
// in the body, linked through a pointer field. crash-ctor kills the process
// after all three allocations, so recovery must free all of them.

#include "persistenttype.hpp"
#include "harness.hpp"

class Node {
public:
    int   v;
    Node* next;
    Node(int v) : v(v), next(nullptr) {}
};

class Chain {
public:
    Node* head;
    int   len;
    Chain(int first) : head(new Node(first)), len(1) {
        head->next = new Node(first + 1);
        head->next->next = new Node(first + 2);
        len = 3;
        harness::crash_point_ctor();
    }
    void print() const {
        std::cout << "STATE len=" << len << " nodes=";
        Node* cur = head;
        while (cur) {
            std::cout << cur->v << " ";
            cur = cur->next;
        }
        std::cout << "\n";
    }
};

class Holder {
public:
    Chain* c = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return c != nullptr; }
    void make() {
        c = new Chain(100);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { c->print(); }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
