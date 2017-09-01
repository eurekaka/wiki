* find_ticket() for reference interate Ticket_list
  PG iterates PROCLOCK with all partition locks held

* two options for reading all tickets:
  * iterate global variable mdl_locks
    mdl_locks is LF_HASH, so we can not get consistent view of whole hash table without a lock, for
    example, we iterate the hash table while someone insert one element, we cannot get this into account;
    not to mention the LF_HASH is not designed for sequential iterating(remove_random_unused() would
    randomly dive into the hash table, not iterating); LF_HASH also contains pointers which are marked as
    deleted(IS_DESTROYED), or are unused;
  * iterate THD.mdl_context
    m_ticket is not protected, m_waiting_for is protected by m_LOCK_waiting_for;
  * lock free code path means we cannot take consistent snapshot at all.

* grab m_LOCK_waiting_for in read mode can hold sessions on done_waiting_for() and will_wait_for()
  grap m_LOCK_wait_status can hold sessions on MDL_wait::set_status() and reset_status(), timed_wait()
  grab m_rwlock in read mode can hold session on slow path beginning of acquire and release, this can protect
  m_LOCK_wait_status

* fast path can only be triggered for nonobtrusive mode, and no granted or pending obtrusive mode, and even no
  obtrusive mode attempt, hence there MUST be no waiting relationship, so it is OK to lose some consistency
  for this part when getting MDL snapshot; after all, materialize_fast_path_locks() would be called when acquiring
  obtrusive mode locks, or in will_wait_for(), so we would not dump m_fast_path_state in snapshot;

* one more outlaw case: when we are getting MDL snapshot, a thread is acquiring a new lock, hence no m_rwlock has been
  taken for this MDL_lock, and this thread has been examined already, so this thread can get the lock but not reflected in
  the snapshot; if then we check another thread and find that thread is blocking on the new MDL lock, then we reflect it
  in the snapshot, the result is that in the snapshot, a thread is waiting for a lock with no holder;

  a naive approach is to add a rwlock at the beginning of acquire_lock

* PFS does not grab lock when updating PFS_metadata_lock, and no protection when iterating the PFS_metadata_lock array;
  so PFS also returns an inconsistent snapshot

* m_ticket in MDL_context is not protected, so it may cause segment fault; e.g, it is releasing lock while we are getting
  snapshot
