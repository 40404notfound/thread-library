#include <iostream>
#include "thread.h"
#include <cassert>

using namespace std;
int writer_num = 0, reader_num = 0;
mutex rw_lock;
cv waiting_readers, waiting_writers;


void readerStart(int id) {
	rw_lock.lock();
	while (writer_num > 0) {
		waiting_readers.wait(rw_lock);
	}
	//printf("Reader %d start reading\n", arg);
	cout << "Reader " << id << " start reading\n";
	reader_num++;
	rw_lock.unlock();
}
void readerFinish(int id) {
	rw_lock.lock();
	reader_num--;
	//printf("Reader %d finish reading\n", arg);
	cout << "Reader " << id << " finish reading\n";
	if (reader_num == 0) {
		waiting_writers.signal();
	}
	rw_lock.unlock();
}
void writerStart(int id) {
	rw_lock.lock();
	while (reader_num > 0 || writer_num > 0) {
		waiting_writers.wait(rw_lock);
	}
	cout << "Writer " << id << " start writing\n";
	writer_num++;
	rw_lock.unlock();
}
void writerFinish(int id) {
	rw_lock.lock();
	writer_num--;
	//printf("Writer %d finish writing\n", arg);
	cout << "Writer " << id << " finish writing\n";
	waiting_readers.broadcast();
	waiting_writers.signal();
	rw_lock.unlock();
}

void read(int arg) {
	readerStart(arg);
	cout<<"Reader "<<arg<<"  reading\n";
	readerFinish(arg);
}

void write(int arg) {
	writerStart(arg);
	cout << "Writer " << arg << "  writing\n";
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
	cpu::boot(1, (thread_startfunc_t)start, (void*)100, false,false, atoi(argv[1]));
	//cpu::boot(3, (thread_startfunc_t)start, (void*)100, false, true, 13);
}
