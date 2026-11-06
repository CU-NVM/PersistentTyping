// nested_pointers.cpp
//
// A sorted list built with a pointer-to-pointer walk (`Node** pp = &head;
// pp = &(*pp)->next; *pp = n;`), a method returning a class pointer, and one
// taking class pointers as parameters. The local `Node**` stays a DRAM
// pointer, but to a pmem slot.

#include "persistenttype.hpp"
#include "harness.hpp"

class Node {
public:
    int   key;
    Node* next;
    Node(int k) : key(k), next(nullptr) {}
};

class SortedList {
public:
    Node* head;
    int   count;
    SortedList() : head(nullptr), count(0) {
        add(30);
        add(10);
        add(20);
        harness::crash_point_ctor();
    }
    void add(int k) {
        Node** pp = &head;
        while (*pp && (*pp)->key < k) pp = &(*pp)->next;
        Node* n = new Node(k);
        n->next = *pp;
        *pp = n;
        count++;
    }
    Node* find(int k) const {
        for (Node* c = head; c; c = c->next)
            if (c->key == k) return c;
        return nullptr;
    }
    void link_after(Node* where, Node* n) {
        n->next = where->next;
        where->next = n;
        count++;
    }
    void print() const {
        std::cout << "STATE count=" << count << " keys=";
        for (Node* c = head; c; c = c->next) std::cout << c->key << " ";
        std::cout << "has25=" << (find(25) != nullptr) << " has99=" << (find(99) != nullptr) << "\n";
    }
};

class Holder {
public:
    SortedList* l = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return l != nullptr; }
    void make() {
        l = new SortedList();
        l->link_after(l->find(20), new Node(25));
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { l->print(); }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
