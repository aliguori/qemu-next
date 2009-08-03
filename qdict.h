#ifndef QDICT_H
#define QDICT_H

#include <stddef.h>

#define QDICT_HASH_SIZE 512

typedef struct QDictEntry {
    struct QDictEntry *next;
    char *key;
    void *value;
} QDictEntry;

typedef struct QDict {
    size_t size;
    QDictEntry *table[QDICT_HASH_SIZE];
} QDict;

/**
 * qdict_size(): return the size of the dictionary
 */
static inline size_t qdict_size(const QDict *qdict)
{
    return qdict->size;
}

QDict *qdict_new(void);
void qdict_add(QDict *qdict, const char *key, void *value);
void *qdict_get(const QDict *qdict, const char *key);
int qdict_exists(const QDict *qdict, const char *key);
void *qdict_del(QDict *qdict, const char *key);
void qdict_destroy(QDict *qdict);

#endif /* QDICT_H */
