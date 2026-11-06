#include "main_pers_root.hpp"
#include "ycsb_benchmark.hpp"
#include "ycsbutils.h"
#include "HashTable.hpp"
#include "persistenttype.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <vector>
#include <random>
#include <string>
#include <stdexcept>
#include <functional>
#include <cstdlib>

using namespace std;
using namespace ycsbc;
using namespace std::chrono;

/* root 'ht' is __pers_get_root()->ht */

std::vector<int64_t> globalOps;

// Set once from main() before prefill (0 = djb2, 1 = mix).
void set_hash_mode(int mode) { hash_mode() = mode; }

void global_init(int duration, int interval) {
	globalOps.resize(duration/interval);
}

void hash_table_init(int buckets,
                     int num_tables,
                     uint64_t num_keys,
                     int payload_size)
{
    // ------------------ ALLOCATION ------------------
    pmem::obj::transaction::run(pmem_pool(), [&] { if (!__pers_get_root()->ht) __pers_get_root()->ht = new pmem_ptr<persistent<HashTable>>[num_tables]; });
    for(int i = 0; i < num_tables; i++) {
        pmem_get_or_create<persistent<HashTable>>(__pers_get_root()->ht[i], buckets);
    }
    // ------------------ SANITY CHECK ------------------
    for (int i = 0; i < num_tables; i++) {
        if (__pers_get_root()->ht[i].get() == nullptr) {
            std::cerr << "Hash table allocation error!" << std::endl;
            return;
        }
    }
    uint64_t num_even = num_keys / 2;
    // ------------------ PREFILL LOOP (every other key) ------------------
    // Prefill EXACTLY half the keyspace -- the even keys 0, 2, 4, ..., num_keys-2 --
    // so the odd half stays absent (inserts during the timed run hit fresh keys).
    for (uint64_t j = 0; j < num_even; j++) {
        uint64_t key_id = 2 * j;
        int table_index = key_hash(key_id) % num_tables;
        __pers_get_root()->ht[table_index].get()->insert(key_id, payload_size);
    }
}



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
)
{
	int64_t ops = 0;
	vector<int64_t> localOps;
	localOps.resize(duration/interval);
	// Warm up untimed for `warmup` seconds so caches reach steady state, THEN
	// reset the counter and measure -- keeps the cold-start transient out of the
	// reported throughput.
	auto startTimer  = std::chrono::steady_clock::now();
	auto warmupEnd   = startTimer + std::chrono::seconds(warmup);
	auto endTimer    = warmupEnd  + std::chrono::seconds(duration);
	auto nextLogTime = warmupEnd  + std::chrono::seconds(interval);
	int intervalIdx = 0;
	bool measuring = (warmup <= 0);
    mt19937 rng(random_device{}());
    uniform_int_distribution<int> op_dist(1, 100);
    uniform_int_distribution<uint64_t> key_dist(0, num_keys-1);
	// Amortize the clock: read steady_clock once per CLOCK_CHECK ops instead of every
	// op, so its overhead/jitter doesn't add noise to the measured throughput.
	const int64_t CLOCK_CHECK = 1024;              // power of two -> cheap (ops & (N-1))
	while (true) {
        // Key distribution selected via --mix (zipfian = YCSB hot-key skew, uniform = flat).
        // Raw zipfian rank used directly as the key (low ranks are hot / clustered).
        uint64_t key_id = use_zipfian ? gen->Next() : key_dist(rng);
        int ht_choice = key_hash(key_id) % num_tables;

        int op_choice = op_dist(rng);
        if (op_choice <= cfg->read_pct) {
            __pers_get_root()->ht[ht_choice].get()->get(key_id, payload_size);
        } else if (op_choice <= cfg->read_pct + cfg->update_pct) {
            __pers_get_root()->ht[ht_choice].get()->update(key_id, payload_size);
        } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct) {
            __pers_get_root()->ht[ht_choice].get()->insert(key_id, payload_size);
        } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct + cfg->scan_pct) {
            for (int j = 0; j < 10 && (key_id + j) < (uint64_t)num_keys; j++) {
                __pers_get_root()->ht[ht_choice].get()->get(key_id + j, payload_size);
            }
        } else {
            __pers_get_root()->ht[ht_choice].get()->get(key_id, payload_size);
            __pers_get_root()->ht[ht_choice].get()->update(key_id, payload_size);
        }
		ops++;
		if ((ops & (CLOCK_CHECK - 1)) == 0) {          // amortized clock read
			auto now = std::chrono::steady_clock::now();
			if (!measuring) {                          // untimed warmup phase
				if (now >= warmupEnd) { measuring = true; ops = 0; }   // -> start measuring
			} else {
				while (intervalIdx < (int)localOps.size() && now >= nextLogTime) {
					localOps[intervalIdx++] = ops;
					nextLogTime += std::chrono::seconds(interval);
				}
				if (now >= endTimer) break;
			}
		}
    }

	for(int i=0; i<localOps.size(); i++){
		globalOps[i] += localOps[i];
	}
}


// ============================================================================
// Workload parsing (used by run_ycsb_benchmark).
// ============================================================================
WorkloadConfig selectWorkload(const string &w) {
    WorkloadConfig workloadA = {50, 50, 0, 0, 0};
    WorkloadConfig workloadB = {95, 5, 0, 0, 0};
    WorkloadConfig workloadC = {100, 0, 0, 0, 0};
    WorkloadConfig workloadD = {95, 0, 5, 0, 0};
    WorkloadConfig workloadE = {0, 0, 5, 95, 0};
    WorkloadConfig workloadF = {50, 0, 0, 0, 50};
    if (w == "A") return workloadA;
    if (w == "B") return workloadB;
    if (w == "C") return workloadC;
    if (w == "D") return workloadD;
    if (w == "E") return workloadE;
    if (w == "F") return workloadF;
    throw runtime_error("Unknown workload " + w);
}
