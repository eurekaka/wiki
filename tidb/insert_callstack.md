* optimizer callstack:
  ```
  Compiler::Compile -> Preprocess -> preprocessor visit //actions such as fill the dbname of table, preprocessor::handleTableName
                    |_ Optimize -> build planBuilder
                    |           |_ planBuilder::build -> planBuilder::buildInsert -> planBuilder::buildValuesListOfInsert -> build Constant and store them in Insert::Lists
                    |           |_ doOptimize //skipped by INSERT
                    |_ build ExecStmt
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
