/*
 * Virtio 9p backend
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "virtio.h"
#include "pc.h"
#include "qemu_socket.h"
#include "virtio-9p.h"

#include <assert.h>

/* FIXME
 * 1) change user needs to set groups and stuff
 */

/* from Linux's linux/virtio_9p.h */

/* The ID for virtio console */
#define VIRTIO_ID_9P	9
/* Maximum number of virtio channels per partition (1 for now) */
#define MAX_9P_CHAN	1

#define MAX_REQ		128

#define BUG_ON(cond) assert(!(cond))

typedef struct V9fsFidState V9fsFidState;

typedef struct V9fsString
{
    int16_t size;
    char *data;
} V9fsString;

typedef struct V9fsQID
{
    int8_t type;
    int32_t version;
    int64_t path;
} V9fsQID;

typedef struct V9fsStat
{
    int16_t size;
    int16_t type;
    int32_t dev;
    V9fsQID qid;
    int32_t mode;
    int32_t atime;
    int32_t mtime;
    int64_t length;
    V9fsString name;
    V9fsString uid;
    V9fsString gid;
    V9fsString muid;
    /* 9p2000.u */
    V9fsString extension;
    int32_t n_uid;
    int32_t n_gid;
    int32_t n_muid;
} V9fsStat;

struct V9fsFidState
{
    int32_t fid;
    V9fsString path;
    int fd;
    DIR *dir;
    uid_t uid;
    V9fsFidState *next;
};

typedef struct V9fsState
{
    VirtIODevice vdev;
    VirtQueue *vq;
    V9fsPDU pdus[MAX_REQ];
    V9fsPDU *free_pdu;
    V9fsFidState *fid_list;
    V9fsPosixFileOperations *ops;
    char *root;
    uid_t uid;
} V9fsState;

int dotu = 1;
int debug_9p_pdu = 1;

extern void pprint_pdu(V9fsPDU *pdu);

static int posix_lstat(V9fsState *s, V9fsString *path, struct stat *stbuf)
{
    return s->ops->lstat(s->ops->opaque, path->data, stbuf);
}

static ssize_t posix_readlink(V9fsState *s, V9fsString *path, V9fsString *buf)
{
    ssize_t len;

    buf->data = malloc(1024);
    if (buf->data == NULL) {
	errno = ENOMEM;
	return -1;
    }

    len = s->ops->readlink(s->ops->opaque, path->data, buf->data, 1024 - 1);
    if (len > -1) {
	buf->size = len;
	buf->data[len] = 0;
    }

    return len;
}

static int posix_chmod(V9fsState *s, V9fsString *path, mode_t mode)
{
    return s->ops->chmod(s->ops->opaque, path->data, mode);
}

static int posix_chown(V9fsState *s, V9fsString *path, uid_t uid, gid_t gid)
{
    return s->ops->chown(s->ops->opaque, path->data, uid, gid);
}

static int posix_mknod(V9fsState *s, V9fsString *path, mode_t mode, dev_t dev)
{
    return s->ops->mknod(s->ops->opaque, path->data, mode, dev);
}

static int posix_mksock(V9fsState *s, V9fsString *path)
{
    return s->ops->mksock(s->ops->opaque, path->data);
}

static int posix_utime(V9fsState *s, V9fsString *path,
		       const struct utimbuf *buf)
{
    return s->ops->utime(s->ops->opaque, path->data, buf);
}

static int posix_remove(V9fsState *s, V9fsString *path)
{
    return s->ops->remove(s->ops->opaque, path->data);
}

static int posix_symlink(V9fsState *s, V9fsString *oldpath,
			 V9fsString *newpath)
{
    return s->ops->symlink(s->ops->opaque, oldpath->data, newpath->data);
}

static int posix_link(V9fsState *s, V9fsString *oldpath, V9fsString *newpath)
{
    return s->ops->link(s->ops->opaque, oldpath->data, newpath->data);
}

static int posix_setuid(V9fsState *s, uid_t uid)
{
    return s->ops->setuid(s->ops->opaque, uid);
}

static int posix_close(V9fsState *s, int fd)
{
    return s->ops->close(s->ops->opaque, fd);
}

static int posix_closedir(V9fsState *s, DIR *dir)
{
    return s->ops->closedir(s->ops->opaque, dir);
}

static DIR *posix_opendir(V9fsState *s, V9fsString *path)
{
    return s->ops->opendir(s->ops->opaque, path->data);
}

static int posix_open(V9fsState *s, V9fsString *path, int flags)
{
    return s->ops->open(s->ops->opaque, path->data, flags);
}

static int posix_open2(V9fsState *s, V9fsString *path, int flags, mode_t mode)
{
    return s->ops->open2(s->ops->opaque, path->data, flags, mode);
}

static void posix_rewinddir(V9fsState *s, DIR *dir)
{
    return s->ops->rewinddir(s->ops->opaque, dir);
}

static off_t posix_telldir(V9fsState *s, DIR *dir)
{
    return s->ops->telldir(s->ops->opaque, dir);
}

static struct dirent *posix_readdir(V9fsState *s, DIR *dir)
{
    return s->ops->readdir(s->ops->opaque, dir);
}

static void posix_seekdir(V9fsState *s, DIR *dir, off_t off)
{
    return s->ops->seekdir(s->ops->opaque, dir, off);
}

static int posix_readv(V9fsState *s, int fd, const struct iovec *iov,
		       int iovcnt)
{
    return s->ops->readv(s->ops->opaque, fd, iov, iovcnt);
}

static int posix_writev(V9fsState *s, int fd, const struct iovec *iov,
			int iovcnt)
{
    return s->ops->writev(s->ops->opaque, fd, iov, iovcnt);
}

