#include "thread.h"
#include <ucontext.h>
#include <queue>
#include <iostream>
#include <set>
#include <cassert>
// not <exception>
#include <stdexcept>
#include <cstdlib>

// @TODO: setup global memory recycle
// @TODO: rewrite into transfer lock style instead of assert outer lock style
// @TODO: move ctor/operator of mutex/cv, so easy(?)

std::queue<thread::impl*> ready_queue;             
std::queue<thread::impl*> idle_queue;               
std::set<cpu*> suspended_set;
thread::impl* last_free_thread = nullptr;                       

/*
// actually libcpu has this
void* operator new(size_t size) {
    void* p = malloc(size);
    if (!p) {
        throw std::runtime_error("OOM");
    }
    return p;
}
*/

void assert_lib_mutex_locked() {
    // assert_interrupts_private("Test locked", 0, true);
    // assert(guard.exchange(1));
}

void lock() {
    cpu::interrupt_disable();
    while(guard.exchange(1, std::memory_order_seq_cst));
}

void unlock() {
    guard.store(0, std::memory_order_seq_cst);
    cpu::interrupt_enable();
}

class lock_guard {
public:
    lock_guard() {
        lock();
    }

    ~lock_guard() {
        unlock();
    }
};

// [[enter as locked]]
void wakeup_one_cpu() {
#ifdef COUT_DEBUG
    std::cout << "wake one\n";
    std::cout << ready_queue.size() << '\n';
    std::cout << idle_queue.size() << '\n';
    std::cout << suspended_set.size() << '\n';
#endif
    assert_lib_mutex_locked();    
    if (!ready_queue.empty() && !suspended_set.empty()) {
        cpu* c = *suspended_set.begin();
        c->interrupt_send();
        suspended_set.erase(c);
    }
}

class thread::impl {
    thread* parent;
    ucontext_t uc;
    char stack[STACK_SIZE];
    std::queue<thread::impl*> join_thd;
    friend class cpu::impl;
    friend class thread;
public:
    impl(thread* p_, thread_startfunc_t fn, void* arg);
    static void thread_start(void(*fn)(void*), void* arg);
    ~impl();
};

class cpu::impl {
    thread::impl* current_thd = nullptr;
public:
    static void run_next() {
#ifdef COUT_DEBUG
        std::cout << "run_next\n";
        std::cout << ready_queue.size() << '\n';
        std::cout << idle_queue.size() << '\n';
        std::cout << suspended_set.size() << '\n';
#endif  
        assert_lib_mutex_locked();
        thread::impl* old_thd = cpu::impl::current();
        thread::impl** cur_thd_p = &cpu::self()->impl_ptr->current_thd;
        if (!ready_queue.empty()) {
            *cur_thd_p = ready_queue.front();
            ready_queue.pop();
        } else {
            *cur_thd_p = idle_queue.front();
            idle_queue.pop();
        }
        thread::impl* cur_thd = *cur_thd_p;
        if (old_thd) {
            swapcontext(&old_thd->uc, &cur_thd->uc); 
        } else {
            setcontext(&cur_thd->uc); 
        }
        delete last_free_thread;
        last_free_thread = nullptr;
    }
    static thread::impl* current() {
        assert_lib_mutex_locked();
        return cpu::self()->impl_ptr->current_thd;
    }
};

// boot thread and idle threads are released
void idle_func(void*) {
    lock();
    while (true) {
        thread::impl* ti = cpu::impl::current();
        idle_queue.push(ti);
        cpu::impl::run_next();
        suspended_set.insert(cpu::self());
        guard.store(0, std::memory_order_seq_cst);
        cpu::interrupt_enable_suspend();
        lock();
    }
}

void timer_handler() {
    lock_guard gd;
    if (!ready_queue.empty()) {
        ready_queue.push(cpu::impl::current());
        cpu::impl::run_next();
    }
}

void ipi_handler() {
    // Just a empty implementation is ok.
}

void cpu::init(thread_startfunc_t fn, void* arg) {
    while(guard.exchange(1, std::memory_order_seq_cst))
        ;
    impl_ptr = new cpu::impl;
    interrupt_vector_table[cpu::TIMER] = ipi_handler;
    interrupt_vector_table[cpu::IPI] = ipi_handler;
    unlock(); // also enable interrupt
    if (fn) {
        thread cpu_main_thread{fn, arg};
    }
    thread cpu_idle_thread{idle_func, nullptr};
    lock();
    interrupt_vector_table[cpu::TIMER] = timer_handler;
    cpu::impl::run_next();
}



