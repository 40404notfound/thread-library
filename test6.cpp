#include <iostream>
#include "thread.h"
#include <fstream>
#include <vector>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <cassert>
using namespace std;
void print(string in);
int requester(void * parameter);
void start(int parameter);
int service(void * parameter);
int maxqueuesize();
bool notinqueue(int index);
bool loadfinish = 0;

vector<thread *> threads;

mutex m;
cv c;

int main(int argc, char ** argv) {


	cpu::boot(1, (thread_startfunc_t)start, (void *)10, 0, 0, 114514);

}




void start(int parameter)
{
	try {
		m.unlock();
	}
	catch (...)
	{
		cout << "pass" << endl;
		return;
	}

	assert(false);

}