static off_t posix_lseek(V9fsState *s, int fd, off_t offset, int whence)
{
    return s->ops->lseek(s->ops->opaque, fd, offset, whence);
}

static int posix_mkdir(V9fsState *s, V9fsString *path, mode_t mode)
{
    return s->ops->mkdir(s->ops->opaque, path->data, mode);
}

static int posix_fstat(V9fsState *s, int fd, struct stat *stbuf)
{
    return s->ops->fstat(s->ops->opaque, fd, stbuf);
}

static int posix_rename(V9fsState *s, V9fsString *oldpath,
			V9fsString *newpath)
{
    return s->ops->rename(s->ops->opaque, oldpath->data, newpath->data);
}

static int posix_truncate(V9fsState *s, V9fsString *path, off_t size)
{
    return s->ops->truncate(s->ops->opaque, path->data, size);
}

static void v9fs_string_init(V9fsString *str)
{
    str->data = NULL;
    str->size = 0;
}

static void v9fs_string_free(V9fsString *str)
{
    free(str->data);
    str->data = NULL;
    str->size = 0;
}

static void v9fs_string_null(V9fsString *str)
{
    v9fs_string_free(str);
}

static void v9fs_string_sprintf(V9fsString *str, const char *fmt, ...)
{
    va_list ap;
    int err;

    v9fs_string_free(str);

    va_start(ap, fmt);
    err = vasprintf(&str->data, fmt, ap);
    BUG_ON(err == -1);
    va_end(ap);

    str->size = err;
}

static void v9fs_string_copy(V9fsString *lhs, V9fsString *rhs)
{
    v9fs_string_free(lhs);
    v9fs_string_sprintf(lhs, "%s", rhs->data);
}

static size_t v9fs_string_size(V9fsString *str)
{
    return str->size;
}

static V9fsFidState *lookup_fid(V9fsState *s, int32_t fid)
{
    V9fsFidState *f;

    for (f = s->fid_list; f; f = f->next) {
	if (f->fid == fid) {
	    posix_setuid(s, f->uid);
	    return f;
	}
    }

    return NULL;
}

static V9fsFidState *alloc_fid(V9fsState *s, int32_t fid)
{
    V9fsFidState *f;

    f = lookup_fid(s, fid);
    if (f)
	return NULL;

    f = qemu_mallocz(sizeof(V9fsFidState));
    BUG_ON(f == NULL);

    f->fid = fid;
    f->fd = -1;
    f->dir = NULL;

    f->next = s->fid_list;
    s->fid_list = f;

    return f;
}

static int free_fid(V9fsState *s, int32_t fid)
{
    V9fsFidState **fidpp, *fidp;

    for (fidpp = &s->fid_list; *fidpp; fidpp = &(*fidpp)->next) {
	if ((*fidpp)->fid == fid)
	    break;
    }

    if (*fidpp == NULL)
	return -ENOENT;

    fidp = *fidpp;
    *fidpp = fidp->next;

    if (fidp->fd != -1)
	posix_close(s, fidp->fd);
    if (fidp->dir)
	posix_closedir(s, fidp->dir);
    v9fs_string_free(&fidp->path);
    qemu_free(fidp);

    return 0;
}

static V9fsPDU *alloc_pdu(V9fsState *s)
{
    V9fsPDU *pdu = NULL;

    if (s->free_pdu) {
	pdu = s->free_pdu;
	s->free_pdu = pdu->next;
    }

    return pdu;
}

static void free_pdu(V9fsState *s, V9fsPDU *pdu)
{
    if (pdu) {
	pdu->next = s->free_pdu;
	s->free_pdu = pdu;
    }
}

static size_t pdu_unpack(void *dst, V9fsPDU *pdu, size_t offset, size_t size)
{
    struct iovec *sg = pdu->elem.out_sg;
    BUG_ON((offset + size) > sg[0].iov_len);
    memcpy(dst, sg[0].iov_base + offset, size);
    return size;
}

/* FIXME i can do this with less variables */
static size_t pdu_pack(V9fsPDU *pdu, size_t offset, const void *src, size_t size)
{
    struct iovec *sg = pdu->elem.in_sg;
    size_t off = 0;
    size_t copied = 0;
    int i = 0;

    for (i = 0; size && i < pdu->elem.in_num; i++) {
	size_t len;

	if (offset >= off && offset < (off + sg[i].iov_len)) {
	    len = MIN(sg[i].iov_len - (offset - off), size);
	    memcpy(sg[i].iov_base + (offset - off), src, len);
	    size -= len;
	    offset += len;
	    off = offset;
	    copied += len;
	    src += len;
	} else
	    off += sg[i].iov_len;
    }

    return copied;
}

static int pdu_copy_sg(V9fsPDU *pdu, size_t offset, int rx, struct iovec *sg)
{
    size_t pos = 0;
    int i, j;
    struct iovec *src_sg;
    unsigned int num;

    if (rx) {
	    src_sg = pdu->elem.in_sg;
	    num = pdu->elem.in_num;
    } else {
	    src_sg = pdu->elem.out_sg;
	    num = pdu->elem.out_num;
    }

    j = 0;
    for (i = 0; i < num; i++) {
	if (offset <= pos) {
	    sg[j].iov_base = src_sg[i].iov_base;
	    sg[j].iov_len = src_sg[i].iov_len;
	    j++;
	} else if (offset < (src_sg[i].iov_len + pos)) {
	    sg[j].iov_base = src_sg[i].iov_base;
	    sg[j].iov_len = src_sg[i].iov_len;
	    sg[j].iov_base += (offset - pos);
	    sg[j].iov_len -= (offset - pos);
	    j++;
	}
	pos += src_sg[i].iov_len;
    }

    return j;
}

