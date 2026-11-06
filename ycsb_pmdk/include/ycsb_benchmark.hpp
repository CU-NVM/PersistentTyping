#ifndef YCSB_BENCHMARK_HPP
#define YCSB_BENCHMARK_HPP

#include <string>
#include <vector>
#include "zipfian_generator.h"
#include <stdexcept>
using namespace ycsbc;
using namespace std;

/**
 *
 * @param workload_key (A, B, C, D, E, F)
 * @param duration
 * @param num_keys
 * @param theta
 * @param buckets
 * @param num_tables
 */

struct WorkloadConfig {
    int read_pct;
    int update_pct;
    int insert_pct;
    int scan_pct;
    int rmw_pct;
};

WorkloadConfig selectWorkload(const string &w);

// Select the key-placement hash: 0 = djb2 (default), 1 = mix. Call before prefill.
void set_hash_mode(int mode);

void global_init(int duration, int interval);

void global_fini();

void hash_table_init(int buckets, int num_tables, uint64_t num_keys, int payload_size);

void ycsb_test(
    int duration,
    const WorkloadConfig* cfg,
    ZipfianGenerator* gen,
    uint64_t num_keys,
    int interval,
    int num_tables,
    bool use_zipfian,
    int payload_size,
    int warmup
);

void run_ycsb_benchmark(
    const std::string& workload_key,
    int duration,
    uint64_t num_keys,
    double theta,
    int buckets,
    int num_tables
);

#endif
