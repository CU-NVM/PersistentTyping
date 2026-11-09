// ptr_array.cpp
//
// Hand-written stand-in for what persist-tool will generate from
//
//     struct HashNode  { int key; HashNode* next; };
//     struct HashTable { HashNode** table; int nbuckets; int size; ... };
//     persistent<HashTable>** ht = new persistent<HashTable>*[NTABLES];
//     ht[i] = new persistent<HashTable>(NBUCKETS);
//
// plus a root array of Tracked, which has a destructor, so `new[]` stores an
// array cookie in front of the elements. Compiled with PERSIST_TRANSFORMED.
//
// usage: ptr_array <mode>
//   run              get-or-create everything, insert one key into every table
//   init-root        only allocate the root slot array
//   crash:<point>    `run`, killed at <point>: root-alloc, ctor-bucket,
//                    insert-links, after-link
//   inspect          print the state, allocate nothing
//   offsets          print where each array's elements start in its allocation
//   cookie-make      allocate the Tracked array if absent
//   cookie-show      print the Tracked array
//   cookie-delete    delete[] the Tracked array
//   unlink-outside   null the Tracked root slot outside any transaction
//   alloc-outside    new persistent<int> outside any transaction
//   delete-outside   delete[] the Tracked array outside any transaction
//   delete-all       delete every node, bucket array, table and the root array

#include "persistenttype.hpp"
#include <csignal>
#include <cstring>
#include <iostream>

static const int NTABLES = 2;
static const int NBUCKETS = 4;
static const int NTRACKED = 3;

static const char* g_crash = nullptr;

// Simulated power cut: no destructors, no pmemobj_close, transaction left open.
static void crash_point(const char* name) {
    if (g_crash && !std::strcmp(g_crash, name)) {
        std::cout.flush();
        std::raise(SIGKILL);
    }
}

static int object_count() {
    int n = 0;
    PMEMoid oid;
    POBJ_FOREACH(global_pool, oid) n++;
    return n;
}

// Distance from the start of the pmem allocation holding `p` to `p`: 0 for
// an array without a cookie, sizeof(size_t) with one.
static long offset_in_allocation(const void* p) {
    PMEMoid oid;
    POBJ_FOREACH(global_pool, oid) {
        const char* start = static_cast<const char*>(pmemobj_direct(oid));
        size_t size = pmemobj_alloc_usable_size(oid);
        if (p >= start && static_cast<const char*>(p) < start + size)
            return static_cast<const char*>(p) - start;
    }
    return -1;
}

struct HashNode {
    int key;
    HashNode* next;
};

template<>
class persistent<HashNode> {
public:
    persistent<int> key;
    pmem_ptr<persistent<HashNode>> next;
    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(HashNode)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(HashNode)); }
    static void  operator delete(void* p)       { pmem_free(p); }
    static void  operator delete[](void* p)     { pmem_free(p); }
    persistent(int k) : key(k), next(nullptr) {}
};

struct HashTable {
    HashNode** table;
    int nbuckets;
    int size;
};

template<>
class persistent<HashTable> {
public:
    pmem_ptr<pmem_ptr<persistent<HashNode>>> table;
    persistent<int> nbuckets;
    persistent<int> size;
    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(HashTable)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(HashTable)); }
    static void  operator delete(void* p)       { pmem_free(p); }
    static void  operator delete[](void* p)     { pmem_free(p); }

    persistent(int n) : nbuckets(n), size(0) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            table = new pmem_ptr<persistent<HashNode>>[n];
            crash_point("ctor-bucket");
        });
    }

    // The statement text is what the user wrote for a HashNode** table.
    void insert(int k) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            persistent<HashNode>* nd = new persistent<HashNode>(k);
            nd->next = table[k % nbuckets];
            crash_point("insert-links");
            table[k % nbuckets] = nd;
            crash_point("after-link");
            size += 1;
        });
    }

    int walk() const {
        int n = 0;
        for (int b = 0; b < nbuckets; b++)
            for (persistent<HashNode>* cur = table[b]; cur; cur = cur->next) n++;
        return n;
    }

    void destroy() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            for (int b = 0; b < nbuckets; b++) {
                persistent<HashNode>* cur = table[b];
                while (cur) {
                    persistent<HashNode>* next = cur->next;
                    delete cur;
                    cur = next;
                }
            }
            delete[] table;
            table = nullptr;
        });
    }
};

struct Tracked {
    int v;
    ~Tracked() {}
};

