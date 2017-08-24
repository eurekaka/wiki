* compare MDL_ticket to PROCLOCK, MDL_context to PGPROC, DML_lock to LOCK, MDL_map to LockHash;

    ```
    struct MDL_key
    {
        enum_mdl_namespace;
        char m_prt[];
    }

    class MDL_request
    {
        enum enum_mdl_type type;
        enum enum_mdl_duration duration;
        MDL_request *next_in_list;
        MDL_request *prev_in_list;
        MDL_ticket *ticket;
        MDL_key key;
    }

    class MDL_ticket: public MDL_wait_for_subgraph
    {
        MDL_ticket *next_in_context;
        MDL_ticket *prev_in_context;

        MDL_ticket *next_in_lock;
        MDL_ticket *prev_in_lock;

        enum enum_mdl_type m_type;
        enum enum_mdl_duration m_duration;

        MDL_context *m_ctx;
        MDL_lock *m_lock;
    }

    class MDL_wait //abstract of the waiting action
    {
        enum enum_wait_status m_wait_status;
        mysql_mutex_t m_LOCK_wait_status;
        mysql_cond_t m_COND_wait_status;

        timed_wait() // use pthread_cond_t for sleep/wake up, classic implementation
        {
            mysql_mutex_lock();
            while(!m_wait_status)
            {
                mysql_cond_timedwait();
            }
            mysql_mutex_unlock();
        }
    }

    class MDL_context
    {
        //member functions implementing lock acquire/release logics;
        MDL_wait m_wait; //use this for waiting
        Ticket_list m_tickets; // locks acquired by this thread, check next_in_context
        MDL_wait_for_subgraph *m_waiting_for; // the lock waiting for, only one
    }

    class MDL_map
    {
        LF_HASH m_locks; // all locks in the server
        MDL_lock *m_global_lock; // singleton for GLOBAL lock
        MDL_LOCK *m_commit_lock; // singleton for COMMIT lock
    }

    class MDL_lock // only one for a unique MDL_key, regardless duration and mode
    {
        MDL_key key;
        mysql_prlock_t m_rwlock; // rwlock, prefer reader, self-implemented using pthread_mutex_t, and pthread_cond_t; PG uses spin lock
        Ticket_list m_granted; // granted threads for this lock, check next_in_lock
        Ticket_list m_waiting; // waiting threads for this lock, check next_in_lock

        // fast path releated
        volatile fast_path_state_t m_fast_path_state; // flag and counter combination, for atomic operations, use fast_path_state_cas()/add()/reset() wrappers

        struct MDL_lock_strategy
        {
            bitmap_t m_granted_incompatible; // compatibility matrics
            bitmap_t m_waiting_incompatible;
            fast_path_state_t m_unobtrusive_lock_increment[MDL_TYPE_END]; // longlong, base_increment, 0 means obtrusive mode
        }
        static const MDL_lock_strategy m_scoped_lock_strategy; // static, members hard-coded, IMPORTANT
        static const MDL_lock_strategy m_object_lock_strategy; // static, members hard-coded, IMPORTANT
    }
    ```
* Init of MDL subsystem:

    ```
    #0 MDL_map::init
    #1 mdl_init
    #2 init_server_components
    #3 mysqld_main
    #4 main
    ```
