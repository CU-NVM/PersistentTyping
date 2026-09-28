/*! \file TestSuite.cpp
 * \brief Testsuite implementation which allows for testing of various data structures
 * \author Nii Mante
 * \date 10/28/2012
 *
 */

#include "TestSuite.hpp"
#include "Node.hpp"
#include "Stack.hpp"
#include "numatype.hpp"

//#include "BinarySearch.hpp"

//#include <iostream>



void StackTest()
{
	Stack a;
	//Stack *b = new Stack();
	numa<Stack ,0>* b = new numa<Stack,0>();
	// a.push(1);
	// std::cout << "----------------------------------" << std::endl;
	// std::cout << "Display Stack after adding 1 node." << std::endl;
	// std::cout << "----------------------------------" << std::endl;
	// a.display();
	

	// a.push(5);
	// a.push(30);
	// std::cout << "----------------------------------" << std::endl;
	// std::cout << "Display Stack after adding 2 nodes." << std::endl;
	// std::cout << "----------------------------------" << std::endl;
	// a.display();

	// a.pop();
	// std::cout << "----------------------------------" << std::endl;
	// std::cout << "Display Stack after removing 1 node" << std::endl;
	// std::cout << "----------------------------------" << std::endl;
	// a.display();

	// std::cout << "----------------------------------" << std::endl;
	// std::cout << "Display empty stack." << std::endl;
	// std::cout << "----------------------------------" << std::endl;
	b->display();

	for(int i = 1; i < 6; i++)
	{
		b->push(i);
	}
	std::cout << "----------------------------------" << std::endl;
	std::cout << "Display Stack after adding 5 node." << std::endl;
	std::cout << "----------------------------------" << std::endl;
	b->display();

	for(int i = 0; i < 3; i++) 
	{	
		b->pop();
	}

	std::cout << "----------------------------------" << std::endl;
	std::cout << "Display Stack after removing 3 nodes." << std::endl;
	std::cout << "----------------------------------" << std::endl;
	b->display();

	delete b;

}

// void BinarySearchTest()
// {
// 	BinarySearchTree a;

// 	// a.insert(4);
// 	// a.insert(2);
// 	// a.insert(5);
// 	// a.insert(1);
// 	// a.insert(3);
	

// 	// a.postOrderPrint();
// 	// a.preOrderPrint();
// 	// a.inOrderPrint();


// 	numa<BinarySearchTree*,1> b = new BinarySearchTree();
// 	b->insert(4);
// 	b->insert(2);
// 	b->insert(5);
// 	b->insert(1);
// 	b->insert(3);

// 	b->postOrderPrint();
// 	b->preOrderPrint();
// 	b->inOrderPrint();
	
// }