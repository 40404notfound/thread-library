#include <iostream>
#include "thread.h"
#include <cassert>
#include <stdexcept>

using namespace std;

mutex m1;
mutex mm;

void func1(int arg) {
	
	mm.lock();
	m1.lock();
	mm.unlock();
	cout << "func1" << endl;
	mm.lock();
	m1.unlock();
	mm.unlock();

}
void func2(void* arg) {

	try {
		mm.lock();
		cout << "func2 steals the lock" << endl;
		m1.unlock();
		m1.lock();
		mm.unlock();
		cout << "func2 steals the lock" << endl;
		mm.lock();
		m1.unlock();
		mm.unlock();
	}
	catch (...)
	{
		cout << "exception thrown, pass test" << endl;
		return;
	}

		cout << "fail test" << endl;
		assert(false);
	
	

	
}

void start(int arg) {

	auto t1 = thread((thread_startfunc_t)func2, (void*)10);
	auto t2 = thread((thread_startfunc_t)func1, (void*)10);

	cout << "func end" << endl;

}

int main()
{
	cpu::boot(1, (thread_startfunc_t)start, (void *)100, false, false, 114514);
}