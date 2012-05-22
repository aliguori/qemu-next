/*
 * Page cache for qemu
 * The cache is base on a hash on the page address
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Orit Wasserman  <owasserm@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef CACHE_H
#define CACHE_H

/* Page cache for storing guest pages */
typedef struct Cache Cache;

/**
 * cache_init: Initialize the page cache
 *
 *
 * Returns new allocated cache or NULL on error
 *
 * @cache pointer to the Cache struct
 * @num_pages: cache maximal number of cached pages
 * @page_size: cache page size
 */
Cache *cache_init(int64_t num_pages, unsigned int page_size);

/**
 * cache_fini: free all cache resources
 * @cache pointer to the Cache struct
 */
void cache_fini(Cache *cache);

/**
 * cache_is_cached: Checks to see if the page is cached
 *
 * Returns %true if page is cached
 *
 * @cache pointer to the Cache struct
 * @addr: page addr
 */
bool cache_is_cached(const Cache *cache, uint64_t addr);

/**
 * get_cached_data: Get the data cached for an addr
 *
 * Returns pointer to the data cached or NULL if not cached
 *
 * @cache pointer to the Cache struct
 * @addr: page addr
 */
uint8_t *get_cached_data(const Cache *cache, uint64_t addr);

/**
 * cache_insert: insert the page into the cache. the previous value will be overwritten
 *
 * @cache pointer to the Cache struct
 * @addr: page address
 * @pdata: pointer to the page
 */
void cache_insert(Cache *cache, uint64_t addr, uint8_t *pdata);

/**
 * cache_resize: resize the page cache. In case of size reduction the extra pages
 * will be freed
 *
 * Returns -1 on error
 *
 * @cache pointer to the Cache struct
 * @num_pages: new page cache size (in pages)
 */
int cache_resize(Cache *cache, int64_t num_pages);

#endif