* MDL lock acquire:

    ```
    acquire_lock --> try_acquire_lock_impl --> find_ticket // check if we have already hold this lock, MDL_contex::m_tickets, similar to PG locallock
                 |                         |__ clone_ticket // if duration is not same
                 |                         |__ fix_pins // allocate harzard pointer for lock-free hash access
                 |                         |__ MDL_ticket::create // m_lock is null now
                 |                         |__ get_unobtrusive_lock_increment
                 |                         |__ retry label
                 |                         |__ mdl_locks.find_or_insert(m_pins, key, &pinned) // pinned must be true for non-singleton locks, i.e, "locked"
                 |                         |__ fast path branch --> old_state = lock->m_fast_path_state // save instant one
                 |                         |                    |__ do{old_state flag checks} while(!lock->fast_path_state_cas(&old_state, old_state + unobtrusive_lock_increment)) // cas would update old_state if not equal, after get out from this loop, lock is granted
                 |                         |                    |__ cleanups: lf_hash_search_unpin, save lock in ticket->m_lock, append the ticket to MDL_context::m_tickets, set mdl_request->ticket
                 |                         |__ slow path branch --> mysql_prlock_wrlock(&lock->m_rwlock) // protecting list and counters
                 |                                              |__ lf_hash_search_unpin // m_rwlock can protect the lock object now
                 |                                              |__ do{} while(!lock->fast_path_state_cas()) // to mark HAS_SLOW_PATH and optional HAS_OBTRUSIVE
                 |                                              |__ ticket->m_lock = lock
                 |                                              |__ if lock->can_grant_lock --> lock->m_granted.add_ticket(ticket) // can_grant_lock checks granted, fast-path granted, and waiting bitmaps
                 |                                              |                           |__ mysql_prlock_unlock
                 |                                              |                           |__ append the ticket to MDL_context::m_tickets
                 |                                              |                           |__ set mdl_request->ticket = ticket
                 |                                              |__ else *out_ticket = ticket // not successful, HOLDING the m_rwlock
                 |__ check mdl_request->ticket and return if granted
                 |__ lock->m_waiting.add_ticket(ticket) // add itself to waiting list
                 |__ m_wait.reset_status() // under protection of m_rwlock
                 |__ mysql_prlock_unlock
                 |__ MDL_context::m_waiting_for = ticket // inform deadlock detector, will_wait_for()
                 |__ find_deadlock
                 |__ m_wait.timed_wait() // two classes, one(with high prio) would notify other threads to release locks, then wait; the other would just wait
                 |__ MDL_context::m_waiting_for = NULL // done_waiting_for()
                 |__ check wait_status and clean up
    ```

* lock free hash implementation? harzard pointer? pin? XXX
* For object locks, unobtrusive modes: S, SH, SR and SW, compatible with each other, commonly used by DML, can fall into fast path; for scoped locks,
  unobtrusive modes: IX;
* GCC provides built-in atomic functions, like say __atomic_compare_exchange_n(type *ptr, type *expected, type *desired, ...),
  atomically (if *ptr == *expected, then *ptr = *desired, else *expected = *ptr);
* MDL_context::upgrade_shared_lock would call acquire_lock to get a new ticket and remove the old ticket; MDL_context::downgrade_lock would simply modify ticket
* MDL_context::release_locks_stored_before does things like rollback, because new ticket is inserted into the list head;
* MDL_context::release_lock

    ```
    release_lock --> if ticket->m_is_fast_path --> get_unobtrusive_lock_increment
                 |                             |__ old_state = lock->m_m_fast_path_state
                 |                             |__ do{mysql_prlock_wrlock();reschedule_waiters();mysql_prlock_unlock()} while(!lock->fast_path_state_cas())
                 |__ slow path --> lock->remove_ticket --> mysql_prlock_wrlock()
                 |                                     |__ remove_ticket
                 |                                     |__ update flags, fast_path_state_cas()
                 |                                     |__ reschedule_waiters()
                 |                                     |__ mysql_prlock_unlock()
                 |__ MDL_context::m_tickets.remove()
                 |__ MDL_ticket::destroy()

    reschedule_waiters(rwlock held) --> iterate m_waiting --> can_grant_lock --> yes --> m_wait.set_status(GRANTED) --> mysql_mutex_lock()
                                                                                     |                              |__ m_wait_status = status_arg
                                                                                     |                              |__ mysql_cond_signal()
                                                                                     |                              |__ mysql_mutex_unlock()
                                                                                     |__ cleanups, m_waiting.remove_ticket, m_granted.add_ticket
    ```
