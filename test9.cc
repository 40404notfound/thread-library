#include "thread.h"
#include <iostream>

mutex mtx1, mtx2;
cv cv1;
int g_var = 0;


void start(void*) {
    for (size_t i = 0; i < 4; ++i)
        thread rec{start, nullptr};
}

int main() {
	cpu::boot(1, start, nullptr, 0, 0, 2222);
}