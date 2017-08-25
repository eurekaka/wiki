* performance_schema tables are considered local to server, and is not replicated;
* tables of performance_schema are in-memory tables, no disk write;
* include/mysql/psi/mysql_mdl.h
