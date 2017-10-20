* limit concurrent short running queries, but do not block on long running queries
* granularity of scheduling? statement; LOCK TABLES? no problem, still one connection has
  a THD, so one thread handles multiple THD;

---

* Per_thread_connection_handler: public Connection_handler, and is stored in
  Connection_handler_manager::connection_handler, type is stored in Connection_handler_manager::thread_handling;
  Connection_handler_manager is a singleton;

  the entry function when network connection is detected is Connection_manager_handler::process_new_connection

  ```
  connection_event_loop --> listen_for_connection_event (thread 0)
                        |__ process_new_connection --> Per_thread_connection_handler::add_connection --> check_idle_thread_and_enqueue_connection --> waiting_channel_info_list->push_back()
                                                                                                     |                                            |__ wake_pthread++
                                                                                                     |                                            |__ mysql_cond_signal(&COND_thread_cache)
                                                                                                     |__ mysql_thread_create(handle_connection)

  if thread cache is enabled by thread_cache_size, then thread would not exit after client exit, it would block on
  Per_thread_connection_handler::block_until_new_connection --> mysql_cond_wait(&COND_thread_cache, &LOCK_thread_cache)
  after waken up, it takes the Channel_info from waiting_channel_info_list->front()

  handle_connection --> for loop --> thd_prepare_connection --> login_connection //do authentication
                                 |__ while(thd_connection_alive) --> do_command --> dispatch_command
                                 |__ close_connection
                                 |__ block_until_new_connection
  ```

* in 5.6, create_thread_to_handle_connection is process_new_connection for thread-per-connection, stored in struct scheduler_functions,
  the object for thread-per-connection is one_thread_per_connection_scheduler_functions

---

* io_event_count: whether read new event from network;
  queue_event_count: whether dequeue and process event from queue

  active_thread_count: listener not counted; threads in waiting list are not counted either; threads blocked are not counted either
  thread_count: all threads in this group
  connection_count: tp_add_connection

  event_count: number of events handled by this thread
  mutex of thread_group_t is mainly for protecting queue, and active_thread_count etc; potential bottleneck; thread pool is indeed something
  like a throttling

  if one thread is blocked by locking, it would not serve another event until finishing the current event;
  one thread would serve one connection until it cannot read from socket in do_command(timeout 0), then it would get the next event;
  if no event available, then it would go to sleep; timer would know it has been sleep for a long time and then shutdown this thread;

---

* 5.6 callstack
  ```
  connection listener thread:
  handle_connections_sockets --> create_new_thread --> tp_add_connection --> queue_put

  THD::scheduler is set to be thread_scheduler in constructor, and change to extra_thread_scheduler in handle_connections_sockets if necessary

  worker thread:
  handle_event --> threadpool_add_connection //do authentication
               |__ threadpool_process_request --> do_command
               |__ connection_abort --> threadpool_remove_connection //exit
  ```

* when is wake_or_create_thread called?
  * check_stall: no listener, and io_event_count 0 => create listener
  * check_stall: queue not empty, queue_event_count 0 => create worker
  * queue_put: active_thread_count 0 => create listener or worker
  * wait_begin: active_thread_count 0, and queue not empty, or no listener
  * listener: active_thread_count 0, wake up a worker
* when does a worker become a listener? or vice versa?
  * in get_event, if no event in the queue, and there is no listener, the worker becomes a listener;
  * in listener(), after get events, if queue is empty, then listener would become a worker;
* when would thread go to sleep?
  * when no event in get_event, would sleep for threadpool_idle_timeout in waiting_threads list,
    then it would exit if not woken up;

* shutdown_pipe is used to interrupt listener on poll syscall;
* after process a event, set_wait_timeout and start_io would be called in handle_event <= preparation
  for get_event;


---

* inc_cdb_connection_count <-- parse_client_handshake_packet <-- server_mpvio_read_packet <-- native_password_authenticate <-- do_auth_once <-- acl_authenticate(2)
  dec_cdb_connection_count <-- handle_connection(5)
  dec_connection_count <-- handle_connection(5)
  valid_connection_count <-- acl_authenticate(4) <-- check_connection <-- login_connection <-- thd_prepare_connection <-- handle_connection
  check_and_incr_conn_count <-- process_new_connection(1)
  check_cdb_connection_count <--acl_authenticate (3)

* m_server_idle and before_header_psi callback change is for accurate statistics of idle state; previously, thread would block in
  do_command --> get_command --> read_packet --> my_net_read --> net_read_packet --> net_read_packet_header, but now this function would
  no block at all, and this thread is idle when it finishes handle an event, so net_before_header_psi should be called in threadpool_process_request;

