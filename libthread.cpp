#include <cassert>
#include "thread.h"
#include <queue>
#include <ucontext.h>

using namespace std;
enum cpu_state_t {
    OS, SUSPEND, USER, STARTING, INIT
};
enum interrrupt_type_t {
    NEWTHREAD, NONE
};

queue<ucontext_t *> ready_threads_ptr;
queue<cpu *> suspended_cpu_ptr;

void timer_handler();

void ipi_handler();
void delete_previous_context_if_needed();
void guard_lock();
void guard_unlock();
void set_cpu_context_from_ready_queue();

void set_cpu_context_from_ready_queue()
{

    cpu::self()->impl_ptr->current_thread = ready_threads_ptr.front();
    ready_threads_ptr.pop();
    setcontext(cpu::self()->impl_ptr->current_thread);
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
suspended_cpu_ptr.push(cpu::self());
guard.store(false);
cpu::interrupt_enable_suspend();

}

void ipi_handler() {
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

    if (cpu::self()->impl_ptr->next_thread != nullptr)//get from previous cpu directly
    {

        cpu::self()->impl_ptr->current_thread = cpu::self()->impl_ptr->next_thread;
        cpu::self()->impl_ptr->next_thread = nullptr;
        //cpu::self()->impl_ptr->no_guard_after_swap = true;

        setcontext(cpu::self()->impl_ptr->current_thread);


    } else {

        if (!ready_threads_ptr.empty())
        {
            ready_threads_ptr.push(cpu::self()->impl_ptr->current_thread);
            cpu::self()->impl_ptr->current_thread = ready_threads_ptr.front();
            ready_threads_ptr.pop();
            swapcontext(
                    ready_threads_ptr.back(),
                    cpu::self()->impl_ptr->current_thread);
        }
        guard_unlock();


    }


    cpu::interrupt_enable();
}

void thread_wrapper(thread_startfunc_t func, void *arg)//TODO
{
    guard_unlock();
    //before
    func(arg);
    //after
    guard_lock();

    if (ready_threads_ptr.empty()) {
        cpu::self()->impl_ptr->previous_stack = (char *) cpu::self()->impl_ptr->current_thread->uc_stack.ss_sp;
        delete cpu::self()->impl_ptr->current_thread;
        cpu::self()->impl_ptr->current_thread = nullptr;

        guard_unlock_suspend();


    } else//switch
    {
        cpu::self()->impl_ptr->previous_stack = (char *) cpu::self()->impl_ptr->current_thread->uc_stack.ss_sp;
        delete cpu::self()->impl_ptr->current_thread;


        set_cpu_context_from_ready_queue();
    }

}

void init_context(ucontext_t* input)
{
    input->uc_stack.ss_sp = new char[STACK_SIZE];
    input->uc_stack.ss_size = STACK_SIZE;
    input->uc_stack.ss_flags = 0;
    input->uc_link = nullptr;//TODO

}

void suspend() {
    cpu::interrupt_enable_suspend();
}

atomic<bool> init_guard(true);
ucontext suspend_context;

void cpu::init(thread_startfunc_t func, void *arg) {
    impl_ptr = new impl();

    cpu::interrupt_vector_table[IPI] = ipi_handler;
    cpu::interrupt_vector_table[TIMER] = timer_handler;
    if (!func) {
        while (init_guard.exchange(true)) {}
        init_guard.store(false);
    } else {

        guard_lock();

        init_context(&suspend_context);

        makecontext(&suspend_context, suspend, 0);
        init_guard.store(false);
    }
    if (func) {
        ucontext_t *temp = new ucontext_t;
        impl_ptr->current_thread = temp;

        init_context(temp);

        makecontext(temp, thread_wrapper, 2, func, arg);
        cpu::impl_ptr->current_thread = temp;

        setcontext(temp);

    } else {

        guard_lock();

        if (ready_threads_ptr.empty())//do nothing
        {
            guard_unlock_suspend();


        } else//switch
        {
            set_cpu_context_from_ready_queue();

        }
    }
    assert(false);
}


