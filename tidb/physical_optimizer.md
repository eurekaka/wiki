* DataSource has 3 implementations:
  - PhysicalTableReader: scan clustered index using RowId(handle)
  - PhysicalIndexReader: covering index scan
  - PhysicalIndexLookUpReader: 'double reading', scan index first, get tuple from clusterd index by RowId

  These 3 reader implements PhysicalPlan interface, detailed plans to implement them are encapsulated inside;
  these readers is for rootTask, encapsulated plans are for copTask;

* LogicalAggregation has 2 implementations:
  - PhysicalStreamAgg: output of outer plan maintains order of group by keys
  - PhysicalHashAgg

* LogicalJoin has 3 implementations:
  - PhysicalHashJoin
  - PhysicalIndexJoin: outer plan output is ordered by join key, inner table rows accessed by
    PhysicalIndexLookUpReader(in batch style?)
  - PhysicalMergeJoin: both other plan and inner plan output is ordered by join key

* TiDB does not support full outer join, because MySQL cannot support; actually, PhysicalMergeJoin can handle
  full outer join;

* ::findBestTask would first call ::getTask(prop) to check if this logical plan has already computed
  the physical implementations satisfying the specified requiredProp, the computed implementations are
  stored in baseLogicalPlan::taskMap, and key is the hash code of the requiredProp; each logical operator
  has a taskMap; this can avoid redundant computation in recursive ::findBestTask;

* statistics:
  ```
  DataSource::deriveStats -> PushDownNot
                          |_ DataSource::getStatsByFilter -> statsInfo //using DataSource::statisticTable info
                                                          |_ compute cardinality foreach column //using NDV*factor, because DataSource::statisticTable.Count is updated in stats lease, while column level stats such as NDV is only updated in ANALYZE
                                                          |_ DataSource::statisticTable.Selectivity(conds)
                                                          |_ statsInfo::scale(selectivity)

  LogicalJoin::deriveStats -> LogicalJoin::children[0/1].deriveStats
                           |_ compute count and cardinality according to join type
  ```

* each column has a histogram, and a CMSketch(quite similar with bloomfilter and HyperLogLog counting);
* planBuilder::buildDataSource would get statistics.Table from cache, and store it in DataSource;
* row count of inner join is computed as: `(N(left)/NDV(left)) * (N(right)/NDV(right)) * min(NDV(left), NDV(right))`,
  it is based on an assumption that left table and right table is distributed evenly respectively, and each left
  bucket can find a match bucket in right table, or vice versa; in code implementation, this is simplified to be
  `N(left) * N(right) / max(NDV(left), NDV(right))`, just a math simplification;

* expectedCnt is stored in requiredProp

* auto generated functions are functions inherited from parent struct, cannot make breakpoint directly:
  - findBestTask: make breakpoint at baseLogicalPlan::findBestTask

* in exhaustPhysicalPlans, each PhysicalPlan would have corresponding requiredProp for childen nodes, stored in
  basePhysicalPlan::childrenReqProps, and is used in `::findBestTask -> child.findBestTask(pp.getChildReqProps)`

* LogicalTopN has 2 implementations:
  - PhysicalTopN: has 3 task types, no special expectedCnt and col order requiredProp for child, because TopN handles them
  - PhysicalLimit: has 3 task types, enforce expectedCnt and col order requiredProp for child, child must provide ordered ouput

* expectedCnt is an approximation, used when computing cost of physical plan; row count of stats is for general
  cost computing, and expectedCnt is for cost computing in case of early termination due to Limit; stats propagates in
  bottom-up style by ::deriveStats, while expectedCnt is in top-down style through requiredProp;

* PhysicalTableScan and PhysicalIndexScan would only RPC TiKV once and buffer all tuples in TiDB in the first call?
  if --enable-streaming variable is set true, no; otherwise(default), yes; PhysicalIndexLoopUpReader contains at
  least one PhysicalTableScan and PhysicalIndexScan

* the reason of introducing task type(copSingleReadTaskType, copDoubleReadTaskType and rootTaskType):
  - indicate where operators are executed, in TiDB or in coprocessor of TiKV;
  - make cost comparison of PhysicalPlan reasonable, alleviate the interere of RPC cost;

  only two task struct: copTask and rootTask; task type is set in childrenReqProps of PhysicalPlan for parent
  node of DataSource, e.g, PhysicalLimit or PhysicalTopN, etc(@sa getPhysLimits)

  PhysicalProjection set childrenReqProps to rootTaskType, this enforces that PhysicalProjection would not
  be pushed down to coprocessor; PhysicalTopN and PhysicalLimit can set childrenReqProps to copSingleReadTaskType
  or copDoubleReadTaskType, this makes them able to belong to a copTask, i.e, pushed down to coprocessor; this logic
  is implemented in `baseLogicalPlan::findBestTask -> PhysicalLimit::attach2Task`

* cost only condiders memory, cpu and network, disk cost not included;

* ::preparePossibleProperties is to update the `column or column combinations from which order is maintained`
  according to properties from child and the current operator logic;

  only LogicalAggregation and LogicalJoin would modify plan node, others would just return result for upper node usage;
  XXX why LogicalJoin can maintain right properties in function LogicalJoin::preparePossibleProperties?

  LogicalAggregation::preparePossibleProperties would store child properties in field `possibleProperties`, and this
  is used in `LogicalAggregation::exhaustPhysicalPlans -> LogicalAggregation::getStreamAggs` to decide if we can use
  stream agg;

  LogicalJoin::preparePossibleProperties would store child properties in field `leftProperties` and `rightProperties`
  respectively, and they are used in `LogicalJoin::exhaustPhysicalPlans -> LogicalJoin::getMergeJoin` to decide if
  merge join can be used;

* for ::exhaustPhysicalPlans of LogicalAggregation and LogicalJoin, no additional PhysicalSort would be added if
  order requirement is not satisfied for stream agg and merge join;

  LogicalJoin::tryToGetIndexJoin may return nil if no index satisfies the join condition;

* LogicalApply is for correlated subquery, would be transformed into PhysicalApply and then NestedLoopApplyExec,
  in `NestedLoopApplyExec::Next`, for each outer tuple, set the value of correlated column through `outerSchema`,
  then rerun the inner node; the difference of NestedLoopApplyExec b/w NLJ is the param setting from outer tuple;
  LogicalApply is a subclass of LogicalJoin!

* query has priority in TiDB, if user has not set priority for this query explicitly, priority would be derived
  in `buildExecutor` according to whether it is a point get plan or expensive plan; expensiveness is checked in
  `Compile` by `isExpensiveQuery`, if row count of any plan nodes exceeds a threshold, it is treated as expensive;
  if user explicitly specifies priority for query, TiDB would obey this hint and put it into corresponding queue;
  Not like index hints, this is deterministic;
