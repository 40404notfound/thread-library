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

void ipi_handler()
{
    assert(cpu::self()->impl_ptr->interrrupt_type!=NONE);
    if(cpu::self()->impl_ptr->interrrupt_type==NEWTHREAD)
    {



        cpu::interrupt_disable();
        while(guard.exchange(true)){}

        if(!ready_threads_ptr.empty()) {
            auto temp = ready_threads_ptr.front();
            ready_threads_ptr.pop();

            guard.store(false);
            cpu::interrupt_enable();

            setcontext(temp);
        }
        else
        {
            suspended_cpu_ptr.push(cpu::self());
            guard.store(false);
            cpu::interrupt_enable_suspend();
        }




    }



}
void timer_handler()
{


}




void cpu::init(thread_startfunc_t func, void * arg)
{
    cpu::interrupt_vector_table[IPI]=ipi_handler;
    cpu::interrupt_vector_table[TIMER]=timer_handler;


   // impl_ptr->os_context=new ucontext_t;


    if(func) {
        ucontext_t * temp= new ucontext_t;
        temp=new ucontext_t;
        temp->uc_stack.ss_sp=new char [STACK_SIZE];
        temp->uc_stack.ss_size=STACK_SIZE;
        temp->uc_stack.ss_flags=0;
        temp->uc_link= nullptr;//TODO
        makecontext(temp,func,1,arg);
        cpu::impl_ptr->cpu_state=USER;
        setcontext(temp);

    }
    else
    {

        while(guard.exchange(true)){}
        suspended_cpu_ptr.push(cpu::self());
        guard.store(false);
        cpu::interrupt_enable_suspend();
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

   // ucontext_t* current_thread;
    void add_ready_thread(ucontext_t* input_thread)
    {
        //ASSERTION
        //assert(cpu::self()->impl_ptr->cpu_state==USER ||cpu::self()->impl_ptr->cpu_state==OS);
        //if(cpu::self()->impl_ptr->cpu_state==USER) assert_interrupts_enabled();
        //END ASSERTION
        cpu::interrupt_disable();
        while (guard.exchange(true)) {}


        ready_threads_ptr.push(input_thread);
        if(!suspended_cpu_ptr.empty()) {
            interrrupt_type=NEWTHREAD;
            suspended_cpu_ptr.front()->interrupt_send();

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
    makecontext(impl_ptr->context,func,1,arg);
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

//TODO:INSTANCE OF IMPL,