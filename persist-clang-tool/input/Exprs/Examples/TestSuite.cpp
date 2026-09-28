/*! \file TestSuite.cpp
 * \brief Testsuite implementation which allows for testing of various data structures
 * \author Nii Mante
 * \date 10/28/2012
 *
 */

#include "TestSuite.hpp"
#include "Node.hpp"
#include "Stack.hpp"
#include "Queue.hpp"
#include "BinarySearch.hpp"
#include "LinkedList.hpp"
// #include "numatype.hpp"
#include <random>
#include <iostream>
#include <thread>
// #include <barrier>
#include <mutex>
#include <syncstream>
#include <chrono>
#include <vector>
#include <cstdlib>
#include <pthread.h>
#include <map>
#include <atomic>
#include "umf_numa_allocator.hpp"

#define MEGABYTE 1048576


using namespace std::chrono;
std::vector<Stack*> Stacks0;
std::vector<Stack*> Stacks1;
// int64_t ops0=0;
// int64_t ops1=0;
int64_t ops0=0;
int64_t ops1=0;

int sharedCounter = 0;
char* Arrays0;
char* Arrays1;

std::vector<mutex*> Stack_lk0;
std::vector<mutex*> Stack_lk1;
pthread_barrier_t bar ;
pthread_barrier_t init_bar;

std::mutex* printLK;
std::mutex* globalLK;



std::vector<int64_t> globalOps0;
std::vector<int64_t> globalOps1;

std::vector<Queue*> Queues0;
std::vector<Queue*> Queues1;
std::vector<mutex*> Queue_lk0;
std::vector<mutex*> Queue_lk1;


std::vector<BinarySearchTree*> BSTs0;
std::vector<BinarySearchTree*> BSTs1;
std::vector<mutex*> BST_lk0;
std::vector<mutex*> BST_lk1;
std::vector<mutex*> BST_reader_lk0;
std::vector<mutex*> BST_reader_lk1;

std::vector<LinkedList*> LLs0;
std::vector<LinkedList*> LLs1;
std::vector<mutex*> LL_lk0;
std::vector<mutex*> LL_lk1;

mutex* Array_Lk0;
mutex* Array_Lk1;

struct prefill_percentage{
	float write;
	float read;
	float remove;
	float update;
};

// chrono::high_resolution_clock::time_point startTimer;
// chrono::high_resolution_clock::time_point endTimer;


int checkNUMANode(void* ptr) {
    int node;
    unsigned long nodemask;

    if (get_mempolicy(&node, &nodemask, sizeof(nodemask) * 8, ptr, MPOL_F_NODE) == 0) {
        // std::cout << "Pointer at " << ptr << " is allocated on NUMA Node " << node << std::endl;
		return node;
    } else {
        std::cerr << "Failed to get NUMA node for pointer at " << ptr << std::endl;
    }
}



void global_init(int num_threads, int duration, int interval){
	pthread_barrier_init(&bar, NULL, num_threads);
	pthread_barrier_init(&init_bar, NULL, 2);
	globalOps0.resize(duration/interval);
	globalOps1.resize(duration/interval);
	ops0 = 0;
	ops1 = 0;
	printLK = new std::mutex();
	globalLK = new std::mutex();
	Array_Lk0 = new mutex();
	Array_Lk1 = new mutex();
}

void singleThreadedStackInit(int num_DS, bool isNuma){
	Stacks0.resize(num_DS);
	if(isNuma){
		cout<<"Initializing numa stack pool"<<endl;
		for(int i = 0; i < num_DS; i++)
		{
			Stacks0[i] = reinterpret_cast<Stack*>(reinterpret_cast<Stack*>(new numa<Stack,0>()));
		}
	}
	else{
		cout<<"Initializing regular stack pool"<<endl;
		for(int i = 0; i < num_DS; i++)
		{
			Stacks0[i] = new Stack();
		}
	}
}


