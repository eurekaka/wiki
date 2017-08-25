* `pin` means one thread marks it is using a global pointer, similar to saving it locally;
* `pinbox` is similar to the global array containing all pins, each thread has one slot, but
  `pinbox` is dynamic-sized;
* `purgatory` is a collection of all the harzard pointers cannot be freed yet; @sa lf_alloc-pin.c
* LF_HASH is implemented using LF_DYNARRAY, which is a recursive leveled array; @sa lf_dynarray.c
* usage of LF_PINS in LF_HASH:
    * each thread would get a LF_PINS for the hash table, each LF_PINS can have at most 4 pins;
      all 4 pins are for a single element in the hash table; pin[0], pin[1] are for temp usage,
      pin[2] is the final pin; @sa my_lfind() for the classic double check, and how pin[0], pin[1]
      are used; therefore, each lf_hash_search() call must have a lf_hash_search_unpin before it
      can call next lf_hash_search(); or the second lf_hash_search() would override the first pin[2];
    * lf_hash_get_pins(LF_HASH *lf_hash); // get one from lf_hash->alloc.pinbox
    * lf_hash_search(LF_HASH *lf_hash, LF_PINS *pins); // pin[2] valid
    * lf_hash_search_unpin(LF_PINS *pins); // set pin[2] 0
    * lf_hash_insert(LF_HASH *lf_hash, LF_PINS *pins); // use pin[] temporarily, no pin finally
    * hence, the pin protection is on element level; the element pointers in hash table is atomically
      read or overriden by CAS; this is enough for lock hash table, the only spot need protection is
      the concurrent element pointer insert(in find_or_insert()) and delete(in remove_random_unused()),
      which is ensured by CAS atomic pointer assignment logic, and pins; there is no concurrent
      element pointer update now, the update is inplace, protected by m_rwlock of MDL_lock; hence,
      semantically, it is safe to using an old version when the latest is NULL now, because old version
      must be 'unused' now;
