#include "virtio.h"
#include "pc.h"
#include "qemu_socket.h"
#include "virtio-9p.h"
#include <sys/uio.h>
#include <arpa/inet.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/un.h>

static const char *base_path;

static const char *rpath(const char *path)
{
    /* FIXME: so wrong... */
    static char buffer[4096];
    snprintf(buffer, sizeof(buffer), "%s/%s", base_path, path);
    return buffer;
}

static int local_lstat(void *opaque, const char *path, struct stat *stbuf)
{
    return lstat(rpath(path), stbuf);
}

static ssize_t local_readlink(void *opaque, const char *path,
			      char *buf, size_t bufsz)
{
    return readlink(rpath(path), buf, bufsz);
}

static int local_chmod(void *opaque, const char *path, mode_t mode)
{
    return chmod(rpath(path), mode);
}

static int local_chown(void *opaque, const char *path, uid_t uid, gid_t gid)
{
    return chown(rpath(path), uid, gid);
}

static int local_mknod(void *opaque, const char *path, mode_t mode, dev_t dev)
{
    return mknod(rpath(path), mode, dev);
}

static int local_mksock(void *opaque, const char *path)
{
    struct sockaddr_un addr;
    int s;

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, 108, "%s", rpath(path));

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s == -1)
	return -1;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr))) {
	close(s);
	return -1;
    }

    close(s);
    return 0;
}

static int local_utime(void *opaque, const char *path,
		       const struct utimbuf *buf)
{
    return utime(rpath(path), buf);
}

static int local_remove(void *opaque, const char *path)
{
    return remove(rpath(path));
}

static int local_symlink(void *opaque, const char *oldpath,
			 const char *newpath)
{
    return symlink(oldpath, rpath(newpath));
}

static int local_link(void *opaque, const char *oldpath, const char *newpath)
{
    char *tmp = strdup(rpath(oldpath));
    int err, serrno = 0;

    if (tmp == NULL)
	return -ENOMEM;

    err = link(tmp, rpath(newpath));
    if (err == -1)
	serrno = errno;

    free(tmp);

    if (err == -1)
	errno = serrno;

    return err;
}

static int local_setuid(void *opaque, uid_t uid)
{
    struct passwd *pw;
    gid_t groups[33];
    int ngroups;
    static uid_t cur_uid = -1;

    if (cur_uid == uid)
	return 0;

    if (setreuid(0, 0))
	return -1;

    pw = getpwuid(uid);
    if (pw == NULL)
	return -1;

    ngroups = 33;
    if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) == -1)
	return -1;

    if (setgroups(ngroups, groups))
	return -1;

    if (setregid(-1, pw->pw_gid))
	return -1;

    if (setreuid(-1, uid))
	return -1;

    cur_uid = uid;

    return 0;
}

static int local_close(void *opaque, int fd)
{
    return close(fd);
}

static int local_closedir(void *opaque, DIR *dir)
{
    return closedir(dir);
}

static int local_open(void *opaque, const char *path, int flags)
{
    return open(rpath(path), flags);
}

static DIR *local_opendir(void *opaque, const char *path)
{
    return opendir(rpath(path));
}

static int local_open2(void *opaque, const char *path, int flags, mode_t mode)
{
    return open(rpath(path), flags, mode);
}

static void local_rewinddir(void *opaque, DIR *dir)
{
    return rewinddir(dir);
}

static off_t local_telldir(void *opaque, DIR *dir)
{
    return telldir(dir);
}

static struct dirent *local_readdir(void *opaque, DIR *dir)
{
    return readdir(dir);
}

static void local_seekdir(void *opaque, DIR *dir, off_t off)
{
    return seekdir(dir, off);
}

static ssize_t local_readv(void *opaque, int fd, const struct iovec *iov,
			   int iovcnt)
{
    return readv(fd, iov, iovcnt);
}

static ssize_t local_writev(void *opaque, int fd, const struct iovec *iov,
			    int iovcnt)
{
    return writev(fd, iov, iovcnt);
}

static off_t local_lseek(void *opaque, int fd, off_t offset, int whence)
{
    return lseek(fd, offset, whence);
}

static int local_mkdir(void *opaque, const char *path, mode_t mode)
{
    return mkdir(rpath(path), mode);
}

static int local_fstat(void *opaque, int fd, struct stat *stbuf)
{
    return fstat(fd, stbuf);
}

static int local_rename(void *opaque, const char *oldpath,
			const char *newpath)
{
    char *tmp;
    int err;

    tmp = strdup(rpath(oldpath));
    if (tmp == NULL)
	return -1;

    err = rename(tmp, rpath(newpath));
    if (err == -1) {
	int serrno = errno;
	free(tmp);
	errno = serrno;
    } else
	free(tmp);

    return err;

}

static int local_truncate(void *opaque, const char *path, off_t size)
{
    return truncate(path, size);
}

static V9fsPosixFileOperations ops = {
    .lstat = local_lstat,
    .readlink = local_readlink,
    .chmod = local_chmod,
    .chown = local_chown,
    .mknod = local_mknod,
    .mksock = local_mksock,
    .utime = local_utime,
    .remove = local_remove,
    .symlink = local_symlink,
    .link = local_link,
    .setuid = local_setuid,
    .close = local_close,
    .closedir = local_closedir,
    .open = local_open,
    .open2 = local_open2,
    .opendir = local_opendir,
    .rewinddir = local_rewinddir,
    .telldir = local_telldir,
    .readdir = local_readdir,
    .seekdir = local_seekdir,
    .readv = local_readv,
    .writev = local_writev,
    .lseek = local_lseek,
    .mkdir = local_mkdir,
    .fstat = local_fstat,
    .rename = local_rename,
    .truncate = local_truncate,
};

V9fsPosixFileOperations *virtio_9p_init_local(const char *path)
{
	base_path = path;
	return &ops;
}