template<>
class persistent<Tracked> {
public:
    persistent<int> v;
    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(Tracked)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(Tracked)); }
    static void  operator delete(void* p)       { pmem_free(p); }
    static void  operator delete[](void* p)     { pmem_free(p); }
    persistent() : v(0) {}
    ~persistent() {}
};

struct __pers_root {
    pmem_ptr<pmem_ptr<persistent<HashTable>>> ht;
    pmem_ptr<persistent<Tracked>> tracked;
};

static __pers_root* root() { return pmem_root<__pers_root>(); }

// What root-setup will emit for `ht = new persistent<HashTable>*[NTABLES]`.
static void get_or_create_root_array() {
    pmem::obj::transaction::run(pmem_pool(), [&]() {
        if (!root()->ht) {
            root()->ht = new pmem_ptr<persistent<HashTable>>[NTABLES];
            crash_point("root-alloc");
        }
    });
}

static void print_state() {
    std::cout << "ROOT " << (root()->ht ? 1 : 0) << "\n";
    if (root()->ht) {
        for (int i = 0; i < NTABLES; i++) {
            persistent<HashTable>* t = root()->ht[i];
            if (t) std::cout << "T" << i << " size " << t->size << " walk " << t->walk() << "\n";
            else   std::cout << "T" << i << " none\n";
        }
    }
    std::cout << "OBJS " << object_count() << "\n";
}

static void print_tracked() {
    persistent<Tracked>* a = root()->tracked;
    std::cout << "HAS " << (a ? 1 : 0) << "\n";
    if (!a) return;
    std::cout << "VALUES";
    for (int i = 0; i < NTRACKED; i++) std::cout << " " << a[i].v;
    std::cout << "\nOFFSET " << offset_in_allocation(a) << "\n";
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "run";
    if (mode.rfind("crash:", 0) == 0) {
        g_crash = argv[1] + 6;
        mode = "run";
        std::cout << "OBJS " << object_count() << "\n";
    }

    if (mode == "run") {
        get_or_create_root_array();
        for (int i = 0; i < NTABLES; i++) {
            persistent<HashTable>* t = pmem_get_or_create(root()->ht[i], NBUCKETS);
            t->insert(t->size * NBUCKETS + i);
        }
        if (g_crash) std::cout << "NOCRASH\n";
        print_state();
    } else if (mode == "init-root") {
        get_or_create_root_array();
        print_state();
    } else if (mode == "inspect") {
        print_state();
    } else if (mode == "offsets") {
        std::cout << "ROOTARRAY " << offset_in_allocation(root()->ht.get()) << "\n";
        std::cout << "BUCKETS " << offset_in_allocation(root()->ht[0]->table.get()) << "\n";
    } else if (mode == "cookie-make") {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            if (root()->tracked) return;
            persistent<Tracked>* a = new persistent<Tracked>[NTRACKED];
            for (int i = 0; i < NTRACKED; i++) a[i].v = 10 * (i + 1);
            root()->tracked = a;
        });
        print_tracked();
    } else if (mode == "cookie-show") {
        print_tracked();
    } else if (mode == "cookie-delete") {
        int before = object_count();
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            delete[] root()->tracked;
            root()->tracked = nullptr;
        });
        std::cout << "OBJS " << before << " " << object_count() << "\n";
        print_tracked();
    } else if (mode == "unlink-outside" || mode == "alloc-outside") {
        // Writes and allocations need the caller's transaction; outside one
        // they throw, and the pool is left as it was.
        try {
            if (mode == "unlink-outside") root()->tracked = nullptr;
            else new persistent<int>(1);
        } catch (const std::exception& e) {
            std::cout << "THREW " << e.what() << "\n";
        }
        print_tracked();
        std::cout << "OBJS " << object_count() << "\n";
    } else if (mode == "delete-outside") {
        // operator delete cannot throw, so the error stops the program.
        delete[] root()->tracked.get();
    } else if (mode == "delete-all") {
        int before = object_count();
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            for (int i = 0; i < NTABLES; i++) {
                persistent<HashTable>* t = root()->ht[i];
                if (!t) continue;
                t->destroy();
                delete t;
            }
            delete[] root()->ht;
            root()->ht = nullptr;
        });
        std::cout << "OBJS " << before << " " << object_count() << "\n";
    } else {
        std::cerr << "unknown mode " << mode << "\n";
        return 2;
    }
    return 0;
}
