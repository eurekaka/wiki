* compile(suitable for debugging) and debug a main package:
  ```
  dlv debug github.com/pingcap/tidb/tidb-server
  break main.main
  continue
  ```

* common used commands:
  ```
  funcs plan.Optimize* //list functions like Optimize in package plan
  breakpoints //info break
  call //experimental
  clearall //delete
  condition //conditional breakpoint
  disassemble
  frame
  exit
  goroutine //info threads
  list
  regs
  set //change variable value
  stack
  step
  whatis //!!!
  ```

* serveral commands to start debugging:
  ```
  dlv debug //compile and debug
  dlv exec //debug
  dlv core //examine a core dump
  dlv attach //attch to a running process
  ```

* if string is too long to display(longer than 64 bytes), use this:
  ```
  p sql[64:]
  ```

* tidb has background goroutine executing "updateStatsWorker" and "Load", hence session.execute would be hit
  frequently, to break only for our simple queries, use:
  ```
  b session.execute //e.g id is 1
  cond 1 len(sql) < 30
  ```