void numa_array_init(std::string DS_config, int size, bool prefill, prefill_percentage &percentages){
	if(DS_config=="numa"){	
		Arrays0 = (char*)umf_alloc(0, sizeof(char)* size, alignof(char));
	}
	else{
		Arrays0 = new char[size];
	}
	if(DS_config=="numa"){	
		Arrays1 = (char*)umf_alloc(1, sizeof(char)* size, alignof(char));
	}
	else{
		Arrays1 = new char[size];
	}
	if(prefill){
		for(int i = 0; i < size/int(percentages.write); i++){
			Arrays0[i] = 'a';
			Arrays1[i] = 'a';
		}
	}
}

void numa_Stack_init(std::string DS_config, int num_DS, bool prefill, prefill_percentage &percentages){
	Stacks0.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		if(DS_config=="numa"){
			//cout<<"Initializing node 0 numa stack pool"<<endl;
			Stacks0[i] = reinterpret_cast<Stack*>(reinterpret_cast<Stack*>(new numa<Stack,0>()));
		}
		else{
			//cout<<"Initializing first regular stack pool"<<endl;
			Stacks0[i] = new Stack();
		}
	}
	
	Stacks1.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		if(DS_config=="numa"){
			//cout<<"Initializing node 1 numa stack pool"<<endl;
			Stacks1[i] = reinterpret_cast<Stack*>(reinterpret_cast<Stack*>(new numa<Stack,1>()));
		}
		else{
			//cout<<"Initializing second regular stack pool"<<endl;
			Stacks1[i] = new Stack();
		}
	}

	Stack_lk0.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		Stack_lk0[i] = new mutex();
	}
	Stack_lk1.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		Stack_lk1[i] = new mutex();
	}

	if(prefill){
		std::mt19937 gen(123);
		std::uniform_int_distribution<> dist1(0, Stacks0.size()-1);
		std::uniform_int_distribution<> dist2(0, Stacks0.size()-1);
		std::uniform_int_distribution<> dist3(100, 200);
		//Prefill in 50 % of the Stacks with random number of nodes (0-200) number of nodes
		for(int i = 0; i < num_DS/int(percentages.write) ; i++)
		{
			int ds = dist1(gen);
			for(int j=0; j < 200*1024; j++)
			{
				Stack_lk0[ds]->lock();
				Stacks0[ds]->push(ds);
				Stack_lk0[ds]->unlock();
			}
		}
		for(int i = 0; i < num_DS/int(percentages.write) ; i++)
		{
			int ds = dist2(gen);
			for(int j=0; j < 200*1024; j++)
			{
				Stack_lk1[ds]->lock();
				Stacks1[ds]->push(ds);
				Stack_lk1[ds]->unlock();
			}
		}
	// 	std::cout<<"Prefilled " <<num_DS/int(percentages.write) <<" stacks with " << 200*1024 << " nodes each"<<std::endl;	
	}
}

void numa_Queue_init(std::string DS_config, int num_DS, bool prefill, prefill_percentage &percentages){
	Queues0.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		if(DS_config=="numa"){
			Queues0[i] = reinterpret_cast<Queue*>(reinterpret_cast<Queue*>(new numa<Queue,0>()));
		}
		else{
			Queues0[i] = new Queue();
		}
	}
	
	Queues1.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		if(DS_config=="numa"){
			Queues1[i] = reinterpret_cast<Queue*>(reinterpret_cast<Queue*>(new numa<Queue,1>()));
		}
		else{
			Queues1[i] = new Queue();
		}
	}

	Queue_lk0.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		Queue_lk0[i] = new mutex();
	}
	Queue_lk1.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		Queue_lk1[i] = new mutex();
	}

	if(prefill){
		std::mt19937 gen(123);
		std::uniform_int_distribution<> dist1(0, Queues0.size()-1);
		std::uniform_int_distribution<> dist2(0, Queues0.size()-1);
		std::uniform_int_distribution<> dist3(100, 200);
		int ds3 = dist3(gen);
		//Prefill in 50 % of the Queue with random number of nodes (0-200) number of nodes
		for(int i = 0; i < num_DS/int(percentages.write) ; i++){
			int ds = dist1(gen);
			for(int j=0; j < 40*1024*1024; j++)
			{
				Queues0[ds]->add(j);
			}
		}
		for(int i = 0; i < num_DS/int(percentages.write) ; i++){
			int ds = dist2(gen);
			for(int j=0; j < 40*1024*1024; j++)
			{
				Queues1[ds]->add(j);
			}
		}
		//std::cout<<"Prefilled " <<num_DS/int(percentages.write) <<" queue with " << ds3 << " nodes each"<<std::endl;		
	}
}

