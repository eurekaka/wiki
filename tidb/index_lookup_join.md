* Close() of executors is called in defer func of writeResultset, i.e, after sending all data to client;
* Close() of IndexLookUpJoin would only close p.children[0], executor of inner child is built for each
  outer tuple in fetchInnerResults, and opened directly after built, in the defer func of fetchInnerResults,
  Close() is called;
* UnionScanExec allocates chunk memory in Open(), no need to free them in Close(), since golang has GC; so
  UnionScanExec does not implement Close() and use baseExecutor::Close
* PhysicalIndexJoin is also specially handled, inner plan is built directly in getIndexJoinByOuterIdx, and the
  prop of inner child is marked nil, so ::findBestTask would quickly return for inner child;