static size_t pdu_unmarshal(V9fsPDU *pdu, size_t offset, const char *fmt, ...)
{
    size_t old_offset = offset;
    va_list ap;
    int i;

    va_start(ap, fmt);
    for (i = 0; fmt[i]; i++) {
	switch (fmt[i]) {
	case 'b': {
	    int8_t *valp = va_arg(ap, int8_t *);
	    offset += pdu_unpack(valp, pdu, offset, sizeof(*valp));
	    break;
	}
	case 'w': {
	    int16_t *valp = va_arg(ap, int16_t *);
	    offset += pdu_unpack(valp, pdu, offset, sizeof(*valp));
	    break;
	}
	case 'd': {
	    int32_t *valp = va_arg(ap, int32_t *);
	    offset += pdu_unpack(valp, pdu, offset, sizeof(*valp));
	    break;
	}
	case 'q': {
	    int64_t *valp = va_arg(ap, int64_t *);
	    offset += pdu_unpack(valp, pdu, offset, sizeof(*valp));
	    break;
	}
	case 'v': {
	    struct iovec *iov = va_arg(ap, struct iovec *);
	    int *iovcnt = va_arg(ap, int *);
	    *iovcnt = pdu_copy_sg(pdu, offset, 0, iov);
	    break;
	}
	case 's': {
	    V9fsString *str = va_arg(ap, V9fsString *);
	    offset += pdu_unmarshal(pdu, offset, "w", &str->size);
	    /* FIXME: sanity check str->size */
	    str->data = malloc(str->size + 1);
	    BUG_ON(str->data == NULL);
	    offset += pdu_unpack(str->data, pdu, offset, str->size);
	    str->data[str->size] = 0;
	    break;
	}
	case 'Q': {
	    V9fsQID *qidp = va_arg(ap, V9fsQID *);
	    offset += pdu_unmarshal(pdu, offset, "bdq",
				    &qidp->type, &qidp->version, &qidp->path);
	    break;
	}
	case 'S': {
	    V9fsStat *statp = va_arg(ap, V9fsStat *);
	    offset += pdu_unmarshal(pdu, offset, "wwdQdddqsssssddd",
				    &statp->size, &statp->type, &statp->dev,
				    &statp->qid, &statp->mode, &statp->atime,
				    &statp->mtime, &statp->length,
				    &statp->name, &statp->uid, &statp->gid,
				    &statp->muid, &statp->extension,
				    &statp->n_uid, &statp->n_gid,
				    &statp->n_muid);
	    break;
	}
	default:
	    break;
	}
    }

    va_end(ap);

    return offset - old_offset;
}

static size_t pdu_marshal(V9fsPDU *pdu, size_t offset, const char *fmt, ...)
{
    size_t old_offset = offset;
    va_list ap;
    int i;

    va_start(ap, fmt);
    for (i = 0; fmt[i]; i++) {
	switch (fmt[i]) {
	case 'b': {
	    int8_t val = va_arg(ap, int);
	    offset += pdu_pack(pdu, offset, &val, sizeof(val));
	    break;
	}
	case 'w': {
	    int16_t val = va_arg(ap, int);
	    offset += pdu_pack(pdu, offset, &val, sizeof(val));
	    break;
	}
	case 'd': {
	    int32_t val = va_arg(ap, int);
	    offset += pdu_pack(pdu, offset, &val, sizeof(val));
	    break;
	}
	case 'q': {
	    int64_t val = va_arg(ap, int64_t);
	    offset += pdu_pack(pdu, offset, &val, sizeof(val));
	    break;
	}
	case 'v': {
	    struct iovec *iov = va_arg(ap, struct iovec *);
	    int *iovcnt = va_arg(ap, int *);
	    *iovcnt = pdu_copy_sg(pdu, offset, 1, iov);
	    break;
	}
	case 's': {
	    V9fsString *str = va_arg(ap, V9fsString *);
	    offset += pdu_marshal(pdu, offset, "w", str->size);
	    offset += pdu_pack(pdu, offset, str->data, str->size);
	    break;
	}
	case 'Q': {
	    V9fsQID *qidp = va_arg(ap, V9fsQID *);
	    offset += pdu_marshal(pdu, offset, "bdq",
				  qidp->type, qidp->version, qidp->path);
	    break;
	}
	case 'S': {
	    V9fsStat *statp = va_arg(ap, V9fsStat *);
	    offset += pdu_marshal(pdu, offset, "wwdQdddqsssssddd",
				  statp->size, statp->type, statp->dev,
				  &statp->qid, statp->mode, statp->atime,
				  statp->mtime, statp->length, &statp->name,
				  &statp->uid, &statp->gid, &statp->muid,
				  &statp->extension, statp->n_uid,
				  statp->n_gid, statp->n_muid);
	    break;
	}
	default:
	    break;
	}
    }
    va_end(ap);

    return offset - old_offset;
}

static void v9fs_stat_free(V9fsStat *stat)
{
    v9fs_string_free(&stat->name);
    v9fs_string_free(&stat->uid);
    v9fs_string_free(&stat->gid);
    v9fs_string_free(&stat->muid);
    v9fs_string_free(&stat->extension);
}

static void complete_pdu(V9fsState *s, V9fsPDU *pdu, ssize_t len)
{
    int8_t id = pdu->id + 1; /* Response */

    if (len < 0) {
	V9fsString str;
	int err = -len;

	str.data = strerror(err);
	str.size = strlen(str.data);

	len = 7;
	len += pdu_marshal(pdu, len, "s", &str);
	if (dotu)
	    len += pdu_marshal(pdu, len, "d", err);

	id = P9_RERROR;
    }

    /* fill out the header */
    pdu_marshal(pdu, 0, "dbw", (int32_t)len, id, pdu->tag);

    /* keep these in sync */
    pdu->size = len;
    pdu->id = id;

    if (debug_9p_pdu)
	pprint_pdu(pdu);

    /* push onto queue and notify */
    virtqueue_push(s->vq, &pdu->elem, len);

    /* FIXME: we should batch these completions */
    virtio_notify(&s->vdev, s->vq);

    free_pdu(s, pdu);
}

