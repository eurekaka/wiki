* PG uses a single LockConflicts matrix for granted compatibility and waiting compatibility
  check, while MySQL uses two different matrixes; waiting matrix can be treated as the denotion
  of lock mode priority;
* Each MDL_context has only one m_waiting_for(MDL_ticket), MDL_ticket tracks the MDL_lock waited
  on, MDL_lock tracks the MDL_context granted by m_granted, and the MDL_context waiting on it(IMPORTANT,
  we must consider waiting MDL_context as well since there are waiting priority), hence we can infer
  the MDL_context dependences of current starting MDL_context; recursively, we can build the dependency
  graph of MDL_context; traverse this graph using DFS algo(actually, first round is BFS, but that is just
  an optimization), with a context Deadlock_detection_visitor tracking the traverse status(current
  victim, the one with lowest weight up till now along the path), once we find a circle(starting
  MDL_context met again, in inspect_edge()), we find a deadlock; then DFS would back trace to the top
  call and set victim m_waiting_status finally;
* No soft edges and hard edges as PG, it is simpler and clearer;
* call stack:

    ```
    MDL_context::find_deadlock --> while(1) --> Deadlock_detection_visitor // each round would spot one victim, remove one node from graph
                                            |__ MDL_context::visit_subgraph --> MDL_ticket::accept_visitor --> MDL_lock::visit_subgraph --> mysql_prlock_rdlock(&m_rwlock)
                                            |                                                                                           |__ enter_node // depth++, if overflow, terimate and say deadlock found
                                            |                                                                                           |__ BFS direct neibors, including granted and waiting, inspect_edge() --> check if it is starting MDL_context
                                            |                                                                                           |__ DFS neibors, including granted and waiting, recursive MDL_context::visit_subgraph
                                            |                                                                                           |__ leave_node // depth--
                                            |                                                                                           |__ mysql_prlock_unlock
                                            |__ set victim m_waiting_status VICTIM
    ```
* The whole deadlock detection is a depth-constrained DFS, with a 1-hop BFS optimization;
* rwlocks in deadlock detection:
  * MDL_context::visit_subgraph would grab m_LOCK_waiting_for before calling MDL_ticket::accept_visitor, and release it after getting back;
  * MDL_lock::visit_subgraph would grab m_rwlock before starting BFS and DFS, and release it after getting back;
  * hence during the traverse, all the m_LOCK_waiting_for and m_rwlock would be held at same time for a single path; when backtracing and
    pop out nodes from the path, corresponding locks would be released;
  * These locks are enough for deadlock detector to get a corrent snapshot, because lock free fast path cannot lead to new waiting relationship;
    while slow paths are protected by m_rwlock, timeout waits are protected by m_LOCK_waiting_for
