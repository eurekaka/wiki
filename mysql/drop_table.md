* drop table when innodb_file_per_table=1
  ```
  buf_LRU_flush_or_remove_pages(BUF_REMOVE_FLUSH_NO_WRITE) --> buf_LRU_remove_pages --> buf_flush_dirty_pages --> buf_pool_mutex_enter
                                                                                                              |__ buf_flush_or_remove_pages --> buf_flush_list_mutex_enter
                                                                                                              |                             |__ buf_flush_try_yield
                                                                                                              |                             |__ buf_flush_list_mutex_exit
                                                                                                              |__ buf_pool_mutex_exit
  ```

* dict_sys->mutex in drop table:
  ```
  row_drop_table_for_mysql --> row_mysql_lock_data_dictionary
                           |__ dict_table_open_on_name --> dict_table_check_if_in_cache_low //need dict_sys->mutex for dict_sys->table_hash
                           |                           |__ dict_load_table //need dict_sys->mutex
                           |__ trx_start_for_ddl
                           |__ dict_stats_wait_bg_to_stop_using_table //set table->stats_bg_flag, need dict_sys->mutex
                           |__ dict_stats_recalc_pool_del //dict_sys->mutex and recalc_pool_mutex
                           |__ dict_stats_drop_table //mysql.innodb_table_stats and mysql.innodb_index_stats
                           |__ dict_table_prevent_eviction //dict_sys->table_LRU --> dict_sys->table_non_LRU
                           |__ dict_table_close --> table->release()
                           |__ check foreign key constraints by other tables
                           |__ check locks on this table, lock_remove_all_on_table, or add to background list
                           |__ mark all indexes unusable
                           |__ use private SQL parser of Innobase to delete entries in mysql SYSTEM TABLES //IMPORTANT, need dict_sys->mutex
                           |__ set data_dir_path if needed
                           |__ row_drop_ancilarry_fts_tables
                           |__ determine filepath
                           |__ row_drop_table_from_cache //free table object(dict_table_t), delete from dict_sys->table_hash, dict_sys->table_id_hash etc, need dict_sys->mutex
                           |__ row_drop_single_table_tablespace --> fil_delete_tablespace --> buf_LRU_flush_or_remove_pages
                           |                                                              |__ os_file_delete --> unlink
                           |__ row_mysql_unlock_data_dictionary
  ```

* innodb_buffer_pool_size: 425 GB
  innodb_buffer_pool_instances: 8
  a lot of DROP TABLE
  table size: 52GB
  simple select of 1-row-table would run 25 seconds
  show processlist: DROP TABLE checking permissions, SELECT opening tables
  iostat shows high IO write

  bottlenecks: Table_cache->lock(held by DROP TABLE, which is blocked by dict_sys->mutex, or buffer pool mutex)
  dict_sys->mutex

* drop table blocked by drop table:
  ```
  mysql_execute_command --> mysql_rm_table --> tdc_remove_table --> table_cache_manager.lock_all_and_tdc
                                                                |__ Table_cache_manager::free_table --> intern_close_table --> closefrm --> ha_innobase::close --> row_prebuilt_free --> dict_table_close --> mutex_enter(&dict_sys->mutex)
                                                                |__ table_cache_manager.unlock_all_and_tdc
  ```

* reproduction steps:
  * drop a big table, not dropped before;
  * drop another big table
  * select from a small table(not opened before)
  * select from a small table

* TRUNCATE is indeed drop+create, so cannot work around this problem;

---

* dict_operation_lock in row_drop_table_for_mysql is for preventing foreign key checks etc while we are dropping the table, and for
  preventing deadlock; dict_sys->mutex serializes CREATE TABLE and DROP TABLE, as well as reading the dictionary data for a table from
  system tables;

* when dropping an innodb_file_per_table table, we can only remove buf_block_t or buf_page_t of this tablespace from flush list, which is
  necessary because page cleaner thread may flush this page to disk after we delete the file; it is not necessary to remove page headers
  from LRU, because no one would access those pages anymore, and they would be evicted by LRU algorithm(since they are not in flush list,
  no error would occur); it is said AHI has already been dealt with when freeing up extents. @sa buf_LRU_flush_or_remove_pages

* why we need buffer pool mutex when removing flush list? buf_page_t.io_fix is protected by this mutex, and we need to check the io status
  of pages; meanwhile, to obey the latching order, we have to release flush_list_mutex for a while in buf_flush_or_remove_page, in order to
  guarantee this page would not be relocated or removed by other threads in buf_flush_remove or buf_flush_relocate_on_flush_list, we hold
  the buf_pool->mutex; @sa buf_flush_or_remove_page

* dict_sys->mutex protects current table from being used by background stats thread, because it would mark table->stats_bg_flag under
  dict_sys->mutex

* if we release dict_syc->mutex before unlinking disk files, it is safe from my point of view, but if there are concurrent create table
  using same table name, the CREATE find no match in dictionary, but fails when creating files on disk; solution is to rename the files
  to a unique temp name, then release the dict_sys->mutex and delete files;
