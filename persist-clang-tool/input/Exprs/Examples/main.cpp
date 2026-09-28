/*! \file main.cpp
 * \brief Test Suite for classic Data Structures.
 * \author Nii Mante
 * \date 10/28/2012
 *
 */

#include "TestSuite.hpp"
#include <thread>
#include <barrier>
#include <mutex>
#include <iostream>
#include <syncstream>
#include <string>
#include <vector>
// #include "numatype.hpp"
// #include "numathreads.hpp"
#include <cmath>
#include <getopt.h>
#include <chrono>
#include <iomanip>
#include <unordered_map>


#define NUMA_NODES 4
using namespace std;


std::string thread_config;
std::string DS_config;
int64_t num_DS = 0;
int num_threads = 0;
int duration = 0;
std::string DS_name;
bool prefill_set = false;
int crossover = 0;
int keyspace = 80000;
int run_freq = 1;
int interval =20;
struct prefill_percentage{
	float write;
	float read;
	float remove;
	float update;
};


extern int64_t ops0;
extern int64_t ops1;
extern std::vector<int64_t> globalOps0;
extern std::vector<int64_t> globalOps1;

std::vector <int64_t> num_ops1;
std::vector <int64_t> num_ops0;
std::vector <int64_t> total_ops;



std::vector <thread_numa<0>*> numa_thread0;
std::vector <thread_numa<1>*> numa_thread1;
std::vector <thread*> regular_thread0;
std::vector <thread*> regular_thread1;
thread_numa<0>* init_thread0;
thread_numa<1>* init_thread1;
std::thread* init_thread_regular0;
std::thread* init_thread_regular1;

void print_function(int duration, int64_t ops0, int64_t ops1, int64_t totalOps){
	auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);
    std::cout<<std::put_time(local_time, "%Y-%m-%d") << ", ";
	std::cout<<std::put_time(local_time, "%H:%M:%S") <<", ";
	std::cout<<DS_name << ", ";
	std::cout<<num_DS << ", ";
	std::cout<<num_threads << ", ";
	std::cout<<thread_config << ", ";
	std::cout<<DS_config << ", ";
	std::cout<<duration << ", ";
	std::cout<<keyspace<<", ";
	std::cout<<interval<<", ";
	std::cout<<ops0 << ", ";
	std::cout<<ops1 << ", ";
	std::cout<<totalOps << "\n";
}

bool parse_prefill(const std::string& optarg, prefill_percentage& percentages) {
    std::istringstream stream(optarg);
    std::string value;
    std::vector<float> values;

    // Split optarg by commas and convert to floats
    while (std::getline(stream, value, ',')) {
        try {
            values.push_back(std::stof(value));
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid percentage value: " << value << std::endl;
            return false;
        }
    }

    // Check if we have exactly 4 values
    if (values.size() != 4) {
        std::cerr << "Error: --prefill requires exactly 4 comma-separated values." << std::endl;
        return false;
    }

    // Assign values to struct fields
    percentages.write = values[0];
    percentages.read = values[1];
    percentages.remove = values[2];
    percentages.update = values[3];
    return true;
}


