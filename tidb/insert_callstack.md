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
  session::executeStatement -> runStmt -> ExecStmt::Exec
  ```
