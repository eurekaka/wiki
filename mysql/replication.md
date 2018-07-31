* fully synchronous replication: master must wait for the finish of committing on slave before it can commit in master;
  semi-sync: only wait for the flush of relay log in limited time, not wait for commit on slave;

* 2 variables for semi-sync: rpl_semi_sync_master_wait_for_slave_count, and rpl_semi_sync_master_wait_point

* set up replication:
  * create repl user
  * grant all to repl
  * reset master
  * start slave mysqld
  * load plugin if needed
  * change master
  * reset master //on slave
  * start slave
