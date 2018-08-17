* DataSource has 3 implementations:
  - PhysicalTableReader: scan clustered index using RowId(handle)
  - PhysicalIndexReader: covering index scan
  - PhysicalIndexLookUpReader: 'double reading', scan index first, get tuple from clusterd index by RowId

  These 3 implements PhysicalPlan interface, detailed plans are stored inside

* LogicalAggregation has 2 implementations:
  - PhysicalStreamAgg: output of outer plan maintains order of group by keys
  - PhysicalHashAgg

* LogicalJoin has 3 implementations:
  - PhysicalHashJoin
  - PhysicalIndexJoin: outer plan output is ordered by join key, inner table rows accessed by PhysicalIndexLookUpReader(in batch style?)
  - PhysicalMergeJoin: both other plan and inner plan output is ordered by join key

* TiDB does not support full outer join, because MySQL cannot support; actually, PhysicalMergeJoin can handle
  full outer join;

* ::findBestTask would first call ::getTask(prop) to check if this logical plan has already computed the physical implementations
  satisfying the specified requiredProp, the computed implementations are stored in baseLogicalPlan::taskMap, and key is the
  hash code of the requiredProp; each logical operator has a taskMap; this can avoid redundant computation in recursive ::findBestTask;

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
* row count of inner join is computed as: `(N(left)/NDV(left)) * (N(right)/NDV(right)) * min(NDV(left), NDV(right))`, it is based on an assumption that left table and right
  table is distributed evenly respectively, and each left bucket can find a match bucket in right table, or vice versa; in code implementation, this is simplified to be
  `N(left) * N(right) / max(NDV(left), NDV(right))`, just a math simplification;
