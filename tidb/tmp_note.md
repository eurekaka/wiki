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
