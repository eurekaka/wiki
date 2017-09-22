* Security_context is in THD, constructor would be called in THD constructor;
  constructor would call Security_context::init()

* check_connection() -> acl_authenticate -> set_master_access()

* is_tencent_root would be marked in acl_authenticate --> do_auth_once

* deny_updates_if_read_only_option
