### Resource Queue Lock
=======================
* Motivations behind current implementation:
	* why using regular lock: status report(pg_locks, users want to know the process is waiting for resource queue), deadlock detection(lwlock has no deadlock detector)
	* why not simply using LockAcquire: semantically, resource queue is not a boolean lock, it is a count in the share memory, so LockAcquire is not enough;
	* hence, simple LockAcquire and simple lwlock + count is not enough, so we have to maintain status of both mechanisms in share memory; indeed it works, but we have some hidden bugs(hard to reproduce) when signals are mixed with this
locking stuff, and the bugs would be escalated to PANIC in lock manager;