void numa_LL_init(std::string DS_config, int num_DS, bool prefill, prefill_percentage &percentages){
	LLs0.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		if(DS_config=="numa"){
			LLs0[i] = reinterpret_cast<LinkedList*>(reinterpret_cast<LinkedList*>(new numa<LinkedList,0>()));
		}
		else{
			LLs0[i] = new LinkedList();
		}
	}
	
	LLs1.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		if(DS_config=="numa"){
			LLs1[i] = reinterpret_cast<LinkedList*>(reinterpret_cast<LinkedList*>(new numa<LinkedList,1>()));
		}
		else{
			LLs1[i] = new LinkedList();
		}
	}

	LL_lk0.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		LL_lk0[i] = new mutex();
	}
	LL_lk1.resize(num_DS);
	for(int i = 0; i < num_DS; i++)
	{
		LL_lk1[i] = new mutex();
	}

	if(prefill){
		std::mt19937 gen(123);
		std::uniform_int_distribution<> dist1(0, LLs0.size()-1);
		std::uniform_int_distribution<> dist2(0, LLs1.size()-1);
		std::uniform_int_distribution<> dist3(100, 200);
		int ds3 = dist3(gen);
		//Prefill in 50 % of the Stacks with random number of nodes (0-200) number of nodes
		for(int i = 0; i < num_DS/int(percentages.write) ; i++)
		{
			int ds = dist1(gen);
			for(int j=0; j < dist3(gen); j++)
			{
				LL_lk0[ds]->lock();
				LLs0[ds]->append(ds);
				LL_lk0[ds]->unlock();
			}
		}	
		for(int i = 0; i < num_DS/int(percentages.write) ; i++)
		{
			int ds = dist2(gen);
			for(int j=0; j < dist3(gen); j++)
			{
				LL_lk1[ds]->lock();
				LLs1[ds]->append(ds);
				LL_lk1[ds]->unlock();
			}
		}
		std::cout<<"Prefilled " <<num_DS/int(percentages.write) <<" ll with " << dist3(gen) << " nodes each"<<std::endl;	
	}
}


void numa_BST_single_init(std::string DS_config, int num_DS, int keyspace, int node, int crossover){
	BSTs0.resize(num_DS);
	BSTs1.resize(num_DS);
	BST_lk0.resize(num_DS);
	BST_lk1.resize(num_DS);

	std::mt19937 gen(123);
	std::uniform_int_distribution<> xDist(1, 100);
	std::uniform_int_distribution<> dist(0, keyspace/2);
	for(int i = 0; i < num_DS; i++)
	{
		int x = xDist(gen);
		
		if(DS_config=="numa"){
			BSTs0[i] = reinterpret_cast<BinarySearchTree*>(reinterpret_cast<BinarySearchTree*>(new numa<BinarySearchTree,0>()));
			BSTs1[i] = reinterpret_cast<BinarySearchTree*>(reinterpret_cast<BinarySearchTree*>(new numa<BinarySearchTree,1>()));
		}
		else{
			BSTs0[i] = new BinarySearchTree();
			BSTs1[i] = new BinarySearchTree();
		}
	}

	
	for(int i = 0; i < num_DS; i++)
	{
		BST_lk0[i] = new mutex();
		BST_lk1[i] = new mutex();
	}	

	
	for(int i = 0; i < num_DS/2 ; i++)
	{
		for(int j=0; j < keyspace/2; j++)
		{
			BSTs0[i]->insert(dist(gen));
			BSTs1[i]->insert(dist(gen));
		}
	}	

}

