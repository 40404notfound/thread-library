#include "thread.h"
#include <iostream>

mutex mtx1, mtx2;
cv cv1;
int g_var = 0;

void second(void* p) {
    int64_t a = (int64_t)p;
    std::cout << a << std::endl;
}

void start(void*) {
    for (size_t i = 0; i < 100000; ++i) {
        ++g_var;
        thread rec{second, (void*)g_var};
    }
}

int main() {
	cpu::boot(1, start, nullptr, 0, 0, 2222);
}