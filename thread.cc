#include "thread.h"
#include <ucontext.h>
#include <queue>
#include <set>
#include <exception>

std::queue<thread::impl*> ready_queue;             
std::queue<thread::impl*> idle_queue;               
std::set<cpu*> suspended_set;                       

void lock() {
    long long res;
    cpu::interrupt_disable();
    do {
        res = guard.exchange(1, std::memory_order_seq_cst); // return previous value
    } while (res);
}

void unlock() {
    guard.store(0, std::memory_order_seq_cst);
    cpu::interrupt_enable();
}

class lock_guard {
    lock_guard() {
        lock();
    }

    ~lock_guard() {
        unlock();
    }
};

void timer_handler() {

}

void ipi_handler() {

}

void wakeup_one_cpu() {
    if (!ready_queue.empty() && !suspended_set.empty()) {
        cpu* c = *suspended_set.begin();
        c->interrupt_send();
        suspended_set.erase(c);
    }
}

class cpu::impl {
    thread::impl* current_thd = nullptr;
public:
    static void run_next() {

    }
    static thread::impl* current() {
        return cpu::self()->impl_ptr->current_thd;
    }
}


void cpu::init(thread_startfunc_t fn, void* arg) {
    lock();
    assert_lib_mutex_locked();
    // initialize queue pointer
    try {
        impl_ptr = new cpu::impl;
    } catch (std::bad_alloc) {
        throw std::runtime_error("OOM");
    }
    interrupt_vector_table[CPU::TIMER] = timer_handler;
    interrupt_vector_table[CPU::IPI] = ipi_handler;
    if (fn) {
        thread cpu_main_thread{fn, arg};
    }
    thread cpu_idle_thread{idle_func, nullptr};
    cpu::impl::run_next();
}


class thread::impl {
    thread* parent;
    ucontext_t uc;
    char stack[STACK_SIZE];
    std::queue<thread::impl*> join_thd;
public:
    impl(thread* p_, thread_startfunc_t fn, void* arg) {
        parent = p_;
        getcontext(&uc);
        uc.uc_link = nullptr;
        uc.uc_stack.ss_sp = &stack;
        uc.uc_stack.ss_size = STACK_SIZE;
        uc.uc_stack.ss_flags = SS_DISABLE;
        makecontext(&uc, thread::impl::thread_start, 2, fn, arg);

        if (fn == idle_func) {
            idle_queue.push(this);
        } else {
            ready_queue.push(this);
            wakeup_one_cpu();
        }
    }

    static void thread_start(void(*fn)(void*), void* arg) {
        unlock();
        fn(arg);
        lock();
        auto current_thd = cpu::impl::current();
        while (true) {
            if (current_thd->join_thd.empty())
                break;
            auto parent_thd = current_thd->join_thd.front();
            ready_queue.push(parent_thd);
            wakeup_one_cpu();
            current_thd->join_thd.pop();
        }
        if (current_thd != nullptr)
            current_thd->parent->impl_ptr = nullptr;
        // how to delete
        cpu::impl::run_next();
    }

};




thread::thread(thread_startfunc_t fn, void* arg) {
    lock_guard lk;
    try {
        impl_ptr = new thread::impl(fn, arg);
    } catch (std::bad_alloc) {
        throw std::runtime_error("OOM");
    }
}


