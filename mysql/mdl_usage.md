* IX is only for scoped locks(GLOBAL, COMMIT, TABLESPACE, SCHEMA), compatible with IX, incompatible with S and X;
* S is to only access metadata, no data read & write, e.g, check existence of table;
* SH is granted ignoring pending X requests; e.g, filling an INFORMATION_SCHEMA table or SHOW CREATE TABLE;
* One can only acquire TL_READ, TL_READ_NO_INSERT, etc table-level locks if holding SR MDL lock on the table;
* All shared MDL lock can allow reading on metadata, including SNRW; lock holder of SNRW can read and write data, as long as it holds the table lock and row lock;
* If modifing metadata, then X lock is needed;
* SU is like SR, cannot write data, but: 1)can be upgraded to SNW, SNRW, X, then it can write data freely; 2) SU is incompatible with SU(and above);
* In MySQL, schema means database;
* MDL_key is a triple: namespace_dbname_table, stored in m_ptr(char array)
* SELECT * FROM tbl;
    * open_table_get_mdl_lock: TABLE:SR:TRX
    * release_transactional_locks: TABLE:SR:TRX
* INSERT INTO tbl VALUES(1);
    * open_table: GLOBAL:IX:STMT
    * open_table_get_mdl_lock: TABLE:SW:TRX
    * ha_commit_trans: COMMIT:IX:EXPLICIT
    * release:
    * ha_commit_trans: COMMIT:IX:EXPLICIT
    * release_transactional_locks: GLOBAL:IX:STMT
    * release_transactional_locks: TABLE:SW:TRX
* ALTER TABLE tbl DROP COLUMN c3, ALGORITHM = INPLACE;(InnoDB, varies on different Storage Engines)
    * open_tables --> lock_table_names: GLOBAL:IX:STMT
    * open_tables --> lock_table_names: SCHEMA:IX:TRX
    * open_tables --> lock_table_names: TABLE:SU:TRX
    * open_tables --> open_and_process_table --> open_table: GLOBAL:IX:STMT
    * open_tables --> open_and_process_table --> open_table --> open_tabl_get_mdl_lock: TABLE:SU:TRX
    * mysql_alter_table --> mysql_inplace_alter_table --> upgrade_shared_lock: TABLE:X:TRX(choosing IN PLACE branch, no SNW lock)
    * mysql_alter_table --> mysql_inplace_alter_table --> downgrade_lock: TABLE:SU:TRX
    * mysql_alter_table --> mysql_inplace_alter_table --> wait_while_table_is_used --> upgrate_shared_lock: TABLE:X:TRX
    * ha_commit_trans: COMMIT:IX:EXPLICIT
    * release:
    * ha_commit_trans: COMMIT:IX:EXPLICIT
    * release_transactional_locks: GLOBAL:IX:STMT
    * release_transactional_locks: TABLE:X:TRX
    * release_transactional_locks: SCHEMA:IX:TRX
* ALTER TABLE tbl DROP COLUMN c3, ALGORITHM = COPY;
    * open_tables --> lock_table_names: GLOBAL:IX:STMT
    * open_tables --> lock_table_names: SCHEMA:IX:TRX
    * open_tables --> lock_table_names: TABLE:SU:TRX
    * open_tables --> open_and_process_table --> open_table: GLOBAL:IX:STMT
    * open_tables --> open_and_process_table --> open_table --> open_tabl_get_mdl_lock: TABLE:SU:TRX
    * mysql_alter_table --> upgrate_shared_lock: TABLE:SNW:TRX
    * mysql_alter_table --> mysql_inplace_alter_table --> wait_while_table_is_used --> upgrate_shared_lock: TABLE:X:TRX
    * release:
    * release_transactional_locks: GLOBAL:IX:STMT
    * release_transactional_locks: TABLE:X:TRX
    * release_transactional_locks: SCHEMA:IX:TRX
* In COPY algo, ALTER TABLE would first get SU lock, then upgrade to SNW, then copy data, and then upgrade to X in rename phase;

* CREATE TABLE would first acquire S TABLE lock to check the existence of table, then upgrade to X for building table; only ALTER TABLE
  and CREATE TABLE would upgrade shared MDL;
    * open_tables --> lock_table_names --> acquire_locks: GLOBAL:IX:STMT
    * open_tables --> lock_table_names --> acquire_locks: SCHEMA:IX:TRX
    * open_tables --> lock_table_names --> acquire_locks: TABLE:S:TRX
    * open_tables --> open_and_process_table --> open_table --> open_tabl_get_mdl_lock: TABLE:S:TRX
    * open_tables --> open_and_process_table --> open_table --> upgrade_shared_lock: TABLE:X:TRX
    * release_transactional_locks: GLOBAL:IX:STMT
    * release_transactional_locks: TABLE:X:TRX
    * release_transactional_locks: SCHEMA:IX:TRX

* Why the existence of SU? to ensure only one upgrader for a lock exists at a time, otherwise, we would have deadlock. For example, if
  no SU, then imagine this sequence:

    ```
    tx1: acquire SW // succ
    tx2: acquire SW // succ
    tx1: upgrade to X // blocked by tx2
    tx2: upgrade to X // blocked by tx1
    ```

  SU can avoid this kind of deadlock by the mutual incompatibility of SU, SNW, SNRW X;
  Actually, CREATE TABLE would have this kind of deadlock, since it acquires no SU, instead, it upgrades from S to X directly. CREATE
  TABLE solves this by backoff and retry;
* LOCK TABLE tbl READ would acquire SR, DML would acquire SW, but they are inter-blocking, blocked on table level lock in engine;

* mysqldump procedure(file position based):
    * FLUSH TABLES;
    * FLUSH TABLES WITH READ LOCK;
        * GLOBAL:S:EXPLICIT and COMMIT:S:EXPLICIT, blocks all new writing stmts and existing transaction commiting;
    * SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ;
    * START TRANSACTION /* WITH CONSISTENT SNAPSHOT */; -- get snapshot
    * SHOW MASTER STATUS; -- get the binlog position
    * UNLOCK TABLES; -- release all mdl locks
    * SAVEPOINT sp; -- subtransaction to release S lock after dump each table, otherwise, the locks would be held until end of whole transaction
    * SELECT FROM t1;
    * ROLLBACK TO sp;
    * SELECT FROM t2;
    * ROLLBACK TO sp;
    * ...
