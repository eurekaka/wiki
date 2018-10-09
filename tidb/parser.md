* there are two kinds of conflicts:
  - reduce/reduce conflict
  - shift/reduce conflict

  shift can be treated as stack push, reduce is stack pop;
  reduce/reduce conflict is that we have more than one choice for reduction;
  reduce/reduce can only be resolved by rewriting grammar;
  shift/reduce can be resolved by precedence or associative primitives;

  we can declare precedence in the first block, and we can also specify precedence
  for a rule using `%prec xxx`

  @sa `https://www.gnu.org/software/bison/manual/html_node/Reduce_002fReduce.html`
  `https://www.gnu.org/software/bison/manual/html_node/Contextual-Precedence.html`
  `https://github.com/pingcap/tidb/pull/7842`