class cpu::impl {


public:

    char *previous_stack = nullptr;
    ucontext_t *current_thread = nullptr;
    ucontext_t *next_thread = nullptr;

    void add_ready_thread(ucontext_t *input_thread) {


        if (!suspended_cpu_ptr.empty()) {
            auto temp = suspended_cpu_ptr.front();
            suspended_cpu_ptr.pop();
            temp->impl_ptr->next_thread = input_thread;
            temp->interrupt_send();

        } else {
            ready_threads_ptr.push(input_thread);
        }


    }


};

thread::thread(thread_startfunc_t func, void *arg) {
    impl_ptr = new impl();
    impl_ptr->context = new ucontext_t;
    init_context(impl_ptr->context);
    makecontext(impl_ptr->context, thread_wrapper, 2, func, arg);

    guard_lock();
    cpu::self()->impl_ptr->add_ready_thread(impl_ptr->context);
    guard_unlock();
}

thread::~thread() { delete impl_ptr; }
thread::join() {


}//TODO
void thread::yield() {
    ipi_handler();
}
class thread::impl {
public:
    ucontext_t *context;
};

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


class mutex::impl {
public:

    bool locked = false;
    queue<ucontext_t *> mutex_queue;
    void real_unlock()
    {
        assert(locked);

        locked = false;
        if (!mutex_queue.empty()) {
            while (guard.exchange(true)) {}
            cpu::self()->impl_ptr->add_ready_thread(mutex_queue.front());
            mutex_queue.pop();
            locked = true;
        }

    }


    void real_lock()
    {
        if (locked) {
            mutex_queue.push(cpu::self()->impl_ptr->current_thread);//add to queue
            cpu::self()->impl_ptr->current_thread = nullptr;

            if (ready_threads_ptr.empty()) {
                suspended_cpu_ptr.push(cpu::self());
                guard.store(false);
                swapcontext(mutex_queue.back(), &suspend_context);
            } else {
                cpu::self()->impl_ptr->current_thread = ready_threads_ptr.front();
                ready_threads_ptr.pop();
                swapcontext(mutex_queue.back(), cpu::self()->impl_ptr->current_thread);
            }


        }

        else
        {
            locked = true;
        }
    }

};



cv::cv(){impl_ptr = new impl();};
cv::~cv(){delete impl_ptr;};

void cv::wait(mutex &input_mutex) {
    guard_lock();

    input_mutex.impl_ptr->real_unlock();//make atomic



    impl_ptr->cv_queue.push(cpu::self()->impl_ptr->current_thread);
    cpu::self()->impl_ptr->current_thread=nullptr;
    if(ready_threads_ptr.empty()) {
        guard.store(false);
        swapcontext(impl_ptr->cv_queue.back(), &suspend_context);
    }
    else
    {
        impl_ptr->cv_queue.push(cpu::self()->impl_ptr->current_thread);
        cpu::self()->impl_ptr->current_thread=ready_threads_ptr.front();
        ready_threads_ptr.pop();
        swapcontext(impl_ptr->cv_queue.back(),cpu::self()->impl_ptr->current_thread);
    }


    guard_unlock();


    input_mutex.lock();
};
void cv::signal()
{
    guard_lock();
    cpu::self()->impl_ptr->add_ready_thread(impl_ptr->cv_queue.front());
    impl_ptr->cv_queue.pop();
    guard_unlock();
};
void cv::broadcast()
{
    guard_lock();
    while(!impl_ptr->cv_queue.empty()) {
        cpu::self()->impl_ptr->add_ready_thread(impl_ptr->cv_queue.front());
        impl_ptr->cv_queue.pop();
    }
    guard_unlock();
};
class cv::impl {
public:
    queue<ucontext_t *> cv_queue;
    atomic<bool> guard = atomic<bool>(false);

};

//TODO:STATIC,EXIT