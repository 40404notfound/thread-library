#include "thread.h"
#include <queue>
#include <set>
#include <cassert>
#include <stdexcept>
#include <cstdlib>
#include <ucontext.h>
#include <pthread.h>
#include <atomic>
using namespace std;

using byte_t = unsigned char;

thread_local int64_t my_cpu_id = 16;                // DE50 
int64_t rand;                                       // DE60 (getpid on random or 1 if deterministic)
// ios_base
std::queue<thread::impl*> ready_queue;              // DE88 (actually should be a pointer)
std::queue<thread::impl*> idle_queue;               // DE90
std::set<cpu*> suspended_set;                       // DE98
thread::impl* wtf;                                  // DEA0 (actually last_executed_thread)
void* malloc_ptr;                                   // init by dlsym(RTLD_NEXT) in init_symbols
void* free_ptr;
void* realloc_ptr;
void* calloc_ptr;
void* cfree_ptr;
void* getenv_ptr;
bool is_symbol_inited;                              // DED9
// ios_base
pthread_mutex_t infra_mutex;                        // DEE0
static std::atomic<bool> guard;                     // DF08
int context_switch_count;
// ios_base
void(*boot_func)(void*);                            // DF18
void* boot_arg;                                     // DF20
thread_local bool is_current_cpu_booted;            // DF28 (only in helper, should be thread local)
cpu_infra_s cpu_infra[16]; // CPUID corresponding interrupt // DF40 - E340
static std::map<cpu*, int> cpu_to_id;               // E340
// long padding
pthread_cond_t cond;                                // E380
unsigned int num_cpus;                              // E3B0 num of booted (4 or 1)
unsigned int num_cpus_inited;                       // E3B4 num of CPU instances
bool is_cpu_inited;                                 // E3B8
bool is_cpu_deter;                                  // E3B9
bool is_all_cpu_booted;                             // E3BA set 1 always
int num_thread_end;                                 // E3BC (actually num of cpu end)
// end: when cpu call thread_yield 
std::atomic<bool> is_cpu_boot_called;               // E3C0
void* setcontext_fn;                                // E3C8 handler of setcontext by dlsym
void* swapcontext_fn                                // E3D0 handler of swapcontext by dlsym

void* operator new(unsigned int size) {
    void* p = malloc(size);
    if (!p) {
        throw std::runtime_error("OOM");
    }
    return p;
}

struct cpu_infra_s {
    cpu* p_cpu; // 8 bytes
    std::atomic<int> memlock_cnt; // 4 bytes
    bool interrupts_are_disabled = 1;
    bool is_last_interrupt_missed = 0;
    bool is_suspended = 0; // is_current_thread_ended
    pthread_cond_t status; // 48 bytes
};

#define BYTE4(cpu_i) cpu_i.interrupts_are_disabled
#define BYTE5(cpu_i) cpu_i.is_last_interrupt_missed
#define BYTE6(cpu_i) cpu_i.is_suspended

void verify_ucontext(const ucontext_t* uc, const char* str) {
    if (my_cpu_id != 16) {
        infra_mutex_lock();
        ++context_switch_count;
        if (uc->uc_mcontext.fpregs != & uc->__fpregs_mem) {
            std::cout << "Bad context passed to " << str << "\n Is this context a copy of context formed by getcontext/makecontext?\n";
            assert(false);
        }
        if (!BYTE4(cpu_infra[my_cpu_id]) && is_cpu_deter || num_cpus > 1 && !guard.load(1, std::memory_order_seq_cst)) {
            assert(false);
        }
        infra_mutex_unlock();
    }
}

void sigusr1_handler(int sig) {
    assert(sig == 10);
    if (my_cpu_id != 1) {
        infra_mutex_lock();
        if (my_cpu_id == 16 || BYTE6(cpu_infra[my_cpu_id)) || cpu_infra[my_cpu_id].memlock_cnt > 0) {
            infra_mutex_unlock();
        } else if (BYTE4(cpu_infra[my_cpu_id])) {
            infra_mutex_unlock();
        } else {
            infra_mutex_unlock();
            cpu* c = cpu_infra[my_cpu_id].p_cpu;
            c->interrupt_vector_table[0]();
        }
    }
}

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

int thread_yield() {
    lock_guard gd;
    if (ready_queue.empty()) {
        assert(cpu::impl::current() != nullptr);
        ready_queue.push(cpu::impl::current());
        cpu::impl::run_next();
    }
}

void infra_mutex_lock() {
    assert((char)my_cpu_id); // !sigusr1_disable
    my_cpu_id &= 0xFFFFFFF0;
    my_cpu_id |= 1;
    pthread_mutex_lock(&infra_mutex);
}

void infra_mutex_unlock() {
    pthread_mutex_unlock(&infra_mutex);
    assert(!(char)my_cpu_id);
    my_cpu_id &= 0xFFFFFFF0;
    my_cpu_id |= 0;
}

