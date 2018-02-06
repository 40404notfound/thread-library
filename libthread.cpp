#include <cassert>
#include "thread.h"
#include <queue>
#include <ucontext.h>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#define  ENTER_STATE(x) cpu::self()->impl_ptr->enter_state(x)
using namespace std;
enum cpu_state_t {OS,SUSPEND,USER,STARTING,INIT};
enum interrrupt_type_t {NEWTHREAD,NONE};

queue<ucontext_t *> ready_threads_ptr;
queue<cpu*>  suspended_cpu_ptr;
void timer_handler();
void ipi_handler();

void ipi_handler()
{
    timer_handler();
}


void timer_handler()
{
    assert_interrupts_enabled();
    cpu::interrupt_disable();
    if(cpu::self()->impl_ptr->next_thread!= nullptr)//get from previous cpu directly
    {
        cpu::self()->impl_ptr->current_thread=cpu::self()->impl_ptr->next_thread;
        cpu::self()->impl_ptr->next_thread= nullptr;
        setcontext(cpu::self()->impl_ptr->current_thread);
        //??

    } else
    {


        while(guard.exchange(true)){}

        if(ready_threads_ptr.empty())//do nothing
        {
            guard.store(false);
            cpu::interrupt_enable();


        } else//switch
        {
            ready_threads_ptr.push(cpu::self()->impl_ptr->current_thread);
            cpu::self()->impl_ptr->current_thread=ready_threads_ptr.front();
            ready_threads_ptr.pop();
            swapcontext(
                    ready_threads_ptr.back(),
                    cpu::self()->impl_ptr->current_thread);


            //??
            guard.store(false);
            cpu::interrupt_enable();
        }





    }




    cpu::interrupt_enable();
}

void thread_wrapper(thread_startfunc_t func, void *arg)//TODO
{
    guard.store(false);
    cpu::interrupt_enable();

    //before
    func(arg);
    //after
    cpu::interrupt_disable();
    while(guard.exchange(true)){}

    if(ready_threads_ptr.empty())
    {
        delete cpu::self()->impl_ptr->current_thread;
        cpu::self()->impl_ptr->current_thread= nullptr;
        suspended_cpu_ptr.push(cpu::self());
        guard.store(false);
        cpu::interrupt_enable_suspend();


    } else//switch
    {
        //ready_threads_ptr.push(cpu::self()->impl_ptr->current_thread);
        delete cpu::self()->impl_ptr->current_thread;
        cpu::self()->impl_ptr->current_thread=ready_threads_ptr.front();
        ready_threads_ptr.pop();
        setcontext(cpu::self()->impl_ptr->current_thread);
    }

}


atomic<bool> init_guard(true);



void cpu::init(thread_startfunc_t func, void * arg)
{
    cpu::interrupt_vector_table[IPI]=ipi_handler;
    cpu::interrupt_vector_table[TIMER]=timer_handler;
    if(!func) {while(init_guard.exchange(true)){} init_guard.store(false)}
    else  {guard.store(true);init_guard.store(false)}



   // impl_ptr->os_context=new ucontext_t;


    if(func) {
        ucontext_t * temp= new ucontext_t;
        impl_ptr->current_thread=temp;
        temp->uc_stack.ss_sp=new char [STACK_SIZE];
        temp->uc_stack.ss_size=STACK_SIZE;
        temp->uc_stack.ss_flags=0;
        temp->uc_link= nullptr;//TODO
        makecontext(temp,thread_wrapper,2,func,arg);
        cpu::impl_ptr->current_thread=temp;
        //cpu::impl_ptr->cpu_state=USER;
        //cpu::interrupt_enable()
        setcontext(temp);

    }
    else
    {
        while(guard.exchange(true)){}

        if(ready_threads_ptr.empty())//do nothing
        {
            suspended_cpu_ptr.push(cpu::self());
            guard.store(false);
            cpu::interrupt_enable_suspend();


        } else//switch
        {
            //ready_threads_ptr.push(cpu::self()->impl_ptr->current_thread);
            cpu::self()->impl_ptr->current_thread=ready_threads_ptr.front();
            ready_threads_ptr.pop();
            setcontext(impl_ptr->current_thread);
        }
    }

//    getcontext(impl_ptr->os_context);
//    assert_interrupts_disabled();
//
//
//        //cpu::interrupt_disable();
//        while(guard.exchange(true)){}
//        if(ready_threads_ptr.empty()) {
//
//
//            suspended_cpu_ptr.push_back(cpu::self());
//            //ENTER_STATE(SUSPEND);
//
//            cpu::self()->impl_ptr->cpu_state=SUSPEND;
//            guard.store(0);
//            interrupt_enable_suspend();
//        }
//        else {
//
//            auto temp = ready_threads_ptr.front();
//            ready_threads_ptr.pop();
//            guard.store(0);
//            //ENTER_STATE(USER);
//            impl_ptr->current_thread=temp;
//            cpu::self()->impl_ptr->cpu_state=USER;
//            setcontext(temp);
//        }



    assert(false);

     //should not return
}



