* optimizer callstack:
  ```
  Compiler::Compile -> Preprocess -> preprocessor visit //actions such as fill the dbname of table, preprocessor::handleTableName
                    |_ Optimize -> build planBuilder
                    |           |_ planBuilder::build -> planBuilder::buildSelect -> planBuilder::buildResultSetNode ... -> planBuilder::buildDataSource -> getPossibleAccessPaths
                    |           |                                                 |                                                                      |_ build DataSource
                    |           |                                                 |_ planBuilder::unfoldWildStar
                    |           |                                                 |_ planBuilder::buildSelection -> splitWhere ----------------------
                    |           |                                                 |                              |_ SplitCNFItems                   |-> logical reduction of WHERE recursively
                    |           |                                                 |                              |_ EvalBool //evaluate const expr---
                    |           |                                                 |                              |_ SetChildren //LogicalSelection as the parent node
                    |           |                                                 |_ planBuilder::buildProjection //LogicalProjection as the parent node
                    |           |                                                 |_ return LogicalPlan
                    |           |_ doOptimize -> logicalOptimize //apply rules, columnPruner, projectionEliminater, and ppdSolver usually, in recursive style
                    |                         |_ physicalOptimize -> baseLogicalPlan::preparePossibleProperties //recursively
                    |                                             |_ baseLogicalPlan::findBestPlan //recursively
                    |                                             |_ basePhysicalPlan::ResolveIndices //recursively
                    |_ build ExecStmt

  DataSource::PredicatePushDown -> ExpressionsToPB
  LogicalSelection::PredicatePushDown would return child, i.e, DataSource, hence remove LogicalSelection node from the tree
  interface inheritance relationship:
  Plan -> LogicalPlan
       |_ PhysicalPlan

  struct inheretance:
  basePlan -> baseLogicalPlan
           |_ basePhysicalPlan
  ```

* executor callstack:
  ```
  session::executeStatement -> runStmt -> ExecStmt::Exec -> ExecStmt::buildExecutor -> newExecutorBuilder
                                                         |                          |_ executorBuilder::build -> executorBuilder::buildInsert -> build InsertExec //wrap up Insert plan node
                                                         |_ InsertExec::Open //nop
                                                         |_ ExecStmt::handleNoDelayExecutor -> InsertExec::Next -> InsertExec::getColumns
                                                                                                                |_ InsertValues::insertRows -> InsertValues::getRow -> Constant::Eval
                                                                                                                                            |_ InsertExec::exec -> InsertExec::insertOneRow -> tableCommon::AddRecord

  tableCommon::AddRecord -> get recordId
                         |_ tableCommon::addIndices -> index::Create -> index::GenIndexKey //build index-row key
                         |                                           |_ RetriverMutator.Set //write index-row key and value(recordID)
                         |_ tableCommon::RecordKey -> EncodeRecordKey
                         |_ EncodeRow
                         |_ txn.Set
  ```
