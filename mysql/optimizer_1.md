* IN/EXISTS is implemented as semi-join, while NOT IN / NOT EXISTS is anti-semi-join
* IN with uncorrelated subquery can be rewriten into EXISTS with correlated subquery
	```
	select * from t1 where t1.c1 in (select t2.c1 from t2);
	select * from t1 where exists (select t2.c1 from t2 where t2.c1 = t1.c1);
	```
* 'type' column in explain shows the access method for base tables, and the result expectations
* constant 'table': returns at most one row;
* optimizer will use index for "col like 'x%'", but not for "col like '%x'", that is,
	when the first char of the pattern is wildcard;
* preprocessing(constants) => join order => group by => order by
* SEL_IMERGE --> SEL_TREE --> SEL_ARG
* will order be considered when choosing access path? XXX
* optimizer trace gives greate info about call stack;
* best_acess_path would choose between range type and other types(ref, etc);
* optimizer call stack:
	```
	optimize --> make_join_plan --> estimate_rowcount --> get_quick_record_count --> test_quick_select(range optimizer entry) --> find potential range indexes
			 |					|																							  |__ get_mm_tree: range analysis module
		     |				    |																							  |__ get_key_scans_params(SEL_TREE) --> check_quick_select(SEL_ARG)
		     |				    |__ Optimize_table_order::choose_table_order --> greedy_search --> best_extension_by_limited_search --> best_access_path
		     |					  																								    |__ best_extension_by_limited_search
		     |__ make_join_select --> attach conditions to table
		     |__ make_join_readinfo --> push_index_cond
	```

* test_quick_select
	```
	test_quick_select --> compute full scan cost
					  |__ init PARAM
					  |__ traverse head.s.keys, compare with keys_to_use, add to PARAM
	```

* key is index(struct KEY), key_part is part of the indexed columns
* SEL_TREE.keys[] is for AND(conjunctions), each element in keys[] is a SQL_ARG graph(OR, disjunctions); each element of keys[] is a index; OR between different indexes
  are recorded in merges[], each element is a OR representation, elements are AND-ed;
* index merge intersect for keys[], and index merge union for merges[];
