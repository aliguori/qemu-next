#ifndef _QEMU_VIRTIO_9P_H
#define _QEMU_VIRTIO_9P_H

#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <utime.h>

enum {
	P9_TVERSION = 100,
	P9_RVERSION,
	P9_TAUTH = 102,
	P9_RAUTH,
	P9_TATTACH = 104,
	P9_RATTACH,
	P9_TERROR = 106,
	P9_RERROR,
	P9_TFLUSH = 108,
	P9_RFLUSH,
	P9_TWALK = 110,
	P9_RWALK,
	P9_TOPEN = 112,
	P9_ROPEN,
	P9_TCREATE = 114,
	P9_RCREATE,
	P9_TREAD = 116,
	P9_RREAD,
	P9_TWRITE = 118,
	P9_RWRITE,
	P9_TCLUNK = 120,
	P9_RCLUNK,
	P9_TREMOVE = 122,
	P9_RREMOVE,
	P9_TSTAT = 124,
	P9_RSTAT,
	P9_TWSTAT = 126,
	P9_RWSTAT,
};

/* open modes */
enum {
	P9_OREAD = 0x00,
	P9_OWRITE = 0x01,
	P9_ORDWR = 0x02,
	P9_OEXEC = 0x03,
	P9_OEXCL = 0x04,
	P9_OTRUNC = 0x10,
	P9_OREXEC = 0x20,
	P9_ORCLOSE = 0x40,
	P9_OAPPEND = 0x80,
};

/* permissions */
enum {
	P9_DMDIR = 0x80000000,
	P9_DMAPPEND = 0x40000000,
	P9_DMEXCL = 0x20000000,
	P9_DMMOUNT = 0x10000000,
	P9_DMAUTH = 0x08000000,
	P9_DMTMP = 0x04000000,
	P9_DMSYMLINK = 0x02000000,
	P9_DMLINK = 0x01000000,
	/* 9P2000.u extensions */
	P9_DMDEVICE = 0x00800000,
	P9_DMNAMEDPIPE = 0x00200000,
	P9_DMSOCKET = 0x00100000,
	P9_DMSETUID = 0x00080000,
	P9_DMSETGID = 0x00040000,
};

/* qid.types */
enum {
	P9_QTDIR = 0x80,
	P9_QTAPPEND = 0x40,
	P9_QTEXCL = 0x20,
	P9_QTMOUNT = 0x10,
	P9_QTAUTH = 0x08,
	P9_QTTMP = 0x04,
	P9_QTSYMLINK = 0x02,
	P9_QTLINK = 0x01,
	P9_QTFILE = 0x00,
};

#define P9_NOTAG	(u16)(~0)
#define P9_NOFID	(u32)(~0)
#define P9_MAXWELEM	16

typedef struct V9fsPDU V9fsPDU;

struct V9fsPDU
{
    uint32_t size;
    uint16_t tag;
    uint8_t id;
    VirtQueueElement elem;
    V9fsPDU *next;
};

typedef struct V9fsPosixFileOpertions
{
    int (*lstat)(void *, const char *, struct stat *);
    ssize_t (*readlink)(void *, const char *, char *, size_t);
    int (*chmod)(void *, const char *, mode_t);
    int (*chown)(void *, const char *, uid_t, gid_t);
    int (*mknod)(void *, const char *, mode_t, dev_t);
    int (*mksock)(void *, const char *);
    int (*utime)(void *, const char *, const struct utimbuf *);
    int (*remove)(void *, const char *);
    int (*symlink)(void *, const char *, const char *);
    int (*link)(void *, const char *, const char *);
    int (*setuid)(void *, uid_t);
    int (*close)(void *, int);
    int (*closedir)(void *, DIR *);
    DIR *(*opendir)(void *, const char *);
    int (*open)(void *, const char *, int);
    int (*open2)(void *, const char *, int, mode_t);
    void (*rewinddir)(void *, DIR *);
    off_t (*telldir)(void *, DIR *);
    struct dirent *(*readdir)(void *, DIR *);
    void (*seekdir)(void *, DIR *, off_t);
    ssize_t (*readv)(void *, int, const struct iovec *, int);
    ssize_t (*writev)(void *, int, const struct iovec *, int);
    off_t (*lseek)(void *, int, off_t, int);
    int (*mkdir)(void *, const char *, mode_t);
    int (*fstat)(void *, int, struct stat *);
    int (*rename)(void *, const char *, const char *);
    int (*truncate)(void *, const char *, off_t);
    void *opaque;
} V9fsPosixFileOperations;

V9fsPosixFileOperations *virtio_9p_init_local(const char *path);

#endif