int64_t infra_rand() {
    int64_t tmp = 16807 * rand;
    int64_t tmp2 = tmp - (8589934597 * tmp >> 64);
    rand = tmp - 0x7FFFFFFF * (tmp2 >> 30);
    if (!rand) {
        rand = 1;
    }
    return rand;
}

struct lock_guard {
public:
    lock_guard() {
        lock();    
    }
    ~lock_guard() {
        unlock();
    }
};

void assert_guard_locked() {
    assert(!guard.exchange(1));
}

void assert_lib_mutex_locked() {
    assert_interrupts_private("thread.cc", 0x109, true);
    assert_guard_locked();
}

void ipi_handler() {}

void cpu::init(thread_startfunc_t fn, void* arg) {
    lock();
    assert_lib_mutex_locked();
    // initialize queue pointer
    impl_ptr = new cpu::impl;
    interrupt_vector_table[CPU::TIMER] = ipi_handler;
    interrupt_vector_table[CPU::IPI] = ipi_handler;
    unlock();
    if (fn) {
        thread cpu_main_thread{fn, arg};
    }
    thread cpu_idle_thread{idle_func, nullptr};
    lock();
    interrupt_vector_table[CPU::TIMER] = thread_yield;
    interrupt_vector_table[CPU::IPI] = ipi_handler;
    cpu::impl::run_next();
    assert(false);
}

struct cpu::impl {
    static void run_next() {
        assert_lib_mutex_locked();
        thread::impl* old_thd = cpu::impl::current();
        thread::impl** cur_thd_p = &cpu::impl::current();
        if (!ready_queue.empty()) {
            *cur_thd_p = ready_queue.front();
            ready_queue.pop();
        } else {
            assert(!idle_queue.empty());
            *cur_thd_p = idle_queue.front();
            assert(*cur_thd_p != nullptr);
            idle_queue.pop();
        }
        thread::impl* cur_thd = *cur_thd_p;
        assert(cur_thd != nullptr);
        if (old_thd) {
            // getcontext(&old_thd->uc)
            // setcontext(&cur_thd->uc)
            swapcontext(&old_thd->uc, &cur_thd->uc); // noreturn function
            // perror(...)
        } else {
            setcontext(&cur_thd->uc); // no return function
            // perror(...)
        }
        thread::impl::check_delete();
    }

    static thread::impl* current() {
        return cpu::self()->impl_ptr->cur_thd;
    }

    thread::impl* cur_thd;
};

struct mutex::impl {
    // push to thd_q
    // push self to cpu::impl::current()->mtx_set
    void lock() {
        assert_lib_mutex_locked();
        if (is_locked || own_thd != nullptr) {
            thd_q.push_back(cpu::impl::current());
            cpu::impl::run_next();
        } else {
            own_thd = cpu::impl::current();
        }
        assert(own_thd == cpu::impl::current());
        cpu::impl::current()->mtx_set.insert(this);
    }
    // test own_thd
    // cpu::impl::current()->mtx_set erase self
    // randomly reorder thd_q
    // pop 1 and push to ready_queue    
    void unlock() {
        assert_lib_mutex_locked();
        if (is_locked && own_thd != cpu::impl::current()) {
            throw std::runtime_error("Thread tried to release mutex it didn't own");
        }
        auto cur_thd = cpu::impl::current();
        assert(cur_thd->mtx_set.find(this) != cur_thd->mtx_set.end());
        cur_thd->mtx_set.erase(this);
        own_thd = nullptr;
        if (!thd_q.empty()) {
            do {
                infra_mutex_lock();
                auto v2 = infra_rand();
                infra_mutex_unlock();
                thd_q.push(thd_q.front());
                thd_q.pop();
            } while (v2 % 3);
        }
        if (!thd_q.empty()) {
            own_thd = thd_q.front();
            thd_q.pop();
            ready_queue.push_back(own_thd);
            wakeup_one_cpu();
        }
    }

    static void move(mutex* p, mutex* q) {
        q->impl_ptr = p->impl_ptr;
        p->impl_ptr = nullptr;
    }
    bool is_locked = false;
    thread::impl* own_thd = nullptr; 
    std::queue<thread::impl*> thd_q; // wait thread
};

struct cv::impl {
    // push to thd_q
    void wait(mutex::impl& mtx) {
        assert_lib_mutex_locked();
        mtx.unlock();
        thq_q.push_back(cpu::impl::current());
        cpu::impl::run_next();
        mtx.lock();
    }
    // randomly reorder thd_q
    // clear and push to ready_queue
    // true => broadcast, all / false => signal, one
    void signal(bool is_broadcast) {
        assert_lib_mutex_locked();
        if (!thd_q.empty()) {
            do {
                infra_mutex_lock();
                auto v2 = infra_rand();
                infra_mutex_unlock();
                thd_q.push(thd_q.front());
                thd_q.pop();
            } while (v2 % 3);
        }
        while (is_broadcast == 1) {
            if (thd_q.empty())
                break;
            ready_queue.push_back(thd_q.front());
            thd_q.pop();
            wakeup_one_cpu();
        }
    }
    // here reverse order...
    static void move(cv* p, cv* q) {
        q->impl_ptr = p->impl_ptr;
        p->impl_ptr = nullptr;
    }
    std::queue<thread::impl*> thd_q; // waiting thread
};

