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

* status change of MDL_lock in LF_HASH:

  ```
  used -> unused -> MDL_lock::IS_DESTROYED(remove_random_unused) -> marked as DELETED(ldelete) -> removed from list(ldelete & lfind) -> added into purgatory(lf_pinbox_free) -> returned to LF_ALLOCATOR(lf_pinbox_real_free)
  ```

* stress test for information_schema.metadata_locks
  * MDL_LOCKS_UNUSED_LOCKS_LOW_WATER_MARK_DEFAULT 0
  * MDL_LOCKS_UNUSED_LOCKS_MIN_RATIO 0
  * LF_PURGATORY_SIZE 1
  * 2 threads for IS query, 12 threads for oltp_read_only.lua, 8 cores, CPU full, 10 tables, each table has 1 row, memory storage engine
  * 110k/qps, 3 mins

* one problem exists: m_fast_path_state is only materialized by MDL_context itself by iterating m_tickets, so if this thread never attemp to acquire
  obtrusive mode lock, then it would not materialize m_fast_path_state, so it may happen a X lock is blocked but holder not reflected in the snapshot;

---

* iterating MDL_context would get complete results, including m_fast_path_state, because m_tickets include all locks acquired, including fast path;
* for safety, we have to consider:
  * MDL_ticket objects in m_tickets and m_waiting_for should be safe to access; <= m_type and m_duration may be simultaneously modified by downgrade_lock(), set_lock_duration(), but it is fine;
  * m_tickets and m_waiting_for pointers should be safe; <= m_LOCK_tickets and m_LOCK_waiting_for to protect this
  * m_LOCK_tickets and m_LOCK_waiting_for should be safe, i.e, not has been destroyed before we call mysql_prlock_rdlock(); <= protected by destroyed flag and LOCK_thd_data
  * MDL_context object in THD is safe for accessing as long as thd is still in thd_list, protected by LOCK_thd_remove; MDL_context object is freed when in deconstructor of THD @sa THD::release_resources
  * NB: pthread_mutex_lock would return EINVAL if mutex is not initialized or destroyed by pthread_mutex_destroy

* performance loss is in lock_tickets_for_write();
* no deadlock, IS would acquire lock in order, and lock is in read mode, no conflict(LOCK_thd_data is mutex, that is fine, we can even change it to a rwlock)
* stress test:
  * frequent connection create and destroy;
  * frequent lock acquire and release;
  * 8 CPU, 16GB memory, 1 threads for IS(0.1s delay), 32 threads for freq_conn, 10 mins

* stress test finding:
  * too frequent IS query would cause "too many connections" error, this is normal problem of IS query, e.g processlist;
  * CPU 600%, bottleneck is connection create
  * "too many connections" hit while no crash, positive

* performance test:
  * 8 threads, 5 mins
  * single_select: cpu 630% mysqld, 160% sysbench, 90k/tps/qps(91555) <= extreme case, 12.8% loss
  * oltp_read_only: cpu 630% mysqld, 160% sysbench, 114k/qps(114009), 8k/tps(8143) <= 8.2% loss
  * no patch, single_select: cpu 630% mysqld, 160% sysbench, 105k/tps/qps(105034)
  * no patch, oltp_read_only: cpu 625% mysqld, 170% sysbench, 124k/qps(124204), 8k/tps(8871)

* performance test, shot 2: <= no performance loss
  * 8 threads, 1 mins
  * single_select: cpu 630% mysqld, 160% sysbench, 104k/tps/qps(104796)
  * oltp_read_only: cpu 630% mysqld, 160% sysbench, 124k/qps(124765), 9k/tps(8911)
  * no patch, single_select: cpu 630% mysqld, 160% sysbench, 104k/tps/qps(104428)
  * no patch, oltp_read_only: cpu 625% mysqld, 170% sysbench, 124k/qps(124118), 8k/tps(8865)

