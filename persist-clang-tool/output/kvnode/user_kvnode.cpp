// A HashNode-shaped benchmark: exercises char* -> pmem_string mapping plus the
// C-string write idioms (new char[] / strcpy / delete[]) that the tool must
// recognise. Deliberately written the way a user would write it, with no
// persistence-aware types in the field declarations.

#include "persistenttype.hpp"
#include <iostream>
#include <cstring>

class Node {
public:
    char* key;
    int   count;
    Node* next;

    Node(const char* word) {
        count = 1;
        next  = nullptr;
        key   = new char[strlen(word) + 1];
        strcpy(key, word);
    }
    ~Node() {
        delete[] key;
    }
};

class Bucket {
    Node* head = nullptr;
    int   size = 0;
public:
    void insert(const char* word) {
        Node* cur = head;
        while (cur) {
            if (strcmp(cur->key, word) == 0) { cur->count++; return; }
            cur = cur->next;
        }
        Node* n = new Node(word);
        n->next = head;
        head = n;
        size++;
    }

    int getCount(const char* word) {
        Node* cur = head;
        while (cur) {
            if (strcmp(cur->key, word) == 0) return cur->count;
            cur = cur->next;
        }
        return 0;
    }

    void printAll() {
        Node* cur = head;
        std::cout << "bucket(" << size << "): ";
        while (cur) {
            std::cout << cur->key << "=" << cur->count << " ";
            cur = cur->next;
        }
        std::cout << "\n";
    }
};

struct __pers_root {
    pmem_ptr<persistent<Bucket>> b;
};

__pers_root* __root = pmem_root<__pers_root>();

int main() {
    persistent<Bucket>* b = pmem_get_or_create<persistent<Bucket>>(__root->b);

    b->insert("alpha");
    b->insert("beta");
    b->insert("alpha");

    b->printAll();
    std::cout << "alpha count = " << b->getCount("alpha") << "\n";
    return 0;
}
