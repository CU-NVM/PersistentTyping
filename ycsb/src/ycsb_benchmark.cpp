#include "ycsb_benchmark.hpp"
#include "ycsbutils.h"
#include "HashTable.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <vector>
#include <random>
#include <string>
#include <thread>
#include <stdexcept>
#include <functional>
#include "numathreads.hpp"
#include <pthread.h>
#include <mutex>
#include <syncstream>
#include <cstdlib>

#include <map>
#include <atomic>

using namespace std;
using namespace ycsbc;
using namespace std::chrono;

#define NODE_ZERO 0
#ifndef MAX_NODE
    #warning "MAX_NODE not defined! Defaulting to 1."
    #define MAX_NODE 1
#endif

HashTable** ht_node0;
HashTable** ht_node1;
std::vector<std::mutex*> ht_node0_locks;
std::vector<std::mutex*> ht_node1_locks;

std::mutex* printLK;
std::mutex* globalLK;

std::vector<int64_t> globalOps0;
std::vector<int64_t> globalOps1;
pthread_barrier_t bar;
pthread_barrier_t init_bar;

// Set once from main() before any threads spawn (0 = djb2, 1 = mix).
void set_hash_mode(int mode) { hash_mode() = mode; }

void global_init(int num_threads, int duration, int interval) {
	pthread_barrier_init(&bar, NULL, num_threads);
	pthread_barrier_init(&init_bar, NULL, num_threads);
	globalOps0.resize(duration/interval);
	globalOps1.resize(duration/interval);
	printLK = new std::mutex();
	globalLK = new std::mutex();
}

