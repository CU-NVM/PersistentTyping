#include "persistenttype.hpp"
#include <iostream>

struct HashNode {
    int key;
    HashNode* next;
    HashNode(int k) : key(k), next(nullptr) {}
};

struct HashTable {
    HashNode** table;
    int nbuckets;
    int size;
    HashTable(int n) : nbuckets(n), size(0) {
        table = new HashNode*[n];
        for (int i = 0; i < n; i++) table[i] = nullptr;
    }
    void insert(int k) {
        HashNode* nd = new HashNode(k);
        nd->next = table[k % nbuckets];
        table[k % nbuckets] = nd;
        size++;
    }
    int walk() const {
        int n = 0;
        for (int b = 0; b < nbuckets; b++)
            for (HashNode* cur = table[b]; cur; cur = cur->next) n++;
        return n;
    }
};

persistent<HashTable>** ht;

int main() {
    int n = 2;
    ht = new persistent<HashTable>*[n];
    for (int i = 0; i < n; i++) ht[i] = new persistent<HashTable>(4);
    for (int i = 0; i < n; i++) {
        ht[i]->insert(ht[i]->size * 4 + i);
        std::cout << "t" << i << " size " << ht[i]->size << " walk " << ht[i]->walk() << "\n";
    }
}
