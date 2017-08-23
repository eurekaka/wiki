* Linux after 2.6 implements Posix pthread interfaces in nptl(native posix thread library), previously,
  it is LinuxThreads; the libpthread is a single library, while the source code is in the glibc;
* pthread mutex uses futex(2) for implementation, similar to semaphore; bare futex(2) is a system call to
  utilize the kernel sleep/wake-up mechanism(similar assembly code as PG), normally when saying "futex operation
  is entirely userspace for the non-contended case", they are referring to the user space assembly code of semaphore
  and pthread mutex, not the real futex(2) system call;
* pthread mutex lock call stack(glibc/nptl/ source code):
  ```
  __pthread_mutex_lock --> LLL_MUTEX_LOCK(macro) --> lll_lock(mutex->__data.__lock) --> assembly checking contension
                                                                                    |__ __lll_lock_wait --> lll_futex_wait(macro) --> lll_futex_timed_wait(macro) --> syscall futex
  ```
* LWLock manages waiting list by itself, while using semaphore for process sleep/wake up; pthread mutex hands over waiting list management and sleep/wake up to kernel through futex;
  From performance perspective, there should be no much difference;
* pthread_spinlock_t is an integer(volatile), and is in pure user space; pthread_spin_lock is implemented in simple assembly code;
