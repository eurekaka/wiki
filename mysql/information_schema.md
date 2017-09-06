* how to add a view in information_schema? // @sa call of fill_schema_processlist

* show processlist;

  ```
  #0 mysqld_list_processes
  #1 mysql_execute_command

  mysqld_list_processes --> build field_list(tupledesc)
                        |__ thd->send_result_metadata --> m_protocol->start_result_metadata
                        |                             |__ m_protocol->start_row ---------------
                        |                             |__ m_protocol->send_field_metadata     |- loop
                        |                             |__ m_protocol->end_row   ---------------
                        |                             |__ m_protocol->end_result_metadata
                        |__ Global_THD_manager::get_instance()->do_for_all_thd_copy(List_process_list) // save to local, List_process_list::(), THD.LOCK_thd_query, THD.LOCK_thd_data?XXX
                        |__ sort
                        |__ foreach thread_info, protocol->start_row(), protocol->store(), protocol->end_row()
  ```

* select * from processlist;

  each information_schema table has a st_schema_table struct, which contains the function pointers
  create_table, fill_table, the struct instances are hard-coded

  ```
  // parser phase, recognize schema tables
  #0 find_schema_table // save it in TABLE_LIST.schema_table
  #1 add_table_to_list // check if it is information_schema dbname, mark OPTION_SCHEMA_TABLE
  # ...
  #x parse_sql // parser entry point
  #x mysql_parse
  #x dispatch_command

  // open table phase, create temp tables for schema_table
  #0 create_schema_table
  #1 TABLE_LIST.schema_table.create_table
  #2 mysql_schema_table // create temporary information_schema table
  #3 open_and_process_table // if tables->schema_table
  #4 open_tables
  #5 open_tables_for_query
  #6 execute_sqlcom_select
  #7 mysql_execute_command

  create_schema_table --> build field_list(tupledesc)
                      |__ build Temp_table_param, mark the schema_table to 1
                      |__ create_tmp_table --> ha_innobase::create

  // before execution, build rows and fill into temp table
  #0 fill_schema_processlist
  #1 do_fill_table // table_list->schema_table->fill_table
  #2 get_schema_tables_result
  #3 JOIN::prepare_result // instantiate derived table and schema_table, check OPTION_SCHEMA_TABLE
  #4 JOIN::exec

  fill_schema_process_list --> Global_THD_manager::get_instance()->do_for_all_thd_copy(Fill_process_list)

  operator() --> TABLE.field[x].store()
             |__ schema_table_store_record --> TABLE.file->ha_write_row --> ha_innobase::write_row
                                           |__ check whether need to convert HEAP to MyISAM

  // query SE phase
  #0 handler::ha_rnd_next
  #1 rr_sequential
  #2 join_init_read_record
  #3 sub_select
  #4 do_select
  #5 JOIN::exec
  ```

* internal temporary table can be HEAP, MyISAM, and InnoDB SE; if the table has TEXT or BLOB type, then
  HEAP would not be chosen;
* HEAP table can be converted to disk when size too large, max_heap_table_size is the sentinel; while
  if a temp table chooses HEAP SE, the size is guarded by both max_heap_table_size and tmp_table_size;
* HEAP SE would store tuples as fixed length row format;

* show session status;

  ```
  mysql_execute_command --> SQLCOM_SHOW_STATUS branch --> execute_sqlcom_select('information_schema.status')

  same as select * from processlist then, except:
  fill_table = fill_status

  fill_status --> thd->status_var
              |__ show_status_array --> loop --> get_one_variable(thd->status_var)

  schema_table identified in parser by:
  MYSQLparse --> STATUS_SYM --> prepare_schema_table --> make_schema_select --> add_table_to_list --> find_schema_table
  ```

* select * from session_status, same as show session status;

* optimizer_trace:
  * normal statements would not fill in optimizer_trace table, stored in THD.opt_trace per session per query;
  * creating and filling in temp table is done when select * from optimizer_trace;

  ```
  fill_optimizer_trace_info --> iterate thd->opt_trace
                            |__ fetch info.query_ptr and info.trace_ptr ...
                            |__ schema_table_store_record
  ```

* explain for connection <id>;

  ```
  mysql_explain_other
  mysql_execute_command

  mysql_explain_other --> build Find_thd_query_lock
                      |__ Global_THD_manager::get_instance()->find_thd(find_thd_query_lock)
                      |__ explain_query

  traverse THD for query plan
  ```

* two options for reading all tickets:
  * iterate global variable mdl_locks
    mdl_locks is LF_HASH, so we can not get consistent view of whole hash table without a lock, for
    example, we iterate the hash table while someone insert one element, we cannot get this into account;
    not to mention the LF_HASH is not designed for sequential iterating(remove_random_unused() would
    randomly dive into the hash table, not iterating); LF_HASH also contains pointers which are marked as
    deleted(IS_DESTROYED), or are unused;
  * iterate THD.mdl_context
    mdl_context.m_tickets is updated and read only by the thread itself, with no lock protection;
    mdl_context.m_waiting_for is updated by the thread itself, but can be read by other threads in deadlock
    detector, so m_LOCK_waiting_for should be acquired before accessing it;
    no consistent snapshot we can take even for a single thread, not to mention for all threads, due to the
    unsafe m_tickets;
  * lock free code path means we cannot take consistent snapshot at all.
