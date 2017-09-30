* structs:
  ```
  typedef struct st_thr_lock_info
  {
    my_thread_id thread_id;
  } THR_LOCK_DATA; => PGPROC

  typedef struct st_thr_lock_data
  {
    THR_LOCK_INFO *owner;
    struct st_thr_lock *lock;
    enum thr_lock_type type;
    mysql_cond_t *cond; //latch to sleep on
  } THR_LOCK_DATA; => PROCLOCK

  struct st_lock_list
  {
    THR_LOCK_DATA *data, **last;
  }

  typedef struct st_thr_lock
  {
    mysql_mutex_t mutex;
    struct st_lock_list read_wait;
    struct st_lock_list read;
    struct st_lock_list write_wait;
    struct st_lock_list write;
  } THR_LOCK; => LOCK

  typedef struct st_mysql_lock
  {
    TABLE **table;
    uint table_count, lock_count;
    THR_LOCK_DATA **locks;
  } MYSQL_LOCK; //PROCLOCK for tables, just a upper wrapper
  ```

* all THR_LOCK are stored in thr_lock_thread_list, protected by THR_LOCK_lock;

* thr_lock uses ordinary locking algorithm, read/write lock, waiting list, conflicting matrix; no much optimization,
  whole procedure is protected by &lock->mutex; call wait_for_lock() to suspend, data->cond is used to sleep on, and
  check if granted/aborted; timeout supported, specified in mysql_lock_tables to be thd->variables.lock_wait_timeout

* thr_lock facility is only called through thr_multi_lock <-- mysql_lock_tables; now thr_lock is skipped because lock_count
  of MYSQL_LOCK is 0; MYSQL_LOCK is allocated and inited in get_lock_data

* ha_innobase::store_lock
  ```
  store_lock --> trx_sys->mvcc->view_close if isolation level is RC or RU
             |__ decide m_prebuilt->select_lock_type based on command(call row_quiesce_set_state(QUIESCE_START) for FLUSH TABLES WITH READ LOCK, quiesce means fsync table)
             |__ decide ++trx->will_lock or not based on select_lock_type
             |__ out 'to' is exactly same as input 'to' => MYSQL_LOCK.lock_count must be 0 in all cases, so thr_lock cannot be called at all
  ```

* thr_multi_lock would sort target locks first, to avoid deadlock
