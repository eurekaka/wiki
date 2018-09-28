* ppd
  ```
  LogicalJoin::PredicatePushDown -> simplifyOuterJoin -> simplifyOuterJoin(innerPlan/outerPlan)
                                 |                    |_ foreach predicates(CNF) isNullRejected(innerTable.Schema()) -> EvaluateExprWithNull //func isstrict not considered
                                 |_ getCartesianJoinGroup //get base tables if all joins are cartesian join, and no Straight join and no prefered join type
                                 |_ joinReOrderSolver::reorderJoin -> traverse conds, split them into 2 classes, one for join, the other for filter, build join edge, and estimate rows of each DataSource based on these two classes **respectively**
                                 |                                 |_ fill rate of each join edge
                                 |                                 |_ Sort DataSource and join edge for each DataSource
                                 |                                 |_ joinReOrderSolver::walkGraphAndComposeJoin //build new join trees; XXX should break for loop in this function when one node is chosen to join?
                                 |                                 |_ joinReOrderSolver::makeBushyJoin //build a single bushy join tree from generated join trees
								 |_ ExtractFiltersFromDNFs //extract common expression in all DNF items, e.g, (a = 1 and b = 1) or (a = 1 and a = 1 and b = 1) or (a = 1 and b = 2) => [(b = 1) or (b = 1) or (b = 2), common: (a = 1)] ,the common part would be ANDed with remained, i.e, ((b = 1) or (b = 1) or (b = 2)) AND (a = 1)
								 |_ [PropagateConstant] //not for outer join, because no new conds added for outer join
                                 |_ extractOnCondition //split Expression slice into eq, left, right and others, in CNF items unit
                                 |_ adjust leftCond/rightCond according to join type
                                 |_ leftPlan/rightPlan.PredicatePushDown(leftCond/rightCond)
                                 |_ addSelection

  ```

* buildJoin would always generate a left deep join tree, i.e, Join is always in left deep tree form
  getCartesianJoinGroup is directly called in LogicalJoin::PredicatePushDown, this is another proof.
  this left deep tree is actually formed by parser;

* joinReOrderSolver::reorderJoin makes no sense at all; statistics and selectivity not considered; this function is ugly
  join optimization of TiDB is not optimal, reorderJoin decide the join order, and combinations, while
  baseLogicalPlan::findBestTask decide implementations of each join node and DataSource(and other operators) in
  recursive style; join reorder only applies for cartesian join query;

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

* decorrelateSolver is to convert LogicalApply of incorrelated inner plan into InnerJoin; for LogicalApply,
  we must use NLJ, while for LogicalJoin, we have other choices; if inner plan is a LogicalSelection, we can
  pull this selection into join condition and decorrelate the columns in the filter, thus make it possible to
  avoid LogicalApply;

* `expression.Column::UniqueID` is unique in session level, generated in `buildDataSource`;
  `expression.Column::ID == model.ColumnInfo::ID`, `expression.Column::ColName == model.ColumnInfo::Name`
  `model.ColumnInfo` is from information schema, `expression.Column` would be saved in `logicalSchemaProducer::schema`;

* `planBuider::rewrite` has an argument `asScalar`, this is used in deeper level for rewriting subquery in expressions,
  expressions with subquery may be converted into semi join later, ordinary semi join would return the rows of the outer
  plan, or nil, but if this subquery is used as argument of `ScalarFunction`, and the function only takes constant argument,
  not column arugment(e.g, logic OR), that is to say, we only care about whether the outer tuple
  has match or not, we does not care about the outer tuples, in this case, we call this requirement `asScalar`, if `asScalar`
  is true, we only output the scalar result true or false for the semi join;

  if `asScalar` is true, in `buildSemiJoin`, we append an aux column in schema of semi join, and in upper level function
  such as `handleInSubquery`, if this `PatternInExpr` is in `ScalarFunction` requring constant arg(e.g, OR), i.e, `asScalar`
  is true, we return the aux column of semi join schema into stack, otherwise, we return the semi join plan into stack;

  Note that in `handleInSubquery`, the built semi join is stored in `er.p`, the base plan is changed.

  Whether this passed in argument should be `asScalar` or not is decided by the caller, for example, in `buildJoin`,
  we pass false, because we do care about the semi join tuples in this case;

  A good example to illustrate this part:
  ```
  explain select * from t1 where t1.b > 1 or t1.b in (select b from t2);
  ```

* expression.Schema stores the pointer of expression.Column, so one Column is shared in the plan tree; few nodes would
  allocate new Column struct, such as `buildDataSource`, and `buildProjection`, other nodes would use Column pointers
  built previously. That is why we build map between Column UniqueID and index ID in `GenerateHistCollFromColumnInfo`

  `ColumnIndex` function uses UniqueID to compare two Column pointers;

* we have 7 join types, but `SemiJoin`, `AntiSemiJoin`, `LeftOuterSemiJoin` and `AntiLeftOuterSemiJoin` are internally
  built for `IN`, `EXISTS`, they are all built in `buildSemiJoin` during expression rewriting. Thus, in `buildJoin`, we
  only consider `InnerJoin`, `LeftOuterJoin` and `RightOuterJoin`;

* In MySQL syntax, cross join equals join, and inner join;
  `natural join` means `using` all common columns of left and right table, `natural` keyword cannot omit;
  ast.Join::NaturalJoin and ast.Join::StraightJoin is set in parser;

* `buildNaturalJoin` and `buildUsingClause` would build equal conds for join columns, and put these conds into
  `OtherConditions` of `LogicalPlan`; for `ON` clause, first convert ast.ExprNode to expression.Expression(in CNF),
  then `SplitCNFItems` and `attachOnConds`, `attachOnConds` would call `extractOnConditions`

* In predicate pushdown, for left outer join, on condition of left table should not be pushed down, and it should be
  be kept in on condition; on condition of equal is unable to pushdown, and it should be kept in on condition; on
  condition of right table should be pushed down to right child plan; on condition of both tables is unable to
  pushdown, and should be kept in on condition;

  where condition of left table can be pushed down to left child plan, where condition of equal is unable to pushdown,
  and it cannot be in join condition either, return it to parent node to build LogicalSelection; where condition of right
  table should NOT be pushed down, because it would effect match of left tuples in join, return it to parent node; where
  condition of both tables is unable to pushdown, return it to parent;

  to conclude, only where condition of left table and on condition of right table can be pushed down for left outer join;

* Oracle has an optimization for left outer join:
  ```
  select t1.a, t1.b from t1 left join t2 on t1.a = t2.a;
  ```
  in this case, t2 is not needed in the projection, then if join key of t2 is unique, we can rewrite this query to:
  ```
  select t1.a, t1.b from t1;
  ```

* `simplifyOuterJoin` does not handle left outer semi join and left outer anti semi join; the purpose of these 2
  join types is to keep the no-match cases, so they cannot and should not be simplified to semi join or anti semi join;
* `exists` is implemented by semi-join, and `not exists` is implemented by left outer semi join with a `Not` selection;
  `in` is implemented by semi-join, and `not in` is implemented by anti-semi-join;
