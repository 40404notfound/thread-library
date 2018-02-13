#include <cassert>
#include "thread.h"
#include <queue>
#include <ucontext.h>
#include <iostream>
#include <unordered_map>
#define CURRENT_THREAD cpu::self()->impl_ptr->current_thread
#define ADD_READY_THREAD(x) cpu::self()->impl_ptr->add_ready_thread(x);
#define RUN_NEXT_THREAD(x) cpu::self()->impl_ptr->run_next_thread(x);
using namespace std;

class thread_info
{
public:
	
	thread_info()
	{
		context = nullptr;
		joined = nullptr;
	}
	void set(thread_info& input)
	{
		context = input.context;
		joined = input.joined;
	}
	void set(ucontext_t * _context, queue<thread_info>* _joined)
	{
		context = _context;
		joined = _joined;
	}
	bool empty()
	{
		return context == nullptr && joined==nullptr;
	}
	void clear()
	{
		context = nullptr;
		joined = nullptr;
	}


	ucontext_t * context;
	queue<thread_info>* joined;
	//thread* parent;
	
};
queue<thread_info> ready_threads_info;
queue<cpu *> suspended_cpu_ptr;
atomic<bool> init_guard(true);
//ucontext suspend_context;
//int num_cpu=0;

void timer_handler();

void ipi_handler();
void delete_previous_context_if_needed();
void guard_lock();
void guard_unlock();
void set_cpu_context_from_ready_queue();
void suspend();

void init_context(ucontext_t* input)
{
	getcontext(input);
	input->uc_stack.ss_sp = new char[STACK_SIZE];
	input->uc_stack.ss_size = STACK_SIZE;
	input->uc_stack.ss_flags = 0;
	input->uc_link = nullptr;//TODO

}



class cpu::impl{


public:


	impl()
	{
		
	}
	char *previous_stack = nullptr;
	thread_info current_thread;
	thread_info next_thread;
    ucontext_t suspend_context;

	void add_ready_thread(thread_info input_thread) {


		if (!suspended_cpu_ptr.empty()) {
			auto temp = suspended_cpu_ptr.front();

			suspended_cpu_ptr.pop();
			//printf("@thread %p send to %p (add_ready_thread)\n", input_thread.context, temp);
			temp->impl_ptr->next_thread = input_thread;

			//printf("+CPU %p activated(add_ready_thread)\n", temp);

			temp->interrupt_send();

		}
		else {
			assert(input_thread.context != nullptr);
			ready_threads_info.push(input_thread);
		}


	}

	void run_next_thread(ucontext_t* savedto = nullptr)
	{
		assert(!(savedto != nullptr && !CURRENT_THREAD.empty()));

		if (cpu::self()->impl_ptr->next_thread.empty())
		{
			if (!ready_threads_info.empty())
			{
				if (CURRENT_THREAD.empty())
				{
					CURRENT_THREAD = ready_threads_info.front();
					ready_threads_info.pop();
					if (savedto == nullptr)
						setcontext(CURRENT_THREAD.context);
					else
						swapcontext(savedto, CURRENT_THREAD.context);
				}
				else
				{
					ready_threads_info.push(CURRENT_THREAD);
					CURRENT_THREAD = ready_threads_info.front();
					ready_threads_info.pop();
					swapcontext(ready_threads_info.back().context, CURRENT_THREAD.context);

				}
			}
			else
			{
				if (CURRENT_THREAD.empty()) {
					if (savedto == nullptr)
						setcontext(&suspend_context);
					else
						swapcontext(savedto, &suspend_context);
				}
			}
	    }
		
	}


};

class thread::impl {
public:
	impl() {}
	thread_info info;
	bool finished = 0;
};



class mutex::impl {
public:
	impl() {}
	bool locked = false;
	queue<thread_info> mutex_queue;
	void real_unlock()
	{
		assert(locked);

		locked = false;
		if (!mutex_queue.empty()) {
			ADD_READY_THREAD(mutex_queue.front())
			mutex_queue.pop();
			locked = true;
		}

	}

	
	void real_lock()
	{
		if (locked) {
			mutex_queue.push(CURRENT_THREAD);//add to queue
			CURRENT_THREAD.clear();
			RUN_NEXT_THREAD(mutex_queue.back().context);
		}

		else
		{
			locked = true;
		}
	}

};

class cv::impl {
public:
	impl() {}
	queue<thread_info> cv_queue;
};






void guard_lock()
{
    cpu::interrupt_disable();
    while (guard.exchange(true)) {}
}

void guard_unlock()
{
	delete_previous_context_if_needed();
    guard.store(false);
    cpu::interrupt_enable();
}



void ipi_handler() {
}

void delete_previous_context_if_needed() {

    if (cpu::self()->impl_ptr->previous_stack != nullptr) {
        delete[] cpu::self()->impl_ptr->previous_stack;
        cpu::self()->impl_ptr->previous_stack = nullptr;
    }
}

void timer_handler() {
    assert_interrupts_enabled();
    guard_lock();
	RUN_NEXT_THREAD();
	guard_unlock();
}

unordered_map<ucontext_t*, bool> thread_status;
unordered_map<ucontext_t*, bool> not_returned;