* 5.7 callstack:
  ```
  connection_event_loop --> listen_for_connection_event (thread 0)
                        |__ process_new_connection --> connection_handler or extra_connection_handler
                                                   |__ Thread_pool_connection_handler::add_connection --> Channel_info::create_thd
                                                                                                      |__ allocate_connection
                                                                                                      |__ thd->scheduler = &tp_event_functions
                                                                                                      |__ add_thd
                                                                                                      |__ connection->thread_group
                                                                                                      |__ queue_put
  worker thread:
  handle_event --> threadpool_add_connection --> thd_prepare_connection --> login_connection --> check_connection --> acl_authenticate
               |                                                        |__ prepare_new_connection_state
               |__ threadpool_process_request --> do_command
               |__ set_wait_timeout
               |__ start_io --> epoll_ctl(EPOLL_CTL_ADD)
               |__ connection_abort --> threadpool_remove_connection --> end_connection
                                                                     |__ close_connection
                                                                     |__ remove_thd
  ```

* Connection_handler_manager::init would set connection_handler and extra_connection_handler, which one to use is decided in process_new_connection
  by channel_info->is_on_extra_port(), on_extra_port is set in constructor of Channel_info <-- Channel_info_tcpip_socket, in
  Mysqld_socket_listener::listen_for_connection_event(), this function would check whether the fd is extra_tcp_port_fd, which is stored
  in Mysqld_socket_listener in constructor (as well as extra_tcp_port); Mysqld_socket_listener constructor is called in network_init, it would
  store extra_tcp_port, and in Mysqld_socket_listener::setup_listener, extra_tcp_port_fd would be created and stored;

* THD::scheduler is because there may exist 2 kinds of threads, with different event_functions

* modifications:
  * extra_port
  * THD::scheduler
  * connection_count & extra_port
  * vio_shutdown
  * THD_WAIT_NET
  * sys_vars
  * threadpool
  * connection handling

* compile tips: a class has static member, but this static member is not initialized, linker would report error "undefined reference to xxx";

---

* 5.6 connection count
  * mysqld_main --> handle_connections_sockets --> set thd->in_white_list
                                               |__ set thd->scheduler
                                               |__ create_new_thread --> check thd->scheduler->max_connections
                                                                     |__ thd->scheduler->connection_count++
                                                                     |__ thd->scheduler->add_connection --> ... --> acl_authenticate //check thd->scheduler->max_connections again after set thd->real_tencent_root
  * 5.6 does not have logic implemented in inc_cdb_connection_count in 5.7, i.e, count tencent_root separately, and no check for tencent_root
    connections as in check_cdb_connection_count of 5.7 (5.6 only checks the total connections in acl_authenticate); this component is simpler
    than 5.7, because each scheduler_functions tracks its own connection_count and max_connections

---

* to run regression tests with a sys var for all test cases, specify it in "--mysqld=--thread-handling=pool-of-threads" of mysql_test_run.pl

* failed tests: rpl.rpl_semi_sync_shutdown_hang
  rpl.rpl_semi_sync_wait_slave_count
  main.ssl
  perfschema.socket_connect
  perfschema.socket_instances_func

* XXX tests: kill_idle_transaction_timeout

* how to enable SSL of mysqld?
  * first compile mysqld with -DWITH-SSL=...; verify SSL is compiled by `select @@have_ssl;`, not 'NO', should be 'DISABLED'
  * then start mysqld with options:
  ```
  ssl
  ssl-ca=/home/gpadmin/workspace/AliSQL/mysql-test/std_data/cacert.pem
  ssl-cert=/home/gpadmin/workspace/AliSQL/mysql-test/std_data/server-cert.pem
  ssl-key=/home/gpadmin/workspace/AliSQL/mysql-test/std_data/server-key.pem
  ```
  verify SSL has been enabled by `select @@have_ssl`, should bu 'YES'
  * then start mysql client with options:
  ```
  --ssl-ca=/home/gpadmin/workspace/AliSQL/mysql-test/std_data/cacert.pem
  --ssl-cert=/home/gpadmin/workspace/AliSQL/mysql-test/std_data/client-cert.pem
  --ssl-key=/home/gpadmin/workspace/AliSQL/mysql-test/std_data/client-key.pem
  ```
  verify the session has been encryped by `\s`;

* how to enforce connection using TCP instead of UNIX socket?
  * mysql --protocol=tcp

* AliSQL and percona has bug when SSL and threadpool is used, that is to say, mysqld would hang in ssl->has_data in process_new_request for a while,
  if new queries come within this hang, it is OK (handle event is not called, the loop in process_new_request is not breaked out); if you wait for a
  while, after mysqld get back from ssl->has_data, then new query would call handle_event, and in do_command-->get_command, error would be returned when
  trying to read the socket, after 10 retries, it would report error "connection lost";

  For `set @@session.wait_timeout`, it would not take effect until mysqld get back from ssl->has_data, and back into handle_event, because the abs_wait_timeout
  is set in handle_event, so you have to wait extra time spent in ssl->has_data before this connection is removed;

  AliSQL passes main.ssl by making all localhost connections using per-thread scheduler, while percona passes this test by requring TLS 1.2

* vio_cancel instead of vio_shutdown, vio_shutdown would close socket, so mysqld cannot receive the last packet exchange, and so threadpool_process_request cannot
  be called to check the KILL_CONNECTION and return 1, so connection_abort cannot be called;
