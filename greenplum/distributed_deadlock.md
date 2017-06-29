### Distributed Deadlock
========================
* UPDATE without exclusive locking on QD:
	* Tx1: lock tuples on s1, wait tuples on s2;
	* Tx2: lock tuples on s2, wait tuples on s1;
	* distributed deadlock, which cannot be detected, so GPDB now acquires an
EXCLUSIVE lock on QD for table when executing UPDATE/DELETE

* One possible solution:
	* Tuple lock is implemented as transaction lock in PostgreSQL, so each time we are blocked on a transaction lock, we report this dependency to master, and master would act as a delegate of segments for lock waiting, so deadlock detector on master can detect this circle. However, some corner cases are not covered.