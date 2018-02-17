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
bool loadfinish = 0;
int counter1;
int counter2;

vector<thread *> threads;

mutex m;
cv c;
void truestart(int in);

int main(int argc, char ** argv) {


	cpu::boot(1, (thread_startfunc_t)truestart, (void *)10, 0, 0, 114514);

}


void func(void * in);
void truestart(int in)
{
	cout << "start" << endl;
	m.lock();
	threads.push_back(new thread((thread_startfunc_t)start, (void *)10));
	m.unlock();



}

void start(int parameter)
{
	long long int para = (long long int)parameter;
	m.lock();
	
	if (counter1++ == 100) { m.unlock(); return; }
	
	m.unlock();

	m.lock();
	cout << para << "b" << endl;
	m.unlock();

	m.lock();
	threads.push_back(new thread((thread_startfunc_t)start, (void *)(para-1)));
	m.unlock();

		m.lock();
		while (loadfinish != 1)
		{
			m.unlock();

			m.lock();
			if(loadfinish != 1) cout << para << "hacking" << endl;
			if (counter2++==100 && loadfinish != 1) { loadfinish = 1; cout << para << "success" << endl;}
			thread::yield();
			
			m.unlock();


			m.lock();
		}
		m.unlock();


	m.lock();
	cout << para << "e" << endl;
	m.unlock();




}


