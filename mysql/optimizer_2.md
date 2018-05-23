* JOIN.qep_tab is an array for execution plan, with JOIN.tables elements;

  ```
  class JOIN
  {
    QEP_TAB *qep_tab;
    uint tables;
    JOIN_TAB *best_ref;
  }

  class QEP_TAB
  {
    TABLE_LIST *table_ref;
  }

  struct TABLE_LIST
  {
    ST_SCHEMA_TABLE *schema_table;
    Temp_table_param *schema_table_param;

    TABLE *table;
  }

  struct TABLE
  {
    TABLE_SHARE *s;
    handler *file;

    Field **field; // columns
  }
  ```

* LooseScan is an execution strategy of semi-join, just get the group info from the scanned table;
  @sa Mariadb Knowledge base: LooseScan

* index used by range is stored in qep_tab->quick()->index, index used by ref is stored in qep_tab->ref().key
* join buffer cache is some kind of batch processing;

* In multi-table join query, order by can be implemented by index scan only if the columns in the order by clause
  all comes from the first nonconstant table, and those columns are compatible with the index chosen on that table;
  that is to say, if ORDER BY columns from multi-table, we must use filesort;

  if query has different order by and group by expression, filesort is used;
