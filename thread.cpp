#include "thread.h"
#include <ucontext.h>
#include <queue>
#include <iostream>
#include <set>
#include <cassert>
// not <exception>
#include <stdexcept>
#include <cstdlib>
#include <type_traits>

// @TODO: rewrite into transfer lock style instead of assert outer lock style
// @TODO: move ctor/operator of mutex/cv, so easy(?)

/*
// Actually C++14
template< bool B, class T = void >
using enable_if_t = typename std::enable_if<B,T>::type;

// Actually C++17
// Oops, variable template are only supported after C++14
// template< class T >
// inline constexpr bool is_pointer_v = std::is_pointer<T>::value;

template <template <typename ...> class C, typename T, typename = enable_if_t<std::is_pointer<T>::value>>
class DelOnDtor : public C<T> {
public:
    ~DelOnDtor() {
        C<T>& ctner = static_cast<C<T>&>(*this);
        for (auto it = ctner.begin(); it != ctner.end(); ++it) {
            delete *it;
        }
    }
};

template <typename T>
class DelOnDtor<std::queue, T> : public std::queue<T> {
public:
    ~DelOnDtor() {
        std::queue<T>& ctner = static_cast<std::queue<T>&>(*this);
        while (!ctner.empty()) {
            T p = ctner.front();
            delete p;
            ctner.pop();
        }
    }
};


DelOnDtor<std::queue, thread::impl*> ready_queue;
DelOnDtor<std::queue, thread::impl*> idle_queue;
*/

std::queue<thread::impl*> ready_queue;
std::queue<thread::impl*> idle_queue;
std::queue<cpu*> suspended_queue;
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

void lock()
{
	cpu::interrupt_disable();
	while (guard.exchange(1, std::memory_order_seq_cst));
}

void unlock()
{
	guard.store(0, std::memory_order_seq_cst);
	cpu::interrupt_enable();
}

class lock_guard
{
public:
	lock_guard()
	{
		lock();
	}

	~lock_guard()
	{
		unlock();
	}
};

// [[enter as locked]]
void wakeup_one_cpu()
{
#ifdef COUT_DEBUG
    std::cout << "wake one\n";
    std::cout << ready_queue.size() << '\n';
    std::cout << idle_queue.size() << '\n';
    std::cout << suspended_set.size() << '\n';
#endif
	if (!ready_queue.empty() && !suspended_queue.empty())
	{
		cpu* c = suspended_queue.front();
		c->interrupt_send();
		suspended_queue.pop();
	}
}

class thread::impl
{
	thread* parent;
	ucontext_t uc;
	char stack[STACK_SIZE];
	std::queue<thread::impl*> join_thd;
	friend class cpu::impl;
	friend class thread;
public:
	impl(thread* p_, thread_startfunc_t fn, void* arg);
	static void thread_start(void (*fn)(void*), void* arg);
	~impl();
};

class cpu::impl
{
	thread::impl* current_thd = nullptr;
public:
	static void run_next()
	{
#ifdef COUT_DEBUG
        std::cout << "run_next\n";
        std::cout << ready_queue.size() << '\n';
        std::cout << idle_queue.size() << '\n';
        std::cout << suspended_set.size() << '\n';
#endif
		thread::impl* old_thd = cpu::impl::current();
		thread::impl** cur_thd_p = &cpu::self()->impl_ptr->current_thd;
		if (!ready_queue.empty())
		{
			*cur_thd_p = ready_queue.front();
			ready_queue.pop();
		}
		else
		{
			*cur_thd_p = idle_queue.front();
			idle_queue.pop();
		}
		thread::impl* cur_thd = *cur_thd_p;
		if (old_thd)
		{
			swapcontext(&old_thd->uc, &cur_thd->uc);
		}
		else
		{
			setcontext(&cur_thd->uc);
		}
		delete last_free_thread;
		last_free_thread = nullptr;
	}

	static thread::impl* current()
	{
		return cpu::self()->impl_ptr->current_thd;
	}
};

// boot thread and idle threads are released
void idle_func(void*)
{
	lock();
	while (true)
	{
		//to
		thread::impl* ti = cpu::impl::current();
		idle_queue.push(ti);
		cpu::impl::run_next();//userthread
		suspended_queue.push(cpu::self());
		//void guard_unlock_suspend()
		//{
		guard.store(0, std::memory_order_seq_cst);
		cpu::interrupt_enable_suspend();
		//}
		//wakeup
		lock();
		//jump
	}
}

void timer_handler()
{
	lock_guard lk;
	if (!ready_queue.empty())
	{
		ready_queue.push(cpu::impl::current());
		cpu::impl::run_next();
	}
}

void ipi_handler()
{
	// Just a empty implementation is ok.
}

void atexit_idle_handler()
{
	while (!idle_queue.empty())
	{
		thread::impl* frt = idle_queue.front();
		idle_queue.pop();
		delete frt;
	}
}

