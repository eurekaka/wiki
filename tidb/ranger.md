* all expressions are organized as a CNF in the top level; if the expression is DNF,
  then len(conditions) == 1, and the the funcname should be LogicOr;
* ranger is quite like MySQL range optimizer, expression extraction and merge,
  then decide which index can be used;

* entry point of ranger:
  ```
  DataSource::deriveStats -> DataSource::deriveTablePathStats -> DetachCondsForTableRange
                          |                                   |_ BuildTableRange
                          |_ DataSource::deriveIndexPathStats -> DetachCondAndBuildRangeForIndex -> detachDNFCondAndBuildRangeForIndex //if expression is DNF(single expression in OR form)
                                                              |                                  |_ detachCNFCondAndBuildRangeForIndex //if expression is CNF or single expression
															  |_ splitIndexFilterConditions
  detachCNFCondAndBuildRangeForIndex -> extractEqAndInCondition
                                     |_ buildCNFIndexRange -> builder::build
  ```

* shortcomings:
  - `a = 1 and (b = 1 or b = 2) and c > 1`, c would not be used in range; `a = 1 and b in (1,2) and c > 1` is OK;
  - `(a = 1 and b = 1) or (a = 2 and b = 2) and c > 1`, c would not be used in range; `((a,b) in ((1,1), (2,2))) and c > 1`, c
    would not be used in range, because IN with multiple columns would be expanded into OR; `(a = 1 and b = 1 and c > 1) or (c = 2 and b = 2 and c > 1)` is OK;