void main_BST_test(int duration, int64_t num_DS, int num_threads, int crossover, int keyspace){
	#ifdef PIN_INIT
			init_thread0 = new thread_numa<0>(numa_BST_init, DS_config, num_DS/2, keyspace, 0,crossover);
			init_thread1 = new thread_numa<1>(numa_BST_init, DS_config, num_DS/2, keyspace, 1,crossover);
			// std::cout<< "multi threaded initialization running" <<std::endl;
			// init_thread_regular0 = new thread(numa_BST_single_init, DS_config, num_DS/2, keyspace, 0, crossover);
			// init_thread_regular1 = new thread(numa_BST_single_init, DS_config, num_DS/2, keyspace, 1, crossover);
			if(init_thread0 != nullptr){
				init_thread0->join();
			}
			
			if(init_thread1 != nullptr){
				init_thread1->join();
			}
			// if(init_thread_regular0 != nullptr){
			// 	init_thread_regular0->join();
			// }

			// if(init_thread_regular1 != nullptr){
			// 	init_thread_regular1->join();
			// }
			
			// if(init_thread0 != nullptr){
			// 	init_thread0->join();
			// }
			
			// if(init_thread1 != nullptr){
			// 	init_thread1->join();
			// }
		#else
			// std::cout<< "single threaded initialization running" <<std::endl;
			numa_BST_single_init(DS_config, num_DS/2, keyspace, -1, crossover);
		#endif

		
		for(int i=0; i < num_threads/2; i++){
			int node = 0;
			int tid = i;
			if(thread_config == "numa"){
				numa_thread0[i] = new thread_numa<0>(BinarySearchTest,tid, duration, node, num_DS/2, num_threads/2, crossover, keyspace, interval);
			}
			else if(thread_config == "regular"){
				regular_thread0[i] = new thread(BinarySearchTest,tid, duration, node, num_DS/2, num_threads/2, crossover, keyspace, interval);
			}
			else{
				numa_thread0[i] = new thread_numa<0>(BinarySearchTest, tid, duration, 1, num_DS/2, num_threads/2, crossover, keyspace, interval);
			}
		}
		for(int i=0; i < num_threads/2; i++){
			int node = 1;
			int tid = i + num_threads/2;
			if(thread_config == "numa"){
				numa_thread1[i] = new thread_numa<1>(BinarySearchTest,tid, duration, node, num_DS/2, num_threads/2, crossover,keyspace, interval);
			}
			else if(thread_config == "regular"){
				regular_thread1[i] = new thread(BinarySearchTest,tid, duration, node, num_DS/2, num_threads/2, crossover,keyspace, interval);
			}
			else{
				numa_thread1[i] = new thread_numa<1>(BinarySearchTest, tid, duration, 0, num_DS/2, num_threads/2, crossover, keyspace, interval);
			}
		}

		if(thread_config == "numa"){
			for(int i=0; i < num_threads/2; i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < num_threads/2; i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}
		else if(thread_config == "regular"){
			for(int i=0; i < regular_thread0.size(); i++){
				if(regular_thread0[i] == nullptr){
					continue;
				}
				regular_thread0[i]->join();
			}
			for(int i=0; i < regular_thread1.size(); i++){
				if(regular_thread1[i] == nullptr){
					continue;
				}
				regular_thread1[i]->join();
			}
		}
		else{
			for(int i=0; i < numa_thread0.size(); i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < numa_thread1.size(); i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}

		num_ops0.push_back(ops0);
		num_ops1.push_back(ops1);
		total_ops.push_back(ops0 + ops1);
		
}



int main(int argc, char *argv[])
{
	prefill_percentage percentages = {0.0f, 0.0f, 0.0f, 0.0f};
	//std::cout<<"Date, Time, DS, num_DS, num_threads, thread_config, DS_config, duration, crossover, keyspace, ops0, ops1, total_ops\n";
	    // Define long options
	static struct option long_options[] = {
		{"th_config", required_argument, nullptr, 'c'},     // --th_config=NUMA/REGULAR
		{"DS_config", required_argument, nullptr, 'd'},     // --DS_config=NUMA/REGULAR
		{"DS_name", required_argument, nullptr, 's'},       // --DS_name=STACK/QUEUE
		{"num_DS", required_argument, nullptr, 'n'},        // -n
		{"num_threads", required_argument, nullptr, 't'},   // -t
		{"duration", required_argument, nullptr, 'D'},      // -d
		{"run_freq", optional_argument, nullptr, 'f'},       // -p
		{"crossover", optional_argument, nullptr, 'x'},       // -x
		{"keyspace", required_argument, nullptr, 'k'},      // -k
		{"interval", required_argument, nullptr, 'i'},      // -i
		{nullptr, 0, nullptr, 0}                            // End of array
	};

 int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "n:t:D:x:k:f:i:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c':  // --th_config option
                thread_config = optarg;             
                break;
            case 'd':  // --DS_config option
                DS_config = optarg;

                break;
            case 'n':  // -n option for num_DS
                num_DS = std::stoll(optarg);
                break;
            case 't':  // -t option for num_threads
                num_threads = std::stoll(optarg);
                break;
            case 'D':  // -D option for duration
                duration = std::stoi(optarg);
                break;
            case 's':  // --DS_name option
                DS_name = optarg;
                break;
			case 'f':
				if(optarg){
					run_freq = std::stoi(optarg);
				}
				break;
			case 'x':
				if(optarg){
					crossover = std::stoi(optarg);
				}
				break;
				//read values separated by a comma into prefill struct
			case 'i':
				if(optarg){
					interval = std::stoi(optarg);
				}
				break;
			case 'k':
				if(optarg){
					keyspace = std::stoi(optarg);
				}
				break;
            case '?':  // Unknown option
                std::cerr << "Unknown option or missing argument.\n";
                return 1;
            default:
                break;
        }
    }

	print_function(0, 0 ,0, 0);
    
	// std::cout<<endl;
	numa_thread0.resize(num_threads);
	numa_thread1.resize(num_threads);
	regular_thread0.resize(num_threads);
	regular_thread1.resize(num_threads);
	global_init(num_threads, duration, interval);
	
	// // #ifdef UMF
	// // 	warmUpPool();
	// // #endif
	// if(DS_config == "numa"){
	// 	singleThreadedStackInit(num_DS, true);
	// }
	// else {
	// 	singleThreadedStackInit(num_DS,false);
	// }

	// // Check if NUMA is available and the node is allowed
    // if (numa_available() == -1) {
    //     std::cout << "NUMA not available.\n";
    //     return 1;
    // }

    // // Pin the main thread to the specified NUMA node
    // if (numa_run_on_node(0) != 0) {
    //     std::cout<<"Could not run on node 0.\n";
    //     return 1;
    // }

	// singleThreadedStackTest(duration, num_DS);
	// std::cout<<"percentage write = "<< percentages.write <<std::endl;
	if(DS_name == "array"){
		numa_array_init(DS_config, num_DS/2, prefill_set, percentages);

		for(int i=0; i < num_threads/2; i++){
			int node = 0;
			int tid = i;
			if(thread_config == "numa"){
				numa_thread0[i] = new thread_numa<0>(ArrayTest, tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else if(thread_config == "regular"){
				regular_thread0[i] = new thread(ArrayTest, tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else{
				numa_thread0[i] = new thread_numa<0>(ArrayTest, tid, duration, i%2, num_DS/2, num_threads/2, crossover);
			}
		}

		for(int i=0; i < num_threads/2; i++){
			int node = 1;
			int tid = i + num_threads/2;
			if(thread_config == "numa"){
				numa_thread1[i] = new thread_numa<1>(ArrayTest, tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else if(thread_config == "regular"){
				regular_thread1[i] = new thread(ArrayTest, tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else{
				numa_thread1[i] = new thread_numa<1>(ArrayTest, tid, duration, i%2, num_DS/2, num_threads/2, crossover);
			}
		}

		if(thread_config == "numa"){
			for(int i=0; i < num_threads/2; i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < num_threads/2; i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}
		else if(thread_config == "regular"){
			for(int i=0; i < regular_thread0.size(); i++){
				if(regular_thread0[i] == nullptr){
					continue;
				}
				regular_thread0[i]->join();
			}
			for(int i=0; i < regular_thread1.size(); i++){
				if(regular_thread1[i] == nullptr){
					continue;
				}
				regular_thread1[i]->join();
			}
		}
		else{
			for(int i=0; i < numa_thread0.size(); i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < numa_thread1.size(); i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}

		for(int i :globalOps0){
			std::cout << i << ", ";
		}
		for(int i :globalOps1){
			std::cout << i << ", ";
		}
		std::cout << ops0 << ", ";
		std::cout << ops1 << ", ";
		std::cout << ops0 + ops1 << "\n";
	}
	else if(DS_name == "stack"){
		numa_Stack_init(DS_config, num_DS/2, prefill_set, percentages);

		for(int i=0; i < num_threads/2; i++){
			int node = 0;
			int tid = i;
			if(thread_config == "numa"){
				numa_thread0[i] = new thread_numa<0>(StackTest, tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else if(thread_config== "regular"){
				regular_thread0[i] = new thread(StackTest, tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else{
				numa_thread0[i] = new thread_numa<0>(StackTest, tid, duration, i%2, num_DS/2, num_threads/2, crossover);
			}
		}
		for(int i=0; i < num_threads/2; i++){
			int node = 1;
			int tid = i + num_threads/2;

			if(thread_config == "numa"){
				numa_thread1[i] = new thread_numa<1>(StackTest, tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else if(thread_config== "regular"){
				regular_thread1[i] = new thread(StackTest, tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else{
				numa_thread1[i] = new thread_numa<1>(StackTest, tid, duration, i%2, num_DS/2, num_threads/2, crossover);
			}
		}

		if(thread_config == "numa"){
			for(int i=0; i < num_threads/2; i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < num_threads/2; i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}
		else if(thread_config == "regular"){
			for(int i=0; i < regular_thread0.size(); i++){
				if(regular_thread0[i] == nullptr){
					continue;
				}
				regular_thread0[i]->join();
			}
			for(int i=0; i < regular_thread1.size(); i++){
				if(regular_thread1[i] == nullptr){
					continue;
				}
				regular_thread1[i]->join();
			}
		}
		else{
			for(int i=0; i < numa_thread0.size(); i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < numa_thread1.size(); i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}

		std::cout << ops0 << ", ";
		std::cout << ops1 << ", ";
		std::cout << ops0 + ops1 << "\n";
	}

	else if(DS_name == "queue"){
		numa_Queue_init(DS_config, num_DS/2, prefill_set, percentages);
		for(int i=0; i < num_threads/2; i++){
			int node = 0;
			int tid = i;
			if(thread_config == "numa"){
				numa_thread0[i] = new thread_numa<0>(QueueTest,tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else if(thread_config == "regular"){
				regular_thread0[i] = new thread(QueueTest,tid, duration, node, num_DS/2, num_threads/2, crossover);
			}
			else{
				numa_thread0[i] = new thread_numa<0>(QueueTest, tid, duration, i%2, num_DS/2, num_threads/2, crossover);
			}
		}
		for(int i=0; i < num_threads/2; i++){
			int node = 1;
			int tid = i + num_threads/2;
			if(thread_config == "numa"){
				numa_thread1[i] = new thread_numa<1>(QueueTest,tid , duration, node, num_DS/2, num_threads/2, crossover);
			}
			else if(thread_config == "regular"){
				regular_thread1[i] = new thread(QueueTest,tid , duration, node, num_DS/2, num_threads/2, crossover);
			}
			else{
				numa_thread1[i] = new thread_numa<1>(QueueTest, tid, duration, i%2, num_DS/2, num_threads/2, crossover);
			}
		}

		if(thread_config == "numa"){
			
			for(int i=0; i < numa_thread0.size(); i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < numa_thread1.size(); i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}

		else if(thread_config == "regular"){
			for(int i=0; i < regular_thread0.size(); i++){
				if(regular_thread0[i] == nullptr){
					continue;
				}
				regular_thread0[i]->join();
			}
			for(int i=0; i < regular_thread1.size(); i++){
				if(regular_thread1[i] == nullptr){
					continue;
				}
				regular_thread1[i]->join();
			}
		}
		else{
			for(int i=0; i < numa_thread0.size(); i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < numa_thread1.size(); i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}

		std::cout << ops0 <<", ";
		std::cout << ops1 << ", ";
		std::cout <<ops0 + ops1 << "";
	}



	else if(DS_name == "bst"){
		for(int i=0; i < run_freq; i++){
			main_BST_test(duration, num_DS, num_threads, crossover, keyspace);
		}
		int64_t sum0 = 0;
		int64_t sum1 = 0;
		int64_t total_sum = 0;
		//get the average of the numbers in the array
		for(int i = 0; i < num_ops0.size(); i++){
			sum0 += num_ops0[i];	
		}
		for(int i = 0; i < num_ops1.size(); i++){
			sum1 += num_ops1[i];
		}
		for(int i = 0; i < total_ops.size(); i++){
			total_sum += total_ops[i];
		}



		int newDuration = interval;
		// //std::cout<< "Ops0 per 20 seconds: ";
		// for(int i :globalOps0){
		// 	print_function(newDuration, i, 0, 0);
		// }
		// std::cout<<std::endl;
		// //std::cout<< "Ops1 per 20 seconds: ";
		// for(int i :globalOps1){
		// 	print_function(newDuration, 0, i, 0);
		
		// }

		for(int i = 0; i< globalOps0.size(); i++){
			print_function(newDuration, globalOps0[i], globalOps1[i], globalOps0[i] + globalOps1[i]);
			newDuration += interval;
		}
		// std::cout<<std::endl;
		// std::cout<<"Total Ops seconds: ";
		// std::cout << ops0 << ", ";
		// std::cout << ops1 << ", ";
		// std::cout << ops0+ops1<< "";
	
	}



	else if(DS_name == "ll"){
		numa_LL_init(DS_config, num_DS/2, prefill_set, percentages);
		for(int i=0; i < num_threads/2; i++){
			int node = 0;
			int tid = i;
			if(thread_config == "numa"){
				numa_thread0[i] = new thread_numa<0>(LinkedListTest,tid, duration, node, num_DS/2, num_threads, crossover);
			}
			else if(thread_config == "regular"){
				regular_thread0[i] = new thread(LinkedListTest,tid, duration, node, num_DS/2, num_threads, crossover);
			}
			else{
				numa_thread0[i] = new thread_numa<0>(LinkedListTest, tid, duration, i%2, num_DS/2, num_threads, crossover);
			}
		}
		for(int i=0; i < num_threads/2; i++){
			int node = 1;
			int tid = i + num_threads/2;
			if(thread_config == "numa"){
				numa_thread1[i] = new thread_numa<1>(LinkedListTest,tid, duration, node, num_DS/2, num_threads, crossover);
			}
			else if(thread_config == "regular"){
				regular_thread1[i] = new thread(LinkedListTest,tid, duration, node, num_DS/2, num_threads, crossover);
			}
			else{
				numa_thread1[i] = new thread_numa<1>(LinkedListTest, tid, duration, i%2, num_DS/2, num_threads, crossover);
			}
		}

		if(thread_config == "numa"){
			for(int i=0; i < numa_thread0.size(); i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < numa_thread1.size(); i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}


		else if(thread_config == "regular"){
			for(int i=0; i < regular_thread0.size(); i++){
				if(regular_thread0[i] == nullptr){
					continue;
				}
				regular_thread0[i]->join();
			}
			for(int i=0; i < regular_thread1.size(); i++){
				if(regular_thread1[i] == nullptr){
					continue;
				}
				regular_thread1[i]->join();
			}
		}
		else{
			for(int i=0; i < numa_thread0.size(); i++){
				if(numa_thread0[i] == nullptr){
					continue;
				}
				numa_thread0[i]->join();
			}
			for(int i=0; i < numa_thread1.size(); i++){
				if(numa_thread1[i] == nullptr){
					continue;
				}
				numa_thread1[i]->join();
			}
		}

		std::cout <<  ops0 << ", ";
		std::cout <<  ops1 << ", ";
		std::cout << ops0 + ops1 << "";
	}
	
	else{
		cout<<"Invalid Data Structure"<<endl;
	}
	global_cleanup();
	// cout<<endl;
}
