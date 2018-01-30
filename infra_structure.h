/*
CPU : 8 uint64_t
U0:
address of cpu N instance
U1:
BYTE6 == is_thread_ended          0
BYTE5 == is_last_interrupt_missed 0
when BYTE4 == 1 && cpu->interrupt_send() => as 1
when as 1 && cpu::interrupt_enable() => as 0
when as 0 && cpu::interrupt_enable_suspended() => ++num_thread_end
BYTE4 == interrupts_are_disabled  1

* 3 file, 3 ctors
* __attribute__(constuctor(101))
* + 1B9A->1B5D->ios_base_init
* + 20F5->20B8->ios_base_init
* + 35A2->350E->cpu_infra_s init + map_init + atomic_init

* cpu::boot:
* + setup global settings
* + use pthread to create N cpu::boot_helper
* + wait for boot_helper static init cpu to signal
* + // do some cleaning (pthread_kill / test cpu::init)
* + send sigusr1 to threads (which interrupts and swap context)

* cpu::boot_helper
* + static cpu cpu_instance; (cpu::cpu)
* + static is_cpu_booted (shared between pthread)
* + only one thread in cpu::boot start boot function, otherwise init with idle_func
* + cpu::init

* cpu::cpu
* + test atomic is_cpu_boot_called
* + my_cpu_id != 16
* + cpu_to_id[addr] = id
* + set sigusr1_handler
* + wakeup cpu::boot

* cpu::init
* + ensure guard not locked
* + init ready_ptr (queue)
* + init idle_ptr (queue)
* + init suspended_ptr (set)
* + if has arg => create thread(fn, arg)
* + if not arg => create thread(idle_func, nullptr)
* + give IPT[0] = thread_yield
* + give IPT[1] = empty_func
* + cpu::impl::run_next