#define P9_QID_TYPE_DIR		0x80
#define P9_QID_TYPE_SYMLINK	0x02

#define P9_STAT_MODE_DIR	0x80000000
#define P9_STAT_MODE_APPEND	0x40000000
#define P9_STAT_MODE_EXCL	0x20000000
#define P9_STAT_MODE_MOUNT	0x10000000
#define P9_STAT_MODE_AUTH	0x08000000
#define P9_STAT_MODE_TMP	0x04000000
#define P9_STAT_MODE_SYMLINK	0x02000000
#define P9_STAT_MODE_LINK	0x01000000
#define P9_STAT_MODE_DEVICE	0x00800000
#define P9_STAT_MODE_NAMED_PIPE	0x00200000
#define P9_STAT_MODE_SOCKET	0x00100000
#define P9_STAT_MODE_SETUID	0x00080000
#define P9_STAT_MODE_SETGID	0x00040000
#define P9_STAT_MODE_SETVTX	0x00010000

#define P9_STAT_MODE_SPECIAL	(P9_STAT_MODE_NAMED_PIPE | \
				 P9_STAT_MODE_SYMLINK | \
				 P9_STAT_MODE_LINK | \
				 P9_STAT_MODE_DEVICE)

/* This is the algorithm from ufs in spfs */
static void stat_to_qid(const struct stat *stbuf, V9fsQID *qidp)
{
    size_t size;

    size = MIN(sizeof(stbuf->st_ino), sizeof(qidp->path));
    memcpy(&qidp->path, &stbuf->st_ino, size);
    qidp->version = stbuf->st_mtime ^ (stbuf->st_size << 8);
    qidp->type = 0;
    if (S_ISDIR(stbuf->st_mode))
	qidp->type |= P9_QID_TYPE_DIR;
    if (S_ISLNK(stbuf->st_mode))
	qidp->type |= P9_QID_TYPE_SYMLINK;
}

static void fid_to_qid(V9fsState *s, V9fsFidState *fidp, V9fsQID *qidp)
{
    struct stat stbuf;
    int err;

    err = posix_lstat(s, &fidp->path, &stbuf);
    BUG_ON(err == -1);

    stat_to_qid(&stbuf, qidp);
}

static uint32_t stat_to_v9mode(const struct stat *stbuf)
{
    uint32_t mode;

    mode = stbuf->st_mode & 0777;
    if (S_ISDIR(stbuf->st_mode))
	mode |= P9_STAT_MODE_DIR;

    if (dotu) {
	if (S_ISLNK(stbuf->st_mode))
	    mode |= P9_STAT_MODE_SYMLINK;
	if (S_ISSOCK(stbuf->st_mode))
	    mode |= P9_STAT_MODE_SOCKET;
	if (S_ISFIFO(stbuf->st_mode))
	    mode |= P9_STAT_MODE_NAMED_PIPE;
	if (S_ISBLK(stbuf->st_mode) || S_ISCHR(stbuf->st_mode))
	    mode |= P9_STAT_MODE_DEVICE;
	if (stbuf->st_mode & S_ISUID)
	    mode |= P9_STAT_MODE_SETUID;
	if (stbuf->st_mode & S_ISGID)
	    mode |= P9_STAT_MODE_SETGID;
	if (stbuf->st_mode & S_ISVTX)
	    mode |= P9_STAT_MODE_SETVTX;
    }

    return mode;
}

static mode_t v9mode_to_mode(uint32_t mode, V9fsString *extension)
{
	mode_t ret;

	ret = mode & 0777;
	if (mode & P9_STAT_MODE_DIR)
		ret |= S_IFDIR;

	if (dotu) {
		if (mode & P9_STAT_MODE_SYMLINK)
			ret |= S_IFLNK;
		if (mode & P9_STAT_MODE_SOCKET)
			ret |= S_IFSOCK;
		if (mode & P9_STAT_MODE_NAMED_PIPE)
			ret |= S_IFIFO;
		if (mode & P9_STAT_MODE_DEVICE) {
			if (extension && extension->data[0] == 'c')
				ret |= S_IFCHR;
			else
				ret |= S_IFBLK;
		}
	}

	if (!(ret&~0777))
		ret |= S_IFREG;

	if (mode & P9_STAT_MODE_SETUID)
		ret |= S_ISUID;
	if (mode & P9_STAT_MODE_SETGID)
		ret |= S_ISGID;
	if (mode & P9_STAT_MODE_SETVTX)
		ret |= S_ISVTX;

	return ret;
}

static void stat_to_v9stat(V9fsState *s, V9fsString *name,
			   const struct stat *stbuf,
			   V9fsStat *v9stat)
{
    int err;
    const char *str;

    memset(v9stat, 0, sizeof(*v9stat));

    stat_to_qid(stbuf, &v9stat->qid);
    v9stat->mode = stat_to_v9mode(stbuf);
    v9stat->atime = stbuf->st_atime;
    v9stat->mtime = stbuf->st_mtime;
    v9stat->length = stbuf->st_size;

    v9fs_string_null(&v9stat->uid);
    v9fs_string_null(&v9stat->gid);
    v9fs_string_null(&v9stat->muid);

    if (dotu) {
	v9stat->n_uid = stbuf->st_uid;
	v9stat->n_gid = stbuf->st_gid;
	v9stat->n_muid = 0;

	v9fs_string_null(&v9stat->extension);

	if (v9stat->mode & P9_STAT_MODE_SYMLINK) {
	    err = posix_readlink(s, name, &v9stat->extension);
	    BUG_ON(err == -1);
	    v9stat->extension.data[err] = 0;
	    v9stat->extension.size = err;
	} else if (v9stat->mode & P9_STAT_MODE_DEVICE) {
	    v9fs_string_sprintf(&v9stat->extension, "%c %u %u",
				S_ISCHR(stbuf->st_mode) ? 'c' : 'b',
				major(stbuf->st_rdev), minor(stbuf->st_rdev));
	}
    }

    str = strrchr(name->data, '/');
    if (str)
	str += 1;
    else
	str = name->data;

    v9fs_string_sprintf(&v9stat->name, "%s", str);

    v9stat->size = 61 +
	v9fs_string_size(&v9stat->name) +
	v9fs_string_size(&v9stat->uid) +
	v9fs_string_size(&v9stat->gid) +
	v9fs_string_size(&v9stat->muid) +
	v9fs_string_size(&v9stat->extension);
}

