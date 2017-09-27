* secondary index rows do not have those 3 hidden system columns: DB_TRX_ID, DB_ROLL_PTR, DB_ROW_ID,
  and update of secondary index tuples are like PG, i.e, mark delete, insert new;

* change buffer(or insert buffer), caches changes to secondary index pages when affected pages are not
  in the buffer pool; rational is that these changes are randomly spread, and hence need a lot of index
  pages to be loaded into buffer pool, which is expensive; on the contrary, these changes would be merged
  into corresponding index pages when those index pages are loaded into buffer pool later, or merged by a
  backgroud thread; change buffer is part of buffer pool, may take 1/2 of buffer pool; those unmerged
  change buffer in memory would be written to insert buffer in system tablespace when necessary

* Adaptive Hash Index(AHI): InnoDB monitors index searches, and if it notices that queries would benifit
  from building a hash index, it does so automatically;(usually when table can fit into memory); hash index
  can be partial, i.e, only contains part of tuples for a table;

* redo log is writen into redo log buffer, whether it is flushed when committing transaction is controled by
  innodb_flush_log_at_trx_commit(classic double 1); file name is ib_logfile0 and ib_logfile1

* InnoDB system tablespace(file on disk), stores:
  * metadata of InnoDB objects
  * doublewrite buffer
  * change buffer(on disk part)
  * rollback segments and undo space

  system tablespace is represented by one or more files, ibdata1 is default;

* metadata in system tablespace overlaps to some degree with infor stored in table metadata files(.frm files)

* why MySQL uses double write?
  * partial write failure; DB page is 8k or 16k, while OS page is normally 4k;
  * physiological redo logging; (logical logging inside DB page, requires DB page consistent)

* how double write solves partial write problem?
  * partial write can only happen either in doublewrite buffer, or in original file;
  * in recovery, if doublewrite buffer page is inconsistent, discard it and apply redo log based on original file contents(it must
    be valid now); if original file page is inconsistent, recover it from doublewrite buffer;
  * consistency is inferred by checksum of page(buf_page_is_corrupted());

* recovery of physiological logging? XXX

* comparison of logical logging, physical logging, and physiological logging:
  * all of them are WAL;
  * logical logging need smaller disk space, but complicate and slow recovery;
  * physical logging need more disk space, but easy and quick recovery;

* system tablespace -> rollback segments -> undo logs; undo logs can be configured to reside in separate undo tablespace
  by innodb_undo_tablespaces

* ibtmp1 is the default temporary tablespace; undo logs of temporary table reside in ibtmp1

* InnoDB locking:
  * row lock: shared and exclusive mode
  * table lock: intention lock, IS and IX; the main purpose of IS and IX is to show that someone is locking a row, or
    going to lock a row in the table; XXX?
  * record lock: lock index record(imagine it as a set of row locks with same value of a column, hence it locks index record)
  * gap lock: lock a range of values for a column or serveral columns
  * ... these are logical locks, except row lock and table lock;

* sorted index build
  * merge sort with temp file to sort all index records first, and build the B-tree
  * bottom-up approach

* BLOB and VARCHAR columns may be too long to fit into a B-tree page, so they normally would be stored
  in singly-linked list of "overflow pages", each column has one such list; such column is called "off-page column";
  sometimes, prefix of the column would be stored in the B-tree, depends on different row format config;

* default page size 16k, extent size 1MB, 64 consecutive pages; segment is something like logical file; two segments
  are allocated for each index, one for nonleaf nodes, one for leaf nodes;

* before commiting, we have to fsync table data to disk; before fsync table data to disk, we have to fsync undo log
  to disk, because if we fsync table data first, and crash happens in the middle step, so we cannot commit this trx
  in recovery, we have to rollback trx, which version of table data to rollback to? so we have to fsync undo log
  before fsync table data;

* undo log should be flushed to disk before redo log is flushed to disk, to ensure we can rollback transaction when
  crash happens; during crash recovery, we would first apply redo log to recover data to crash point, then apply undo
  log to rollback uncommitted transactions; to avoid flushing undo logs, innodb treats undo logs as data, and undo log
  manipulation would have records in redo log, so innodb can ensure undo log is persisted before corresponding redo records;

* redo log records of different transactions are mixed appended in redo log buffer, so when one transaction commits, redo logs
  before this transaction would be flushed to disk as well, hence we need rollback uncommitted transactions in recovery;

* redo log would only be sequentially appended, so if one transaction rollbacks, its redo logs would not be deleted from
  redo log buffer or disk log file, therefore we have to record the rollback operation as new redo logs;

* format of innodb redo log:
  <tablespace_id>+<page_id>+<record_type>+<data>

* mini transaction(MTR) of innodb:
  * one redo log only maps to one page, but there are operations which would manipulate several pages and is logically atomic,
    that is to say, several redo logs may be logically atomic, such as B-tree split, during recovery, either all of such redo logs
    are applied, or none of them;
  * such redo logs would be writen into mtr log buffer, not redo log buffer directly; and when this group of operations are complete,
    mtr log buffer is dumped into redo log buffer, so redo logs of mts is placed together inside redo log buffer and specially marked;

* format of innodb undo log(logical logging, thus having partial execution problem of undo log record):
  <record_type>+<table_id>+<data>

* sharp checkpoint: block all other DML statements, fsync buffer pages to disk, update checkpoint LSN;
  fuzzy checkpoint: get a checkpoint LSN from redo log buffer, then flush buffer pages, concurrent DML is allowed, so after flushing all
  buffer pages, the real checkpoint LSN may be larger than the stored one, which is OK, because redo log can guarantee idempotence;

* how logical and physiological redo log ensures idempotence?
  each buffer page has a latest LSN which modifies this page, in recovery, get checkpoint LSN first, then compare it with LSN recorded in
  page, if page LSN is larger than checkpoint LSN, skip this redo log on this page;

* modify buffer page first, then append redo log buffer, and get the LSN(LSN can only be get after appending redo log buffer), then record this
  LSN into buffer page;

  MTR may modify several pages, these pages would be written a same LSN of the MTR(a MTR has only one LSN, for all redo logs inside the MTR)

* MTR has no rollback mechanism; MTR would acquire page lock on buffer pages, to make LSN on these pages consistent;

* function adding 3 system columns: dict_table_add_system_columns()
* RR would acquire snapshot(ReadView) for a transaction; RC would acquire snapshot for a statement;
