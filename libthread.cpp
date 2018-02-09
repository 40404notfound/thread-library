#include <cassert>
#include "thread.h"
#include <queue>
#include <ucontext.h>
#include <iostream>

using namespace std;

class thread_info
{
public:
	thread_info(){}
	thread_info(ucontext_t * _context, mutex *_mutex_ptr)
	{
		context = _context;
		mutex_ptr = _mutex_ptr;
	}

	void set(ucontext_t * _context, mutex *_mutex_ptr)
	{
		context = _context;
		mutex_ptr = _mutex_ptr;
	}


	ucontext_t * context;
	mutex * mutex_ptr;
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
	thread_info current_thread = thread_info(nullptr, nullptr);
	thread_info next_thread = thread_info(nullptr, nullptr);
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


};

class thread::impl {
public:
	impl() {}
	thread_info info;
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
			cpu::self()->impl_ptr->add_ready_thread(mutex_queue.front());
			mutex_queue.pop();
			locked = true;
		}

	}


	void real_lock()
	{
		if (locked) {
			mutex_queue.push(cpu::self()->impl_ptr->current_thread);//add to queue
			cpu::self()->impl_ptr->current_thread.set(nullptr, nullptr);

			if (ready_threads_info.empty()) {
				
				assert(cpu::self()->impl_ptr->next_thread.context == nullptr);
				swapcontext(mutex_queue.back().context, &cpu::self()->impl_ptr->suspend_context);
			}
			else {
				cpu::self()->impl_ptr->current_thread = ready_threads_info.front();
				ready_threads_info.pop();
				swapcontext(mutex_queue.back().context, cpu::self()->impl_ptr->current_thread.context);
			}


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
	//atomic<bool> guard = atomic<bool>(false);

};



void set_cpu_context_from_ready_queue()
{

    cpu::self()->impl_ptr->current_thread = ready_threads_info.front();
    ready_threads_info.pop();
    setcontext(cpu::self()->impl_ptr->current_thread.context);
};


void guard_lock()
{
    cpu::interrupt_disable();
    while (guard.exchange(true)) {}
}
void guard_unlock()
{
    //if (!cpu::self()->impl_ptr->no_guard_after_swap.exchange(false))
    guard.store(false);
    delete_previous_context_if_needed();
    cpu::interrupt_enable();

}
void guard_unlock_suspend(){
	//printf("@CPU %p suspended(guard_unlock_suspend)\n", cpu::self());
suspended_cpu_ptr.push(cpu::self());
guard.store(false);
cpu::interrupt_enable_suspend();

}

void ipi_handler() {
	assert_interrupts_enabled();

    timer_handler();

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
	//printf("@timer_handler called by %p\n", cpu::self());
    if (cpu::self()->impl_ptr->next_thread.context != nullptr)//get from previous cpu directly
    {
		
		//printf("+thread %p received by %p \n",cpu::self()->impl_ptr->next_thread.context, cpu::self());

        cpu::self()->impl_ptr->current_thread = cpu::self()->impl_ptr->next_thread;
        cpu::self()->impl_ptr->next_thread.set(nullptr, nullptr);
        //cpu::self()->impl_ptr->no_guard_after_swap = true;

        setcontext(cpu::self()->impl_ptr->current_thread.context);


    } else {

        if (!ready_threads_info.empty())
        {
			//printf("+thread %p exchanged to %p by %p \n",cpu::self()->impl_ptr->current_thread.context,ready_threads_info.front().context, cpu::self());
			//if (ready_threads_info.size() == 12)
				//printf("hahah");
			assert(cpu::self()->impl_ptr->current_thread.context != nullptr);
            ready_threads_info.push(cpu::self()->impl_ptr->current_thread);
            cpu::self()->impl_ptr->current_thread = ready_threads_info.front();
			
            ready_threads_info.pop();
            swapcontext(
                    ready_threads_info.back().context,
                    cpu::self()->impl_ptr->current_thread.context);
        }
        


    }
	guard_unlock();


    
}

void thread_wrapper(thread_startfunc_t func, void *arg)//TODO
{
    guard_unlock();
    //before
    func(arg);
    //after
    guard_lock();

    if(cpu::self()->impl_ptr->current_thread.mutex_ptr)
		cpu::self()->impl_ptr->current_thread.mutex_ptr->impl_ptr->real_unlock();
    if (ready_threads_info.empty()) {
        cpu::self()->impl_ptr->previous_stack = (char *) cpu::self()->impl_ptr->current_thread.context->uc_stack.ss_sp;
        delete cpu::self()->impl_ptr->current_thread.context;
        delete cpu::self()->impl_ptr->current_thread.mutex_ptr;
        cpu::self()->impl_ptr->current_thread.set(nullptr,nullptr);

        guard_unlock_suspend();


    } else//switch
    {
        cpu::self()->impl_ptr->previous_stack = (char *) cpu::self()->impl_ptr->current_thread.context->uc_stack.ss_sp;
        delete cpu::self()->impl_ptr->current_thread.context;
        delete cpu::self()->impl_ptr->current_thread.mutex_ptr;
        set_cpu_context_from_ready_queue();
    }

}



void suspend() {
	assert(cpu::self()->impl_ptr->next_thread.context == nullptr);
	//printf("@CPU %p suspended (suspend_context)\n", cpu::self());
	suspended_cpu_ptr.push(cpu::self());
	guard.store(false);//??
	//assert(cpu::self()->impl_ptr->next_thread.context == nullptr);
    cpu::interrupt_enable_suspend();
}



void cpu::init(thread_startfunc_t func, void *arg) {
    impl_ptr = new impl();

    cpu::interrupt_vector_table[IPI] = ipi_handler;
    cpu::interrupt_vector_table[TIMER] = timer_handler;
    if (!func) {
        while (init_guard.exchange(true)) {}
        init_guard.store(false);
        init_context(&impl_ptr->suspend_context);

        makecontext(&impl_ptr->suspend_context, suspend, 0);
    } else {

		guard.store(true);
        //num_cpu++;

        init_context(&impl_ptr->suspend_context);

        makecontext(&impl_ptr->suspend_context, suspend, 0);
        init_guard.store(false);
    }

    if (func) {
        ucontext_t *temp = new ucontext_t;
		
        //impl_ptr->current_thread.context = temp;

        init_context(temp);

        makecontext(temp, (void (*)()) thread_wrapper, 2, func, arg);
        cpu::impl_ptr->current_thread.set(temp, nullptr);

        setcontext(temp);

    } else {

		while (guard.exchange(true)) {}//modified

        if (ready_threads_info.empty())//do nothing
        {
			assert(cpu::self()->impl_ptr->next_thread.context == nullptr);
            guard_unlock_suspend();


        } else//switch
        {
            set_cpu_context_from_ready_queue();

        }
    }
    assert(false);
}






thread::thread(thread_startfunc_t func, void *arg) {
    impl_ptr = new impl();
    impl_ptr->info.context = new ucontext_t;
    impl_ptr->info.mutex_ptr=new mutex;
    init_context(impl_ptr->info.context);
    makecontext(impl_ptr->info.context, (void(*)()) thread_wrapper, 2, func, arg);

    guard_lock();
    impl_ptr->info.mutex_ptr->impl_ptr->real_lock();
    cpu::self()->impl_ptr->add_ready_thread(impl_ptr->info);//TODO: CHECK
    guard_unlock();
}

thread::~thread() { delete impl_ptr; }
void thread::join() {

    this->impl_ptr->info.mutex_ptr->lock();
    this->impl_ptr->info.mutex_ptr->unlock();



}
void thread::yield() {
    ipi_handler();
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
    impl_ptr->cv_queue.push(cpu::self()->impl_ptr->current_thread);
    cpu::self()->impl_ptr->current_thread.set(nullptr, nullptr);
    if(ready_threads_info.empty()) {
		

		assert(cpu::self()->impl_ptr->next_thread.context == nullptr);
        
		
        swapcontext(impl_ptr->cv_queue.back().context, &cpu::self()->impl_ptr->suspend_context);
    }
    else
    {
        //impl_ptr->cv_queue.push(cpu::self()->impl_ptr->current_thread);
        cpu::self()->impl_ptr->current_thread=ready_threads_info.front();
        ready_threads_info.pop();
        swapcontext(impl_ptr->cv_queue.back().context,cpu::self()->impl_ptr->current_thread.context);
    }


    guard_unlock();


    input_mutex.lock();
};
void cv::signal()
{
    guard_lock();
	if (!impl_ptr->cv_queue.empty())
	{

		assert(impl_ptr->cv_queue.front().context != nullptr);
		cpu::self()->impl_ptr->add_ready_thread(impl_ptr->cv_queue.front());
		impl_ptr->cv_queue.pop();
	}
    guard_unlock();
};
void cv::broadcast()
{
    guard_lock();
    while(!impl_ptr->cv_queue.empty()) {
		assert(impl_ptr->cv_queue.front().context != nullptr);
        cpu::self()->impl_ptr->add_ready_thread(impl_ptr->cv_queue.front());
        impl_ptr->cv_queue.pop();
    }
    guard_unlock();
};


//TODO:How to delete suspendcontext?��static

//TODO:seperate two interrupter,������fix small bug������