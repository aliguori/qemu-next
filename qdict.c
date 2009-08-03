/*
 * QEMU dictionary data type.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qdict.h"
#include "qemu-common.h"

/**
 * qdict_new(): Create a new dictionary data-type
 */
QDict *qdict_new(void)
{
    return qemu_mallocz(sizeof(QDict));
}

/**
 * tdb_hash(): based on the hash agorithm from gdbm, via tdb
 * (from module-init-tools)
 */
static unsigned int tdb_hash(const char *name)
{
    unsigned value;	/* Used to compute the hash value.  */
    unsigned   i;	/* Used to cycle through random values. */

    /* Set the initial value from the key size. */
    for (value = 0x238F13AF * strlen(name), i=0; name[i]; i++)
        value = (value + (((const unsigned char *)name)[i] << (i*5 % 24)));

    return (1103515243 * value + 12345);
}

/**
 * qdict_find(): Low-level lookup function
 */
static void *qdict_find(const QDict *qdict,
                        const char *key, unsigned int hash)
{
    QDictEntry *e;

    for (e = qdict->table[hash]; e; e = e->next)
        if (!strcmp(e->key, key))
            return e->value;
    return NULL;
}

/**
 * qdict_get(): Lookup for a given 'key'
 *
 * return corresponding 'value' or NULL if 'key' doesn't exist.
 */
void *qdict_get(const QDict *qdict, const char *key)
{
    assert(qdict != NULL);
    assert(key != NULL);
    return qdict_find(qdict, key, tdb_hash(key) % QDICT_HASH_SIZE);
}

/**
 * qdict_exists(): Check if 'key' exists
 *
 * return 1 if 'key' exists in the dict, 0 otherwise
 */
int qdict_exists(const QDict *qdict, const char *key)
{
    QDictEntry *e;

    assert(qdict != NULL);
    assert(key != NULL);

    for (e = qdict->table[tdb_hash(key) % QDICT_HASH_SIZE]; e; e = e->next)
        if (!strcmp(e->key, key))
            return 1;
    return 0;
}

/**
 * alloc_entry(): allocate a new QDictEntry
 */
static QDictEntry *alloc_entry(const char *key, void *value)
{
    QDictEntry *entry;

    entry = qemu_malloc(sizeof(*entry));
    entry->key = qemu_strdup(key);
    entry->value = value;
    entry->next = NULL;

    return entry;
}

/**
 * qdict_add(): Add a new value into the dictionary
 *
 * Add the pair 'key:value' into qdict. Does nothing if 'key' already
 * exist.
 */
void qdict_add(QDict *qdict, const char *key, void *value)
{
    unsigned int hash;
    QDictEntry *entry;

    assert(qdict != NULL);
    assert(key != NULL);

    hash = tdb_hash(key) % QDICT_HASH_SIZE;
    if (qdict_find(qdict, key, hash)) {
        /* Don't add again if it's already there */
        return;
    }

    entry = alloc_entry(key, value);
    entry->next = qdict->table[hash];
    qdict->table[hash] = entry;
    qdict->size++;
}

/**
 * qdict_del(): Delete a given 'key' from the dictionary
 *
 * This will destroy all data allocated by 'key', except 'value'
 * which is returned.
 */
void *qdict_del(QDict *qdict, const char *key)
{
    unsigned int hash;
    QDictEntry *e, *prev;

    assert(qdict != NULL);
    assert(key != NULL);

    prev = NULL;
    hash = tdb_hash(key) % QDICT_HASH_SIZE;
    for (e = qdict->table[hash]; e; e = e->next) {
        if (!strcmp(e->key, key)) {
            void *value = e->value;
            if (!prev)
                qdict->table[hash] = e->next;
            else
                prev->next = e->next;
            qemu_free(e->key);
            qemu_free(e);
            qdict->size--;
            return value;
        }

        prev = e;
    }

    return NULL;
}

/**
 * qdict_destroy(): Destroy given 'qdict'
 *
 * This will destroy all data allocated by 'qdict', but added
 * values must be freed by the caller.
 */
void qdict_destroy(QDict *qdict)
{
    int i;

    assert(qdict != NULL);

    for (i = 0; i < QDICT_HASH_SIZE; i++) {
        QDictEntry *e = qdict->table[i];
        while (e) {
            QDictEntry *tmp = e->next;
            qemu_free(e->key);
            qemu_free(e);
            e = tmp;
        }
    }

    qemu_free(qdict);
}