void numa_hash_table_init(int thread_id,
                          int node,
                          std::string DS_config,
                          int buckets,
                          int num_tables,        // tables per node
                          uint64_t num_keys,
                          int num_total_threads,
                          int payload_size)
{
    int threads_per_node = num_total_threads / 2;
    // ------------------ GLOBAL ALLOCATION (ONCE) ------------------
    if (thread_id == 0) {
        if(DS_config == "numa") {
            //std::cout << "Thread " << thread_id << " initializing NUMA hash tables on Node " << NODE_ZERO << std::endl;
            ht_node0 = reinterpret_cast<HashTable**>( new numa<HashTable*, NODE_ZERO>[num_tables]);
            ht_node0_locks.resize(num_tables);
            for(int i = 0; i < num_tables; i++) {
                ht_node0[i] = reinterpret_cast<HashTable*>( new numa<HashTable, NODE_ZERO>(buckets));
                ht_node0_locks[i] = new std::mutex();
            }
            //std::cout << "Thread " << thread_id << " finished initializing NUMA hash tables on Node " << NODE_ZERO << std::endl;
        }
        else {
            ht_node0 = reinterpret_cast<HashTable**>( new HashTable*[num_tables]);
            ht_node0_locks.resize(num_tables);
            for(int i = 0; i < num_tables; i++) {
                ht_node0[i] = new HashTable(buckets);
                ht_node0_locks[i] = new std::mutex();
            }
        }
    }
    if (thread_id == threads_per_node && node == 1) {   // first node-1 thread allocates
        if(DS_config == "numa") {
            //std::cout << "Thread " << thread_id << " initializing NUMA hash tables on Node " << MAX_NODE<< std::endl;
            ht_node1 = reinterpret_cast<HashTable**>( new numa<HashTable*, MAX_NODE>[num_tables]);
            ht_node1_locks.resize(num_tables);
            for(int i = 0; i < num_tables; i++) {
                ht_node1[i] = reinterpret_cast<HashTable*>( new numa<HashTable, MAX_NODE>(buckets));
                ht_node1_locks[i] = new std::mutex();
            }
            //std::cout << "Thread " << thread_id << " finished initializing NUMA hash tables on Node " << MAX_NODE << std::endl;
        }
        else {  
            ht_node1 = reinterpret_cast<HashTable**>( new HashTable*[num_tables]);
            ht_node1_locks.resize(num_tables);
            for(int i = 0; i < num_tables; i++) {
                ht_node1[i] = new HashTable(buckets);
                ht_node1_locks[i] = new std::mutex();
            }
        }   
    }
    pthread_barrier_wait(&init_bar);
    // ------------------ SANITY CHECK ------------------
    if (thread_id == 0) {
        for (int i = 0; i < num_tables; i++) {
            if (ht_node0[i] == nullptr || ht_node1[i] == nullptr || ht_node0_locks[i] == nullptr || ht_node1_locks[i] == nullptr) {
                std::cerr << "Hash table allocation error!" << std::endl;
                return;
            }
        }
    }
    pthread_barrier_wait(&init_bar);
    // PREFILL ENABLED: removed the early `return;` so the prefill loop below runs.
    int tables_per_node = num_tables;
    int actual_total_tables = tables_per_node * 2;
    uint64_t num_even = num_keys / 2;
    // ------------------ PREFILL LOOP (every other key) ------------------
    // Prefill EXACTLY half the keyspace -- the even keys 0, 2, 4, ..., num_keys-2 --
    // so the odd half stays absent (inserts during the timed run hit fresh keys).
    //
    // Each thread takes a GLOBAL slice of the even keys (stride by thread_id over
    // ALL threads, so every even key is inserted exactly once) and inserts each into
    // its HOME table regardless of which node the inserting thread runs on. Under
    // regular DS the node + payload therefore first-touch on the *inserter's* node,
    // not the key's home node -> ~half the records are mis-placed; under numa DS
    // `numa<char,home>` still lands on the home node explicitly. That mis-placement
    // is exactly what lets numa/numa beat numa/regular: naive first-touch places
    // wrong, explicit NUMA typing places right.
    for (uint64_t j = (uint64_t)thread_id; j < num_even; j += num_total_threads) {
        uint64_t key_id = 2 * j;
        int table_index = key_hash(key_id) % actual_total_tables;

        if (table_index < tables_per_node) {                     // key's home is node 0
            ht_node0_locks[table_index]->lock();
            ht_node0[table_index]->insert(key_id, payload_size);
            ht_node0_locks[table_index]->unlock();
        } else {                                                 // key's home is node 1
            int local_idx = table_index - tables_per_node;
            ht_node1_locks[local_idx]->lock();
            ht_node1[local_idx]->insert(key_id, payload_size);
            ht_node1_locks[local_idx]->unlock();
        }
    }
    // all threads must finish prefilling before the timed workload starts
    pthread_barrier_wait(&init_bar);
}



