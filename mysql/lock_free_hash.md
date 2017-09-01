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
      @sa comments of lf_hash_search() for mandatory usage;
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
      must be 'unused' now(though it may still be referenced, that is the value of pin);

* structures:

  ```
  typedef struct
  {
    intptr volatile link; // next in the list
    uint32 hashnr;
  } LF_SLIST; // element of the list in a bucket

  typedef struct st_lf_hash
  {
    LF_DYNARRAY array; // elements reside here, each element is (LF_SLIST *)
    LF_ALLOCATOR alloc; // allocator, each element sizeof(LF_SLIST) + element_size, including outdated objects

    uint element_size;
    int32 volatile size; // bucket count
    int32 volatile count; // element count
  } LF_HASH;

  typedef struct st_lf_allocator
  {
    LF_PINBOX pinbox;
    uchar * volatile top; // data node ptr, linked by free_ptr_offset as purgatory, it is a stack of free objects
    uint element_size;
  } LF_ALLOCATOR;

  typedef struct
  {
    LF_DYNARRAY pinarray; // LF_PINS type
    uint32 volatile pinstack_top_ver; // atomic type for CAS, low 16 bits for index of pinarray,
                                      // high 16 bits for version, to prevent ABA problem of lock-free stack
                                      // the stack maintains a freelist
    uint32 volatile pins_in_array; // atomic type, to atomically expand pinarray index
    uint free_ptr_offset; // used for purgatory
    void *free_func_arg; // pointer to parent LF_ALLOCATOR
  } LF_PINBOX;

  typedef struct st_lf_pins // a flag, and a list of harzard pointers
  {
    void * volatile pin[4];
    LF_PINBOX *pinbox; // pointer to parent PINBOX
    void *purgatory;
    uint32 purgatory_count;
    uint32 link; // idx of next in stack if free, else it is its own idx
  } LF_PINS;

  typedef struct
  {
    void * volatile level[4];
    uint size_of_element;
  } LF_DYNARRAY;
  ```

* lf_dynarray_value would return a pointer to element stored in index 'idx'; the whole LF_DYNARRAY is a forest, with 4 trees,
  index-3 tree has 4 levels, each element is a 256-array; pointers in the middle levels are void **, pointers in the base level
  are type *, so lf_dynarray_value would compute `(uchar *ptr) + array->size_of_element * idx` at last;
* lf_dynarray_lvalue would return a pointer to element stored in index 'idx', and allocates memory if it is NULL; dynamic is
  reflected here, only malloc memory when we need the 'idx';
* concurrency is protected by volatile pointer ptr_ptr and casptr, lf is relected here;
* does LF_DYNARRAY need harzard pointer? no; LF_DYNARRAY is accessed by 'idx', the the memory and pointers are managed by
  LF_DYNARRAY internally, so there is no Copy-on-Write and pointer override, only in-place modification
* lf_dynarray_func would be evaluated for the base array, not array element, so we need a loop to traverse the base array inside;
* lf_pinbox_get_pins would first try to get one from free stack, if fails, then expand the pinarray;
  lf_pinbox_put_pins would push the LF_PINS into the free stack;
* add_to_purgatory would save the previous purgatory into last bytes of addr, and save addr into the purgatory; purgatory hence is
  like a list;
* chances are lf_dynarray_iterate find NULL in pinbox->pinarray and terminate while lf_pinbox_get_pins just expand the pinarray,
  so the last expanded elements would not be iterated;
* a LF_PINS is for one thread and one harzard pointer, a LF_PINBOX can household all threads for all harzard pointers, and all
  the shared object can reside in a single LF_ALLOCATOR
  typical usage is like: globally call lf_alloc_init, which would init LF_PINBOX as well, then each thread call lf_pinbox_get_pins,
  and then use this LF_PINS to call lf_alloc_new to allocate objects, and call lf_pin and lf_unpin to mark using harzard pointers;
* lf_alloc_new would return one from free stack or malloc new object, freed objects are put back into stack by alloc_free() which is
  a callback when pf_pinbox_real_free unpinned objects; therefore, LF_ALLOCATOR is a objects pool, which contains free objects, and
  pinned but outdated objects, and working objects; alloc_free is called in lf_pinbox_real_free, that is to say, for unppined objects
  in purgatory list, we do not really return the memory to OS, we return them to LF_ALLOCATOR free stack;

* lf_hash_insert

  ```
  lf_hash_insert --> lf_alloc_new // allocate memory from LF_ALLOCATOR, sizeof(LF_SLIST) + element_size
                 |__ append data after LF_SLIST // LF_SLIST is like a header of each element
                 |__ compute hashnr and bucket number, using LF_HASH.size
                 |__ lf_dynarray_lvalue(LF_HASH.array, bucket_number) // get bucket pointer, LF_SLIST type
                 |__ initialize_bucket
                 |__ linsert // insert into list of the bucket --> my_lfind
                 |__ check if we need double bucket count
  ```

* reference my_lfind for traversing list in LF_HASH
