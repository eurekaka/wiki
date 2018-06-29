* usually, buf passed from server to engine is for result row, the first byte is for NULL bits;
* if table column is defined with default value, then newly inserted rows would write the default
  value to disk, so when this row is read from disk, this column is not NULL; if we do ALTER TABLE
  MODIFY COLUMN DEFAULT for table with existing rows, existing rows with NULL in that column would
  not be changed to the default value, when these rows are read from disk, corresponding column would
  be marked NULL in the first byte, even if there are code to copy default value into buf; in the server
  layer, it would first check the NULL bit, and would not read the default value copied, so those code
  has no actual meaning;
* NULL is checked in InnoDB from len == UNIV_SQL_NULL;
* for agg, if all rows are NULL, then return NULL; otherwise, treat NULL as 0;
* there is a logic in row_sel_field_store_in_mysql_format_func:

  ```
  if (!templ->is_unsigned) {
      dest[len - 1] = (byte) (dest[len - 1] ^ 128);
  }
  ```

  this has counterpart in row_mysql_store_col_in_innobase_format:

  ```
  if (!(dtype->prtype & DATA_UNSIGNED)) {
      *buf ^= 128;
  }
  ```

  these has NO actual meaning at all, and can be removed;
  besides, code in these two function assumes that the data on the disk has different byte order
  with it in memory, this has NO actual meaning either, we can remove this if there is no compatibility
  concern;

* little-endian and big-endian is for byte order, inside each byte, they have same bit order; bit order follows same rule
  as big-endian;

  for a number 0xabcd1234 as an example:

  ```
  big_endian representation:
  low memory addr --> high memory addr
  ab cd 12 34

  little_endian representation:
  low memory addr --> high memory addr
  34 12 cd ab

  inside each byte, representation of 0x01 is:
  00000001
  ```

  x86 CPU is little_endian, PowerPC and network byte order are big_endian;

* 8 bytes can express long long type numbers in memory, they are in binary radix, if we want to express these numbers in
  decimal form of cstring, we need 20 bytes plus 1 byte for sign

* in mysql, numeric types would be converted to signed longlong or signed double for arithmetic computations, thus if column
  is defined as BIGINT UNSIGNED, and if the value is larger than 2^63, then result may be wrong if computation is involved;

* function implementations like shortget and longget in little_endian.h and big_endian.h have no fundamental difference, all
  are memcpy in fact, the byte order of numbers would not be changed;

* row_sel_store_mysql_rec cannot fail in this scenario because we restrict the data type to be numeric;

* AHI and prefetch is disabled when agg is pushed down, because they have no improvement at all in this case;

* agg pushdown means computation logic is pushed down to engine, hence type definition as well;

* we only need 9 bytes to store the agg result, first byte for NULL bits;

* intrinsic table is internal temporary table; user created temporary table is not intrinsic table, it uses row_search_mvcc
* callstack of scan:

  ```
  full table scan:
  rr_sequential -> ha_rnd_next -> rnd_next -> index_first -> index_read
                                           |_ general_fetch

  index scan:
  sub_select -> join_read_first -> ha_index_first -> index_first -> index_read
             |_ join_read_next -> ha_index_next -> index_next -> general_fetch

  general pattern is:
  -> index_read (first call) -> row_search_mvcc
  |_ general_fetch -> row_search_mvcc
  ```
