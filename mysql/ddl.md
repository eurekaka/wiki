* before 5.6, DDL has 2 implementations: COPY and INPLACE(INPLACE is just for index add and remove); in 5.6,
  online DDL is introduced; online DDL also has COPY and INPLACE implementations, so online DDL is like an
  general optimization for DDL;

* by default, MySQL would use as little locking as possible for online DDL, but we can explicitly specify
  locking options for online DDL; if specified locking is not available, error would be reported;

  locking options include: NONE, SHARED, DEFAULT, EXCLUSIVE

  this is just a static check, that is to say, based on specific DDL command, decide whether the locking option
  is possible, before really do the DDL; locking option is the lower bound of lock mode; if original lock mode
  is higher than locking option, raise error; if not, upgrade to locking option; @sa Alter_info::requested_lock

* from my understanding, core idea of online DDL is incremental implementation, i.e, row_log_t; online DDL
  would normally take longer time to finish than DDL, but is more responsive for DML queries;

* table rows of InnoDB are stored in a clustered index organized based on primary key;
  secondary index of MySQL: key is indexed columns, value is primary key;

* when DDL operations on primary key uses algorithm=inplace, data rows would still be copied, but its performance
  should exceed algorithm=copy as well for 3 reasons, one is that, there is no undo and redo logging for inplace
  algorithm;

* adding a secondary index would first scan the table, and then sort rows, and then build B-tree; this is more efficient
  than inserting rows into B-tree in random order, and has higher fill-factor in B-tree node;

