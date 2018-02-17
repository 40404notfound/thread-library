#include "thread.h"
#include <iostream>

mutex mtx1, mtx2;
cv scv;

void func2(void* mtx) {
    mutex* p = (mutex*)mtx;
    p->lock();
    scv.wait(*p);
    std::cout << mtx << std::endl;
    scv.signal();
    p->unlock();
}

void start(void*) {
    thread t1{func2, &mtx1};
    thread t2{func2, &mtx1};
    t2.join();
    t1.join();
}

int main() {
    cpu::boot(1, start, nullptr,0 , 0, 2222);
}