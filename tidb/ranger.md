* all expressions are organized as a CNF in the top level;
* ranger is quite like MySQL range optimizer, expression extraction and merge,
  then decide which index can be used;

* entry point of ranger:
  ```
  DataSource::deriveStats -> DataSource::deriveTablePathStats -> DetachCondsForTableRange
  						  |									  |_ BuildTableRange
  						  |_ DataSource::deriveIndexPathStats -> DetachCondAndBuildRangeForIndex
  ```
