#include <iostream>
#include "thread.h"
#include <cassert>

using namespace std;
void func2(int arg) {

	int sum = 0;
	for(int i=0 ;i<10000;i++)
	{
		sum += i;
	}
	cout << sum << endl;
}
void func3(void* arg) {
	thread* in = (thread*)arg;
	in->join();
	int sum = 0;
	for (int i = 0; i<10001; i++)
	{
		sum += i;
	}
	cout << sum << endl;
}

void start2(int arg) {

	auto t1=thread((thread_startfunc_t) func2,(void*)10);
	auto t2 = thread((thread_startfunc_t)func3, (void*)&t1);
	t1.join();
	cout << "func end" << endl;

}

int main2()
{
	cpu::boot(1, (thread_startfunc_t)start2, (void *)100, false, false, 114514);
}