void numa_BST_init(std::string DS_config, int num_DS, int keyspace, int node, int crossover){
	pthread_barrier_wait(&init_bar);
	crossover = -1;
	//std::cout<<"crossover value from here is "<<crossover<<std::endl;
	if(node==0 ){
	
		BSTs0.resize(num_DS);
		BSTs1.resize(num_DS);
		BST_lk0.resize(num_DS);
		BST_lk1.resize(num_DS);
		BST_reader_lk0.resize(num_DS);
		BST_reader_lk1.resize(num_DS);
	}
	pthread_barrier_wait(&init_bar);
	std::mt19937 gen(123);
	std::uniform_int_distribution<> xDist(1, 100);
	std::uniform_int_distribution<> dist(0, keyspace);
	
	if(node == 0){
		for(int i = 0; i < num_DS; i++)
		{
			int x = xDist(gen);
			if(x <= crossover){
				if(DS_config=="numa"){
					BSTs1[i] = reinterpret_cast<BinarySearchTree*>(reinterpret_cast<BinarySearchTree*>(new numa<BinarySearchTree,1>()));
				}
				else{
					BSTs1[i] = new BinarySearchTree();
				}
			}else{
				if(DS_config=="numa"){
					BSTs0[i] =reinterpret_cast<BinarySearchTree*>(reinterpret_cast<BinarySearchTree*>(new numa<BinarySearchTree,0>()));
				}
				else{
					BSTs0[i] = new BinarySearchTree();
				}
			}
		}

		
		for(int i = 0; i < num_DS; i++)
		{
			int x = xDist(gen);
			if(x<=crossover){
				BST_lk1[i] = new mutex();
				BST_reader_lk1[i] = new mutex();
			}else{
				BST_lk0[i] = new mutex();
				BST_reader_lk0[i] = new mutex();
			}
		}

		for(int i = 0; i < keyspace/2 ; i++)
		{	
			int x = xDist(gen);
			if(x <= crossover){
				for(int j=0; j < num_DS; j++){
					BSTs1[j]->insert(dist(gen));
				}
			}else{
				for(int j=0; j < num_DS; j++){
					BSTs0[j]->insert(dist(gen));
				}
			}
		}
	}

	if(node == 1){
		
		for(int i = 0; i < num_DS; i++)
		{
			int x = xDist(gen);
			if(x <= crossover){
				if(DS_config=="numa"){
					BSTs0[i] = reinterpret_cast<BinarySearchTree*>(reinterpret_cast<BinarySearchTree*>(new numa<BinarySearchTree,0>()));
				}
				else{
					BSTs0[i] = new BinarySearchTree();
				}
			}else{
				if(DS_config=="numa"){
					BSTs1[i] = reinterpret_cast<BinarySearchTree*>(reinterpret_cast<BinarySearchTree*>(new numa<BinarySearchTree,1>()));
				}
				else{
					BSTs1[i] = new BinarySearchTree();
				}
			}
		}

		for(int i = 0; i < num_DS; i++)
		{	
			int x = xDist(gen);
			if(x<=crossover){
				BST_lk0[i] = new mutex();
			}else{
				BST_lk1[i] = new mutex();
			}
		}	
		
		for(int i = 0; i < keyspace/2 ; i++)
		{
			int x = xDist(gen);
			if(x <= crossover){
				for(int j=0; j < num_DS; j++){
					BSTs0[j]->insert(dist(gen));
				}
			}else{
				for(int j=0; j < num_DS; j++){
					BSTs1[j]->insert(dist(gen));
				}
			}
		}
	}

	pthread_barrier_wait(&init_bar);
	
	//std::cout<<"BSTs initialized"<<std::endl<<std::endl<<std::endl<<std::endl;

}

void singleThreadedStackTest(int duration, int64_t num_DS){
	std::mt19937 gen(123);
	std::uniform_int_distribution<> dist(0, Stacks0.size()-1);
	//std::cout << "Thread " << tid << " about to start working on node id"<<node << std::endl;
	int ops = 0;
	auto startTimer = std::chrono::steady_clock::now();
	auto endTimer = startTimer + std::chrono::seconds(duration);
	while (std::chrono::steady_clock::now() < endTimer) {
		int ds = dist(gen);
		int op = dist(gen)%2;
		
			if(op == 0){
				Stacks0[ds]->push(1);
				ops++;
			}
			else{
				int val= Stacks0[ds]->pop();
				ops++;
			}
	}

	std::cout << "OPS FOR SINGLE THREAD IS: " << ops << std::endl;
}



