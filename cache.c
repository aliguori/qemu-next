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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdbool.h>
#include <glib.h>
#include <strings.h>

#include "qemu-common.h"
#include "qemu/cache.h"

#ifdef DEBUG_CACHE
#define DPRINTF(fmt, ...) \
    do { fprintf(stdout, "cache: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

typedef struct CacheItem CacheItem;

struct CacheItem {
    uint64_t it_addr;
    unsigned long it_age;
    uint8_t *it_data;
};

struct Cache {
    CacheItem *page_cache;
    unsigned int page_size;
    int64_t max_num_items;
    uint64_t max_item_age;
    int64_t num_items;
};

Cache *cache_init(int64_t num_pages, unsigned int page_size)
{
    int i;

    Cache *cache = g_malloc(sizeof(Cache));
    if (!cache) {
        DPRINTF("Error allocation Cache\n");
        return NULL;
    }

    if (num_pages <= 0) {
        DPRINTF("invalid number pages\n");
        return NULL;
    }

    /* round down to the nearst power of 2 */
    if (!is_power_of_2(num_pages)) {
        num_pages = 1 << ffs(num_pages);
        DPRINTF("rounding down to %ld\n", num_pages);
    }
    cache->page_size = page_size;
    cache->num_items = 0;
    cache->max_item_age = 0;
    cache->max_num_items = num_pages;

    DPRINTF("Setting cache buckets to %lu\n", cache->max_num_items);

    cache->page_cache = g_malloc((cache->max_num_items) *
                                 sizeof(CacheItem));
    if (!cache->page_cache) {
        DPRINTF("could not allocate cache\n");
        g_free(cache);
        return NULL;
    }

    for (i = 0; i < cache->max_num_items; i++) {
        cache->page_cache[i].it_data = NULL;
        cache->page_cache[i].it_age = 0;
        cache->page_cache[i].it_addr = -1;
    }

    return cache;
}

void cache_fini(Cache *cache)
{
    int i;

    g_assert(cache);
    g_assert(cache->page_cache);

    for (i = 0; i < cache->max_num_items; i++) {
        g_free(cache->page_cache[i].it_data);
        cache->page_cache[i].it_data = 0;
    }

    g_free(cache->page_cache);
    cache->page_cache = NULL;
}

static unsigned long cache_get_cache_pos(const Cache *cache, uint64_t address)
{
    unsigned long pos;

    g_assert(cache->max_num_items);
    pos = (address/cache->page_size) & (cache->max_num_items - 1);
    return pos;
}

bool cache_is_cached(const Cache *cache, uint64_t addr)
{
    unsigned long pos;

    g_assert(cache);
    g_assert(cache->page_cache);

    pos = cache_get_cache_pos(cache, addr);

    return (cache->page_cache[pos].it_addr == addr);
}

static CacheItem *cache_get_by_addr(const Cache *cache, uint64_t addr)
{
    unsigned long pos;

    g_assert(cache);
    g_assert(cache->page_cache);

    pos = cache_get_cache_pos(cache, addr);

    return &cache->page_cache[pos];
}

uint8_t *get_cached_data(const Cache *cache, uint64_t addr)
{
    return cache_get_by_addr(cache, addr)->it_data;
}

void cache_insert(Cache *cache, unsigned long addr, uint8_t *pdata)
{

    CacheItem *it = NULL;

    g_assert(cache);
    g_assert(cache->page_cache);

    /* actual update of entry */
    it = cache_get_by_addr(cache, addr);

    if (!it->it_data) {
        cache->num_items++;
    }

    it->it_data = pdata;
    it->it_age = ++cache->max_item_age;
    it->it_addr = addr;
}

int cache_resize(Cache *cache, int64_t new_num_pages)
{
    Cache *new_cache;
    int i;

    CacheItem *old_it, *new_it;

    g_assert(cache);

    /* same size */
    if (new_num_pages == cache->max_num_items) {
        return 0;
    }

    /* cache was not inited */
    if (cache->page_cache == NULL) {
        return -1;
    }

    new_cache = cache_init(new_num_pages, cache->page_size);
    if (!(new_cache)) {
        DPRINTF("Error creating new cache\n");
        return -1;
    }

    /* move all data from old cache */
    for (i = 0; i < cache->max_num_items; i++) {
        old_it = &cache->page_cache[i];
        if (old_it->it_addr != -1) {
            /* check for collision , if there  is keep the first value */
            new_it = cache_get_by_addr(new_cache, old_it->it_addr);
            if (new_it->it_data) {
                g_free(old_it->it_data);
            } else {
                cache_insert(new_cache, old_it->it_addr, old_it->it_data);
            }
        }
    }

    cache->page_cache = new_cache->page_cache;
    cache->max_num_items = new_cache->max_num_items;
    cache->num_items = new_cache->num_items;

    g_free(new_cache);

    return 0;
}
