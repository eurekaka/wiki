### Cache Invalidation of PostgreSQL
====================================
* If we have modified syscache, we broadcast this change when commiting
transaction. During the command which modifies syscache, we does not reflect the
change in the cache until the end of the command.

* If we are reading syscache, for each command internal, if there are several
readings, should we check the validility each time since we are using
`SnapshotNow` for catalog table?
	* Yes, `AcceptInvalidationMessages` calls are spread across code base, and each time
we want to read syscache, we have to `heap_open` the table first, `heap_open` would
lock the table/oid, `LockRelation` related functions would call
`AcceptInvalidationmessages`, that is to say, each time we want to use syscache,
we would check for invalidation messages. However, there always exist a race condition here, so is cache of PostgreSQL not safe enough?
