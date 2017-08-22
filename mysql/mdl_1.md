* IX is only for scoped locks(GLOBAL, COMMIT, TABLESPACE, SCHEMA), compatible with IX, incompatible with S and X;
* S is to only access metadata, no data read & write, e.g, check existence of table;
* SH is granted ignoring pending X requests; e.g, filling an INFORMATION_SCHEMA table or SHOW CREATE TABLE;
* One can only acquire TL_READ, TL_READ_NO_INSERT, etc table-level locks if holding SR MDL lock on the table;
* All shared MDL lock can allow reading on metadata, including SNRW; lock holder of SNRW can read and write data, as long as it holds the table lock and row lock;
* If modifing metadata, then X lock is needed;
* upgradable? ix?
* SU is like SR, cannot write data, but: 1)can be upgraded to SNW, SNRW, X, then it can write data freely; 2) SU is incompatible with SU(and above);
* In MySQL, schema means database;
* GLOBAL and COMMIT is scoped lock namespace, others are object lock namespace;
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
* ALTER TABLE tbl DROP COLUMN c3;
    * open_tables --> lock_table_names: GLOBAL:IX:STMT
    * open_tables --> lock_table_names: SCHEMA:IX:TRX
    * open_tables --> lock_table_names: TABLE:SU:TRX
    * open_tables --> open_and_process_table --> open_table: GLOBAL:IX:STMT
    * open_tables --> open_and_process_table --> open_table --> open_tabl_get_mdl_lock: TABLE:SU:TRX
    * mysql_alter_table --> mysql_inplace_alter_table --> upgrate_shared_lock: TABLE:X:TRX(choosing IN PLACE branch, no SNW lock)
    * mysql_alter_table --> mysql_inplace_alter_table --> wait_while_table_is_used --> upgrate_shared_lock: TABLE:X:TRX
    * ha_commit_trans: COMMIT:IX:EXPLICIT
    * release:
    * ha_commit_trans: COMMIT:IX:EXPLICIT
    * release_transactional_locks: GLOBAL:IX:STMT
    * release_transactional_locks: TABLE:X:TRX
    * release_transactional_locks: SCHEMA:IX:TRX
* In COPY branch, ALTER TABLE would first get SU lock, then upgrade to SNW, then copy data, and then upgrade to X in rename phase;
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
        enum enum_wait_status;
        timed_wait();
    }

    class MDL_context
    {
        //member functions implementing lock acquire/release logics;
        MDL_wait m_wait; //use this for waiting
        Ticket_list m_tickets; // locks acquired by this thread, check next_in_context
        MDL_wait_for_subgraph *m_waiting_for; // the lock waiting for
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
        mysql_prlock_t m_rwlock; // spinlock
        Ticket_list m_granted; // granted threads for this lock, check next_in_lock
        Ticket_list m_waiting; // waiting threads for this lock, check next_in_lock
    }
    ```
