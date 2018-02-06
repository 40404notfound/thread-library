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

void ipi_handler() {
    timer_handler();
}

void delete_previous_context_if_needed() {

    if (cpu::self()->impl_ptr->previous_stack != nullptr) {
        delete cpu::self()->impl_ptr->previous_stack;
        cpu::self()->impl_ptr->previous_stack = nullptr;
    }
}

void timer_handler() {
    assert_interrupts_enabled();
    cpu::interrupt_disable();
    if (cpu::self()->impl_ptr->next_thread != nullptr)//get from previous cpu directly
    {

        cpu::self()->impl_ptr->current_thread = cpu::self()->impl_ptr->next_thread;
        cpu::self()->impl_ptr->next_thread = nullptr;
        cpu::self()->impl_ptr->no_guard_after_swap = true;
        //delete_previous_context_if_needed();

        setcontext(cpu::self()->impl_ptr->current_thread);
        //??

    } else {


        while (guard.exchange(true)) {}

        if (ready_threads_ptr.empty())//do nothing
        {
            guard.store(false);
            cpu::interrupt_enable();


        } else//switch
        {
            ready_threads_ptr.push(cpu::self()->impl_ptr->current_thread);
            cpu::self()->impl_ptr->current_thread = ready_threads_ptr.front();
            ready_threads_ptr.pop();
            swapcontext(
                    ready_threads_ptr.back(),
                    cpu::self()->impl_ptr->current_thread);


            //??
            if (!cpu::self()->impl_ptr->no_guard_after_swap.exchange(false))
                guard.store(false);

            delete_previous_context_if_needed();
            cpu::interrupt_enable();
        }


    }



    cpu::interrupt_enable();
}

void thread_wrapper(thread_startfunc_t func, void *arg)//TODO
{
    if (!cpu::self()->impl_ptr->no_guard_after_swap.exchange(false))
        guard.store(false);

    delete_previous_context_if_needed();
    cpu::interrupt_enable();

    //before
    func(arg);
    //after
    cpu::interrupt_disable();
    while (guard.exchange(true)) {}

    if (ready_threads_ptr.empty()) {
        cpu::self()->impl_ptr->previous_stack =(char*) cpu::self()->impl_ptr->current_thread->uc_stack.ss_sp;
        delete cpu::self()->impl_ptr->current_thread;
        cpu::self()->impl_ptr->current_thread = nullptr;
        suspended_cpu_ptr.push(cpu::self());
        guard.store(false);
        cpu::interrupt_enable_suspend();


    } else//switch
    {
        //ready_threads_ptr.push(cpu::self()->impl_ptr->current_thread);
        cpu::self()->impl_ptr->previous_stack =(char*) cpu::self()->impl_ptr->current_thread->uc_stack.ss_sp;
        delete cpu::self()->impl_ptr->current_thread;
        cpu::self()->impl_ptr->current_thread = ready_threads_ptr.front();
        ready_threads_ptr.pop();
        setcontext(cpu::self()->impl_ptr->current_thread);
    }

}


atomic<bool> init_guard(true);


void cpu::init(thread_startfunc_t func, void *arg) {
    cpu::interrupt_vector_table[IPI] = ipi_handler;
    cpu::interrupt_vector_table[TIMER] = timer_handler;
    if (!func) {
        while (init_guard.exchange(true)) {}
        init_guard.store(false);
    }
    else {
        guard.store(true);
        init_guard.store(false);
    }



    // impl_ptr->os_context=new ucontext_t;


    if (func) {
        ucontext_t *temp = new ucontext_t;
        impl_ptr->current_thread = temp;
        temp->uc_stack.ss_sp = new char[STACK_SIZE];
        temp->uc_stack.ss_size = STACK_SIZE;
        temp->uc_stack.ss_flags = 0;
        temp->uc_link = nullptr;//TODO
        makecontext(temp, thread_wrapper, 2, func, arg);
        cpu::impl_ptr->current_thread = temp;
        //cpu::impl_ptr->cpu_state=USER;
        //cpu::interrupt_enable()
        setcontext(temp);

    } else {
        while (guard.exchange(true)) {}

        if (ready_threads_ptr.empty())//do nothing
        {
            suspended_cpu_ptr.push(cpu::self());
            guard.store(false);
            cpu::interrupt_enable_suspend();


        } else//switch
        {
            //ready_threads_ptr.push(cpu::self()->impl_ptr->current_thread);
            cpu::self()->impl_ptr->current_thread = ready_threads_ptr.front();
            ready_threads_ptr.pop();
            setcontext(impl_ptr->current_thread);
        }
    }
    assert(false);
}


