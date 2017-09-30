* for LOCK TABLES <view_name>, base tables are implicitly locked;
  for LOCK TABLES <table_name>, tables used in triggers on this table are also implicitly locked

* LOCK TABLES implicitly releases all locks held before by this session;

* FLUSH TABLES WITH READ LOCK would acquire a global read lock, not table lock, this lock can
  be released by UNLOCK TABLES;

* LOCK TABLES is permitted but ignored for temporary tables, because no other sessions can see
  temporary tables created by current session;

* For innodb tables ,READ LOCAL is same as READ; LOW_PRIORITY is a deprecated modifier

* All target tables should be put into one LOCK TABLES statement, otherwise, later LOCK TABLES would
  implicitly release previously acquired locks; after LOCK TABLES, this session can only use locks
  held now, any attempts to acquire other locks would raise error, until the UNLOCK TABLES is executed;

* LOCK TABLES has no effect on information_schema tables, because indeed they are temporary tables for each session;

* WRITE has higher priority than READ, which means that if session 1 gets READ lock, so session 2 acquiring WRITE lock
  would be blocked, and session 3 acquiring READ lock CANNOT proceed until session 2 is granted the WRITE lock and releases
  the WRITE lock;

* brief summary of LOCK TABLES:
  * sort tables first internally;
  * write locks before read locks;
  * lock tables one by one in order; <= this can avoid deadlock

* for partition table, partitions are locked and unlocks as a whole, no partition pruning support

* LOCK TABLES would implicitly commit existing transaction before execution;
* UNLOCK TABLES would implicity commit existing transaction after execution, only if a LOCK TABLES is executed;
  for this sequence:
  ```
  FLUSH TABLES WITH READ LOCK;
  BEGIN;
  SELECT xxx;
  UNLOCK TABLES; -- unlock locks acquired by FTWRL, but not commit transaction
  ```

* if BEGIN is issued after LOCK TABLES, implict UNLOCK TABLES is executed before BEGIN;

* for this sequence:
  ```
  LOCK TABLES tbl write;
  ALTER TABLE tbl xxx;
  ALTER TABLE tbl xxx; -- error, ALTER TABLE is special, UNLOCK TABLES is triggered by previous ALTER TABLE(after)
  ```

* statements that cause implicit commit, such as ALTER USER, would not release locks acquired by LOCK TABLES; that is to say,
  implicity UNLOCK TABLES can only be triggered by BEGIN(before) and ALTER TABLE(after) after LOCK TABLES;

* statements that cause implicit commit mostly would end the active transaction before execution, and end the current transaction
  after execution

---

* now it is for autocommit=0

* correct way to use lock tables:
  ```
  set autocommit = 0; --start a transaction
  LOCK TABLES xxx;
  xxx;
  COMMIT;
  UNLOCK TABLES;
  ```

* from implementation perspective, LOCK TABLES would acquire innodb table lock(innodb_table_locks = 1) and mysql table lock(MDL), innodb
  releases its table lock at next transaction commit(but MDL would not), so if autocommit is 1, innodb table lock is released at the end
  of LOCK TABLES statement, it is said that this can easily cause deadlock; to avoid this deadlock, current implementation is getting no
  innodb table lock if autocommit is 1; @sa code in ha_innobase::external_lock

* why innodb_table_locks?
  innodb would release all row locks when rollback a transaction, if the rollback fails, some row locks can remain, because innodb does
  not record which row locks belong to what sql statement; so if no innodb_table_locks, LOCK TABLES would succeed even if there are some
  remained row locks in innodb; note that this does not cause integrity problem, so OK to skip this innodb table lock;

  innodb_table_locks is only used for LOCK TABLES in ha_innobase::external_lock

---

* LOCK TABLES tbl read:
  ```
  (SQLCOM_LOCK_TABLES branch)
  mysql_execute_command --> trans_commit_implict
                        |__ Locked_tables_list::unlock_locked_tables --> THD::leave_locked_tables_mode --> MDL_context::set_transaction_duration_for_all_locks //EXPLICT -> TRANSACTION
                        |                                            |__ close_thread_tables --> mysql_unlock_tables --> unlock_external --> handler::ha_external_lock --> ha_innobase::external_lock
                        |__ MDL_context::release_transactional_locks
                        |__ lock_tables_open_and_lock_tables --> lock_table_names --> acquire_locks //MDL_SHARED_READ_ONLY-TRANSACTION-TABLE
                                                             |__ open_tables --> open_and_process_table --> open_table --> open_table_get_mdl_lock //MDL_SHARED_READ_ONLY-TRANSACTION-TABLE
                                                             |__ lock_tables --> mysql_lock_tables --> lock_external --> handler::ha_external_lock --> ha_innobase::external_lock
                                                             |                                     |__ thr_multi_lock //thr_lock skipped
                                                             |__ Locked_tables_list::init_locked_tables --> THD::enter_locked_tables_mode --> MDL_context::set_explicit_duration_for_all_locks //MDL_SHARED_READ_ONLY-EXPLICIT-TABLE
  SHARED_READ_ONLY is like SHARED_NO_WRITE, except that, it is compatible with SU and SNW; no much difference
  ```

* LOCK TABLES tbl write:
  ```
  (SQLCOM_LOCK_TABLES branch)
  mysql_execute_command --> trans_commit_implict
                        |__ Locked_tables_list::unlock_locked_tables --> THD::leave_locked_tables_mode --> MDL_context::set_transaction_duration_for_all_locks //EXPLICT -> TRANSACTION
                        |                                            |__ close_thread_tables --> mysql_unlock_tables --> unlock_external --> handler::ha_external_lock --> ha_innobase::external_lock
                        |__ MDL_context::release_transactional_locks
                        |__ lock_tables_open_and_lock_tables --> lock_table_names --> acquire_locks //IX-STMT-GLOBAL, IX-TRX-SCHEMA, MDL_SHARED_NO_READ_WRITE-TRX-TABLE
                                                             |__ open_tables --> open_and_process_table --> open_table --> open_table_get_mdl_lock //IX-STMT-GLOBAL, MDL_SHARED_NO_READ_WRITE-TRX-TABLE
                                                             |__ lock_tables --> mysql_lock_tables --> lock_external --> handler::ha_external_lock --> ha_innobase::external_lock
                                                             |                                     |__ thr_multi_lock //thr_lock skipped
                                                             |__ Locked_tables_list::init_locked_tables --> THD::enter_locked_tables_mode --> MDL_context::set_explicit_duration_for_all_locks //IX-EXPLICIT-GLOBAL, IX-EXPLICIT-SCHEMA, MDL_SHARED_NO_READ_WRITE-EXPLICIT-TABLE
  ```
