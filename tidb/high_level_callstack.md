* Server struct represents MySQL protocol layer, it has a global instance
  pointer declared in tidb-server.go, the Server is built in createServer()
  called by main();

* callstack:
  ```
  main -> createServer
       |_ runServer -> Server::Run -> Server::listener::Accept
                                   |_ go Server::onConn -> Server::newConn //build clientConn(THD)
                                                        |_ clientConn::handshake //authentication
                                                        |_ clientConn::Run

  clientConn::Run -> clientConn::readPacket
                  |_ clientConn::dispatch -> set clientConn::ctx to instance of TiDBContext, implements QueryCtx
                                          |_ check message type
                                          |_ clientConn::handleQuery -> clientConn::QueryCtx::Execute
                                                                     |_ clientConn::writeResultset

  TiDBContext::Execute -> session::Execute //jump into session package -> session::execute
  Session interface is the abstrack of SQL engine procedures
  session::execute -> session::prepareTxnCtx
                   |_ session::ParseSQL
                   |_ Compiler::Compile //jump into executor package, Compiler is optimizer, returns ExecStmt for execution
                   |_ session::executeStatement
  ```

* critical structs and interfaces:
  - Server
  - clientConn
  - Session/session
  - Compiler

  - RecordSet/recordSet: each recordSet has an Executor, provide the Next() interface; recordSet::Next() would call Executor::Next()
  - Executor: interface of an operator, provide Next() method
  - ExecStmt: plan node, ExecStmt::Exec -> ExecStmt::buildExecutor would return an Executor for this plan node; ExecStmt implements Statement interface

* entry point of executor:
  - SELECT query: writeChunks()
  - INSERT query: ExecStmt::Exec

* is-a relationship of interfaces:
  Node -> StmtNode -> DMLNode
  is-a relationship of structs (partially) satisfying these interfaces:
  node -> stmtNode -> dmlNode -> InsertStmt

  Node interface defines the Accept method, but node, stmtNode and dmlNode does not implements this method,
  so these structs do not satisfy Node interface; only leaf struct such as InsertStmt implements Accept method;

  Visitor is an interface, preprocessor is a struct satisfying this interface;
  implementaion of Accept method usually likes:
  - Visitor::Enter
  - call Accept method for children nodes
  - Visitor::Leave
  comparatively, Accept is walker, Visitor is mutator;

* Why we need executor build? to convert general physical plan to operators of tikv;
