* 4 types of goroutines to implement parallel hash join:
  - 1) main goroutine: start functional goroutines, and then drain the joinResultCh;
  - 2) goroutine to build hash table for inner table;
  - 3) goroutine to scan outer table;
  - 4) goroutines to do probing;

  communications of these goroutines:
  - 3 would wait for the finish of 2 before dispatching outer rows to workers(4)

  - 3 write chunks into chan(outerResultChs[i]) of each worker;
  - 4 write a outerChkResource(a chunk and a chan) to chan of 3(outerChkResourceCh), notice 3
    that 4 has used up the outer rows, then 3 would use this chunk to read outer table into, and
	write the chunk into the this chan; this design is for saving memory, i.e, using a
	single chunk to passing outer rows for each worker;

  - 4 would call getNewJoinResult to get a chunk to write join result into;
  - 4 read the outerResultChs[i] to get outer rows, do the probing, write the join
    result into hashJoinWorkerResult::chk, and write this hashJoinWorkerResult
	into joinResultCh when chunk is full;
  - 1 would read joinResultCh to get join result, and write this chunk to hashJoinWorkerResult::src,
    i.e, return this chunk memory to reuse; this design is for saving memory, i.e, using
	a single chunk to passing join result to 1 for each worker;
  - 4 would wait for the chunk to be available in getNewJoinResult, i.e, after 1 has read the join result,
    in specific, 4 wait for joinChkResourceCh[i];

* NULL does not match NULL, so rows with NULL join key would not be in hash table for inner table, and they
  would be skipped when doing probe for outer table;

* joinResultGenerator is an abstract of different behaviors for join types;
* filters for inner table can be pushed down coprocessor, while filters for outer table cannot, because it
  would effect the correctness of outer join; for inner join, it is ok;

  In function `HashJoinExec::runJoinWorker -> HashJoinExec::join2Chunk`, for the outer rows not satisfying
  outer filters, joinResultGenerator::onMissMatch is called, this row may be outputed if this is left outer join
  for example; this is why the outer filters cannot be pushed down to coprocessor;

* allocate a chunk:
  ```
  NewChunkWithCapacity -> new Chunk
  					   |_ Chunk::addColumnByFieldType -> getFixedLen
					   							      |_ Chunk::addFixedLenColumn/Chunk::addVarLenColumn
  ```
