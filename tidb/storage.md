* each region is 64MB by default;
* read and write are all handled by leader??? leader is a thread?
* each region and its replica forms a raft group;
* region data is replicated as raft log;
* tikv implements mvcc by adding a version to each key(timestamp?)
* tikv implements 2pc in percolator style(solve single coordinator bottleneck and latency problem);
* tikv adopts optimistic locking for transaction, each statement inside a transaction would not
  detect write-write conflicts until transaction commit(INSERT IGNORE is an exception), each
  statement would write affected key-value maps into local transaction buffer, and do the locking
  and checking in transaction commit, if check fails, retry this whole transaction automatically;
  this is similar with spanner; it works well for scenario with few conflicts, but bad for
  situation with a lot of contention;

  read-write conflicts are handled by mvcc;

* region distribution info? single point?
  data distribution model:
  - GPDB: each compute node can get storage node of region by hash function, no single point, but
    cluster size must be fixed, not frendly for auto-scaling;
  - Hadoop: each compute node ask NameNode for data distribution info, good for auto-scaling, but
    not as effecient as first one;

* only int primary key of one column is chosen as RowID/handle;
* seems no catalog table; catalog info is stored as serialized bytes in key-value, key is table id
  or database id, but how about users catalog?

* in Tidb, 8 bytes to store integer/float-point values, 16 bytes to store datetime/timestamp values
  and 40 bytes to store decimal values;
