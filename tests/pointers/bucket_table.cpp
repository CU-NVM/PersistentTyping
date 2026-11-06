// bucket_table.cpp
//
// A hash table with a bucket array: a HashNode** field allocated with
// `new HashNode*[n]`, and nodes linked in with `nd->next = table[k]; table[k] = nd;`.
// The constructor inserts six keys, so crash-ctor kills the process after the
// bucket array and six nodes are allocated; recovery must free all of them.

#include "persistenttype.hpp"
#include "harness.hpp"

class HashNode {
public:
    int       key;
    HashNode* next;
    HashNode(int k) : key(k), next(nullptr) {}
};

class HashTable {
public:
    HashNode** table;
    int        nbuckets;
    int        size;
    HashTable(int n) : nbuckets(n), size(0) {
        table = new HashNode*[n];
        for (int i = 0; i < n; i++) table[i] = nullptr;
        for (int k = 0; k < 6; k++) insert(k * 3);
        harness::crash_point_ctor();
    }
    void insert(int k) {
        HashNode* nd = new HashNode(k);
        nd->next = table[k % nbuckets];
        table[k % nbuckets] = nd;
        size++;
    }
    bool contains(int k) const {
        for (HashNode* cur = table[k % nbuckets]; cur; cur = cur->next)
            if (cur->key == k) return true;
        return false;
    }
    void print() const {
        std::cout << "STATE size=" << size << " buckets=";
        for (int b = 0; b < nbuckets; b++) {
            std::cout << "[";
            for (HashNode* cur = table[b]; cur; cur = cur->next) std::cout << cur->key << " ";
            std::cout << "]";
        }
        std::cout << " has9=" << contains(9) << " has10=" << contains(10) << "\n";
    }
};

class Holder {
public:
    HashTable* t = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return t != nullptr; }
    void make() {
        t = new HashTable(4);
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { t->print(); }
};

int main(int argc, char** argv) {
    harness::init(argc, argv);
    persistent<Holder>* h = new persistent<Holder>();
    return harness::drive(h);
}