void ycsb_test(
    int thread_id,
    int num_total_threads,
    int numa_node,
    int duration,
    const WorkloadConfig* cfg,
    ZipfianGenerator* gen,
    uint64_t num_keys,
    int local_pct,
    int interval,
    int num_tables,
    bool use_zipfian,
    int payload_size,
    int warmup
)
{

  #ifdef DEBUG
	if(tid == 1 && node==0)
	{	// startTime = chrono::high_resolution_clock::now();
		std::cout << "Only thread "<< tid << " will print this." << std::endl;
	}		
	#endif

	pthread_barrier_wait(&bar);

	int64_t ops = 0;
	thread_local vector<int64_t> localOps;
	localOps.resize(duration/interval);
	// Warm up untimed for `warmup` seconds so AutoNUMA / caches / THP reach steady
	// state, THEN reset the counter and measure -- keeps the cold-start transient
	// out of the reported throughput.
	auto startTimer  = std::chrono::steady_clock::now();
	auto warmupEnd   = startTimer + std::chrono::seconds(warmup);
	auto endTimer    = warmupEnd  + std::chrono::seconds(duration);
	auto nextLogTime = warmupEnd  + std::chrono::seconds(interval);
	int intervalIdx = 0;
	bool measuring = (warmup <= 0);
    mt19937 rng(random_device{}());
    uniform_int_distribution<int> op_dist(1, 100);
    uniform_int_distribution<int> locality_dist(1, 100);
    uniform_int_distribution<uint64_t> key_dist(0, num_keys-1);
	// Amortize the clock: read steady_clock once per CLOCK_CHECK ops instead of every
	// op, so its overhead/jitter doesn't add noise to the measured throughput.
	const int64_t CLOCK_CHECK = 1024;              // power of two -> cheap (ops & (N-1))
	while (true) {
        // Key distribution selected via --mix (zipfian = YCSB hot-key skew, uniform = flat).
        // Raw zipfian rank used directly as the key (low ranks are hot / clustered).
        uint64_t key_id = use_zipfian ? gen->Next() : key_dist(rng);
        int locality_choice = locality_dist(rng);
        int ht_choice = key_hash(key_id) % num_tables;

        if (numa_node == 0) {
            if (locality_choice <= local_pct) {
                int op_choice = op_dist(rng);
                if (op_choice <= cfg->read_pct) {
                    ht_node0_locks[ht_choice]->lock();
                    ht_node0[ht_choice]->get(key_id, payload_size);
                    ht_node0_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct) {
                    ht_node0_locks[ht_choice]->lock();
                    ht_node0[ht_choice]->update(key_id, payload_size);
                    ht_node0_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct) {
                    ht_node0_locks[ht_choice]->lock();
                    ht_node0[ht_choice]->insert(key_id, payload_size);
                    ht_node0_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct + cfg->scan_pct) {
                    for (int j = 0; j < 10 && (key_id + j) < (uint64_t)num_keys; j++) {
                        ht_node0_locks[ht_choice]->lock();
                        ht_node0[ht_choice]->get(key_id + j, payload_size);
                        ht_node0_locks[ht_choice]->unlock();
                    }
                } else {
                    ht_node0_locks[ht_choice]->lock();
                    ht_node0[ht_choice]->get(key_id, payload_size);
                    ht_node0[ht_choice]->update(key_id, payload_size);
                    ht_node0_locks[ht_choice]->unlock();
                }
            }
            else {
                int op_choice = op_dist(rng);
                if (op_choice <= cfg->read_pct) {
                    ht_node1_locks[ht_choice]->lock();
                    ht_node1[ht_choice]->get(key_id, payload_size);
                    ht_node1_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct) {
                    ht_node1_locks[ht_choice]->lock();
                    ht_node1[ht_choice]->update(key_id, payload_size);
                    ht_node1_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct) {
                    ht_node1_locks[ht_choice]->lock();
                    ht_node1[ht_choice]->insert(key_id, payload_size);
                    ht_node1_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct + cfg->scan_pct) {
                    for (int j = 0; j < 10 && (key_id + j) < (uint64_t)num_keys; j++) {
                        ht_node1_locks[ht_choice]->lock();
                        ht_node1[ht_choice]->get(key_id + j, payload_size);
                        ht_node1_locks[ht_choice]->unlock();
                    }
                } else {
                    ht_node1_locks[ht_choice]->lock();
                    ht_node1[ht_choice]->get(key_id, payload_size);
                    ht_node1[ht_choice]->update(key_id, payload_size);
                    ht_node1_locks[ht_choice]->unlock();
                }
            }
        }

        else if (numa_node == 1)
        {
            if (locality_choice <= local_pct) {
                int op_choice = op_dist(rng);
                if (op_choice <= cfg->read_pct) {
                    ht_node1_locks[ht_choice]->lock();
                    ht_node1[ht_choice]->get(key_id, payload_size);
                    ht_node1_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct) {
                    ht_node1_locks[ht_choice]->lock();
                    ht_node1[ht_choice]->update(key_id, payload_size);
                    ht_node1_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct) {
                    ht_node1_locks[ht_choice]->lock();
                    ht_node1[ht_choice]->insert(key_id, payload_size);
                    ht_node1_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct + cfg->scan_pct) {
                    for (int j = 0; j < 10 && (key_id + j) < (uint64_t)num_keys; j++) {
                        ht_node1_locks[ht_choice]->lock();
                        ht_node1[ht_choice]->get(key_id + j, payload_size);
                        ht_node1_locks[ht_choice]->unlock();
                    }
                } else {
                    ht_node1_locks[ht_choice]->lock();
                    ht_node1[ht_choice]->get(key_id, payload_size);
                    ht_node1[ht_choice]->update(key_id, payload_size);
                    ht_node1_locks[ht_choice]->unlock();
                }
            }
            else {
                int op_choice = op_dist(rng);
                if (op_choice <= cfg->read_pct) {
                    ht_node0_locks[ht_choice]->lock();
                    ht_node0[ht_choice]->get(key_id, payload_size);
                    ht_node0_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct) {
                    ht_node0_locks[ht_choice]->lock();
                    ht_node0[ht_choice]->update(key_id, payload_size);
                    ht_node0_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct) {
                    ht_node0_locks[ht_choice]->lock();
                    ht_node0[ht_choice]->insert(key_id, payload_size);
                    ht_node0_locks[ht_choice]->unlock();
                } else if (op_choice <= cfg->read_pct + cfg->update_pct + cfg->insert_pct + cfg->scan_pct) {
                    for (int j = 0; j < 10 && (key_id + j) < (uint64_t)num_keys; j++) {
                        ht_node0_locks[ht_choice]->lock();
                        ht_node0[ht_choice]->get(key_id + j, payload_size);
                        ht_node0_locks[ht_choice]->unlock();
                    }
                } else {
                    ht_node0_locks[ht_choice]->lock();
                    ht_node0[ht_choice]->get(key_id, payload_size);
                    ht_node0[ht_choice]->update(key_id, payload_size);
                    ht_node0_locks[ht_choice]->unlock();
                }
            }
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

    
     
	globalLK->lock();
	if(numa_node==0)
	{
		for(int i=0; i<localOps.size(); i++){
			globalOps0[i] += localOps[i];
		}
	}
	else
	{
		for(int i=0; i<localOps.size(); i++){
			globalOps1[i] += localOps[i];
		}
	}
	globalLK->unlock();

	pthread_barrier_wait(&bar);
    
}


// ============================================================================
// Workload parsing (moved out of main.cpp; used by run_ycsb_benchmark).
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

vector<MixedWorkloadConfig> parse_mixed_workload(const string& w_key, int num_threads) {
    vector<MixedWorkloadConfig> pool;
    int total_pct = 0;
    stringstream ss(w_key);
    string item;
    while (getline(ss, item, ',')) {                        // build a pool of per-thread tasks
        stringstream ss2(item);
        string w_type, local_s, remote_s, pct_s;
        getline(ss2, w_type, '-');
        getline(ss2, local_s, '-');
        getline(ss2, remote_s, '-');
        getline(ss2, pct_s, '-');
        int local_pct = stoi(local_s);
        int thread_pct = stoi(pct_s);
        total_pct += thread_pct;
        if (total_pct > 100) {
            cerr << "Error: Total thread percentage exceeds 100%\n";
            exit(1);
        }
        int threads_for_this = (num_threads * thread_pct) / 100;
        WorkloadConfig cfg = selectWorkload(w_type);
        for (int i = 0; i < threads_for_this; ++i) pool.push_back({cfg, local_pct});
    }
    while (pool.size() < (size_t)num_threads && !pool.empty()) pool.push_back(pool.back());

    vector<MixedWorkloadConfig> thread_tasks(num_threads);   // interleave across the two nodes
    int threads_per_node = num_threads / 2;
    for (int i = 0; i < threads_per_node; i++) {
        thread_tasks[i] = pool[i * 2];
        thread_tasks[i + threads_per_node] = pool[i * 2 + 1];
    }
    return thread_tasks;
}



