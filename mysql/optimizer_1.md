* IN/EXISTS is implemented as semi-join, while NOT IN / NOT EXISTS is anti-semi-join
* IN with uncorrelated subquery can be rewriten into EXISTS with correlated subquery
	```
	select * from t1 where t1.c1 in (select t2.c1 from t2);
	select * from t1 where exists (select t2.c1 from t2 where t2.c1 = t1.c1);
	```
