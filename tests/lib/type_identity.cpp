// type_identity.cpp
//
// Compile-only, with PERSIST_TRANSFORMED: the pmem_ptr spelling and the
// persistent<...*> spelling are one type, and a slot is exactly one PMEMoid
// with no destructor, so an array of slots has no array cookie.

#include "persistenttype.hpp"
#include <type_traits>

struct HashNode { int key; HashNode* next; };

template<>
class persistent<HashNode> {
public:
    persistent<int> key;
    pmem_ptr<persistent<HashNode>> next;
};

enum class Color { Red, Green };

static_assert(std::is_same_v<pmem_ptr<persistent<HashNode>>,
                             persistent<persistent<HashNode>*>>);
static_assert(std::is_same_v<pmem_ptr<pmem_ptr<persistent<HashNode>>>,
                             persistent<persistent<persistent<HashNode>*>*>>);
static_assert(std::is_same_v<pmem_ptr<persistent<char>>, persistent<persistent<char>*>>);

static_assert(sizeof(pmem_ptr<persistent<HashNode>>) == sizeof(PMEMoid));
static_assert(sizeof(pmem_ptr<pmem_ptr<persistent<HashNode>>>) == sizeof(PMEMoid));
static_assert(std::is_trivially_destructible_v<pmem_ptr<persistent<HashNode>>>);

static_assert(sizeof(persistent<int>) == sizeof(int));
static_assert(sizeof(persistent<Color>) == sizeof(Color));

// Reads convert to a raw pointer; that is what lets user statements compile
// unchanged.
static_assert(std::is_convertible_v<pmem_ptr<persistent<HashNode>>, persistent<HashNode>*>);
static_assert(std::is_convertible_v<pmem_ptr<pmem_ptr<persistent<HashNode>>>,
                                    pmem_ptr<persistent<HashNode>>*>);

int main() { return 0; }