// Try to push cpu which are exeucting idle_func to suspended_set
void idle_func(void*) {
    lock();
    while (true) {
        thread::impl* ti = cpu::impl::current();
        idle_queue.push(ti);
        cpu::impl::run_next();
        std::set<cpu*>::iterator ss = suspended_set.find(cpu::self());
        if (ss != suspended_set.end()) {
            break;
        }
        cpu* v9 = suspended_set.insert(cpu::self());;
        suspended_set.insert(v9);
        guard.store(0, std::memory_order_seq_cst); // just unlock
        cpu::interrupt_enable_suspend();
        lock();
    }
    assert(false);
}

// wakeup idle cpu
void wakeup_one_cpu() {
    assert_lib_mutex_locked();
    if (!ready_queue.empty() && !suspended_set.empty()) {
        cpu* c = *suspended_set.begin();
        c->interrupt_send();
        suspended_set.erase(c);
    }
}


thread::thread(void(*f)(void*), void* arg) {
    if (f == nullptr) {
        throw std::runtime_error("Tried to construct thread with a null function pointer");
    }
    lock();
    this->impl_ptr = new thread::impl(this, f, arg);
    unlock();
    infra_mutex_lock();
    int64_t res = infra_rand();
    infra_mutex_unlocked();
    if (res % 5) {
        assert_interrupts_private("thread.cc", 0x362, 0);
        cpu* c = cpu::self();
        c->interrupt_vector_table[0]();
    }
}

thread::~thread() {
    lock_guard gd;
    if (this->impl_ptr != nullptr)
        this->impl_ptr->parent = nullptr;
}

// operator= is same
thread::thread(thread&& other) {
    lock_guard gd;
    this->impl_ptr.move(this, &other);
}

void thread::join() {
    lock_guard gd;
    if (this->impl_ptr != nullptr) {
        this->impl_ptr->thd_q.push(cpu::impl::current());
        cpu::impl::run_next();
    }
}


struct thread::impl {

    impl(thread* p, void(*f)(void*), void* arg) {
        aasert_lib_mutex_locked();
        this->parent = p;
        uc = {0}; // memset(&uc, 0, sizeof(ucontext_t));
        assert(getcontext(&uc) == 0);
        uc.uc_link = NULL;
        uc.uc_stack.ss_sp = &stack;
        uc.uc_stack.ss_size = 0x4000;
        uc.uc_stack.ss_flags = SS_DISABLE;
        makecontext(&uc, thread::impl::thread_start, 2, f, arg);
        
        if (f == idle_func) {
            idle_queue.push(this);
        } else {
            ready_queue.push(this);
            wakeup_one_cpu();
        }
    }

    static void move(thread* p, thread* q) {
        p->impl_ptr = q->impl_ptr;
        q->impl_ptr = NULL;
        if (p->impl_ptr != nullptr) {
            p->impl_ptr->parent = p;
        }
    }

    static void check_delete() {
        assert(cpu::impl::current() != wtf);
        if (wtf != nullptr) {
            assert(!(wtf->parent && wtf->parent->impl_ptr));
            if (wtf != nullptr) {
                delete wtf;
            }
            wtf = nullptr;
        }
    }

    static void thread_start(void(*fn)(void*), void* arg) {
        assert_lib_mutex_locked();
        check_delete();
        unlock();
        fn(arg);
        lock();
        auto v4 = cpu::impl::current();
        while (true) {
            if (v4->thd_q.empty())
                break;
            auto v5 = v4->thd_q.front();
            ready_queue.push(v5);
            wakeup_one_cpu();
            v4->thd_q.pop();
        }
        for (auto it = v4->mtx_set.begin(); i != v4->mtx_set.end(); ++it ) {
            mutex::impl* mi = *it;
            mi->is_not_locked = 1;
            thread::impl* v13 = mi->own_thd;
            assert(v13 == cpu::impl::current());
            mi->own_thd = nullptr;
        }
        assert(!wtf);
        wtf = cpu::impl::current();
        if ( wtf->parent != NULL )
            wtf->parent->impl_ptr = NULL;
        cpu::impl::run_next();
        assert(false);
    }


    ucontext_t uc; // at 0, size 936
    /*
        unsigned long int __ctx(uc_flags);
        struct ucontext_t *uc_link;
	    stack_t uc_stack;
	    mcontext_t uc_mcontext;
	    sigset_t uc_sigmask;
	    struct _libc_fpstate __fpregs_mem;
    */
    char stack[STACK_SIZE]; // at 936, size 262144 => 0x4000
    std::set<mutex::impl*> mtx_set; // at 263080, size 48, lock acquired by thread
    std::queue<thread::impl*> thd_q; // at 263128 size 80, join thread queue
    thread* parent; // at 263208, size 8
    // total size 263216
};