#include "thread.h"
#include <iostream>

mutex mtx1, mtx2;

void func1(void*) {
    while (true) {
        mtx1.lock();
        mtx2.lock();
    }
}

void func2(void*) {
    while(true) {
        mtx1.unlock();
        mtx2.unlock();
    }
}

void start(void*) {
    thread t1{func1, nullptr};
    thread t2{func2, nullptr};
}

int main7() {
    cpu::boot(4, start, nullptr, true, true, 2222);
}