* delve only supports x86_64 arch, no support for 386; delve requires go 1.9 at least;
* tidb requires 1.10 at least, otherwise math.Round has not been introduced yet;
  tidb cannot use go 1.10.3 on linux, it would report "Unexpected directory layout",
  1.11beta3 proved to work fine;

* to disable optimizations:
  ```
  go build -gcflags all='-N -l'

  or cd into tidb-server and run `dlv debug`
  ```
