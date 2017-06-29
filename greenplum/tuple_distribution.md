### Tuple Distribution
========================
* One way to make GPDB expanding easier:
	* First, compute the hash value of a column data, then map it to a bucket by mod, where max number of buckets is fixed, like say 10000, and then map this bucket number to segments by mod; the core idea here is that, if we add more segments, we only need to move tuples bucket by bucket, not tuple by tuple.
	* Hadoop handles this by recording the data node of each tuple on name node, so every time more data nodes are added, no data movement is needed, but the cost is querying name node each time we want to access a tuple.
	* Or some algorithm like consistent hash.