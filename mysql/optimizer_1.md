* IN/EXISTS is implemented as semi-join, while NOT IN / NOT EXISTS is anti-semi-join
* IN with uncorrelated subquery can be rewriten into EXISTS with correlated subquery
	```
	select * from t1 where t1.c1 in (select t2.c1 from t2);
	select * from t1 where exists (select t2.c1 from t2 where t2.c1 = t1.c1);
	```
* all joins in mysql is implemented in nest-loop algorithm in DFS style, for example, for a
  3-table-join, first read one row from 1st table, then find a matching row in the 2nd table,
  then find a matching row in the 3rd table, output the selected columns, then backtrace to the
  2nd table, and find the next matching row, and then dive into the 3rd table, etc.

* 'type' column in explain shows the access method for base tables, and the result expectations
* explain join types:
  * system: only one row in the table
  * constant: compare to a constant and returns at most one row
  * eq_ref: depends on previous join output, only one matching row can be found;
  * ref: depends on previous join output, may return more than one row;
  * index_merge: return multiple rows with multiple range scan for a single table;
  * range: compare to a constant(range), and returns more than one rows
  * index: only scan the secondary index, no table/primary index scan;
  * all: full table scan

* optimizer will use index for "col like 'x%'", but not for "col like '%x'", that is,
  when the first char of the pattern is wildcard;
* preprocessing(constants) => range optimizer => join order => group by
* SEL_IMERGE --> SEL_TREE --> SEL_ARG
* optimizer trace gives great info about call stack;
* best_acess_path would choose between range type and other types(ref, etc);
* optimizer call stack:
  ```
  optimize --> make_join_plan --> extract_func_dependent_tables --> join_read_const_table
           |                  |__ estimate_rowcount --> add_group_and_distinct_keys
           |                  |                     |__ get_quick_record_count --> test_quick_select(range optimizer entry) --> find potential range indexes
           |                  |                                                                                             |__ get_mm_tree: range analysis module
           |                  |                                                                                             |__ get_key_scans_params(SEL_TREE) --> check_quick_select(SEL_ARG)
           |                  |__ Optimize_table_order::choose_table_order --> greedy_search --> best_extension_by_limited_search --> best_access_path(for one table) --> find_best_ref
           |                  |                                                                                                   |                                   |__ compare with range and full scan
           |                  |                                                                                                   |__ best_extension_by_limited_search
           |                  |__ get_best_combination //build JOIN_TAB according to the join order
           |__ make_join_select --> attach conditions to table
           |__ make_join_readinfo --> push_index_cond
           |__ make_tmp_tables_info --> make_group_fields
                                    |__ get_end_select_func
  ```

* test_quick_select
  ```
  test_quick_select --> compute full scan cost
                    |__ init PARAM
                    |__ traverse head.s.keys, compare with keys_to_use, add to PARAM
                    |__ get_mm_tree --> recursive get_mm_tree call based on predicate, e.g, AND, OR //get_mm_tree is for SEL_TREE build and simplification
                    |               |__ tree_and/tree_or: would do the merge of intervals --> key_or/key_and
                    |               |__ check tree->type == SEL_TREE::IMPOSSIBLE/SEL_TREE::ALWAYS
                    |               |__ (simplest case)get_full_func_mm_tree --> get_func_mm_tree --> get_mm_parts(new SEL_TREE) --> get_mm_leaf
                    |__ get_key_scans_params(SEL_TREE) --> check_quick_select(SEL_ARG) //check_quick_select calculate number of rows satisfying the specific index SEL_ARG,
                                                                                       //get_key_scans_params calculate cost and compare with full table scan
  ```

* key is index(struct KEY), key_part is part of the indexed columns
* SEL_TREE.keys[] is for AND(conjunctions), each element in keys[] is a SEL_ARG graph(OR, disjunctions); each element of keys[] is a index; OR between different indexes
  are recorded in merges[], each element is a OR representation, elements are AND-ed;
* ICP(Index Condition Pushdown) basically applies to index like (c1,c2,c3) while the WHERE condition is like "c1=xx and c2 like '%yy%' and c3 like '%zz%'", so c2 and c3 cannot be in range qualifier;

* Index Merge algorithm:
  ```
  foreach range scan:
    while (get_next):
      put rowid into Unique

  foreach rowid in Unique:
    output row
  ```
  code is in QUICK_ROR_INTERSECT_SELECT::get_next(), each range scan is QUICK_RANGE_SELECT::get_next(), row is in head->record[0]
  ROR is Rowid-Ordered-Retrieval

* if query has multiple ranges, and optimizer chooses range scan, e.g, 'b > 1 and b < 3 or b > 100' has 2 ranges, then in execution,
  it would first do range scan of 'b > 1 and b < 3', after this scan reaches end of file, then 'b > 100' range scan would start;
  this is implementated in function handler::multi_range_read_next, that is to say, row_search_mvcc is called multiple 'rounds', this
  is very IMPORTANT;

  same applies to tables having multiple partitions;

* MySQL cursor can only be used in stored procedure!!!
