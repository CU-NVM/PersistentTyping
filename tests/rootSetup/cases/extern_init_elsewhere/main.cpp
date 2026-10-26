#include "counter.hpp"
#include "rt.hpp"

int main() {
    counter = new persistent<int>(0);
    bump(counter, "counter");
}