static void v9fs_version(V9fsState *s, V9fsPDU *pdu)
{
    int32_t msize;
    V9fsString version;
    size_t offset = 7;

    pdu_unmarshal(pdu, offset, "ds", &msize, &version);
    BUG_ON(strcmp(version.data, "9P2000.u") != 0);

    offset += pdu_marshal(pdu, offset, "ds", msize, &version);
    complete_pdu(s, pdu, offset);

    v9fs_string_free(&version);
}

static void v9fs_attach(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid, afid, n_uname;
    V9fsString uname, aname;
    V9fsFidState *fidp;
    V9fsQID qid;
    size_t offset = 7;
    ssize_t err;

    pdu_unmarshal(pdu, offset, "ddssd", &fid, &afid, &uname, &aname, &n_uname);

    fidp = alloc_fid(s, fid);
    if (fidp == NULL) {
	err = -EINVAL;
	goto out;
    }

    fidp->uid = n_uname;

    v9fs_string_sprintf(&fidp->path, "%s", s->root);
    fid_to_qid(s, fidp, &qid);

    offset += pdu_marshal(pdu, offset, "Q", &qid);

    err = offset;
out:
    complete_pdu(s, pdu, err);
    v9fs_string_free(&uname);
    v9fs_string_free(&aname);
}

static void v9fs_stat(V9fsState *s, V9fsPDU *pdu)
{
    V9fsFidState *fidp;
    struct stat stbuf;
    V9fsStat v9stat;
    size_t offset = 7;
    int32_t fid;
    ssize_t err;

    memset(&v9stat, 0, sizeof(v9stat));

    pdu_unmarshal(pdu, offset, "d", &fid);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
	err = -ENOENT;
	goto out;
    }

    err = posix_lstat(s, &fidp->path, &stbuf);
    if (err == -1) {
	err = -errno;
	goto out;
    }

    stat_to_v9stat(s, &fidp->path, &stbuf, &v9stat);

    offset += pdu_marshal(pdu, offset, "wS", 0, &v9stat);

    err = offset;
out:
    complete_pdu(s, pdu, err);
    v9fs_stat_free(&v9stat);
}

static void v9fs_walk(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid, newfid;
    int16_t nwnames;
    V9fsString *wnames = NULL;
    V9fsQID *qids = NULL;
    V9fsFidState *fidp, *newfidp;
    size_t offset = 7;
    int i;
    ssize_t err;
    V9fsString path;

    offset += pdu_unmarshal(pdu, offset, "ddw", &fid, &newfid, &nwnames);

    if(nwnames) {
        wnames = qemu_mallocz(sizeof(wnames[0]) * nwnames);
        BUG_ON(wnames == NULL);

        qids = qemu_mallocz(sizeof(qids[0]) * nwnames);
        BUG_ON(qids == NULL);

        for (i = 0; i < nwnames; i++)
            offset += pdu_unmarshal(pdu, offset, "s", &wnames[i]);
    }

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
	err = -ENOENT;
	goto out;
    }

    /* FIXME: is this really valid? */
    if (fid == newfid) {
	v9fs_string_init(&path);

	for (i = 0; i < nwnames; i++) {
		struct stat stbuf;

		v9fs_string_sprintf(&path, "%s/%s",
				    fidp->path.data, wnames[i].data);
		v9fs_string_copy(&fidp->path, &path);

		err = posix_lstat(s, &fidp->path, &stbuf);
		if (err == -1) {
			v9fs_string_free(&path);
			err = -ENOENT;
			goto out;
		}
		BUG_ON(err == -1);

		stat_to_qid(&stbuf, &qids[i]);
	}

	v9fs_string_free(&path);
    } else {
	newfidp = alloc_fid(s, newfid);
	if (newfidp == NULL) {
	    err = -EINVAL;
	    goto out;
	}

	newfidp->uid = fidp->uid;

	v9fs_string_init(&path);

	v9fs_string_copy(&newfidp->path, &fidp->path);
	for (i = 0; i < nwnames; i++) {
		struct stat stbuf;

		v9fs_string_sprintf(&path, "%s/%s",
				    newfidp->path.data, wnames[i].data);
		v9fs_string_copy(&newfidp->path, &path);

		err = posix_lstat(s, &newfidp->path, &stbuf);
		if (err == -1) {
			free_fid(s, newfid);
			v9fs_string_free(&path);
			err = -ENOENT;
			goto out;
		}
		BUG_ON(err == -1);

		stat_to_qid(&stbuf, &qids[i]);
	}

	v9fs_string_free(&path);
    }

    offset = 7;
    offset += pdu_marshal(pdu, offset, "w", nwnames);

    for (i = 0; i < nwnames; i++)
	offset += pdu_marshal(pdu, offset, "Q", &qids[i]);

    err = offset;
