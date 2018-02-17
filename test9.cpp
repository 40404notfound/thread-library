#include "thread.h"
#include <iostream>

mutex mtx1, mtx2;
cv cv1;
int g_var = 0;
atomic<bool> t


void start(void*) {
	try {
		for (size_t i = 0; i < 4; ++i)
			thread rec{ start, nullptr };
	}
	catch(...)
	{
		t.store(1);
		return£»
	}


}

int main() {
	cpu::boot(1, start, nullptr, 0, 0, 2222);
	if (t.load() != 1) assert(0);
}