void thread_wrapper(thread_startfunc_t func, void *arg)//TODO
{
    guard_unlock();
    //before
    func(arg);
    //after
    guard_lock();

	cpu::self()->impl_ptr->previous_stack = (char*) CURRENT_THREAD.context->uc_stack.ss_sp;
	while (!CURRENT_THREAD.joined->empty()) {
		ADD_READY_THREAD(CURRENT_THREAD.joined->front())
			CURRENT_THREAD.joined->pop();
	}
	
	
	not_returned[CURRENT_THREAD.context] = false;
	if (!thread_status[CURRENT_THREAD.context])
	{
		thread_status.erase(CURRENT_THREAD.context);
		not_returned.erase(CURRENT_THREAD.context);
	}
	CURRENT_THREAD.clear();
	delete CURRENT_THREAD.joined;
	delete CURRENT_THREAD.context;
	RUN_NEXT_THREAD();

}



void suspend() {

	while (1)
	{
		suspended_cpu_ptr.push(cpu::self());
		delete_previous_context_if_needed();
		guard.store(false);
		cpu::interrupt_enable_suspend();

		guard_lock();
		CURRENT_THREAD = cpu::self()->impl_ptr->next_thread;
		cpu::self()->impl_ptr->next_thread.clear();
		swapcontext(&cpu::self()->impl_ptr->suspend_context, CURRENT_THREAD.context);
	}
}



void cpu::init(thread_startfunc_t func, void *arg) {
    impl_ptr = new impl();
	init_context(&impl_ptr->suspend_context);
	makecontext(&impl_ptr->suspend_context, suspend, 0);

    cpu::interrupt_vector_table[IPI] = ipi_handler;
    cpu::interrupt_vector_table[TIMER] = timer_handler;
    if (!func) {
        while (init_guard.exchange(true)) {}
        init_guard.store(false);
        
    } else {

		guard.store(true);
        //num_cpu++
        init_guard.store(false);
    }

    if (func) {
        ucontext_t *temp = new ucontext_t;
		
        //impl_ptr->current_thread.context = temp;

        init_context(temp);

        makecontext(temp, (void (*)()) thread_wrapper, 2, func, arg);

        CURRENT_THREAD.set(temp, new queue<thread_info>);
		//CURRENT_THREAD.parent = nullptr;
		thread_status[CURRENT_THREAD.context] = false;
		not_returned[CURRENT_THREAD.context] = true;
        setcontext(temp);

    } else {

		while (guard.exchange(true)) {}//modified
		RUN_NEXT_THREAD();
    }
    assert(false);
}






thread::thread(thread_startfunc_t func, void *arg) {
    impl_ptr = new impl();
    impl_ptr->info.context = new ucontext_t;
	impl_ptr->info.joined = new queue<thread_info>;
	//impl_ptr->info.parent = this;
    init_context(impl_ptr->info.context);
    makecontext(impl_ptr->info.context, (void(*)()) thread_wrapper, 2, func, arg);
    guard_lock();
	thread_status[impl_ptr->info.context] = true;
	not_returned[impl_ptr->info.context] = true;
    cpu::self()->impl_ptr->add_ready_thread(impl_ptr->info);//TODO: CHECK
    guard_unlock();
}



thread::~thread()
{
	guard_lock();

	thread_status[impl_ptr->info.context] = false;
	if(!not_returned[impl_ptr->info.context])
	{
		thread_status.erase(impl_ptr->info.context);
		not_returned.erase(impl_ptr->info.context);
	}

	guard_unlock();
	delete impl_ptr;
}
void thread::join() {
	guard_lock();
	if (not_returned[impl_ptr->info.context])
	{
		auto queue = this->impl_ptr->info.joined;
		queue->push(CURRENT_THREAD);
		CURRENT_THREAD.clear();
		RUN_NEXT_THREAD(queue->back().context);
	}
	guard_unlock();



}
void thread::yield() {
	timer_handler();
}


mutex::mutex() { impl_ptr = new impl(); }
mutex::~mutex() { delete impl_ptr; }
void mutex::lock()//permission???
{

    guard_lock();
    impl_ptr->real_lock();
    guard_unlock();

}


void mutex::unlock() //permission???
{

    guard_lock();

    impl_ptr->real_unlock();
    guard_unlock();

}






cv::cv(){impl_ptr = new impl();};
cv::~cv(){delete impl_ptr;};

void cv::wait(mutex &input_mutex) {
    guard_lock();

    input_mutex.impl_ptr->real_unlock();//make atomic


	assert(cpu::self()->impl_ptr->current_thread.context != nullptr);
    impl_ptr->cv_queue.push(CURRENT_THREAD);
	CURRENT_THREAD.clear();
	RUN_NEXT_THREAD(impl_ptr->cv_queue.back().context);
    guard_unlock();
    input_mutex.lock();
};
void cv::signal()
{
    guard_lock();
	if (!impl_ptr->cv_queue.empty())
	{

		assert(impl_ptr->cv_queue.front().context != nullptr);
		ADD_READY_THREAD(impl_ptr->cv_queue.front());
		impl_ptr->cv_queue.pop();
	}
    guard_unlock();
};
void cv::broadcast()
{
    guard_lock();
    while(!impl_ptr->cv_queue.empty()) {
		assert(impl_ptr->cv_queue.front().context != nullptr);
        ADD_READY_THREAD(impl_ptr->cv_queue.front());
        impl_ptr->cv_queue.pop();
    }
    guard_unlock();
};


//TODO:How to delete suspendcontext?��static

//TODO:seperate two interrupter,������fix small bug������