void ArrayTest(int tid,  int duration, int node, int64_t num_DS, int num_threads, int crossover){

	int ops = 0;
	auto startTimer = std::chrono::steady_clock::now();
	auto endTimer = startTimer + std::chrono::seconds(duration);
	while (std::chrono::steady_clock::now() < endTimer) {
		int x = 0;
		if(node==0){
			Array_Lk0->lock();
			Arrays0[x] = 1;
			Array_Lk0->unlock();
			
		}
		else{
			Array_Lk1->lock();
			Arrays1[x] = 1;
			Array_Lk1->unlock();
		}
		ops++;
	}

	globalLK->lock();
	if(node==0)
	{
		ops0 += ops;
	}
	else
	{
		ops1 += ops;
	}
	globalLK->unlock();

	pthread_barrier_wait(&bar);
}


void StackTest(int tid,  int duration, int node, int64_t num_DS, int num_threads, int crossover)
{	
	#ifdef DEBUG
	if(tid == 1 && node==0)
	{	// startTime = chrono::high_resolution_clock::now();
		std::cout << "Only thread "<< tid << " will print this." << std::endl;

	}		
	#endif

	// globalLK->lock();
	// bool tcache;
	// size_t sz = sizeof(bool);
	// mallctl("thread.tcache.enabled", &tcache, &sz, NULL, 0);
	// printf("Thread cache enabled: %s\n", tcache ? "yes" : "no");
	// globalLK->unlock();
	
	pthread_barrier_wait(&bar);
    std::mt19937 gen(123);
    std::uniform_int_distribution<> dist(0, Stacks0.size()-1);
	std::uniform_int_distribution<> xDist(1, 100);
	
	//std::cout << "Thread " << tid << " about to start working on node id"<<node << std::endl;
	int64_t ops = 0;
	auto startTimer = std::chrono::steady_clock::now();
	auto endTimer = startTimer + std::chrono::seconds(duration);
	while (std::chrono::steady_clock::now() < endTimer) {
		int ds = dist(gen);
		int op = dist(gen)%2;
		int x = xDist(gen);
		if(node==0){
			if(op == 0)
			{
				if(x < crossover){
					Stack_lk1[ds]->lock();
					Stacks1[ds]->push(ds);
					Stack_lk1[ds]->unlock();
				}else{
					Stack_lk0[ds]->lock();
					Stacks0[ds]->push(ds);
					Stack_lk0[ds]->unlock();
				}
			}
			else
			{
				if(x < crossover){
					Stack_lk1[ds]->lock();
					int val= Stacks1[ds]->pop();
					Stack_lk1[ds]->unlock();
				}
				else{
					Stack_lk0[ds]->lock();
					int val= Stacks0[ds]->pop();
					Stack_lk0[ds]->unlock();
				}
			}
		}
		else{
			if(op == 0)
			{
				if(x < crossover){
					Stack_lk0[ds]->lock();
					Stacks0[ds]->push(ds);
					Stack_lk0[ds]->unlock();
				}
				else{
					Stack_lk1[ds]->lock();
					Stacks1[ds]->push(ds);
					Stack_lk1[ds]->unlock();
				}
			}
			else
			{
				if(x < crossover){
					Stack_lk0[ds]->lock();
					int val= Stacks0[ds]->pop();
					Stack_lk0[ds]->unlock();
				}
				else{
					Stack_lk1[ds]->lock();
					int val= Stacks1[ds]->pop();
					Stack_lk1[ds]->unlock();
				}
			}
		}
		ops++;
	}


	globalLK->lock();
	if(node==0)
	{
		ops0 += ops;
	}
	else
	{
		ops1 += ops;
	}
	globalLK->unlock();

	pthread_barrier_wait(&bar);
}

