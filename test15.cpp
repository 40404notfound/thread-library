#include <iostream>
#include "thread.h"
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_set>
#include <algorithm>
using namespace std;
void print(string in);
int requester(void * parameter);
void start(int parameter);
int service(void * parameter);
int maxqueuesize();
bool notinqueue(int index);

vector<thread *> threads;




int main(int argc, char ** argv) {


	cpu::boot(1, (thread_startfunc_t)start, (void *)10, 0, 0, 114514);

}


void func(void * in);
void start(int parameter)
{
	int para = (int)parameter;
	if (para == 0) return;

	cout <<para<<"b" << endl;
		threads.push_back(new thread((thread_startfunc_t)start, (void *)(para-1)));
		threads.back()->join();

		cout << para << "e" << endl;




}