out:
    complete_pdu(s, pdu, err);

    if(nwnames) {
        for (i = 0; i < nwnames; i++)
            v9fs_string_free(&wnames[i]);

        qemu_free(wnames);
        qemu_free(qids);
    }
}

static void v9fs_clunk(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    size_t offset = 7;
    int err;

    pdu_unmarshal(pdu, offset, "d", &fid);

    err = free_fid(s, fid);
    if (err < 0)
	goto out;

    offset = 7;
    err = offset;
out:
    complete_pdu(s, pdu, err);
}

enum {
	Oread		= 0x00,
	Owrite		= 0x01,
	Ordwr		= 0x02,
	Oexec		= 0x03,
	Oexcl		= 0x04,
	Otrunc		= 0x10,
	Orexec		= 0x20,
	Orclose		= 0x40,
	Oappend		= 0x80,
};

static int omode_to_uflags(int8_t mode)
{
    int ret = 0;

    switch (mode & 3) {
    case Oread:
	ret = O_RDONLY;
	break;
    case Ordwr:
	ret = O_RDWR;
	break;
    case Owrite:
	ret = O_WRONLY;
	break;
    case Oexec:
	ret = O_RDONLY;
	break;
    }

    if (mode & Otrunc)
	ret |= O_TRUNC;

    if (mode & Oappend)
	ret |= O_APPEND;

    if (mode & Oexcl)
	ret |= O_EXCL;

    return ret;
}

static void v9fs_open(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    int8_t mode;
    size_t offset = 7;
    V9fsFidState *fidp;
    V9fsQID qid;
    ssize_t err;
    struct stat stbuf;

    pdu_unmarshal(pdu, offset, "db", &fid, &mode);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
	err = -ENOENT;
	goto out;
    }

    err = posix_lstat(s, &fidp->path, &stbuf);
    BUG_ON(err == -1);

    stat_to_qid(&stbuf, &qid);

    if (S_ISDIR(stbuf.st_mode)) {
	fidp->dir = posix_opendir(s, &fidp->path);
	if (fidp->dir == NULL) {
		err = -errno;
		goto out;
	}
    } else {
	fidp->fd = posix_open(s, &fidp->path, omode_to_uflags(mode));
	if (fidp->fd == -1) {
		err = -errno;
		goto out;
	}
    }

    offset += pdu_marshal(pdu, offset, "Qd", &qid, 0);

    err = offset;
out:
    complete_pdu(s, pdu, err);
}

static struct iovec *adjust_sg(struct iovec *sg, int len, int *iovcnt)
{
    while (len && *iovcnt) {
	if (len < sg->iov_len) {
	    sg->iov_len -= len;
	    sg->iov_base += len;
	    len = 0;
	} else {
	    len -= sg->iov_len;
	    sg++;
	    *iovcnt -= 1;
	}
    }

    return sg;
}

static struct iovec *cap_sg(struct iovec *sg, int cap, int *cnt)
{
    int i;
    int total = 0;

    for (i = 0; i < *cnt; i++) {
	if ((total + sg[i].iov_len) > cap) {
	    sg[i].iov_len -= ((total + sg[i].iov_len) - cap);
	    i++;
	    break;
	}
	total += sg[i].iov_len;
    }

    *cnt = i;

    return sg;
}

static void print_sg(struct iovec *sg, int cnt)
{
    int i;

    printf("sg[%d]: {", cnt);
    for (i = 0; i < cnt; i++) {
	if (i)
	    printf(", ");
	printf("(%p, %ld)", sg[i].iov_base, sg[i].iov_len);
    }
    printf("}\n");
}

static void v9fs_read(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid, count, total;
    int64_t off;
    size_t offset = 7;
    V9fsFidState *fidp;
    ssize_t err;

    pdu_unmarshal(pdu, offset, "dqd", &fid, &off, &count);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
	err = -EINVAL;
	goto out;
    }

    if (fidp->dir) {
	struct dirent *dent;
	off_t dir_pos;

	if (off == 0)
	    posix_rewinddir(s, fidp->dir);

	count = 0;
	dir_pos = posix_telldir(s, fidp->dir);
	while ((dent = posix_readdir(s, fidp->dir))) {
	    struct stat stbuf;
	    V9fsString name;
	    int err;
	    V9fsStat v9stat;
	    int32_t len;

	    if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
		continue;

	    memset(&v9stat, 0, sizeof(v9stat));
	    v9fs_string_init(&name);
	    v9fs_string_sprintf(&name, "%s/%s", fidp->path.data, dent->d_name);

	    err = posix_lstat(s, &name, &stbuf);
	    BUG_ON(err == -1);

	    stat_to_v9stat(s, &name, &stbuf, &v9stat);

	    len = pdu_marshal(pdu, offset + 4 + count, "S", &v9stat);
	    if (len != (v9stat.size + 2)) {
		posix_seekdir(s, fidp->dir, dir_pos);
		v9fs_stat_free(&v9stat);
		v9fs_string_free(&name);
		break;
	    }

	    count += len;

	    v9fs_stat_free(&v9stat);
	    v9fs_string_free(&name);

	    dir_pos = dent->d_off;
	}

	offset += pdu_marshal(pdu, offset, "d", count);
	offset += count + 4;
    } else if (fidp->fd != -1) {
	struct iovec iov[128]; /* FIXME: bad, bad, bad */
	struct iovec *sg = iov;
	int cnt;
	int len;

	pdu_marshal(pdu, offset + 4, "v", sg, &cnt);

	err = posix_lseek(s, fidp->fd, off, SEEK_SET);
	BUG_ON(err == -1);

	sg = cap_sg(sg, count, &cnt);

	total = 0;
	while (total < count) {
		do {
		    if (0)
			print_sg(sg, cnt);
		    len = posix_readv(s, fidp->fd, sg, cnt);
		} while (len == -1 && errno == EINTR);

		BUG_ON(len < 0);

		total += len;
		sg = adjust_sg(sg, len, &cnt);

		if (len == 0)
			break;
	}

	offset += pdu_marshal(pdu, offset, "d", total);
	offset += count;
    } else {
	err = -EINVAL;
	goto out;
    }

    err = offset;
