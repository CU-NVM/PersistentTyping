#pragma once
/*! \file TestSuite.hpp
 * \brief Testsuite program which allows for testing of various data structures
 * \author Nii Mante
 * \date 10/28/2012
 *
 */

#ifndef _TESTSUITE_HPP_
#define _TESTSUITE_HPP_


#include <thread>
#include <barrier>
#include <mutex>
#include <iostream>
#include <syncstream>
#include <string>
#include <vector>
#include "numatype.hpp"
#include "numathreads.hpp"
#include <jemalloc/jemalloc.h>
#include <umf/pools/pool_jemalloc.h>
using namespace std;

extern struct prefill_percentage percentages;



void global_init(int num_threads, int duration, int interval);
void singleThreadedStackTest(int duration, int64_t num_DS);
void singleThreadedStackInit(int num_DS, bool isNuma);
void warmUpPool();
void numa_array_init(std::string DS_config, int size, bool prefill, prefill_percentage &percentages);
void numa_Stack_init(std::string DS_config, int num_DS, bool prefill, prefill_percentage &percentages);
void numa_Queue_init(std::string DS_config, int num_DS, bool prefill, prefill_percentage &percentages);
void numa_BST_init(std::string DS_config, int num_DS, int keyspace, int node, int crossover);
void numa_LL_init(std::string DS_config, int num_DS, bool prefill, prefill_percentage &percentages);
void sync_init(int num_threads);

/*!
 * \brief Test function for the Stack classes
 * 
 * Invokes the functions of the Stack class 
 * (pop, push, ctor, dtor, display)
 * 
 */

void ArrayTest(int t_id, int duration, int node, int64_t num_DS, int num_threads, int crossover);


void numa_BST_single_init(std::string DS_config, int num_DS, int keyspace, int node, int crossover);

void main_BST_test(int duration,  int64_t num_DS, int num_threads, int crossover, int keyspace);

void StackTest(int t_id, int duration, int node, int64_t num_DS, int num_threads, int crossover);

void QueueTest(int t_id, int duration, int node, int64_t num_DS, int num_threads, int crossover);

void BinarySearchTest(int t_id, int duration, int node, int64_t num_DS, int num_threads, int crossover, int keyspace, int interval);

void LinkedListTest(int t_id, int duration, int node, int64_t num_DS, int num_threads, int crossover);

void global_cleanup();

#endif 