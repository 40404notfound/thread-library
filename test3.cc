#include <iostream>
#include <cstdlib>
#include <utility>
#include "thread.h"

using namespace std;

int g = 0;

mutex mutex1;
cv cv1;

void loop(void *a)
{
    char *id = (char *) a;
    int i;

    mutex1.lock();
    cout << "loop called with id " << id << endl;

    for (i=0; i<5; i++, g++) {
	cout << id << ":\t" << i << "\t" << g << endl;
        mutex1.unlock();
	thread::yield();
        mutex1.lock();
    }
    cout << id << ":\t" << i << "\t" << g << endl;
    mutex1.unlock();
}

void parent(void *a) {
    intptr_t arg = (intptr_t) a;
    cout << "parent init" << endl;

    mutex1.lock();
    cout << "parent called with arg " << arg << endl;
    mutex1.unlock();

    thread t1 ( (thread_startfunc_t) loop, (void *) "child thread");
    thread t2 ( (thread_startfunc_t) loop, (void *) "another child thread");
    t2 = std::move(t1);
    thread t3{std::move(t2)};
    thread t4{std::move(t2)};
    loop( (void *) "parent thread");
}

int main3() {
    cout << "boot" << endl;
    
    cpu::boot(4, (thread_startfunc_t) parent, (void *) 20, false, false, 0);
}