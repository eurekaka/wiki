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

* buildKeySolver and ::buildKeyInfo is to pull up info about unique indices from children node, and combine them with Schema() of itself,
  to tell upper node a information: for the output of this node, the columns in Keys  maintains the uniqueness;
