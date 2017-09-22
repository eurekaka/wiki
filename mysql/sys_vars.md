* entry point for querying system variable: get_system_var()
  ```
  get_system_var --> my_hash_search(&system_variable_hash)
  ```

* when are variables added into system_variable_hash?
  ```
  class inheritance:
  Sys_var_mybool: Sys_var_typelib: sys_var
  
  global variable all_sys_vars is passed in to constructor of sys_var,
  so each Sys_var_mybool would be appended to tail of all_sys_vars;

  sys_var_init --> my_hash_init(&system_variable_hash)
               |__ mysql_add_sys_var_chain(all_sys_vars)
  ```
