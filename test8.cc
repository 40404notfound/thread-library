#include "thread.h"
#include <iostream>

mutex mtx1, mtx2;
cv cv1;
int g_var = 0;

void func1(void*) {
    mtx1.lock();
    std::cout << g_var << std::endl;
    while (g_var)
        cv1.wait(mtx1);
    std::cout << g_var << std::endl;
    ++g_var;
    std::cout << g_var << std::endl;
    mtx1.unlock();
}

void func2(void*) {
    mtx1.lock();
    std::cout << g_var << std::endl;
    while (!g_var)
        cv1.wait(mtx1);
    std::cout << g_var << std::endl;
    --g_var;
    std::cout << g_var << std::endl;
    cv1.broadcast();
    mtx1.unlock();
}

void start(void*) {
    for (size_t i = 0; i < 10; ++i)
        thread t1{func1, nullptr};
    for (size_t i = 0; i < 10; ++i)
        thread t2{func2, nullptr};
}

int main() {
    cpu::boot(4, start, nullptr, true, true, 2222);
}