class cpu::impl {


public:

    cpu_state_t cpu_state = INIT;
    interrrupt_type_t interrrupt_type = NONE;
    char *previous_stack = nullptr;
    atomic<bool> no_guard_after_swap = atomic<bool>(false);

    ucontext_t *current_thread = nullptr;
    ucontext_t *next_thread = nullptr;

    void add_ready_thread(ucontext_t *input_thread) {
        cpu::interrupt_disable();
        while (guard.exchange(true)) {}


        if (!suspended_cpu_ptr.empty()) {
            //interrrupt_type=NEWTHREAD;
            auto temp = suspended_cpu_ptr.front();
            suspended_cpu_ptr.pop();
            temp->impl_ptr->next_thread = input_thread;
            temp->interrupt_send();

        } else {
            ready_threads_ptr.push(input_thread);
        }

        guard.store(false);

        cpu::interrupt_enable();
    }


};

thread::thread(thread_startfunc_t func, void *arg) {
    impl_ptr->context = new ucontext_t;
    impl_ptr->context->uc_stack.ss_sp = new char[STACK_SIZE];
    impl_ptr->context->uc_stack.ss_size = STACK_SIZE;
    impl_ptr->context->uc_stack.ss_flags = 0;
    impl_ptr->context->uc_link = nullptr;//TODO
    makecontext(impl_ptr->context, thread_wrapper, 2, func, arg);
    cpu::self()->impl_ptr->add_ready_thread(impl_ptr->context);
}

thread::~thread() {}//TODO
thread::join() {}//TODO
void thread::yield() {
    ipi_handler();
}//TODO
class thread::impl {
public:
    ucontext_t *context;
};//TODO

mutex::mutex() {}//TODO
mutex::~mutex() {}//TODO
void mutex::lock()//permission???
{

    cpu::interrupt_disable();

    while (impl_ptr->guard.exchange(true)) {}

    if (impl_ptr->locked) {
        impl_ptr->mutex_queue.push(new ucontext_t);//add to queue
        swapcontext(impl_ptr->mutex_queue.back(),)//TODO:switch to next thread
    } else {
        impl_ptr->locked = true;

    }
    impl_ptr->guard.store(false);
    cpu::interrupt_enable();

}//TODO
void mutex::unlock() //permission???
{


    cpu::interrupt_disable();
    while (impl_ptr->guard.exchange(true)) {}


    impl_ptr->locked = false;
    if (!impl_ptr->mutex_queue.empty()) {
        while (guard.exchange(true)) {}
        ready_threads_ptr.push(impl_ptr->mutex_queue.front());
        impl_ptr->guard.store(false);
        impl_ptr->mutex_queue.pop();
        impl_ptr->locked = true;
    }


    impl_ptr->guard.store(false);
    cpu::interrupt_enable();
}//TODO
class mutex::impl {
public:

    bool locked = false;
    atomic<bool> guard = atomic<bool>(false);

    queue<ucontext_t *> mutex_queue;

};//TODO

cv::cv();//TODO
cv::~cv();//TODO

void cv::wait(mutex &input_mutex) {
    input_mutex.unlock();


    input_mutex.lock();
};//TODO
void cv::signal();//TODO
void cv::broadcast();//TODO
class cv::impl {
public:
    queue<ucontext_t *> cv_queue;
    atomic<bool> guard = atomic<bool>(false);

};//TODO

//TODO:INSTANCE OF IMPL,STATIC