* callstack of create index:
  ```
  mysql_execute_command --> Alter_info constructor
                        |__ mysql_alter_table --> open_tables --> lock_table_names(timeout) //MDL locks
                                              |               |__ open_and_process_table --> create temp table for IS
                                              |               |                          |__ open_table //MDL locks --> open_table_get_mdl_lock //MDL locks
                                              |               |                                                     |__ table_cache_manger.get_cache
                                              |               |                                                     |__ Table_cache::get_table
                                              |               |__ set TABLE.erginfo.lock_type //TL_READ_NO_INSERT
                                              |__ mysql_prepare_alter_table //prepare create_info, and prepare target table columns
                                              |__ create_table_impl --> mysql_parepare_create_table --> prepare_create_field
                                              |                     |                               |__ various checks
                                              |                     |__ rea_create_table //create .frm file, path is ./test/#sql-6db3_6, temp table --> mysql_create_frm --> create_frm --> my_create --> open
                                              |                                                                                                     |                    |              |__ mysql_file_write
                                              |                                                                                                     |                    |__ mysql_file_pwrite
                                              |                                                                                                     |                    |__ mysql_file_close
                                              |                                                                                                     |__ ha_create_handler_files //empty
                                              |__ Alter_inplace_info constructor
                                              |__ open_table_uncached //open the temp table just created
                                              |__ update_altered_table //mark fields participating in index creation
                                              |__ ha_innobase::check_if_supported_inplace_alter //HA_ALTER_INPLACE_NO_LOCK_AFTER_PREPARE
                                              |__ mysql_inplace_alter_table --> upgrade_shared_lock //before prepare phase
                                                                            |__ lock_tables //take table level lock --> mysql_lock_tables //thd.vriables.lock_wait_timeout, used by MDL_context::acquire_lock as well -->lock_tables_check //check if allowed
                                                                            |                                                                                                                                         |__ get_lock_data --> malloc MYSQL_LOCK(TABLE* and THR_LOCK_DATA*)
                                                                            |                                                                                                                                         |                 |__ ha_innobase::store_lock //inform engine?
                                                                            |                                                                                                                                         |__ lock_external //table_count --> decide lock_type F_RDLCK;
                                                                            |                                                                                                                                         |                               |__ handler::ha_external_lock --> ha_innobase::external_lock
                                                                            |                                                                                                                                         |__ thr_multi_lock(timeout) //lock_count=0, skipped, innodb no longer relies on THR_LOCK now, but relies on MDL and its own locking subsystem
                                                                            |__ handler::ha_prepare_inplace_alter_table //prepare phase --> ha_innobase::prepare_inplace_alter_table
                                                                            |__ downgrade_lock
                                                                            |__ handler::ha_inplace_alter_table --> ha_innobase::inplace_alter_table
                                                                            |__ wait_while_table_is_used --> upgrade_shared_lock(MDL_EXCLUSIVE)
                                                                            |__ lock_fk_dependent_tables //SRO MDL lock on related tables
                                                                            |__ handler::ha_commit_inplace_alter_table --> ha_innobase::commit_inplace_alter_table

  ha_innobase::external_lock --> update_thd
                             |__ m_prebuit->sql_stat_start = true; //IMPORTANT
                             |__ innobase_register_trx
                             |__ decide m_prebuilt->select_lock_type //LOCK_X or LOCK_S
                             |__ m_mysql_has_locks = true
                             |__ TrxInInnoDB::begin_stmt, then return
                             |__ unlock branch, would call innobase_commit if it is autocommit, and call trx_sys->mvcc->view_close if <= READ_COMMITTED, m_mysql_has_locked = false;

  external_lock is something like marking, not real locking; act as trx entry point of innodb

  //prepare phase, exclusive MDL lock held, modify metadata and get a snapshot
  ha_innobase::prepare_inplace_alter_table --> various validation checks
                                           |__ prepare_inplace_alter_table_dict --> row_mysql_lock_data_dictionary_func --> rw_lock_x_lock_inline(dict_operation_lock) //prevent innodb bg threads accessing
                                                                                |__ dict_locked = true
                                                                                |__ dict_table_check_for_dup_indexes
                                                                                |__ row_merge_create_index //create the index and load into dict --> dict_mem_index_create //create index memory object
                                                                                |                                                                |__ dict_table_get_col_name //get indexed column name
                                                                                |                                                                |__ dict_mem_index_add_field
                                                                                |                                                                |__ row_merge_create_index_graph //add the index to SYS_INDEXES, a system dict
                                                                                |__ trx_assign_read_view --> trx_sys->mvcc->view_open
                                                                                |__ trx_commit_for_mysql //commit the dict trx for adding index
                                                                                |__ row_mysql_unlock_data_dictionary --> rw_lock_x_unlock(dict_operation_lock)
                                                                                |__ dict_locked = false;

  //main phase
  ha_innobase::inplace_alter_table --> dict_table_get_first_index //get the index definition built in prepare phase in SYS_INDEXES
                                   |__ row_merge_build_indexes //read clustered index and build index --> allocate 3*srv_sort_buf_size mem
                                                                                                      |__ row_merge_read_clustered_index --> row_merge_buf_create
                                                                                                      |                                  |__ mtr_start
                                                                                                      |                                  |__ dict_table_get_first_index
                                                                                                      |                                  |__ btr_pcur_open_at_index_side
                                                                                                      |                                  |__ btr_pcur_get_page_cur
                                                                                                      |                                  |__ page_curr_get_rec                                                                  <--|
                                                                                                      |                                  |__ rec_get_offsets                                                                       |
                                                                                                      |                                  |__ if online --> trx->read_view->changes_visible & row_vers_build_for_consistent_read    |
                                                                                                      |                                  |__ row_build_w_add_vcol //build a index entry ____________________________________________
                                                                                                      |                                  |__ row_merge_buf_sort
                                                                                                      |                                  |__ row_merge_insert_index_tuples //build index btree bottom-up
                                                                                                      |                                  |__ row_merge_buf_free
                                                                                                      |                                  |__ btr_pcur_close
                                                                                                      |__ row_merge_file_destroy
                                                                                                      |__ row_merge_write_redo
                                                                                                      |__ row_log_apply //apply additional records by IUD

  ha_innobase::commit_inplace_alter_table --> trx_start_if_not_started_xa
                                          |__ row_merge_lock_table //MDL may be not enough for some cases @sa comments --> trx->in_innodb|=TRX_FORCE_ROLLBACK_DISABLE //no rollback for ddl
                                                                                                                       |__ lock_table_for_trx --> lock_table    <-|
                                                                                                                                              |__ queue __________|
  ```

* reference open_tables->lock_table_names for timed lockingXXX
  THD::lock and extra_lock XXX
  thd->lock = mysql_lock_tables // MYSQL_LOCK
  thr_lock_type

  is_inplace_alter_impossible

  MYSQL_LOCK_IGNORE_TIMEOUT Open_table_context constructor

  innodb_lock_wait_timeout???
  Table_locks_immediate
  Table_locks_waited

* m_prebuilt contains most of the InnoDB data associated with a table; @sa ha_innobase::index_read
* comments of ha_innobase::check_if_supported_inplace_alter has a lot of information
  prepare phase must hold exclusive MDL lock, lock of main phase is decided by HA_ALTER_INPLACE_NO_LOCK_AFTER_PREPARE etc;
* Alter_inplace_info::online is set true only if (HA_ALTER_INPLACE_NO_LOCK or HA_ALTER_INPLACE_NO_LOCK_AFTER_PREPARE) and (requested_lock is ALTER_TABLE_LOCK_DEFAULT or ALTER_TABLE_LOCK_NONE)
