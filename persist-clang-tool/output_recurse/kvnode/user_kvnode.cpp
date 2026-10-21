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

template<>
class persistent<Node> {
public:
    pmem_string key;
    persistent<int> count;
    pmem_ptr<persistent<Node>> next;
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Node)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    persistent(const char * word) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        count = 1;
        next  = nullptr;
        key = word;
    });
    }

    ~persistent() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
    });
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

template<>
class persistent<Bucket> {
public:
    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(Bucket)); }
    static void  operator delete(void* p)     { pmem_free(p); }
    void insert(const char * word) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        persistent<Node>* cur = head.get();
        while (cur) {
            if (strcmp(cur->key, word) == 0) { cur->count++; return; }
            cur = cur->next.get();
        }
        persistent<Node>* n = new persistent<Node>(word);
        n->next = head.get();
        head = n;
        size++;
    });
    }

    int getCount(const char * word) {
        int __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> int {
        persistent<Node>* cur = head.get();
        while (cur) {
            if (strcmp(cur->key, word) == 0) return cur->count;
            cur = cur->next.get();
        }
        return 0;
    }();
        });
        return __pers_ret;
    }

    void printAll() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        persistent<Node>* cur = head.get();
        std::cout << "bucket(" << size << "): ";
        while (cur) {
            std::cout << cur->key << "=" << cur->count << " ";
            cur = cur->next.get();
        }
        std::cout << "\n";
    });
    }

private:
    pmem_ptr<persistent<Node>> head{nullptr};
    persistent<int> size = 0;
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
