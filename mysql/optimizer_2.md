* JOIN.qep_tab is an array for execution plan, with JOIN.tables elements;

  ```
  class JOIN
  {
    QEP_TAB *qep_tab;
    uint tables;
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