void QueueTest(int tid, int duration, int node, int64_t num_DS, int num_threads, int crossover)
{	
	#ifdef DEBUG
	if(tid == 1 && node==0)
	{	// startTime = chrono::high_resolution_clock::now();
		std::cout << "Only thread "<< tid << " will print this." << std::endl;
	}		
	#endif
	std::mt19937 gen(123);
	std::uniform_int_distribution<> dist1(0, Queues0.size()-1);
	std::uniform_int_distribution<> xDist(1, 100);
	int ops = 0;
	auto startTimer = std::chrono::steady_clock::now();
	auto endTimer = startTimer + std::chrono::seconds(duration);
	while (std::chrono::steady_clock::now() < endTimer) {
		int ds = dist1(gen);
		int op = dist1(gen)%2;
		int x = xDist(gen);

		if(node==0){
			if(op == 0)
			{
				if(x < crossover){
					Queue_lk1[ds]->lock();
					Queues1[ds]->add(ds);
					Queue_lk1[ds]->unlock();
				}else{
					Queue_lk0[ds]->lock();
					Queues0[ds]->add(ds);
					Queue_lk0[ds]->unlock();
				}
			}
			else
			{
				if(x < crossover){
					Queue_lk1[ds]->lock();
					int val= Queues1[ds]->del();
					Queue_lk1[ds]->unlock();
				}
				else{
					Queue_lk0[ds]->lock();
					int val= Queues0[ds]->del();
					Queue_lk0[ds]->unlock();
				}
			}
		}
		else{
			if(op == 0)
			{
				if(x < crossover){
					Queue_lk0[ds]->lock();
					Queues0[ds]->add(ds);
					Queue_lk0[ds]->unlock();
				}
				else{
					Queue_lk1[ds]->lock();
					Queues1[ds]->add(ds);
					Queue_lk1[ds]->unlock();
				}
			}
			else
			{
				if(x < crossover){
					Queue_lk0[ds]->lock();
					int val= Queues0[ds]->del();
					Queue_lk0[ds]->unlock();
				}
				else{
					Queue_lk1[ds]->lock();
					int val= Queues1[ds]->del();
					Queue_lk1[ds]->unlock();
				}
			}
		}
		ops++;
	}

	globalLK->lock();
	if(node==0)
	{
		ops0 += ops;
	}
	else
	{
		ops1 += ops;
	}
	globalLK->unlock();

	pthread_barrier_wait(&bar);
}


void LinkedListTest(int tid, int duration, int node, int64_t num_DS, int num_threads, int crossover)
{	
	#ifdef DEBUG
	if(tid == 1 && node==0)
	{	// startTime = chrono::high_resolution_clock::now();
		std::cout << "Only thread "<< tid << " will print this." << std::endl;
	}		
	#endif

	pthread_barrier_wait(&bar);
	std::mt19937 gen(123);
	std::uniform_int_distribution<> dist(0, LLs0.size()-1);
	std::uniform_int_distribution<> opDist(1, 100);
	std::uniform_int_distribution<> xDist(1, 100);
	//std::cout << "Thread " << tid << " about to start working on node id"<<node << std::endl;
	int ops = 0;
	auto startTimer = std::chrono::steady_clock::now();
	auto endTimer = startTimer + std::chrono::seconds(duration);
	while (std::chrono::steady_clock::now() < endTimer) {
		int ds = dist(gen);
		int op = dist(gen)%2;
		int x = xDist(gen);
		if(node==0){
			if(op == 0)
			{
				if(x < crossover){
					LL_lk1[ds]->lock();
					LLs1[ds]->append(ds);
					LL_lk1[ds]->unlock();
				}else{
					LL_lk0[ds]->lock();
					LLs0[ds]->append(ds);
					LL_lk0[ds]->unlock();
				}
			}
			else
			{
				if(x < crossover){
					LL_lk1[ds]->lock();
					LLs1[ds]->removeHead();
					LL_lk1[ds]->unlock();
				}
				else{
					LL_lk0[ds]->lock();
					LLs0[ds]->removeHead();
					LL_lk0[ds]->unlock();
				}
			}
		}
		else{
			if(op == 0)
			{
				if(x < crossover){
					LL_lk0[ds]->lock();
					LLs0[ds]->append(ds);
					LL_lk0[ds]->unlock();
				}
				else{
					LL_lk1[ds]->lock();
					LLs1[ds]->append(ds);
					LL_lk1[ds]->unlock();
				}
			}
			else
			{
				if(x < crossover){
					LL_lk0[ds]->lock();
					LLs0[ds]->removeHead();
					LL_lk0[ds]->unlock();
				}
				else{
					LL_lk1[ds]->lock();
					LLs1[ds]->removeHead();
					LL_lk1[ds]->unlock();
				}
			}
		}
		ops++;
		
	}

	globalLK->lock();
	if(node==0)
	{
		ops0 += ops;
	}
	else
	{
		ops1 += ops;
	}

	globalLK->unlock();

	pthread_barrier_wait(&bar);
}

