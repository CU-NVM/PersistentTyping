#include "ycsb_benchmark.hpp"
#include "zipfian_generator.h"
#include <iostream>
#include <string>
#include <vector>
#include <getopt.h>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace std;
using namespace ycsbc;

int bucket_count = 1024;
string workload_key = "A";
uint64_t num_keys = 10000;
double theta = 0.99;
string key_mix = "uniform";   // --mix: "uniform" or "zipfian"
string hash_fn = "djb2";      // --hash: "djb2" or "mix"
string DS_config = "persistify";
int duration = 20;
int interval = 10;
int num_tables = 10;
int payload_size = 64;          // -p: bytes of per-record payload (char* value)
int warmup = 0;                 // -W: untimed warmup seconds before measuring

extern std::vector<int64_t> globalOps;

ZipfianGenerator* generator;

void print_function(int duration, int64_t totalOps) {
	auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);
    std::cout<<std::put_time(local_time, "%Y-%m-%d") << ", ";
	std::cout<<std::put_time(local_time, "%H:%M:%S") <<", ";
	std::cout<<num_tables << ", ";
	std::cout<<DS_config << ", ";
	std::cout<<key_mix << ", ";        // uniform | zipfian
    std::cout<<bucket_count<<", ";
    std::cout<<workload_key<<", ";
	std::cout<<duration << ", ";
	std::cout<<num_keys<<", ";
	std::cout<<interval<<", ";
	std::cout<<totalOps << "\n";
}

void compile_options(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"buckets",    required_argument, nullptr, 'b'},
        {"workload",   required_argument, nullptr, 'w'},
        {"duration",   required_argument, nullptr, 'u'},
        {"keys",       required_argument, nullptr, 'k'},
        {"theta",      required_argument, nullptr, 'z'},
        {"interval",   required_argument, nullptr, 'i'},
        {"tables",     required_argument, nullptr, 'a'},
        {"mix",        required_argument, nullptr, 'm'},
        {"hash",       required_argument, nullptr, 'H'},
        {"payload",    required_argument, nullptr, 'p'},
        {"warmup",     required_argument, nullptr, 'W'},
        {"help",       no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "b:w:u:k:z:i:a:m:H:p:W:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b': bucket_count = std::stoi(optarg); break;
            case 'w': workload_key = optarg; break;
            case 'u': duration = std::stoi(optarg); break;
            case 'k': num_keys = std::stoull(optarg); break;
            case 'z': theta = std::stod(optarg); break;
            case 'a': num_tables = std::stoi(optarg); break;
            case 'i': interval = std::stoi(optarg); break;
            case 'm':
                key_mix = optarg;
                if (key_mix != "uniform" && key_mix != "zipfian") {
                    cerr << "Invalid --mix value '" << key_mix << "' (expected 'uniform' or 'zipfian').\n";
                    exit(1);
                }
                break;
            case 'H':
                hash_fn = optarg;
                if (hash_fn != "djb2" && hash_fn != "mix") {
                    cerr << "Invalid --hash value '" << hash_fn << "' (expected 'djb2' or 'mix').\n";
                    exit(1);
                }
                break;
            case 'p': payload_size = std::stoi(optarg); break;
            case 'W': warmup = std::stoi(optarg); break;
            case 'h':
                cout << "Usage: ./runner [options]\n";
                cout << "Options:\n";
                cout << "  -b, --buckets <num>      Number of hash table buckets (default: 1024)\n";
                cout << "  -w, --workload <A-F>     YCSB workload type (default: A)\n";
                cout << "  -u, --duration <sec>     Measured run duration in seconds (default: 20)\n";
                cout << "  -k, --keys <num>         Number of keys (default: 10000)\n";
                cout << "  -z, --theta <float>      Zipfian theta (default: 0.99)\n";
                cout << "  -i, --interval <sec>     Reporting interval in seconds (default: 10)\n";
                cout << "  -a, --tables <num>       Number of hash tables (default: 10)\n";
                cout << "  -m, --mix <dist>         Key distribution: uniform or zipfian (default: uniform)\n";
                cout << "  -H, --hash <fn>          Key placement hash: djb2 or mix (default: djb2)\n";
                cout << "  -p, --payload <bytes>    Per-record payload size in bytes (default: 64)\n";
                cout << "  -W, --warmup <sec>       Untimed warmup seconds before measuring (default: 0)\n";
                exit(0);
            case '?':
                cerr << "Unknown option or missing argument.\n";
                exit(1);
            default: break;
        }
    }
}

// selectWorkload lives in ycsb_benchmark.cpp (declared in ycsb_benchmark.hpp).
void run_ycsb_benchmark(
    const string& workload_key,
    int duration,
    uint64_t num_keys,
    double theta,
    int buckets,
    int num_tables
)
{
    // Building a ZipfianGenerator runs an O(num_keys) zeta precompute; only pay
    // that when --mix=zipfian. For uniform, leave it nullptr (ycsb_test won't use it).
    if (key_mix == "zipfian")
        generator = new ZipfianGenerator(0, num_keys - 1, theta);
    else
        generator = nullptr;

    WorkloadConfig cfg = selectWorkload(workload_key);

    global_init(duration, interval);

//Initialization
    hash_table_init(buckets, num_tables, num_keys, payload_size);
//End Initialization

    bool use_zipfian = (key_mix == "zipfian");
    ycsb_test(duration, &cfg, generator, num_keys, interval, num_tables, use_zipfian, payload_size, warmup);
}

int main(int argc, char** argv) {

    compile_options(argc, argv);

    // Select the key-placement hash before prefill.
    set_hash_mode(hash_fn == "mix" ? 1 : 0);
    std::cerr << "[config] hash=" << hash_fn << " mix=" << key_mix << " payload=" << payload_size << "\n";

    print_function(0, 0); // Print header
    run_ycsb_benchmark(
        workload_key,
        duration,
        num_keys,
        theta,
        bucket_count,
        num_tables
    );

    delete generator;

	int newDuration = interval;
	for(int i = 0; i< globalOps.size(); i++){
		print_function(newDuration, globalOps[i]);
		newDuration += interval;
	}

    return 0;
}
