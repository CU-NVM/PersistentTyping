#include <stdio.h>
#include <jemalloc/jemalloc.h>

int main() {
    size_t sz;
    unsigned narenas;

    sz = sizeof(unsigned);
    printf("sz: %d\n", sz);  
    mallctl("opt.narenas", &narenas, &sz, NULL, 0);
    printf("Number of arenas: %u\n", narenas);

    bool prof_enabled;
    sz = sizeof(prof_enabled);
    mallctl("opt.prof", &prof_enabled, &sz, NULL, 0);
    printf("Profiling enabled: %s\n", prof_enabled ? "true" : "false");

    return 0;
}