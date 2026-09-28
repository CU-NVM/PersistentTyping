#include<iostream>
#include<thread>
#include<mutex>
#include<barrier>

std::barrier b(3);

void* print(){
    b.arrive_and_wait();
    std::cout<<"Hello"<<std::endl;
    b.arrive_and_wait();
    std::cout<<"World"<<std::endl;
    return nullptr;
}

int main(){
    std::thread t1(print);
    std::thread t2(print);
    std::thread t3(print);

    t1.join();
    t2.join();
    t3.join();
    return 0;
}