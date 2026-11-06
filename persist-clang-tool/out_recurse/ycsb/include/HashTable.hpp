#ifndef PERSIST_TRANSFORMED
#define PERSIST_TRANSFORMED
#endif
#include "persistenttype.hpp"

#include "HashNode.hpp"
#include <iostream>
#include <cstring>
#include <vector>


using namespace std;
class HashTable{
    HashNode** table;
    int bucket_count;

public:
    HashTable(int buckets);
    ~HashTable();
    int hash(uint64_t key);
    bool insert(uint64_t key, int payload_size);   // true if newly inserted
    bool get(uint64_t key, int payload_size);      // read + touch payload; true if found
    bool update(uint64_t key, int payload_size);   // in-place payload write; insert if absent
};

template<>
class persistent<HashTable> {
public:
    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(HashTable)); }
    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(HashTable)); }
    static void  operator delete(void* p)       { pmem_free(p); }
    static void  operator delete[](void* p)     { pmem_free(p); }
    persistent(int buckets) {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
    bucket_count = buckets;
    table = new pmem_ptr<persistent<HashNode>>[bucket_count];
    for(int i = 0; i < bucket_count; i++) {
        table[i] = nullptr;
    }
});
    }

    ~persistent() {
        pmem::obj::transaction::run(pmem_pool(), [&]() {
    for(int i = 0; i < bucket_count; i++) {
        persistent<HashNode>* curr = table[i];
        while(curr) {
            persistent<HashNode>* toDelete = curr;
            curr = curr->next;
            delete toDelete;
        }
    }
    delete[] table;
});
    }

    int hash(uint64_t key) {
        int __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> int {
    return key_hash(key) % bucket_count;   // djb2 or mix64, per --hash
}();
        });
        return __pers_ret;
    }

    bool insert(uint64_t key, int payload_size) {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool {
    int idx = hash(key);
    persistent<HashNode>* curr = table[idx];
    while(curr){
        if(curr->key == key) return false;
        curr = curr->next;
    }
    persistent<HashNode>* newNode = new persistent<HashNode>(key, payload_size);
    newNode->next = table[idx];
    table[idx] = newNode;
    return true;
}();
        });
        return __pers_ret;
    }

    bool get(uint64_t key, int payload_size) {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool {
    int idx = hash(key);
    persistent<HashNode>* curr = table[idx];
    while(curr){
        if(curr->key == key){
            volatile char sink = 0;
            for(int i = 0; i < payload_size; ++i) sink ^= curr->value[i];
            (void)sink;
            return true;
        }
        curr = curr->next;
    }
    return false;
}();
        });
        return __pers_ret;
    }

    bool update(uint64_t key, int payload_size) {
        bool __pers_ret = {};
        pmem::obj::transaction::run(pmem_pool(), [&]() {
            __pers_ret = [&]() -> bool {
    int idx = hash(key);
    persistent<HashNode>* curr = table[idx];
    while(curr){
        if(curr->key == key){
            unsigned char x = (unsigned char)(key & 0xFF);
            for (int i = 0; i < payload_size; ++i) {
                x ^= (unsigned char)curr->value[i];
                x = (unsigned char)((x << 1) | (x >> 7));   // rotate left by 1
                curr->value[i] = (char)x;
            }
            return true;
        }
        curr = curr->next;
    }
    return insert(key, payload_size);
}();
        });
        return __pers_ret;
    }

private:
    pmem_ptr<pmem_ptr<persistent<HashNode>>> table;
    persistent<int> bucket_count;
};



HashTable::HashTable(int buckets) {
    bucket_count = buckets;
    table = new HashNode*[bucket_count];
    for(int i = 0; i < bucket_count; i++) {
        table[i] = nullptr;
    }
}

HashTable::~HashTable() {
    for(int i = 0; i < bucket_count; i++) {
        HashNode* curr = table[i];
        while(curr) {
            HashNode* toDelete = curr;
            curr = curr->next;
            delete toDelete;
        }
    }
    delete[] table;
}

int HashTable::hash(uint64_t key) {
    return key_hash(key) % bucket_count;   // djb2 or mix64, per --hash
}

// Insert a key with a fresh payload. Returns false if the key already exists.
bool HashTable::insert(uint64_t key, int payload_size){
    int idx = hash(key);
    HashNode* curr = table[idx];
    while(curr){
        if(curr->key == key) return false;
        curr = curr->next;
    }
    HashNode* newNode = new HashNode(key, payload_size);
    newNode->next = table[idx];
    table[idx] = newNode;
    return true;
}

// Read: walk the chain, and on a hit TOUCH every payload byte so the read
// actually pulls the record's memory across the interconnect. Returns found.
bool HashTable::get(uint64_t key, int payload_size){
    int idx = hash(key);
    HashNode* curr = table[idx];
    while(curr){
        if(curr->key == key){
            volatile char sink = 0;
            for(int i = 0; i < payload_size; ++i) sink ^= curr->value[i];
            (void)sink;
            return true;
        }
        curr = curr->next;
    }
    return false;
}

// Update: mutate the payload IN PLACE (no reallocation) so writes don't lean on
// the slow allocator. If the key is absent, insert it.
bool HashTable::update(uint64_t key, int payload_size){
    int idx = hash(key);
    HashNode* curr = table[idx];
    while(curr){
        if(curr->key == key){
            unsigned char x = (unsigned char)(key & 0xFF);
            for (int i = 0; i < payload_size; ++i) {
                x ^= (unsigned char)curr->value[i];
                x = (unsigned char)((x << 1) | (x >> 7));   // rotate left by 1
                curr->value[i] = (char)x;
            }
            return true;
        }
        curr = curr->next;
    }
    return insert(key, payload_size);
}