class cpu::impl
{


    public:
    //ucontext_t * os_context;

    cpu_state_t  cpu_state=INIT;
    interrrupt_type_t interrrupt_type=NONE;

    ucontext_t* current_thread= nullptr;
    ucontext_t* next_thread= nullptr;
    void add_ready_thread(ucontext_t* input_thread)
    {
        //ASSERTION
        //assert(cpu::self()->impl_ptr->cpu_state==USER ||cpu::self()->impl_ptr->cpu_state==OS);
        //if(cpu::self()->impl_ptr->cpu_state==USER) assert_interrupts_enabled();
        //END ASSERTION
        cpu::interrupt_disable();
        while (guard.exchange(true)) {}



        if(!suspended_cpu_ptr.empty()) {
            //interrrupt_type=NEWTHREAD;
            auto temp=suspended_cpu_ptr.front();
            suspended_cpu_ptr.pop();
            temp->impl_ptr->next_thread=input_thread;
            temp->interrupt_send();

        } else
        {
            ready_threads_ptr.push(input_thread);
        }

        guard.store(false);

        cpu::interrupt_enable();
    }



};

thread::thread(thread_startfunc_t func, void * arg)
{


    impl_ptr->context=new ucontext_t;
    impl_ptr->context->uc_stack.ss_sp=new char [STACK_SIZE];
    impl_ptr->context->uc_stack.ss_size=STACK_SIZE;
    impl_ptr->context->uc_stack.ss_flags=0;
    impl_ptr->context->uc_link= nullptr;//TODO
    makecontext(impl_ptr->context,thread_wrapper,2,func,arg);
    cpu::self()->impl_ptr->add_ready_thread(impl_ptr->context);
}

thread::~thread() {}//TODO
thread::join() {}//TODO
void thread::yield()
{
    ipi_handler();
}//TODO
class thread::impl
{
public:
    ucontext_t* context;
};//TODO

mutex::mutex() {}//TODO
mutex::~mutex() {}//TODO
void mutex::lock()//permission???
{

    cpu::interrupt_disable();

    while(impl_ptr->guard.exchange(true)){}

    if(impl_ptr->locked) {
        impl_ptr->mutex_queue.push(new ucontext_t);//add to queue
        swapcontext(impl_ptr->mutex_queue.back(),)//TODO:switch to next thread
    }
    else
    {
        impl_ptr->locked = true;

    }
        impl_ptr->guard.store(false);
    cpu::interrupt_enable();

}//TODO
void mutex::unlock() //permission???
{


    cpu::interrupt_disable();
    while(impl_ptr->guard.exchange(true)){}


    impl_ptr->locked = false;
    if(!impl_ptr->mutex_queue.empty()) {
        while(guard.exchange(true)){}
        ready_threads_ptr.push(impl_ptr->mutex_queue.front());
        impl_ptr->guard.store(false);
        impl_ptr->mutex_queue.pop();
        impl_ptr->locked = true;
    }


    impl_ptr->guard.store(false);
    cpu::interrupt_enable();
}//TODO
class mutex::impl
{
public:

    bool locked=false;
    atomic<bool> guard=atomic<bool>(false);

    queue<ucontext_t*> mutex_queue;

};//TODO

cv::cv();//TODO
cv::~cv();//TODO

void cv::wait(mutex& input_mutex)
{
    input_mutex.unlock();



    input_mutex.lock();
};//TODO
void cv::signal();//TODO
void cv::broadcast();//TODO
class cv::impl
{
public:
    queue<ucontext_t*> cv_queue;
    atomic<bool> guard=atomic<bool>(false);

};//TODO

//TODO:INSTANCE OF IMPL,STATIC