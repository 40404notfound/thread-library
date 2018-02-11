#include "thread.h"
#include <iostream>

mutex mtx1, mtx2;

void child(void*) {
    std::cout << "child mtx1 on" << std::endl;
    mtx2.lock();
    std::cout << "child mtx2 on" << std::endl;
    thread::yield();
    mtx2.lock();
    std::cout << "child mtx2 off" << std::endl;
}

void parent(void*) {
    std::cout << "parent mtx2 on" << std::endl;
    mtx1.lock();
    std::cout << "parent mtx1 on" << std::endl;
    thread::yield();
    mtx1.unlock();
    std::cout << "parent mtx1 off" << std::endl;
}

void start(void*) {
    thread s1{parent, nullptr};
    thread s2{parent, nullptr};
    thread s3{child, nullptr};
    thread s4{child, nullptr};
    s1.join();
    s2.join();
}

int main6() {
    cpu::boot(1, start, nullptr, true, true, 0);
}