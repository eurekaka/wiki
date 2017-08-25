* goal is to get a consistent read of all object members;
* naive approach is using rwlock
* another approach is Copy-on-Write, and override the global pointer with the new object address;
* a problem of Copy-on-Write approach: when to free the memory of old object? we have to ensure
  there is no reference to the old object before we can free it;
* one approach is to introduce a reference counter in object, but here is the race condition:

    ```
    thd1: save object pointer to register(old one)          //1
    thd2: save object pointer to register(old one)          //2
    thd2: override old object counter to new object         //3
    thd2: read and check reference counter                  //4
    thd2: free old object memory                            //5
    thd1: atomic increment reference counter, SIGSEGV       //6 key here is 1 and 6 are not atomic
    ```

* to solve this race condition, one approach is to introduce an independent rwlock to protect
  reference counter(not in the object);
* another approach for the race condition is:
    * introduce an intermediate RefNode struct:

        ```
        typedef struct RefNode
        {
            atomic_t state; // let's say, 64 bits, low 32 bits for refcount(aka state.count), high 32 bits for version(state.version)
            void *ptr; // pointer to target object
        } RefNode;

        atomic_t g_state; // low 32 bits for index of RefNode(aka g_state.ref), high 32 bits for version(aka g_state.version)
        ```

    * for reading:

        ```
        atomic_t old_g_state = g_state; // save g_state to local
        while(1)
        {
            RefNode *ref_node = RefNodeArray[old_g_state.ref];
            if (atomic_check_version_and_inc_ref(ref_node->state.version, old_g_state.version))
            {
                ref = ref_node; // succ
                break;
            }
            else
            {
                old_g_state = g_state;
            }
        }

        /*
         * ...
         * logics using the members of ref->ptr
         */

        // finished reading
        if (atomic_decr_ref_and_inc_version(ref))
        {
            free(ref->ptr);
            return_one(RefNodeArray, ref);
        }
        ```

    * for writing:

        ```
        /*
         * ...
         * reading code, refcount++
         */

        atomic_t new_g_state;
        RefNode *new_ref_node = get_one(RefNodeArray);

        new_ref_node->ptr = &new_object;
        new_ref_node->state.count = 1; // refcount

        new_g_state.ref = index_of(new_ref_node);
        new_g_state.version = new_ref_node->state.version;

        atomic_t old_g_state = atomic_store_and_return_old(g_state, new_g_state);
        if (atomic_decr_ref_and_inc_version(old_refnode)) // from old_g_state
        {
            free(old_refnode->ptr);
            return_one(RefNodeArray, old_refnode);
        }
        ```

    * key point in this design is the intermediate RefNode and version; for above race condition,
      if thd1 saves g_state to register and then is scheduled, then thd2 does blabla, next time when
      thd1 gets back and tries to increase the refcount, there is a failed atomic version check; RefNode
      would not be freed until system exit, so it is safe to access RefNode fields in version check;
    * problems of this approach:
        * lock free array needed for RefNodeArray; this is simple, use an atomic array index, and an
          atomic flag in each element;
        * pre-allocate memory for RefNodeArray, otherwise the index cannot be 32 bits;
* besides from reference counter approaches, another approach to remove rwlock in Copy-on-Write: Hazard Pointer.
  The shared object pointer is called hazard pointer, each thread would save the global pointer locally, and one
  can only free the old object when no thread has that pointer locally; usually, the local pointers are put into
  an global array;
    * for reading:

    ```
    void *g_hp_local_array[MAX_THREAD_ID]; // global
    
    void *ref = NULL;
    
    while(1)
    {
        ref = g_ptr; // save global pointer locally             //1
        g_hp_local_array[thread_id] = ref;                      //2

        if (ref == g_ptr) // not overriden, safe now on         //3
            break;
    }
    // the double check is very important, if thd1 executes line 1, then is scheduled before line 2, then thd 2
    // find no reference for the old g_ptr in g_hp_local_array, so frees the object, then thd1 gets back to line 2

    /*
     * ...
     * read ref members
     */

    // reading finished
    g_hp_local_array[thread_id] = NULL;
    ```

    * writing is simple, override g_ptr first, then iterate g_hp_local_array to see if it can free the object;
* ABA problem in lock free solutions using CAS, especially for lock free list and stack manipulation:
    * For a list, head-->A->B->C, if thd1 wants to remove an element A from the head of list, it first peeks the head to
      be A, and save B as next of A locally(IMPORTANT), but before it tries to call cas(head, A, B), it was scheduled;
      thd2 gets the CPU and removes A from the list, so the list is now head-->B->C, then thd2 removes B from the list
      and frees memory pointed by B, then thd2 add A back to the head of the list, so now the list looks like head-->A->C;
      then thd1 finally gets back on CPU and calls cas(head, A, B), it succeeds, so the list is broken as head-->B, C, and B
      is a dangling pointer, which is dangerous;
    * same applies to stack;
    * be careful when using CAS
