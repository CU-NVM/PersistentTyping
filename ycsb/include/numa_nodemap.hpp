#pragma once
#ifndef _NUMA_NODEMAP_HPP_
#define _NUMA_NODEMAP_HPP_

#include <numa.h>
#include <cstdlib>
#include <string>
#include <vector>

// Maps a logical NUMA partition id (0..N-1, as used by numa<T,k> and
// thread_numa<k>) to a physical, CPU-bearing node id -- resolved per machine at
// runtime, so the same binary runs anywhere.
//
// CPU-bearing nodes are ordered "outside-in" so consecutive logical partitions
// land on the farthest-apart physical nodes (the worst-case locality this
// project studies): CPU nodes [0..7] -> order [0,7,1,6,2,5,3,4].
// Logical k then maps to order[k % N]. (On a 2-CPU-node box the order is [0,1].)
//
// Override the order once per machine with NUMA_NODE_ORDER="0,7,1,6,...".

// Built once on first use; thread-safe via C++11 static initialization.
inline const std::vector<unsigned>& numa_node_order() {
    static const std::vector<unsigned> order = [] {
        std::vector<unsigned> nodes;

        // 1. Explicit override from the environment (e.g. set in machine.env).
        if (const char* env = std::getenv("NUMA_NODE_ORDER")) {
            std::string tok;
            for (char c : std::string(env) + ",") {
                if (c == ',') {
                    if (!tok.empty()) nodes.push_back((unsigned)std::strtoul(tok.c_str(), nullptr, 10));
                    tok.clear();
                } else tok += c;
            }
            if (!nodes.empty()) return nodes;
        }

        // 2. Otherwise auto-detect the CPU-bearing physical nodes (ascending).
        std::vector<unsigned> cpu;
        if (numa_available() == 0) {
            bitmask* cpus = numa_allocate_cpumask();
            for (int n = 0; n <= numa_max_node(); ++n)
                if (numa_node_to_cpus(n, cpus) == 0 && numa_bitmask_weight(cpus) > 0)
                    cpu.push_back((unsigned)n);
            numa_free_cpumask(cpus);
        }
        if (cpu.empty()) cpu.push_back(0);

        // 3. Reorder outside-in: [a,b,...,y,z] -> [a,z,b,y,...].
        for (int lo = 0, hi = (int)cpu.size() - 1; lo <= hi; ) {
            nodes.push_back(cpu[lo++]);
            if (lo <= hi) nodes.push_back(cpu[hi--]);
        }
        return nodes;
    }();
    return order;
}

inline unsigned numa_node_map(unsigned logical) {
    const auto& order = numa_node_order();
    return order[logical % order.size()];
}

inline unsigned numa_node_count() { return (unsigned)numa_node_order().size(); }

#endif // _NUMA_NODEMAP_HPP_
