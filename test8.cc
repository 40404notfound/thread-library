#include "thread.h"
#include <iostream>
#include <vector>

mutex mtx1, mtx2;
cv cv1;
int g_var = 0;

void second(void* p) {
    for (size_t i = 0; i < 1000; ++i) {
        int64_t a = (int64_t)p;
        std::cout << a + i << std::endl;
    }
}

void start(void*) {
    std::vector<thread> v;
    for (size_t i = 0; i < 100000; ++i) {
        ++g_var;
        v.emplace_back(second, (void*)g_var);
    }
    for (size_t i = 0; i < v.size(); ++i) {
        v[i].join();
    }
}

int main() {
	cpu::boot(1, start, nullptr, 0, 0, 2222);
}