void cpu::init(thread_startfunc_t fn, void* arg)
{
	while (guard.exchange(1, std::memory_order_seq_cst));
	impl_ptr = new cpu::impl;
	interrupt_vector_table[cpu::TIMER] = ipi_handler;
	interrupt_vector_table[cpu::IPI] = ipi_handler;
	unlock(); // also enable interrupt
	if (fn)
	{
		thread cpu_main_thread{fn, arg};
	}
	thread cpu_idle_thread{idle_func, nullptr};
	lock();
	// however, atexit cannot release per thread
	// atexit(atexit_idle_handler);
	interrupt_vector_table[cpu::TIMER] = timer_handler;
	cpu::impl::run_next();
}


thread::impl::impl(thread* p, thread_startfunc_t fn, void* arg)
{
	if (fn == nullptr)
	{
		throw std::runtime_error("nullptr function");
	}
	parent = p;
	getcontext(&uc);
	uc.uc_link = nullptr;
	uc.uc_stack.ss_sp = &stack;
	uc.uc_stack.ss_size = STACK_SIZE;
	uc.uc_stack.ss_flags = SS_DISABLE;
	makecontext(&uc, reinterpret_cast<void(*)()>(thread::impl::thread_start), 2, fn, arg);

	if (fn == idle_func)
	{
		idle_queue.push(this);
	}
	else
	{
		ready_queue.push(this);
		wakeup_one_cpu();
	}
}

void thread::impl::thread_start(void (*fn)(void*), void* arg)
{
	delete last_free_thread;
	last_free_thread = nullptr;
	unlock();


	fn(arg);


	lock();
	auto current_thd = cpu::impl::current();
	while (!current_thd->join_thd.empty())
	{
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

thread::impl::~impl()
{
	if (parent != nullptr)
	{
		parent->impl_ptr = nullptr;
	}
}


thread::thread(thread_startfunc_t fn, void* arg)
{
	lock_guard lk;
	impl_ptr = new thread::impl{this, fn, arg};
}

thread::~thread()
{
	lock_guard lk;
	if (this->impl_ptr != nullptr)
		this->impl_ptr->parent = nullptr;
}

thread::thread(thread&& other)
{
	lock_guard lk;
	impl_ptr = other.impl_ptr;
	other.impl_ptr = nullptr;
	if (impl_ptr != nullptr)
	{
		impl_ptr->parent = this;
	}
}

thread& thread::operator=(thread&& other)
{
	lock_guard lk;
	impl_ptr = other.impl_ptr;
	other.impl_ptr = nullptr;
	if (impl_ptr != nullptr)
	{
		impl_ptr->parent = this;
	}
	return *this;
}

void thread::join()
{
	lock_guard lk;
	if (this->impl_ptr != nullptr)
	{
		this->impl_ptr->join_thd.push(cpu::impl::current());
		cpu::impl::run_next();
	}
}

void thread::yield()
{
	lock_guard lk;
	if (!ready_queue.empty())
	{
		ready_queue.push(cpu::impl::current());
		cpu::impl::run_next();
	}
}

class mutex::impl
{
	thread::impl* own_thd = nullptr;
	std::queue<thread::impl*> thd_q; // locked thread
public:
	void lock()
	{
		if (own_thd != nullptr)
		{
			thd_q.push(cpu::impl::current());
			cpu::impl::run_next();
		}
		else
		{
			own_thd = cpu::impl::current();
		}
	}

	void unlock()
	{
		if (own_thd != cpu::impl::current())
		{
			throw std::runtime_error("release not-own");
		}
		own_thd = nullptr;
		if (!thd_q.empty())
		{
			ready_queue.push(thd_q.front());
			own_thd = thd_q.front();
			thd_q.pop();
			wakeup_one_cpu();
		}
	}
};

mutex::mutex()
{
	lock_guard lk;
	impl_ptr = new mutex::impl;
}

void mutex::lock()
{
	lock_guard lk;
	impl_ptr->lock();
}

void mutex::unlock()
{
	lock_guard lk;
	impl_ptr->unlock();
}

mutex::~mutex()
{
	lock_guard lk;
	delete impl_ptr;
}


class cv::impl
{
	std::queue<thread::impl*> thd_q; // waiting thread
public:
	void wait(mutex::impl* mtx)
	{
		mtx->unlock();
		thd_q.push(cpu::impl::current());
		cpu::impl::run_next();
		mtx->lock();
	}

	void signal()
	{
		if (!thd_q.empty())
		{
			ready_queue.push(thd_q.front());
			thd_q.pop();
			wakeup_one_cpu();
		}
	}

	void broadcast()
	{
		while (!thd_q.empty())
		{
			ready_queue.push(thd_q.front());
			thd_q.pop();
			wakeup_one_cpu();
		}
	}
};

cv::cv()
{
	lock_guard lk;
	impl_ptr = new cv::impl;
}

cv::~cv()
{
	lock_guard lk;
	delete impl_ptr;
}

void cv::signal()
{
	lock_guard lk;
	impl_ptr->signal();
}

void cv::broadcast()
{
	lock_guard lk;
	impl_ptr->broadcast();
}

void cv::wait(mutex& mtx)
{
	lock_guard lk;
	impl_ptr->wait(mtx.impl_ptr);
}
