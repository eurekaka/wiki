* Linux after 2.6 implements Posix pthread interfaces in nptl(native posix thread library), previously,
  it is LinuxThreads; the libpthread is a single library, while the source code is in the glibc;
* pthread mutex uses futex(2) for implementation, similar to posix semaphore(sem_post(3), implemented in
  glibc, while system V semaphore is a system call semop(2); glibc also has a difinition of semop, not shown
  in man page, but in source code, it is a wrapper of ipc(2) system call for invoking semop(2)); bare futex(2)
  is a system call to utilize the kernel sleep/wake-up mechanism(similar assembly code as PG), normally
  when saying "futex operation is entirely userspace for the non-contended case", they are referring to
  the user space assembly code of semaphore and pthread mutex, not the real futex(2) system call;
* pthread mutex lock call stack(glibc/nptl/ source code):
  
  ```
  __pthread_mutex_lock --> LLL_MUTEX_LOCK(macro) --> lll_lock(mutex->__data.__lock) --> assembly checking contension
                                                                                    |__ __lll_lock_wait --> lll_futex_wait(macro) --> lll_futex_timed_wait(macro) --> syscall futex
  ```
* LWLock manages waiting list by itself, while using semaphore for process sleep/wake up; pthread mutex hands over
  waiting list management and sleep/wake up to kernel through futex; From performance perspective, there should
  be no much difference;
* pthread_spinlock_t is an integer(volatile), and is in pure user space; pthread_spin_lock is implemented in
  simple assembly code;
* In typical use, a condition expression is evaluated under the protection of a mutex lock.
  When the condition expression is false, the thread blocks on the condition variable. The
  condition variable is then signaled by another thread when it changes the condition value.
  This causes one or all of the threads waiting on the condition to unblock and to try to
  acquire the mutex lock again.

  Because the condition can change before an awakened thread returns from pthread_cond_wait(),
  the condition that caused the wait must be retested before the mutex lock is acquired. The
  recommended test method is to write the condition check as a while() loop that calls pthread_cond_wait().

  ```
  pthread_mutex_lock();
      while(condition_is_false)
          pthread_cond_wait();
  pthread_mutex_unlock();
  ```
* The pthread_cond_wait() function atomically unlocks the mutex and blocks the current thread on the
  condition specified by the cond argument. The current thread unblocks only after another thread
  calls pthread_cond_signal(3) or pthread_cond_broadcast(3) with the same condition variable. The
  mutex must be locked before calling this function, otherwise the behavior is undefined. Before
  pthread_cond_wait() returns to the calling function, it re-acquires the mutex.
* example of wait and signal:

  ```
  pthread_mutex_t count_lock;
  pthread_cond_t count_nonzero;
  unsigned count;
  
  decrement_count()
  {
      pthread_mutex_lock(&count_lock);
      while (count == 0)
          pthread_cond_wait(&count_nonzero, &count_lock);
      count = count - 1;
      pthread_mutex_unlock(&count_lock);
  }
  
  increment_count()
  {
      pthread_mutex_lock(&count_lock);
      count = count + 1;
      pthread_cond_signal(&count_nonzero); // must before unlock mutex, to explicitly ensure an order of wait/signal, otherwise, it is hard to say which one comes first, hence hard to predict the behaviour
      pthread_mutex_unlock(&count_lock);
  }
  ```
* pthread_cond_signal awake only one thread waiting on the condition, the scheduling policy
  determines the order in which blocked threads are awakened. For SCHED_OTHER, threads are
  awakened in priority order. pthread_cond_broadcast awake all waiting threads on the cond;
* pthread_cond_wait/pthread_cond_signal is quite like WaitLatch/SetLatch; WaitLatch/SetLatch
  is implemented in user space, use a do while loop to encapsulate WaitLatch; the sleep/wake
  up is implemented by poll/pipe/signal; WaitLatch is used in Lock manager(ProcSleep);
  in LWLockAcquire, the sleep/wake up is implemented by semaphore with a for loop and flag check;
* the condition variable is shared among threads, so we have to use a mutex to protect the
  access to it; why the mutex(alias ext_mutex) has to be passed to pthread_cond_wait()? it is
  related to the implementation of pthread_cond_wait(), it maintains a "waiting list" in user
  space(imagine it as a waiting list for easier understanding, actually it is a integer), the waiting
  thread adds itself into the list, while the pthread_cond_signal() remove one thread from the
  list and awake it using futex(2); the waiting list is protected by the mutex(alias inner_mutex) of
  condition variable; if the ext_mutex is not passed in, imagine this case:

  ```
  // thread A
  pthread_mutex_lock(mtx);        // a1
  while(flag == 0) {              // a2 
      pthread_mutex_unlock(mtx);  // a3
      pthread_cond_just_wait(cv); // a4
      pthread_mutex_lock(mtx);    // a5
  }
  pthread_mutex_unlock(mtx);
  
  // thread B
  pthread_mutex_lock(mtx);   // b1
  flag = 1;                  // b2
  pthread_cond_signal(cv);   // b3
  pthread_mutex_unlock(mtx); // b4
  ```

  then for execution sequence a1, a2, a3, b1, b2, b3, b4, a4, thread A would not be awaken, because
  in b4, A is not in the waiting list yet; so the result is: A calls pthread_cond_wait before B calls
  pthread_cond_signal, but A is not awaken;

  libpthread solves this by passing the ext_mutex into pthread_cond_wait, so pthread_cond_wait would hold
  the ext_mutex, acquire the inner_mutex, then release ext_mutex, add itself to waiting list, release the
  inner_mutex, go to sleep; after awaken, it acquires the ext_mutex again, and then return;

  WaitLatch has no such problem, because it uses pipe for sleep/wake up;
* For all kinds of locks, only two things are important: tas and sleep/wake up
* callstack of pthread_cond_wait:

  ```
  __pthread_cond_wait --> lll_lock(cond->__data.__lock)
		      |__ __pthread_mutex_unlock_usercnt(mutex)
		      |__ cond->__data.__futex++ // integer
		      |__ cond->__data.__mutex = mutex // save the mutex for re-acquiring later
		      |__ futex_val = cond->__data.__futex
		      |__ lll_unlock(cond->__data.__lock)
		      |__ lll_futex_wait(&cond->__data.__futex, futex_val)(macro) --> lll_futex_timed_wait --> syscall futex
  ```
* brief summary:
  * pthread_spinlock_t: assembly for tas, no sleep/wake up;
  * pthread_mutex_t: assembly for tas, futex for sleep/wake up;
  * pthread_cond_t:
      * for the __lock protecting the cond struct members: assembly for tas, futex for sleep/wake
                                                           up(lll_lock, as pthread_mutex_t)
      * for the condition logic, futex for sleep/wake up;
  * spin lock in pg: assembly for tas, no sleep/wake up;
  * LWLock in pg: spin lock for tas, semaphore for sleep/wake up;
  * Lock in pg: LWLock for tas, Latch for sleep/wake up;
  * Latch in pg: signal/pipe/poll for sleep/wake up;
* pthread_rwlock_t, pthread_rwlock_init, pthread_rwlock_rdlock, pthread_rwlock_wrlock, similar as pthread_mutex_t,
  but it supports read/write mode; assembly code for tas, futex for sleep/wake up;
