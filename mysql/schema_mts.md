* A group in MTS is a set of binlog events of a transaction; generally speaking, a group consists of event sequence like:
  ```
  B-[p]-g-r-[g-r]-T
  B: BEGIN; contains db info;
  p: INTVAR, RANDVAR, USERVAR event, no db/table info involved; only in STATEMENT format binlog;
  g: Table_map, and Query event, contains db/table info; Table_map is in ROW format binlog, Query is in STATEMENT format binlog; Table_map contains db and table info, Query only contains db info;
  r: Delete_row_event, Update_row_event, Write_row_event;
  T: COMMIT, ROLLBACK, XID; XID is for InnoDB(XA), others are for non-transactional engines, e.g, MyISAM
  
  @sa: https://www.jianshu.com/p/c16686b35807
  ```
* A transaction can involve multiple databases, because InnoDB does not have db concept; in Query event, number of dbs modified is included; in Table_map_event, if the event involves foreign key,
  then the number of involved dbs is non-deterministic, so mts_dbs_num is set to be OVER_MAX, and enforce an isolated replay of this transaction; if no foreign key involved, the db number can only
  be 1;
  
* Unlike redo log, binlog records of a transaction is continuous in binlog file, this makes it much simple to do MTS; when assigning worker for a transaction, if the db mapping exists in the hash already,
  this transaction has to wait for the termination of the previous transaction on that worker, strictly speaking, the invalidation of this mapping;
  @sa: map_db_to_worker
  ```
  do   
  {    
      mysql_cond_wait(&slave_worker_hash_cond, &slave_worker_hash_lock);
  } while (entry->usage != 0 && !thd->killed);
  ```

* In table level MTS, if binlog format is set to be STATEMENT, then all transactions are serially replayed:
  ```
  @f contains_partition_info
  case QUERY_EVENT:
    if ((ends_group() && end_group_sets_max_dbs) ||
        (table_mode && (!starts_group() && !ends_group())))
        /*in table mode replication, all query event is assigned to worker 0*/
    {
      res= true; 
      static_cast<Query_log_event*>(this)->mts_accessed_dbs=
        OVER_MAX_DBS_IN_EVENT_MTS; // mark OVER_MAX to enforce serial execution
    }
    else  
      res= (!ends_group() && !starts_group()) ? true : false;

  @f get_slave_worker
    if (mts_dbs.num == OVER_MAX_DBS_IN_EVENT_MTS)
    {
      // Worker with id 0 to handle serial execution
      if (!ret_worker)
        ret_worker= *(Slave_worker**) dynamic_array_ptr(&rli->workers, 0);
      // No need to know a possible error out of synchronization call.
      (void) wait_for_workers_to_finish(rli, ret_worker);
      /*
        this marking is transferred further into T-event of the current group.
      */
      rli->curr_group_isolated= TRUE;
    }
  ```
  the reason behind this behavior is: if binlog format is set to be MIXED, we may see both Table_map_event in trx1, while see Query event in trx2; when assigning worker
  for trx2, we don't know whether we can execute trx2 parellelly with trx1, because Query event contains no table info, so for integrity consideration, we have to wait for
  other workers to finish before we can replay trx2;
  
  Therefore, to make table level MTS work, we have to set binlog format ROW;
