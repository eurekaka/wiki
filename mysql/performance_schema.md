* performance_schema tables are considered local to server, and is not replicated;
* tables of performance_schema are in-memory tables, no disk write;

* try_acquire_lock_impl --> mysql_mdl_create --> inline_mysql_mdl_create --> PSI_server(PSI_v1 type)->create_metadata_lock(create_metadata_lock_v1_t type) --> pfs_create_metadata_lock_v1() --> create_metadata_lock()
* create_metadata_lock --> global_mdl_container.allocate() // PFS_mdl_container, aka, PFS_buffer_container<PFS_metadata_lock>, remove one from m_array

  allocate memory to store the information
* acquire_lock --> pfs_start_metadata_wait_v1 // modify PSI_metadata_locker_state(thread local), and PFS_thread.m_events_waits_current(PFS_thread stored by pthread_setspecific()), or global_metadata_stat
               |__ pfs_end_metadata_wait_v1 // modify global_metadata_stat, or insert into table events_waits_history, events_waits_history_long, PFS_thread.m_instr_class_waits_stats;
               |__ pfs_set_metadata_lock_status_v1 // modify PFS_metadata_lock
* struct with PSI_ prefix is an abstract of v1 and v2, they are opaque structs, no definition, just for variable passing, must be reinterpreted in each implementation

* each PSI submodule has a struct PFS_instr_class, e.g, global_metadata_class, which records whether the submodule is enabled; inited in register_global_classes()
  besides, MDL has a single instrument class PFS_single_stat global_metadata_stat

  ```
  #0 register_global_classes
  #1 initialize_performance_schema
  #2 mysqld_main
  ```

* where does information stored?

  PFS_metadata_lock, PFS_thread, events_waits_history, and events_waits_history_long

* how to read information?

  ```
  select * from metadata_locks;

  #0 ha_perfschema::rnd_next
  #1 handler::ha_rnd_next
  #2 rr_sequential
  #3 join_init_read_record
  #4 sub_select
  #5 do_select
  #6 JOIN::exec

  rnd_next --> table_metadata_locks::rnd_next --> global_mdl_container.iterate // find all PFS_metadata_lock
  ```

  select * from events_waits_current/events_waits_history/events_waits_history_long; // stages, statements, transactions, waits

* what does 'update setup_instruments' do? update perfschema storage engine table, change global_metadata_class.m_enabled value in memory

  ```
  #0 table_setup_instruments::update_row_values
  #1 PFS_engine_table::update_row
  #2 ha_perfschema::update_row
  #3 handler::ha_update_row
  #4 mysql_update
  #5 Sql_cmd_update::try_single_table_update
  #6 Sql_cmd_update::execute
  #7 mysql_execute_command
  #8 mysql_parse
  #9 dispatch_command
  ```

* VICTIM, KILLED and TIMEOUT status? where does this recorded? XXX
* GET_LOCK() implementation? locking service? XXX