thread::impl::impl(thread* p_, thread_startfunc_t fn, void* arg) {
    if (fn == nullptr) {
        throw std::runtime_error("nullptr function");
    }
    parent = p_;
    getcontext(&uc);
    uc.uc_link = nullptr;
    uc.uc_stack.ss_sp = &stack;
    uc.uc_stack.ss_size = STACK_SIZE;
    uc.uc_stack.ss_flags = SS_DISABLE;
    makecontext(&uc, reinterpret_cast<void(*)()>(thread::impl::thread_start), 2, fn, arg);

    if (fn == idle_func) {
        idle_queue.push(this);
    } else {
        ready_queue.push(this);
        wakeup_one_cpu();
    }
}

void thread::impl::thread_start(void(*fn)(void*), void* arg) {
    delete last_free_thread;
    last_free_thread = nullptr;
    unlock();
    fn(arg);
    lock();
    auto current_thd = cpu::impl::current();
    while (!current_thd->join_thd.empty()) {
        auto parent_thd = current_thd->join_thd.front();
        ready_queue.push(parent_thd);
        wakeup_one_cpu();
        current_thd->join_thd.pop();
    }
    if (current_thd->parent != nullptr)
        current_thd->parent->impl_ptr = nullptr;
    last_free_thread = current_thd;
    cpu::impl::run_next();
}

thread::impl::~impl() {
    if (parent != nullptr) {
        parent->impl_ptr = nullptr;
    }
}


thread::thread(thread_startfunc_t fn, void* arg) {
    lock_guard lk;
    impl_ptr = new thread::impl{this, fn, arg};
}

thread::~thread() {
    lock_guard gd;
    if (this->impl_ptr != nullptr)
        this->impl_ptr->parent = nullptr;
}

// operator= is same
thread::thread(thread&& other) {
    lock_guard gd;
    impl_ptr = other.impl_ptr;
    other.impl_ptr = nullptr;
    if (impl_ptr != nullptr) {
        impl_ptr->parent = this;
    }
}

thread& thread::operator=(thread&& other) {
    lock_guard gd;
    impl_ptr = other.impl_ptr;
    other.impl_ptr = nullptr;
    if (impl_ptr != nullptr) {
        impl_ptr->parent = this;
    }
    return *this;
}

void thread::join() {
    lock_guard gd;
    if (this->impl_ptr != nullptr) {
        this->impl_ptr->join_thd.push(cpu::impl::current());
        cpu::impl::run_next();
    }
}

void thread::yield() {
    lock_guard gd;
    if (!ready_queue.empty()) {
        ready_queue.push(cpu::impl::current());
        cpu::impl::run_next();
    }
}

struct mutex::impl {
    void lock() {
        if (own_thd != nullptr) {
            thd_q.push(cpu::impl::current());
            cpu::impl::run_next();
        } else {
            own_thd = cpu::impl::current();
        }
    }
    void unlock() {
        if (own_thd != cpu::impl::current()) {
            throw std::runtime_error("Thread tried to release mutex it didn't own");
        }
        auto cur_thd = cpu::impl::current();
        own_thd = nullptr;
        if (!thd_q.empty()) {
            own_thd = thd_q.front();
            thd_q.pop();
            ready_queue.push(own_thd);
            wakeup_one_cpu();
        }
    }
    thread::impl* own_thd = nullptr; 
    std::queue<thread::impl*> thd_q; // locked thread
};

mutex::mutex() {
    lock_guard lk;
    impl_ptr = new mutex::impl;
}

void mutex::lock() {
    lock_guard lk;
    impl_ptr->lock();
}

void mutex::unlock() {
    lock_guard lk;
    impl_ptr->unlock();
}

mutex::~mutex() {
    lock_guard lk;
    delete impl_ptr;
}


struct cv::impl {
    void wait(mutex::impl* mtx) {
        mtx->unlock();
        thd_q.push(cpu::impl::current());
        cpu::impl::run_next();
        mtx->lock();
    }
    void signal(bool is_broadcast) {
        do {
            if (thd_q.empty())
                break;
            ready_queue.push(thd_q.front());
            thd_q.pop();
            wakeup_one_cpu();
        } while (is_broadcast == 1);
    }
    std::queue<thread::impl*> thd_q; // waiting thread
};

cv::cv() {
    lock_guard lk;
    impl_ptr = new cv::impl;
}

cv::~cv() {
    lock_guard lk;
    delete impl_ptr;
}

void cv::signal() {
    lock_guard lk;
    impl_ptr->signal(false);
}

void cv::broadcast() {
    lock_guard lk;
    impl_ptr->signal(true);
}

void cv::wait(mutex& mtx) {
    lock_guard lk;
    impl_ptr->wait(mtx.impl_ptr);
}