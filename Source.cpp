#include <iostream>
#include "thread.h"
#include <cassert>

using namespace std;

mutex rwLock;
cv waitingReaders, waitingWriters;
int numWriters = 0, numReaders = 0;

void readerStart(int arg) {
	rwLock.lock();
	while (numWriters > 0) {
		waitingReaders.wait(rwLock);
	}
	//printf("Reader %d start reading\n", arg);
	cout << "Reader " << arg << " start reading\n";
	numReaders++;
	rwLock.unlock();
}
void readerFinish(int arg) {
	rwLock.lock();
	numReaders--;
	//printf("Reader %d finish reading\n", arg);
	cout << "Reader " << arg << " finish reading\n";
	if (numReaders == 0) {
		waitingWriters.signal();
	}
	rwLock.unlock();
}
void writerStart(int arg) {
	rwLock.lock();
	while (numReaders > 0 || numWriters > 0) {
		waitingWriters.wait(rwLock);
	}
	cout << "Writer " << arg << " start writing\n";
	numWriters++;
	rwLock.unlock();
}
void writerFinish(int arg) {
	rwLock.lock();
	numWriters--;
	//printf("Writer %d finish writing\n", arg);
	cout << "Writer " << arg << " finish writing\n";
	waitingReaders.broadcast();
	waitingWriters.signal();
	rwLock.unlock();
}

void read(int arg) {
	readerStart(arg);
	cout<<"Reader "<<arg<<" doing reading\n";
	readerFinish(arg);
}

void write(int arg) {
	writerStart(arg);
	cout << "Writer " << arg << " doing writing\n";
	writerFinish(arg);
}

void start(int arg) {
	//assert(false);
	for (int i = 0; i<arg; i++) {
		thread t_write((thread_startfunc_t)write, (void*)i);
		thread t_read((thread_startfunc_t)read, (void*)i);
		
	}
	int i = 1;

}

int main(int argc ,char ** argv) {
	cpu::boot(3, (thread_startfunc_t)start, (void*)100, true, true, atoi(argv[1]));
	//cpu::boot(3, (thread_startfunc_t)start, (void*)100, false, true, 13);
}