void BinarySearchTest(int tid, int duration, int node, int64_t num_DS, int num_threads, int crossover, int keyspace, int interval)
{	
	std::cout<<"THREAD GOT HERE"<<std::endl;
	#ifdef DEBUG
	if(tid == 1 && node==0)
	{	// startTime = chrono::high_resolution_clock::now();
		std::cout << "Only thread "<< tid << " will print this." << std::endl;
	}		
	#endif

	pthread_barrier_wait(&bar);
	//std::cout<<"crossover value from test is "<<crossover<<std::endl;
	std::mt19937 gen(tid);
	std::uniform_int_distribution<> dist(0, BSTs0.size()-1);
	std::uniform_int_distribution<> opDist(1, 100);
	std::uniform_int_distribution<> xDist(1, 100);
	std::uniform_int_distribution<> keyDist(0,keyspace);

	int64_t ops;
	thread_local vector<int64_t> localOps;
	localOps.resize(duration/interval);
	int x = xDist(gen);
	auto startTimer = std::chrono::steady_clock::now();
	auto endTimer = startTimer + std::chrono::seconds(duration);
    auto nextLogTime = startTimer + std::chrono::seconds(interval);
	int intervalIdx = 0;

	while (duration_cast<seconds>(steady_clock::now() - startTimer).count() < duration) {
		int ds = dist(gen);


		int key = keyDist(gen);
		if(node==0){
			if(opDist(gen)<=80)
			{

				BST_lk0[ds]->lock();
				int level = BSTs0[ds]->lookup(key);
				// globalLK->lock();
				// std::cout<<"Look up traversed "<<level<<" levels"<<std::endl;
				// globalLK->unlock();
				BST_lk0[ds]->unlock();
			
			}
			else {
				int ds_a= dist(gen);
				int ds_b = dist(gen);
				int txn = opDist(gen);
				if(txn%4==0){
					BST_lk0[ds_a]->lock();
					BST_lk1[ds_b]->lock();
					BSTs0[ds_a]->remove(key);
					int level = BSTs1[ds_b]->insert(key);
					// globalLK->lock();
					// std::cout<<"Insert traversed "<<level<<" levels"<<std::endl;
					// globalLK->unlock();

					BST_lk0[ds_a]->unlock();
					BST_lk1[ds_b]->unlock();
				}else if(txn%4==1){
					BST_lk0[ds_a]->lock();
					BST_lk1[ds_b]->lock();
					BSTs1[ds_b]->remove(key);
					int level = BSTs0[ds_a]->insert(key);
					// globalLK->lock();
					// std::cout<<"Insert traversed "<<level<<" levels"<<std::endl;
					// globalLK->unlock();

					BST_lk0[ds_a]->unlock();
					BST_lk1[ds_b]->unlock();
				}else if(txn%4==2){
					int lk1 = (ds_a<ds_b)?ds_a:ds_b;
					int lk2 = (ds_a<ds_b)?ds_b:ds_a;
					if(ds_a==ds_b){continue;}
					BST_lk0[lk1]->lock();
					BST_lk0[lk2]->lock();
					BSTs0[ds_a]->remove(key);
					int level = BSTs0[ds_b]->insert(key);
					// globalLK->lock();
					// std::cout<<"Insert traversed "<<level<<" levels"<<std::endl;
					// globalLK->unlock();

					BST_lk0[lk1]->unlock();
					BST_lk0[lk2]->unlock();
				}else{
					int lk1 = (ds_a<ds_b)?ds_a:ds_b;
					int lk2 = (ds_a<ds_b)?ds_b:ds_a;
					if(ds_a==ds_b){continue;}
					BST_lk1[lk1]->lock();
					BST_lk1[lk2]->lock();
					BSTs1[ds_a]->remove(key);
					int level = BSTs1[ds_b]->insert(key);
					// globalLK->lock();
					// std::cout<<"Insert traversed "<<level<<" levels"<<std::endl;
					// globalLK->unlock();

					BST_lk1[lk1]->unlock();
					BST_lk1[lk2]->unlock();
				}
			}
		}
		else{
			if(opDist(gen)<=80)
			{
				BST_lk1[ds]->lock();
				int level = BSTs1[ds]->lookup(key);
				// globalLK->lock();
				// std::cout<<"Look up traversed "<<level<<" levels"<<std::endl;
				// globalLK->unlock();
				BST_lk1[ds]->unlock();
			}
			else {
				int ds_a= dist(gen);
				int ds_b = dist(gen);
				int txn = opDist(gen);
				if(txn%4==0){
					BST_lk0[ds_a]->lock();
					BST_lk1[ds_b]->lock();
					BSTs0[ds_a]->remove(key);
					int level = BSTs1[ds_b]->insert(key);
					// globalLK->lock();
					// std::cout<<"Insert traversed "<<level<<" levels"<<std::endl;
					// globalLK->unlock();

					BST_lk0[ds_a]->unlock();
					BST_lk1[ds_b]->unlock();
				}else if(txn%4==1){
					BST_lk0[ds_a]->lock();
					BST_lk1[ds_b]->lock();
					BSTs1[ds_b]->remove(key);
					int level= BSTs0[ds_a]->insert(key);
					// globalLK->lock();
					// std::cout<<"Insert traversed "<<level<<" levels"<<std::endl;
					// globalLK->unlock();
					BST_lk0[ds_a]->unlock();
					BST_lk1[ds_b]->unlock();
				}else if(txn%4==2){
					int lk1 = ds_a<ds_b?ds_a:ds_b;
					int lk2 = ds_a<ds_b?ds_b:ds_a;
					if(ds_a==ds_b){continue;}
					BST_lk0[lk1]->lock();
					BST_lk0[lk2]->lock();
					BSTs0[ds_a]->remove(key);
					int level = BSTs0[ds_b]->insert(key);
					// globalLK->lock();
					// std::cout<<"Insert traversed "<<level<<" levels"<<std::endl;
					// globalLK->unlock();
					BST_lk0[lk1]->unlock();
					BST_lk0[lk2]->unlock();
				}else{
					int lk1 = ds_a<ds_b?ds_a:ds_b;
					int lk2 = ds_a<ds_b?ds_b:ds_a;
					if(ds_a==ds_b){continue;}
					BST_lk1[lk1]->lock();
					BST_lk1[lk2]->lock();
					BSTs1[ds_a]->remove(key);
					int level = BSTs1[ds_b]->insert(key);
					// globalLK->lock();
					// std::cout<<"Insert traversed "<<level<<" levels"<<std::endl;
					// globalLK->unlock();
					BST_lk1[lk1]->unlock();
					BST_lk1[lk2]->unlock();
				}
			}
		}
		ops++;
		if(std::chrono::steady_clock::now() >= nextLogTime){
			localOps[intervalIdx] = ops;
			intervalIdx++;
			nextLogTime += std::chrono::seconds(interval);
		}
	}


	globalLK->lock();
	if(node==0)
	{
		for(int i=0; i<localOps.size(); i++){
			globalOps0[i] += localOps[i];
		}
		ops0 = globalOps0[globalOps0.size()-1];
	}
	else
	{
		for(int i=0; i<localOps.size(); i++){
			globalOps1[i] += localOps[i];
		}
		ops1 = globalOps1[globalOps1.size()-1];
	}
	globalLK->unlock();

	pthread_barrier_wait(&bar);
}



void global_cleanup(){
}