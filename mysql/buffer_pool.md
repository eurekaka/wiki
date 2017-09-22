* callstack of loading a compressed table for the first time when buffer pool is not overloaded:
  ```
  buf_page_get_gen --> buf_read_page --> buf_read_page_low --> buf_page_init_for_read --> buf_buddy_alloc
                   |                                       |                          |__ buf_page_alloc_descriptor
                   |                                       |                          |__ bpage->state = BUF_BLOCK_ZIP_PAGE
                   |                                       |                          |__ HASH_INSERT(page_hash)
                   |                                       |                          |__ buf_LRU_add_block --> buf_pool->LRU
                   |                                       |                                                |__ incr_LRU_size_in_bytes
                   |                                       |                                                |__ if buf_page_belongs_to_unzip_LRU, buf_unzip_LRU_add_block(false)
                   |                                       |__ read data into bpage.zip.data
                   |__ goto loop, block NULL, buf_page_hash_get_low
                   |__ switch fix_block.state BUF_BLOCK_ZIP_PAGE
                   |__ buf_LRU_get_free_block --> buf_LRU_get_free_only // get a new buf_block_t(from buffer pool, not malloc)
                   |__ buf_relocate(bpage, &block->page) --> memcpy
                   |                                     |__ remove from buf_pool->LRU
                   |                                     |__ insert new into buf_pool->LRU
                   |                                     |__ delete from page hash
                   |                                     |__ insert new into page hash // state still is BUF_BLOCK_ZIP_PAGE
                   |__ block->page.state = BUF_BLOCK_FILE_PAGE
                   |__ buf_unzip_LRU_add_block // not decompressed yet --> buf_page_belongs_to_unzip_LRU(true now)
                   |__ buf_page_free_descriptor(bpage) // free the malloced one
                   |__ buf_zip_decompress(block) --> page_zip_decompress
  ```

* buf_block_t in LRU can consume memory (UNIV_PAGE_SIZE + zip_size)
* incr_LRU_size_in_bytes in called only when compressed page is added into buf_pool->LRU, in buf_relocate, no change is made; hence in srv_export_innodb_status,
  the bytes is calculated as LRU_bytes + unzip_LRU * UNIV_PAGE_SIZE
* LRU_bytes would be decreased in buf_LRU_remove_block

* callstack when evicting one from unzip_LRU or LRU list:
  ```
  buf_LRU_get_free_block --> buf_LRU_get_free_only(return)
                         |__ buf_LRU_scan_and_free_block --> buf_LRU_free_from_unzip_LRU_list(if true, return) --> buf_LRU_evict_from_unzip_LRU
                                                         |                                                     |__ buf_LRU_free_page --> buf_LRU_block_remove_hashed --> buf_LRU_remove_block
                                                         |                                                                           |__ reinsert compressed page into page hash and buf_pool->LRU, incr_LRU_size_in_bytes
                                                         |                                                                           |__ buf_LRU_block_free_hashed_page(bpage) // return freed page to buf_pool->free
                                                         |__ buf_LRU_free_from_common_LRU_list
  ```
