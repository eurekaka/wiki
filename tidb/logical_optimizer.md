* ppd
  ```
  LogicalJoin::PredicatePushDown -> simplifyOuterJoin -> simplifyOuterJoin(innerPlan/outerPlan)
                                 |                    |_ foreach predicates(CNF) isNullRejected(innerTable.Schema()) -> EvaluateExprWithNull //func isstrict not considered
                                 |_ getCartesianJoinGroup //get base tables if all joins are cartesian join, and no Straight join and no prefered join type
                                 |_ joinReOrderSolver::reorderJoin -> traverse conds, split them into 2 classes, one for join, the other for filter, build join edge and estimate rows of each DataSource based on these two classes respectively
                                 |                                 |_ fill rate of each join edge
                                 |                                 |_ Sort DataSource and join edge for each DataSource
                                 |                                 |_ joinReOrderSolver::walkGraphAndComposeJoin //build new join trees; XXX should break for loop in this function when one node is chosen to join?
                                 |                                 |_ joinReOrderSolver::makeBushyJoin //build a single bushy join tree from generated join trees
                                 |_ extractOnCondition
                                 |_ adjust leftCond/rightCond according to join type
                                 |_ leftPlan/rightPlan.PredicatePushDown(leftCond/rightCond)
                                 |_ addSelection

  ```

* buildJoin would always generate a left deep join tree? i.e, Join is always in left deep tree form?
  since getCartesianJoinGroup is directly called in LogicalJoin::PredicatePushDown, the answer should be yes.

* joinReOrderSolver::reorderJoin makes no sense at all; statistics and selectivity not considered; this function is ugly
  join optimization of TiDB is not optimal, reorderJoin decide the join order, and combinations, while baseLogicalPlan::findBestTask
  decide implementations of each join node and DataSource(and other operators) in recursive style; join reorder only applies for
  cartesian join query; PG generates optimal join plan due to the Dynamic Programing implementation;

* ppd would terminate when coming across LogicalLimit and LogicalMaxOneRow
  for LogicalMaxOneRow, consider this query:
  ```
  select * from t where t.a > (select a from tbl);
  ```
  a LogicalMaxOneRow would be added over the subquery, and this part of LogicalPlan would be evaluated
  immediately if it is incorrelated subquery, that is to say, this subquery would be executed first to get
  results, the MaxOneRowExec check is done here; @sa handleScalarSubquery
  if the subquery is correlated, for example:
  ```
  select * from t where t.a > (select tbl.a from tbl where tbl.b > t.b);
  ```
  a LogicalApply with LeftOuterJoin is built, and a LogicalSelection `t.a > tbl.a` would be added on top of
  LogicalApply, so MaxOneRowExec is executed in the innerPlan of NestedLoopApplyExec;

  For queries like:
  ```
  select * from t where t.a in (select a from tbl);
  ```
  they belong to another story, no LogicalMaxOneRow in the plan, because the subquery can return multiple
  rows now, this would be built to semi-join, or transformed into inner join over aggregation; @sa handleInSubquery

* buildKeySolver and ::buildKeyInfo is to pull up info about unique indices from children node, and combine them with Schema() of itself,
  to tell upper node a information: for the output of this node, the columns in Keys  maintains the uniqueness;

* pushDownTopN would first convert LogicalLimit into LogicalTopN, and remove the child LogicalSort node, then
  push down this TopN node to the top of DataSource usually;

* ast.exprNode -> expression.Expression: planBuilder::build -> buildSelect -> buildSelection -> rewrite (example)

* semi-join IN can be converted to EXIST correlated subquery point scan, but it has no help for physical plan,
  because both semi-join and correlated subquery are expensive, and they both would be implemented as
  PhysicalApply/NestedLoopApplyExec(@sa handleExistSubquery), no efficiency improvement; we should convert
  semi-join to inner-join over aggregation, inner join would always choose better physical plan than
  semi-join; another approach is to compute the result of subquery first(must be un-correlated, and subquery
  result should be smaller compared with left table), then optimizer can choose better physical plan over
  this IN expression.

* only 4 structs implement Expression interface:
  - Constant
  - Column
  - ScalarFunction
  - CorrelatedColumn: subclass of Column

* decorrelateSolver is to convert LogicalApply of incorrelated inner plan into InnerJoin;
