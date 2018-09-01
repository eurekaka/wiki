* `prepare` is very simple in optimizer, just return a Prepare node,
  with select query recorded, then a `PrepareExec` is built, and its
  `Next` method would parse the recorded select query, and apply
  the preprocess, then encapsulate this SeleteStmt into a struct `Prepared`;
  the SelectStmt would be converted into logical plan in this `Next` method
  as well; this `Prepared` struct would be allocated an ID, and stored the
  map into the `PreparedStmts` of session vars; no physical optimization
  involved here;

* `execute` would first build an `Execute` node in plan builder, which records
  the values of parameters in `UsingVars`, then it would pick the special code
  path in `Optimize -> Execute::optimizePreparedPlan`; in this function,
  it would first get `Prepared` from `PreparedStmts` of session vars by matching
  the name, then evaluate the parameters, and save them in `PreparedParams` of
  session vars(later ParamMaker would query this for param value); then `getPhysicalPlan`
  would produce the final plan of the executed statement; plan cache for prepared
  statement is checked here in `getPhysicalPlan`, if any match is found, it would
  call `rebuildRange` and reuse the plan;
