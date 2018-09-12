* order is needed for column values to build histogram for this column:
  - if column does not belong to any index, full sort is too expensive, so we sample
    the rows to limit total number of rows to S, and then sort this sampled set, then
	build histogram using this sorted set; note that the height of histogram is not
	strictly equal, because multiple rows with same column value are in a single
	bucket, so bucket may contain rows more than computed height(S/n_bucket);
	note that, histogram build is started after combining samples from multiple regions;
  - if column belongs to an index, then we build histogram for this column using
    the full data rows, because index access provide the order; since we do not
	have S now, it is hard to decide the height of each bucket; solution is to
	initialize each bucket with height 1, then if buckets are used up, double the current
	height of each bucket, and collapse 2 nearby buckets into a single one, then continue
	inserting rows into buckets; each region can build a histogram independently, because
	rows are in order, when combining histograms from multiple regions, we need to collapse
	lower bound and upper bound of region histograms, because we want to ensure rows with
	same target column value in a single bucket;

* after a query finishes, we can update statistics according to the real returned result:
  - CMSketch is simple, update each cell value to += (R - E);
  - update of histogram contains 2 parts:
    - height of buckets: make frequency more accurate for each bucket;
	- boudary of buckets: split and merge buckets, make range query fit more closely with bucket range;

* when computing percentage inside a bucket for non-numeric types, convert these types(bytes) to scalar
  values, @sa statistics/scalar.go

* for expression on one column, range query usually uses histograms, while point query leverages CMSketch;
* function `Selectivity` is the API from statistics package for optimizer, it splits range expressions
  into as less groups as possible, where each group can do a simple selectivity count on a histogram or
  CMSketch of one column or one index, then compute the final selectivity of all expressions as product
  of previously computed selectivities; the split uses greedy algorithm and int64 as bitset to record
  covered expressions(that is why we have a constraint of no more than 63 expressions);
  index histogram and pk histogram is preferred for a range expression in selectiviy estimation, because
  they are accurate;

* function `SampleCollector::collect` implements the sampling for ordinary column, and function `BuildColumn`
  inserts sampled rows into histogram;
* query feedback:
  ```
  //collect feedback info, each query would produce feedback
  TableRangesToKVRanges/IndexRangesToKVRanges -> Histogram::SplitRange //before distsql to tikv, set up the query feedback
  selectResult::getSelectResp -> QueryFeedBack::Update //for rows returned from tikv in distsql, update real count in query feedback

  //update histogram according to collected feedback info in a background goroutine
  Handle::UpdateStatsByLocalFeedback -> UpdateHistogram -> splitBuckets //match splitted ranges with bucket boundaries
  														|_ mergeBuckets
  ```

* no matter what physical plan is chosen for a node, its statistics would not change; thus `stats` field is in basePlan;
* `buildDataSource` would first fill `statisticTable` by `getStatsTable`, this pointer would be used in
  `DataSource::deriveStats -> DataSource::getStatsByFilter -> HistColl::GenerateHistCollFromColumnInfo` to generate a new
  HistColl struct, and save it in `statsInfo::histColl`; that is to say, we make a copy of the `DataSource::statisticTable`
  and substitute column ID with corresponding UniqueID, then save this copy into `DataSource::stats::histColl`, this is
  for further propagation of the statistics to upper node; previously, only `DataSource` would use this table statistics;
