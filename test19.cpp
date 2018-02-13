#include <iostream>
#include "thread.h"
#include <cassert>
#include <stdexcept>

using namespace std;

mutex mm;

void func1(int arg) {

	mm.lock();
	while (true){}
 
	mm.unlock();

}


void start(int arg) {

	try {
		int i = 0;
		while (1)
		{
			//cout << "created " << i << endl;
			new thread((thread_startfunc_t)func1, (void*)10);
			i++;
		}
	}
	catch(std::bad_alloc& e)
	{
		cout << "pass" << endl;
		return;
	}
	catch(...)
	{
		cout << "FAIL" << endl;
		assert(false);
	}

	cout << "func end" << endl;
	assert(false);


}

int main()
{
	cpu::boot(1, (thread_startfunc_t)start, (void *)114514, false, false, 114514);
}