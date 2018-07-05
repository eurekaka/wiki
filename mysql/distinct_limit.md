* how MySQL implements query "select distinct(c1) from tbl limit 10000,2":

  it would call interfaces of innodb like rr_sequential to get tuple one by one,
  and store the tuple into a temp table; the temp table is initially a ha_heap table,
  if max_heap_table_size is hit, this heap table would be transformed into ha_myisam;
  the temp table would enforce the duplicate check(the heap table may be in rb-tree form);
  heap table has a variable max_records which is 10002 in this case, it would not accept
  more tuples than this number to achieve quick termination of limit, by checking `records`
  variable;

  @sa function end_write() for details, pay attention to variable found_records and send_records
  for quick termination;
