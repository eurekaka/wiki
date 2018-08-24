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
                                                         |_ ExecStmt::handleNoDelayExecutor -> InsertExec::Next -> InsertValues::getColumns
                                                                                                                |_ InsertValues::insertRows -> InsertValues::getRow -> Constant::Eval
                                                                                                                                            |_ InsertExec::exec -> InsertExec::insertOneRow -> tikvTxn::SetOption(PresumeKeyNotExists) //not check duplicates until txn commit
                                                                                                                                                                                            |_ tableCommon::AddRecord
                                                                                                                                                                                            |_ tikvTxn::DelOption(PresumeKeyNotExists)

  tableCommon::AddRecord -> get recordId
                         |_ tableCommon::addIndices -> index::Create -> index::GenIndexKey //build index-row key
                         |                                           |_ RetriverMutator.Set //write index-row key and value(recordID)
                         |_ tableCommon::RecordKey -> EncodeRecordKey
                         |_ EncodeRow
                         |_ txn.Set
  ```

* executor callstack of `INSERT IGNORE`, check and generate warning in the statement, not in txn commit:
  ```
  InsertExec::exec -> InsertValues::batchCheckAndInsert -> batchChecker::batchGetInsertKeys -> batchChecker::getKeysNeedCheck //build keys to be checked for duplicity
                                                        |                                   |_ BatchGetValues //get key-value map from tikv and txn cache using the keys above
                                                        |_ check whether kv map exists for the inserting row
                                                        |_ insertOneRow //safe to insert now
                                                        |_ add the inserted row into the kv map, for check of later rows in the statement
  ```

  reason of check duplicate in statement for INSERT IGNORE:
  - if in txn commit, txn has to differentiate rows from INSERT and INSERT IGNORE;
  - clients want to know immediately which rows are duplicte from warnings;


* executor callstack of `INSERT ON DUPLICATE UPDATE`, first check is in statement, check after update is in txn commit:
  ```
  InsertExec::exec -> InsertValues::batchUpdateDupRows -> batchChecker::batchGetInsertKeys -> batchChecker::getKeysNeedCheck //build keys to be checked for duplicity
                                                       |                                   |_ BatchGetValues //get key-value map from tikv and txn cache using the keys above
                                                       |_ batchChecker::initDupOldRowValue -> batchChecker::initDupOldRowFromHandleKey //collect keys
                                                       |                                   |_ batchChecker::initDupOldRowFromUniqueKey -> batchChecker::batchGetOldValues -> BatchGetValues //get rows possiblly to be updated due to duplicity
                                                       |_ check whether kv map exists for the inserting row
                                                            |_ if duplicate -> InsertExec::updateDupRow
                                                            |_ if not duplicate -> insertOneRow
                                                                                |_ batchChecker::fillBackKeys //add the inserted row into the kv map, for check of later rows in the statement
  ```

* compared with `INSERT ON DUPLICATE UPDATE`, `REPLACE` is much simpler in implementation, first find all conflicted rows, delete them, then do the insert of the row;
  ```
  ReplaceExec::Next -> InsertValues::getColumns
                    |_ InsertValues::insertRows -> InsertValues::getRow -> Constant::Eval
                                                |_ ReplaceExec::exec -> batchChecker::batchGetInsertKeys -> batchChecker::getKeysNeedCheck
                                                                     |                                   |_ BatchGetValues
                                                                     |_ batchChecker::initDupOldRowValue -> batchChecker::initDupOldRowFromHandleKey
                                                                     |                                   |_ batchChecker::initDupOldRowFromUniqueKey -> batchChecker::batchGetOldValues -> BatchGetValues
                                                                     |_ ReplaceExec::replaceRow -> ReplaceExec::removeRow
                                                                                                |_ ReplaceExec::removeIndexRow -> ReplaceExec::removeRow
                                                                                                |_ ReplaceExec::addRow
                                                                                                |_ batchChecker::fillBackKeys
  ```

* inheritance relationship:
  ```
  batchChecker -> InsertValues -> InsertExec
                               |_ ReplaceExe
  ```
