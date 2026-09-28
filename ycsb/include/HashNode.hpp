#include <iostream>
#include "numatype.hpp"
#include <cstring>
#include <cstdint>

// ---- Key hashing (selectable at runtime via --hash) ----------------------
// hash_mode(): 0 = djb2 (original), 1 = mix (djb2 followed by a strong
// avalanche finalizer). Shared int (inline function-local static) set once
// before the worker/prefill threads start, then only read.
inline int& hash_mode() { static int m = 0; return m; }

inline unsigned long djb2_hash(const char* s) {
    unsigned long h = 5381;
    int c;
    while ((c = *s++)) h = ((h << 5) + h) + c;   // h * 33 + c
    return h;
}

// splitmix64 / murmur3-style finalizer: tiny input change -> unrelated output.
inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

// djb2 over the raw bytes of a 64-bit key: weak avalanche, so near-sequential
// keys stay near-sequential -- the "clustered" placement option.
inline unsigned long djb2_hash_u64(uint64_t k) {
    unsigned long h = 5381;
    for (int i = 0; i < 8; ++i) { h = ((h << 5) + h) + (unsigned char)(k & 0xFF); k >>= 8; }
    return h;
}

// The hash every table/bucket selector should use.
// hash_mode(): 0 = djb2 (weak / clustered), 1 = mix64 (strong avalanche).
inline uint64_t key_hash(uint64_t k) {
    return hash_mode() ? mix64(k) : (uint64_t)djb2_hash_u64(k);
}

class HashNode {
public:
    uint64_t key;
    char* value;          // payload buffer, `payload_size` bytes
    HashNode* next;

    HashNode(uint64_t k, int payload_size);
    ~HashNode();
};

HashNode::HashNode(uint64_t k, int payload_size) {
    key = k;
    next = nullptr;
    value = new char[payload_size];
    std::memset(value, (int)(k & 0xFF), payload_size);
}
HashNode::~HashNode() {
    delete[] value;
}
