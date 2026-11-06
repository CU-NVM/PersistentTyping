#ifndef PERSIST_TRANSFORMED
#define PERSIST_TRANSFORMED
#endif
#include "string_member_pers_root.hpp"
// string_member.cpp
//
// C string fields. A `char*` field is a pmem pointer to an array of
// persistent<char>, filled with the new[] + strcpy idiom in the constructor
// and replaced in rename() through a local char*; a `char tag[12]` field is
// built with strncpy + strcat. Both are read with strlen, strcmp and
// `std::cout <<`, and every write through a C function is logged first.

#include "persistenttype.hpp"
#include "harness.hpp"
#include <cstring>

class Widget {
public:
    char* name;
    char  tag[12];
    Widget(const char* n) {
        name = new char[std::strlen(n) + 1];
        std::strcpy(name, n);
        std::strncpy(tag, "tag-", sizeof(tag));
        std::strcat(tag, n);
        harness::crash_point_ctor();
    }
    void rename(const char* n) {
        char* fresh = new char[std::strlen(n) + 1];
        std::strcpy(fresh, n);
        delete[] name;
        name = fresh;
    }
    void print() const {
        std::cout << "STATE name=" << name << " len=" << std::strlen(name)
                  << " tag=" << tag << " is_beta=" << (std::strcmp(name, "beta") == 0) << "\n";
    }
};

template<>
class persistent<Widget> {
public:
    pmem_ptr<persistent<char>> name;
    persistent<char> tag[12];
    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(Widget)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(Widget)); }
    static void  operator delete(void* p)       { pmem_free(p); }
    static void  operator delete[](void* p)     { pmem_free(p); }
    persistent(const char * n) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        name = new persistent<char>[std::strlen(n) + 1];
        std::strcpy(persist_detail::writable(name, std::strlen(n) + 1), n);
        std::strncpy(persist_detail::writable(tag, (sizeof(tag))), "tag-", sizeof(tag));
        std::strcat(persist_detail::writable(tag, std::strlen(persist_detail::chars(tag)) + std::strlen(n) + 1), n);
        harness::crash_point_ctor();
    });
    }

    void rename(const char * n) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        persistent<char>* fresh = new persistent<char>[std::strlen(n) + 1];
        std::strcpy(persist_detail::writable(fresh, std::strlen(n) + 1), n);
        delete[] name;
        name = fresh;
    });
    }

    void print() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        std::cout << "STATE name=" << persist_detail::chars(name) << " len=" << std::strlen(persist_detail::chars(name))
                  << " tag=" << persist_detail::chars(tag) << " is_beta=" << (std::strcmp(persist_detail::chars(name), "beta") == 0) << "\n";
    });
    }

};


class Holder {
public:
    Widget* w = nullptr;
    int runs = 0;
    void bump() { runs++; }
    bool has() const { return w != nullptr; }
    void make() {
        w = new Widget("alpha");
        w->rename("beta");
        harness::abort_point();
        harness::crash_point_make();
    }
    void show() const { w->print(); }
};

template<>
class persistent<Holder> {
public:
    pmem_ptr<persistent<Widget>> w{nullptr};
    persistent<int> runs = 0;
    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(Holder)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(Holder)); }
    static void  operator delete(void* p)       { pmem_free(p); }
    static void  operator delete[](void* p)     { pmem_free(p); }
    void bump() {
        pmem::obj::transaction::run(pmem_pool(), [&]() { runs++; });
    }

    bool has() const {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool { return w != nullptr; }();
        });
        return __pers_ret;
    }

    void make() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
        w = new persistent<Widget>("alpha");
        w->rename("beta");
        harness::abort_point();
        harness::crash_point_make();
    });
    }

    void show() const {
        pmem::obj::transaction::run(pmem_pool(), [&]() { w->print(); });
    }

};


__pers_root* __pers_get_root() {
    static __pers_root* root = pmem_root<__pers_root>();
    return root;
}

int main(int argc, char** argv) {
    harness::init(argc, argv);
    pmem_get_or_create<persistent<Holder>>(__pers_get_root()->h);
    return harness::drive(__pers_get_root()->h.get());
}
