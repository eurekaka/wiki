* stack of innodb read using index:
  ```
  JOIN::exec --> do_select --> sub_select --> join_read_always_key --> ha_innobase::index_read --> build_template //m_prebuilt->mysql_template, used for row format transform of server and engine
                                                                                               |__ row_sel_convert_mysql_key_to_innobase //m_prebuilt->search_tuple
                                                                                               |__ row_search_mvcc

  tuples not satisfying ICP would not be returned from row_search_mvcc

  row_search_mvcc --> check whether cached tuples exist
                  |__ mtr_start
                  |__ check whether AHI contains tuples
                  |__ trx_assign_read_view
                  |__ btr_pcur_open_with_no_init --> btr_pcur_get_btr_cur
                  |                              |__ btr_cur_search_to_nth_level
                  |__ btr_pcur_get_rec
                  |__ mark rec_loop
                  |__ cmp_dtuple_rec //if not equal, return DB_RECORD_NOT_FOUND
                  |__ lock_sec_rec_cons_read_sees --> page_get_max_trx_id //check if tuple in the secondary index is visible, if not, query cluster index for previous version
                  |                               |__ view->sees
                  |__ row_search_idx_cond_check //if ICP_NO_MATCH, goto next_rec; if ICP_MATCH, find tuple in clustered index by row_sel_get_clust_rec_for_mysql;
                  |__ row_sel_get_clust_rec_for_mysql
                  |__ row_sel_store_mysql_rec //convert tuple to mysql format
                  |__ pre-fetch staff
                  |__ row_sel_store_row_id_to_prebuilt
                  |__ btr_pcur_store_position, then goto normal_return
                  |__ mark next_rec: btr_pcur_move_to_next, then goto rec_loop
                  |__ mark normal_return: mtr_commit
  ```
* in InnoDB secondary index, each page has a max_trx_id indicating the highest id of a trx which may have modified a record on the page
* entry point of plan execution:
  ```
  do_select --> join->first_select/sub_select(0)
            |__ sub_select(1) --> QEP_TAB->next_select/sub_select_op --> JOIN_CACHE::join_records --> join_matching_records --> QEP_TAB->read_first_record
            |                                                        |                                                      |__ generate_full_extensions --> QEP_TAB->next_select/end_send(0) --> Query_result_send::send_data //store into network buffer
            |                                                        |__ sub_select(1)
            |__ join->select_lex->query_result()->send_eof

  QEP_TAB->next_select is end_send for the last table in the join, while sub_select_op for intermediate tables; for 3-table-join, QEP_TAB->next_read in generate_full_extensions is sub_select_op --> JOIN_CACHE::put_record/JOIN_CACHE::join_records

  sub_select --> if end_of_records, qep_tab->next_select
             |__ while loop --> qep_tab->read_first_record or qep_tab->read_record.read_record
             |              |__ evaluate_join_record --> qep_tab->next_select(qep_tab+1, 0) //sub_select_op --> JOIN_CACHE::put_record/JOIN_CACHE::join_records
             |__ evaluate_null_complemented_join_record

  qep_tab->read_first_record is join_init_read_record

  join_init_read_record --> QEP_TAB->remove_duplicates //distinct
                        |__ QEP_TAB->sort_table //filesort
                        |__ init_read_record //set reading function, in read_record field, to be rr_quick or rr_sequential normally
                        |__ QEP_TAB->read_record.read_record() //read first match

  rr_quick means index access method, rr_sequential means full table scan;
  ```

* stack to set limit and offset:
  ```
  mysql_execute_command --> execute_sqlcom_select --> handle_query --> st_select_lex_unit::set_limit
  ```

* whole picture stack of index scan:
  ```
  mysql_execute_command --> execute_sqlcom_select --> handle_select --> mysql_select --> mysql_prepare_select
                                                                                     |__ mysql_execute_select --> join->optimize
                                                                                                              |__ join->exec --> prepare_result
                                                                                                                             |__ send_result_set_metadata
                                                                                                                             |__ do_select --> join->first_select/sub_select --> join_tab->prepare_scan
                                                                                                                                                                             |__ if in_first_read, join_tab->read_first_record/join_init_read_record --> init_read_record //set read_record function pointer
                                                                                                                                                                             |                                                                       |__ read_record/rr_quick --> QUICK_ROR_INTERSECT_SELECT::get_next --> QUICK_RANGE_SELECT::get_next --> ha_innobase::multi_range_read_next --> DsMrr_impl::dsmrr_next --> handler::multi_range_read_next --> quick_range_seq_next
                                                                                                                                                                             |                                                                                                                                                                                                                                                                               |__ read_range_first --> ha_index_read_map --> index_read_map --> ha_innobase::index_read --> build_template --> build_template_needs_field //check read_set and write_set
                                                                                                                                                                             |                                                                                                                                                                                                                                                                                                                                                                         |                  |__ build_template_field
                                                                                                                                                                             |                                                                                                                                                                                                                                                                                                                                                                         |__ row_search_for_mysql --> trx_assign_read_view
                                                                                                                                                                             |__ else info->read_record
  ```