out:
    complete_pdu(s, pdu, err);
}

static void v9fs_write(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid, count, total;
    int64_t off;
    size_t offset = 7;
    V9fsFidState *fidp;
    ssize_t err;
    struct iovec iov[128]; /* FIXME: bad, bad, bad */
    struct iovec *sg = iov;
    int cnt;

    pdu_unmarshal(pdu, offset, "dqdv", &fid, &off, &count, sg, &cnt);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
	err = -EINVAL;
	goto out;
    }

    if (fidp->fd == -1) {
	err = -EINVAL;
	goto out;
    }

    err = posix_lseek(s, fidp->fd, off, SEEK_SET);
    BUG_ON(err == -1);

    sg = cap_sg(sg, count, &cnt);

    total = 0;
    while (total < count) {
	int len;

	do {
	    if (0)
		print_sg(sg, cnt);
	    len = posix_writev(s, fidp->fd, sg, cnt);
	} while (len == -1 && errno == EINTR);

	BUG_ON(len < 0);

	total += len;
	sg = adjust_sg(sg, len, &cnt);
	if (len == 0)
	    break;
    }

    offset += pdu_marshal(pdu, offset, "d", total);

    err = offset;
out:
    complete_pdu(s, pdu, err);
}

static void v9fs_create(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsString name, extension, fullname;
    int32_t perm;
    int8_t mode;
    size_t offset = 7;
    V9fsQID qid;
    ssize_t err;
    struct stat stbuf;
    V9fsFidState *fidp;

    v9fs_string_init(&fullname);

    pdu_unmarshal(pdu, offset, "dsdbs", &fid, &name, &perm, &mode, &extension);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
	err = -EINVAL;
	goto out;
    }

    v9fs_string_sprintf(&fullname, "%s/%s", fidp->path.data, name.data);

    if (posix_lstat(s, &fullname, &stbuf) == 0 || errno != ENOENT) {
	err = -EEXIST;
	goto out;
    }

    if (perm & P9_STAT_MODE_DIR) {
	if (posix_mkdir(s, &fullname, perm & 0777)) {
	    err = -errno;
	    goto out;
	}
	if (posix_lstat(s, &fullname, &stbuf)) {
	    err = -errno;
	    goto out;
	}

	fidp->dir = posix_opendir(s, &fullname);
	if (!fidp->dir) {
	    err = -errno;
	    goto out;
	}
    } else if (perm & P9_STAT_MODE_SYMLINK) {
	if (posix_symlink(s, &fullname, &extension)) {
	    err = -errno;
	    goto out;
	}
    } else if (perm & P9_STAT_MODE_LINK) {
	int32_t nfid = atoi(extension.data);
	V9fsFidState *nfidp = lookup_fid(s, nfid);

	if (nfidp == NULL) {
	    err = -errno;
	    goto out;
	}

	if (posix_link(s, &nfidp->path, &fullname)) {
	    err = -errno;
	    goto out;
	}
    } else if (perm & P9_STAT_MODE_DEVICE) {
	char ctype;
	uint32_t major, minor;
	mode_t nmode = 0;

	if (sscanf(extension.data, "%c %u %u", &ctype, &major, &minor) != 3) {
	    err = -errno;
	    goto out;
	}

	switch (ctype) {
	case 'c':
	    nmode = S_IFCHR;
	    break;
	case 'b':
	    nmode = S_IFBLK;
	    break;
	default:
	    err = -EIO;
	    goto out;
	}

	nmode |= perm & 0777;
	if (posix_mknod(s, &fullname, nmode, makedev(major, minor))) {
	    err = -errno;
	    goto out;
	}
    } else if (perm & P9_STAT_MODE_NAMED_PIPE) {
	if (posix_mknod(s, &fullname, S_IFIFO | (mode & 0777), 0)) {
	    err = -errno;
	    goto out;
	}
    } else if (perm & P9_STAT_MODE_SOCKET) {
	if (posix_mksock(s, &fullname)) {
	    err = -errno;
	    goto out;
	}
	if (posix_chmod(s, &fullname, perm & 0777)) {
	    err = -errno;
	    goto out;
	}
    } else {
	fidp->fd = posix_open2(s, &fullname,
			       omode_to_uflags(mode) | O_CREAT,
			       perm & 0777);
	if (fidp->fd == -1) {
	    err = -errno;
	    goto out;
	}

	if (posix_fstat(s, fidp->fd, &stbuf)) {
	    fidp->fd = -1;
	    err = -errno;
	}
    }

    v9fs_string_copy(&fidp->path, &fullname);
    stat_to_qid(&stbuf, &qid);

    offset += pdu_marshal(pdu, offset, "Qd", &qid, 0);

    err = offset;
out:
    v9fs_string_free(&name);
    v9fs_string_free(&extension);
    v9fs_string_free(&fullname);
    complete_pdu(s, pdu, err);
}

static void v9fs_flush(V9fsState *s, V9fsPDU *pdu)
{
    /* A nop call with no return */
    complete_pdu(s, pdu, 7);
}

static void v9fs_remove(V9fsState *s, V9fsPDU *pdu)
{
    size_t offset = 7;
    int32_t fid;
    V9fsFidState *fidp;
    int err;

    pdu_unmarshal(pdu, offset, "d", &fid);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
	err = -EINVAL;
	goto out;
    }

    if (posix_remove(s, &fidp->path)) {
	err = -errno;
	goto out;
    }

    err = free_fid(s, fid);
    if (err < 0)
	goto out;

    err = offset;
