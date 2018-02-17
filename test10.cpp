#include "thread.h"
#include <iostream>
#include <cassert>

mutex mtx1, mtx2;
cv cv1;
int g_var = 0;

void second(void* p) {
    int64_t a = (int64_t)p;
    std::cout << a << std::endl;
}

void start(void*) {
	try {
		for (size_t i = 0; i < 100000; ++i) {
			++g_var;
			thread rec{ second, (void*)g_var };
		}
	}
	catch (...)
	{
		return;
	}
	assert(false);
}

int main() {
	cpu::boot(1, start, nullptr, 0, 0, 2222);
}