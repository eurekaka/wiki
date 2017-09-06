* PG iterates PROCLOCK with all partition locks held

* two options for reading all tickets:
  * iterate global variable mdl_locks
    mdl_locks is LF_HASH, so we can not get consistent view of whole hash table without a lock, for
    example, we iterate the hash table while someone insert one element, we cannot get this into account;
    LF_HASH also contains pointers which are marked as deleted(IS_DESTROYED), or are unused;
  * iterate THD.mdl_context
    m_ticket is not protected, m_waiting_for is protected by m_LOCK_waiting_for;
  * lock free code path means we cannot take consistent snapshot at all.

* fast path can only be triggered for nonobtrusive mode, and no granted or pending obtrusive mode, and even no
  obtrusive mode attempt, hence there MUST be no waiting relationship, so it is OK to lose some consistency
  for this part when getting MDL snapshot; after all, materialize_fast_path_locks() would be called when acquiring
  obtrusive mode locks, or in will_wait_for(), so we would not dump m_fast_path_state in snapshot;

* one more outlaw case: when we are getting MDL snapshot, a thread is acquiring a *new* lock, hence no m_rwlock has been
  taken for this MDL_lock, and this bucket has been examined already, so this thread can get the lock but not reflected in
  the snapshot; if then we check another thread and find that thread is blocking on the new MDL lock, then we reflect it
  in the snapshot, the result is that in the snapshot, a thread is waiting for a lock with no holder; performance_schema
  has this problem as well; a naive approach is to add a rwlock at the beginning of acquire_lock

* PFS has a pfs_lock protecting PFS_metadata_lock struct, but no protection when iterating the PFS_metadata_lock array;
  so PFS also returns an inconsistent snapshot

* m_ticket in MDL_context is not protected, so it may cause segment fault; e.g, it is releasing lock while we are getting
  snapshot

* No deadlock caused by acquiring MDL_lock.m_rwlock; MDL_lock::lock_acquire() would not wait for another rwlock when holding
  one rwlock, so IS cannot have deadlock with MDL_lock::lock_acquire(); can IS have deadlock with another IS? imagine that,
  if we have such kind of deadlock, it means that thread A sees rwlock(a)->rwlock(b) in the list of bucket 0, and it is waiting
  for b with a held; then thread B must see rwlock(b)->rwlock(a) in bucket 0, and it is waiting for a with b held; this is impossible;
  so no deadlock;

* TABLE_SHARE is also involved in the deadlock detection, when one thread is waiting for another thread for table cache flush;
  class Wait_for_flush has a virtual function accept_visitor as well, it is pretty like MDL_ticket, stores info about MDL_context
  and TABLE_SHARE;

* varchar is stored using 3x memory space internally; like say, varchar(32) would use 96 bytes;
  a row is read from and written into storage engine as a cstring, and formed into tuple fields by Item, @sa Query_result_send::send_data

* List<> and HASH would only store pointers of objects, not objects themselves;