out:
    complete_pdu(s, pdu, err);
}

static void v9fs_wstat(V9fsState *s, V9fsPDU *pdu)
{
    size_t offset = 7;
    int32_t fid;
    int16_t unused;
    V9fsStat v9stat;
    V9fsFidState *fidp;
    uid_t uid;
    gid_t gid;
    int err;

    pdu_unmarshal(pdu, offset, "dwS", &fid, &unused, &v9stat);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
	err = -EINVAL;
	goto out;
    }

    uid = v9stat.n_uid;
    gid = v9stat.n_gid;

    if (v9stat.mode != -1) {
	if (v9stat.mode & P9_STAT_MODE_DIR && fidp->dir == NULL) {
	    err = -EIO;
	    goto out;
	}

	if (posix_chmod(s, &fidp->path,
			v9mode_to_mode(v9stat.mode, &v9stat.extension))) {
	    err = -errno;
	    goto out;
	}
    }

    if (v9stat.mtime != -1) {
	struct utimbuf tb;
	tb.actime = 0;
	tb.modtime = v9stat.mtime;
	if (posix_utime(s, &fidp->path, &tb)) {
	    err = -errno;
	    goto out;
	}
    }

    if (gid != -1) {
	if (posix_chown(s, &fidp->path, uid, gid)) {
	    err = -errno;
	    goto out;
	}
    }

    if (v9stat.name.size != 0) {
	char *old_name, *new_name;
	V9fsString nname;
	char *end;

	old_name = fidp->path.data;
	if ((end = strrchr(old_name, '/')))
	    end++;
	else
	    end = old_name;

	new_name = malloc(end - old_name + v9stat.name.size + 1);
	BUG_ON(new_name == NULL);

	memset(new_name, 0, end - old_name + v9stat.name.size + 1);
	memcpy(new_name, old_name, end - old_name);
	memcpy(new_name + (end - old_name), v9stat.name.data, v9stat.name.size);
	nname.data = new_name;
	nname.size = strlen(new_name);

	if (strcmp(new_name, fidp->path.data) != 0) {
	    if (posix_rename(s, &fidp->path, &nname)) {
		err = -errno;
		v9fs_string_free(&nname);
		goto out;
	    }
	}
	v9fs_string_free(&nname);
    }

    if (v9stat.length != -1) {
	if (posix_truncate(s, &fidp->path, v9stat.length) < 0) {
	    err = -errno;
	    goto out;
	}
    }

    err = offset;
out:
    v9fs_stat_free(&v9stat);
    complete_pdu(s, pdu, err);
}

typedef void (pdu_handler_t)(V9fsState *s, V9fsPDU *pdu);

static pdu_handler_t *pdu_handlers[] = {
    [P9_TVERSION] = v9fs_version,
    [P9_TATTACH] = v9fs_attach,
    [P9_TSTAT] = v9fs_stat,
    [P9_TWALK] = v9fs_walk,
    [P9_TCLUNK] = v9fs_clunk,
    [P9_TOPEN] = v9fs_open,
    [P9_TREAD] = v9fs_read,
#if 0
    [P9_TAUTH] = v9fs_auth,
#endif
    [P9_TFLUSH] = v9fs_flush,
    [P9_TCREATE] = v9fs_create,
    [P9_TWRITE] = v9fs_write,
    [P9_TWSTAT] = v9fs_wstat,
    [P9_TREMOVE] = v9fs_remove,
};

static void submit_pdu(V9fsState *s, V9fsPDU *pdu)
{
    pdu_handler_t *handler;

    if (debug_9p_pdu)
	pprint_pdu(pdu);

    BUG_ON(pdu->id >= ARRAY_SIZE(pdu_handlers));

    handler = pdu_handlers[pdu->id];
    BUG_ON(handler == NULL);

    handler(s, pdu);
}

static void handle_9p_output(VirtIODevice *vdev, VirtQueue *vq)
{
    V9fsState *s = (V9fsState *)vdev;
    V9fsPDU *pdu;
    ssize_t len;

    while ((pdu = alloc_pdu(s)) &&
	   (len = virtqueue_pop(vq, &pdu->elem)) != 0) {
	uint8_t *ptr;

	BUG_ON(pdu->elem.out_num == 0 || pdu->elem.in_num == 0);
	BUG_ON(pdu->elem.out_sg[0].iov_len < 7);

	ptr = pdu->elem.out_sg[0].iov_base;

	memcpy(&pdu->size, ptr, 4);
	pdu->id = ptr[4];
	memcpy(&pdu->tag, ptr + 5, 2);

	submit_pdu(s, pdu);
    }

    free_pdu(s, pdu);
}

static uint32_t virtio_9p_get_features(VirtIODevice *vdev)
{
	return 0;
}

VirtIODevice *virtio_9p_init(DeviceState *dev, const char *path)
{
    V9fsState *s;
    int i;

    s = (V9fsState *)virtio_common_init("virtio-9p",
				     VIRTIO_ID_9P,
				     0, sizeof(V9fsState));

    /* initialize pdu allocator */
    s->free_pdu = &s->pdus[0];
    for (i = 0; i < (MAX_REQ - 1); i++)
	s->pdus[i].next = &s->pdus[i + 1];
    s->pdus[i].next = NULL;

    s->vq = virtio_add_queue(&s->vdev, MAX_REQ, handle_9p_output);
    BUG_ON(s->vq == NULL);

    s->root = strdup("/");
    BUG_ON(s->root == NULL);
    s->uid = -1;

    s->ops = virtio_9p_init_local(path);
    s->vdev.get_features = virtio_9p_get_features;

    return &s->vdev;
}