* performance test, shot 3: <= no performance loss
  * 8 threads, 5 mins
  * single_select: cpu 630% mysqld, 160% sysbench, tps/qps(103721)
  * oltp_read_only: cpu 630% mysqld, 160% sysbench, qps(124278), tps(8877)
  * no patch, single_select: cpu 630% mysqld, 160% sysbench, tps/qps(102967)
  * no patch, oltp_read_only: cpu 625% mysqld, 170% sysbench, qps(122908), tps(8779)

* load average: (thread runnable + thread running)/full_cpu_thread_running, sum of all CPUs

---

* performance test shot 4: sysbench on separate machine
  * 100.67.159.99 as mysqld host(48 cores, 500G), 100.67.54.146 as sysbench host(48 cores, 500G)
  * oltp_read_only(no patch): 160 threads, mysqld 48 cpu full, 291245/qps
  * single_select(no patch): 160 threads, mysqld 48 cpu full, 291782/qps
  * oltp_read_only: 160 threads, mysqld 48 cpu full, 289958/qps
  * single_select: 160 threads, mysqld 48 cpu full, 291631/qps

* performance test shot 5: general test
  * 10 tables, each 50000 rows, all in memory, heap engine
  * readonly with patch:
    * 32 threads: 21038/qps
    * 64 threads: 23500, CPU full, run 50
    * 128 threads: 23000, run 70-80
    * 256 threads: 22500, run 160
    * 512 threads: 22500, run 340-350
    * 768 threads: 22800, run 520-530
    * 1000 threads: 22500 run 680-690
    * bottleneck is CPU and latency
  * mixed with patch:
    * 32: 15000 + 4200, run 30-32
    * 64: 21000 + 6000, run 62-64
    * 128: 23800 + 6800, run 124-126
    * 256: 25200 + 7200, run 240-250
    * 512: 26000 + 7400, run 490-500
    * 768: 26000 7500 740
    * 1000: 26000 7500 980
    * CPU not full, bottleneck is locking

  * readonly:
    * 32 threads: 20700 32
    * 64 threads: 22500 54, CPU full
    * 128 threads: 22100 80
    * 256 threads: 21700 150-160
    * 512 threads: 21800 350
    * 768 threads: 21700 520
    * 1000 threads: 21800 700
  * mixed:
    * 32: 14400 4100 32
    * 64: 20300 5800 62
    * 128: 23000 6500 125
    * 256: 24000 6900 240
    * 512: 25000 7100 500
    * 768: 25000 7100 740
    * 1000: 25000 7100 980
    * CPU not full, bottleneck is locking



* performance test shot 6: general test
  * 100 tables, each 5000 rows, all in memory, innodb engine
  * select.lua with patch:
    * 32 threads: 255000 15
    * 64 threads: 410000 30
    * 128 threads: 550000 45 cpu full (70/30)
    * 256 threads: 556000 45 70/30
    * 512 threads: 550000 50 70/30
    * 768 threads: 530000 70 70/30
    * 1000 threads: 380000 350 50/50
  * mixed with patch:
    * 32: 100000 29000 25
    * 64: 140000 40000 50
    * 128: 144000 40000 100
    * 256: 136000 38000 220
    * 512: 80000 23000 500
    * 768: 56000 16000 760
    * 1000: 50000 14000 990
    * CPU not full, bottleneck is locking

  * select.lua:
    * 32 threads: 257000 20
    * 64 threads: 415000 30
    * 128 threads: 550000 45, cpu full(70/30)
    * 256 threads: 560000 45 (70/30)
    * 512 threads: 555000 50 (70/30)
    * 768 threads: 500000 120 70/30
    * 1000 threads: 280000 360 30/70
    * bottleneck is CPU
  * mixed:
    * 32: 100000 29000 23
    * 64: 140000 34000 50
    * 128: 144000 40000 100
    * 256: 135000 38000 230
    * 512: 81000 23000 500
    * 768: 55000 15000 760
    * 1000: 49000 14000 990
    * CPU not full, bottleneck is locking
