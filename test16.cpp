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
int start(void * parameter);
int service(void * parameter);
int maxqueuesize();
bool notinqueue(int index);


vector<pair<int, int>> tracks;
vector<bool> islast;
int currenttrack = 0;
int maxqueue;
int threadcounter;
int waited = 0;
int numoffile = 5;
int size = 100;


mutex queuemutex;
mutex threadcountermutex;
mutex startmutex;
mutex debug;

cv requestcv;

cv startcv;


int main(int argc, char ** argv) {

	maxqueue = 3;

	cpu::boot(1, (thread_startfunc_t)start, (void *)0, 0, 0, 114514);

}


int start(void * parameter)
{



	islast.assign(numoffile, 0);
	threadcounter = numoffile;
	for (int i = 0; i<numoffile; i++)
	{
		thread((thread_startfunc_t)requester, (void *)i);
	}




	thread((thread_startfunc_t)service, (void *)0);







}



int requester(void * parameter)
{




	intptr_t ID = (intptr_t)parameter;

	queue<int> t;


	for (int i = 0; i<size; i++)
	{
		t.push(rand() % 1000);
	}



	int tracknumber;


	if (t.empty()) {
		islast[ID] = 1;
		threadcounter--;
		waited++;
		if (waited == numoffile)
			 startcv.broadcast();
		return 0;
	}


	startmutex.lock();
	print(to_string(ID) + "start");

	waited++;
	if (waited == numoffile)
		startcv.broadcast();
	else
		startcv.wait(startmutex);


	startmutex.unlock();

	while (1)
	{
		queuemutex.lock();

		print(to_string(ID) + "lock");
		while (tracks.size() >= maxqueuesize())
		{

			print(to_string(ID) + "wait");
			requestcv.wait(queuemutex);
			print(to_string(ID) + "stop wait");
		}
		tracknumber = t.front();
		t.pop();
		tracks.emplace_back(tracknumber, ID);

		debug.lock();
		cout << ID << "r" << tracknumber << endl;

		debug.unlock();
		bool exit = 0;
		if (t.size() == 0)
		{
			exit = 1;
			islast[ID] = 1;
		}
		print(to_string(ID) + "unlock");
		requestcv.broadcast();
		queuemutex.unlock();
		if (exit) break;
	}


	return 0;
}

int service(void * parameter)
{


	startmutex.lock();

	if (waited == numoffile)
		startcv.broadcast();
	else
		startcv.wait(startmutex);


	startmutex.unlock();

	print("service start");
	if (threadcounter == 0)
		return 0;

	while (1)//fixit
	{
		queuemutex.lock();
		print("service lock");
		while (tracks.size() < maxqueuesize())
		{

			print("service wait");
			requestcv.wait(queuemutex);
			print("service stop wait");
		}

		int min = -1;
		int minindex = -1;
		for (int i = 0; i<tracks.size(); i++)
		{
			if (min == -1)
			{
				min = abs(tracks[i].first - currenttrack);
				minindex = i;
			}
			else
			{
				if (abs(tracks[i].first - currenttrack)<min)
				{
					min = abs(tracks[i].first - currenttrack);
					minindex = i;
				}
			}
		}

		debug.lock();
		cout << tracks[minindex].second << "s" << tracks[minindex].first << endl;
		debug.unlock();
		currenttrack = tracks[minindex].first;

		auto target = tracks[minindex];


		tracks.erase(tracks.begin() + minindex);


		//decrease thread counter

		if (notinqueue(target.second) && islast[target.second])
			threadcounter--;

		bool allout = 1;
		for (int i = 0; i<islast.size(); i++)
			if (islast[i] == 0)
			{
				allout = 0;
				break;
			}


		bool exit = 0;
		if (allout && tracks.size() == 0)
			exit = 1;


		print("service unlock");
		requestcv.broadcast();
		queuemutex.unlock();
		if (exit) break;
	}

}

bool notinqueue(int ID)
{


	for (int i = 0; i<tracks.size(); i++)
	{
		if (tracks[i].second == ID)
			return 0;
	}
	return 1;

}


int maxqueuesize()
{
	return min(threadcounter, maxqueue);
}

void print(string in)
{
}