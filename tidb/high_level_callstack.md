* Server struct represents MySQL protocol layer, it has a global instance
  pointer declared in tidb-server.go, the Server is built in createServer()
  called by main();

* callstack:
  ```
  main -> createServer
       |_ runServer -> Server::Run -> Server::listener::Accept
                                   |_ go Server::onConn -> Server::newConn //build clientConn(PGPROC)
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

  - RecordSet/recordSet: each recordSet has an Executor, provide the NEXT() interface
  - Executor: interface of an operator
  - ExecStmt: plan node, ExecStmt::Exec -> ExecStmt::buildExecutor would return an Executor for this plan node

* entry point of executor:
  - SELECT query: writeChunks()
  - INSERT query: ExecStmt::Exec
