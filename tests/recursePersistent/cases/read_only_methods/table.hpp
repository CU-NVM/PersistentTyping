#pragma once
#include "persistenttype.hpp"
#include <cstring>
#include <cstdint>

inline uint64_t key_hash(uint64_t k) { return k * 2654435761u; }

class HashNode {
public:
    uint64_t key; char* value; HashNode* next;
    HashNode(uint64_t k, int n) : key(k), value(new char[n]), next(nullptr) { std::memset(value, (int)(k & 0xFF), n); }
};

class HashTable {
    HashNode** table; int bucket_count;
public:
    HashTable(int b) : table(new HashNode*[b]), bucket_count(b) { for (int i = 0; i < b; i++) table[i] = nullptr; }
    int hash(uint64_t key) { return key_hash(key) % bucket_count; }
    bool insert(uint64_t key, int payload) {
        int idx = hash(key);
        for (HashNode* c = table[idx]; c; c = c->next) if (c->key == key) return false;
        HashNode* n = new HashNode(key, payload); n->next = table[idx]; table[idx] = n; return true;
    }
    bool get(uint64_t key, int payload) {
        for (HashNode* c = table[hash(key)]; c; c = c->next)
            if (c->key == key) { volatile char sink = 0; for (int i = 0; i < payload; ++i) sink ^= c->value[i]; (void)sink; return true; }
        return false;
    }
    bool update(uint64_t key, int payload) {
        for (HashNode* c = table[hash(key)]; c; c = c->next)
            if (c->key == key) { for (int i = 0; i < payload; ++i) c->value[i] = (char)(c->value[i] + 1); return true; }
        return insert(key, payload);
    }
    int chain(HashNode* c) { return c ? 1 + chain(c->next) : 0; }
    int longest() const { int best = 0; for (int b = 0; b < bucket_count; b++) { int n = 0; for (HashNode* c = table[b]; c; c = c->next) n++; if (n > best) best = n; } return best; }
    void unlink_first(int b) const { if (table[b]) table[b] = table[b]->next; }
    void wipe(int b) { if (table[b]) std::memset(table[b]->value, 0, 4); }
    int histogram() { int counts[4] = {0, 0, 0, 0}; for (int b = 0; b < bucket_count; b++) counts[b % 4]++; return counts[0]; }
    void touch(uint64_t k) { update(k, 4); }
};
