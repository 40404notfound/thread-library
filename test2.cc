#include "thread.h"
#include <iostream>
// EHIJ

int global_sum = 0;
int num_threads = 25;
int cop_threads = 0;
mutex mtx1, mtx2;
cv cv1;

void add(void* s) {
    mtx2.lock();
    std::cout << "Add init" << std::endl;
    global_sum += reinterpret_cast<intptr_t>(s);
    cop_threads += 1;
    cv1.signal();
    std::cout << "Add end" << std::endl;
    mtx2.unlock();
}

void parent(void*) {
    mtx1.lock();
    std::cout << "Parent init" << std::endl;
    for (size_t i = 0; i < num_threads; ++i) {
        thread thd_add{&add, (void*)i};
    }
    while (cop_threads != num_threads) {
        cv1.wait(mtx1);
    }
    std::cout << "Parent end" << std::endl;
    std::cout << "Result: " << global_sum << std::endl;
    mtx1.unlock();
}

int main2() {
    cpu::boot(4, parent, nullptr, true, true, 2324);
}