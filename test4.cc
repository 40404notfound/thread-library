#include "thread.h"
#include <iostream>

mutex mtx1, mtx2;

void child(void*) {
    mtx1.lock();
    std::cout << "child mtx1 on" << std::endl;
    thread::yield();
    mtx2.lock();
    std::cout << "child mtx2 on" << std::endl;
    mtx1.unlock();
    std::cout << "child mtx1 off" << std::endl;
    thread::yield();
    mtx2.unlock();
    std::cout << "child mtx2 off" << std::endl;
}

void parent(void*) {
    mtx2.lock();
    std::cout << "parent mtx1 on" << std::endl;
    thread::yield();
    mtx1.lock();
    std::cout << "parent mtx2 on" << std::endl;
    mtx2.unlock();
    std::cout << "parent mtx1 off" << std::endl;
    thread::yield();
    mtx1.unlock();
    std::cout << "parent mtx2 off" << std::endl;
}

void start(void*) {
    thread s1{parent, nullptr};
    thread s2{child, nullptr};
    s1.join();
    s2.join();
    std::cout << "Start end" << std::endl;
}

int main4() {
    cpu::boot(1, start, nullptr, true, true, 0);
}