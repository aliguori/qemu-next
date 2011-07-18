#ifndef BLOCK_FILE_H
#define BLOCK_FILE_H

#include "block_dev.h"

typedef struct BlockFile {
    BlockDev parent;

    char *filename;
    int fd;

    int open_flags;

#ifdef CONFIG_LINUX_AIO
    int use_aio;
    void *aio_ctx;
#endif
    uint8_t *aligned_buf;
    unsigned aligned_buf_size;
#ifdef CONFIG_XFS
    bool is_xfs : 1;
#endif
} BlockFile;

#define TYPE_BLOCK_FILE "block-file"
#define BLOCK_FILE(obj) TYPE_CHECK(BlockFile, obj, TYPE_BLOCK_FILE)

void block_file_initialize(BlockFile *obj, const char *id);

void block_file_finalize(BlockFile *obj);

const char *block_file_get_filename(BlockFile *obj);
void block_file_set_filename(BlockFile *obj, const char *value);

#endif
