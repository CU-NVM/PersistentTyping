#pragma once
namespace app {
    struct Node { int v; };
    namespace detail { struct Leaf { int v; }; }
}
namespace lib {
    inline namespace v1 { struct Item { int v; }; }
}
