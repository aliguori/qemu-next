/*
 * QEMU OpenGL driver
 *
 * Copyright (c) 2009 Nokia Corporation
 * Author: Juha Riihimäki <juha.riihimaki@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* NOTE: in its current state, doesn NOT work on 64bit machines!
 * Assumes pointer size <= 32bits */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include "qemugl.h"

//#define QEMUGL_DEBUG
//#define QEMUGL_ENABLE_READ

#ifdef QEMUGL_DEBUG
#define QEMUGL_TRACE(fmt,...) printk(KERN_DEBUG "%s@%d: " fmt "\n", \
									 __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define QEMUGL_TRACE(...)
#endif
#define QEMUGL_ERROR(fmt,...) printk(KERN_ERR "%s@%d: " fmt "\n", \
									 __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define QEMUGL_WRITE_REG(value, reg) __raw_writel(value, qemugl_hw_base + reg)
#define QEMUGL_READ_REG(reg) __raw_readl(qemugl_hw_base + reg)

struct qemugl_client {
  pid_t pid;
  int last_hw_result;
  struct {
	int width;
	int height;
	int bytesperpixel;
	int bytesperline;
	void *data;
  } fb;

  struct qemugl_client *next;
};

static const dev_t qemugl_dev = MKDEV(QEMUGL_DEVICE_MAJOR, QEMUGL_DEVICE_MINOR);
static struct cdev *qemugl_cdev = NULL;
static struct class *qemugl_class = NULL;
static struct device *qemugl_device = NULL;
static struct mutex qemugl_mutex;
static void __iomem *qemugl_hw_base;
static struct qemugl_client *qemugl_clients = NULL;

static struct qemugl_client *qemugl_getclient(pid_t pid)
{
  struct qemugl_client *c = qemugl_clients;
  for (; c; c = c->next) {
	if (c->pid == pid) {
	  return c;
	}
  }
  return NULL;
}

static int qemugl_addclient(pid_t pid)
{
  struct qemugl_client *c = qemugl_getclient(pid);
  if (c) {
	return 0;
  }
  c = qemugl_clients;
  if (!c) {
	c = qemugl_clients = kzalloc(sizeof(*c), GFP_KERNEL);
  } else {
	while (c->next) {
	  c = c->next;
	}
	c->next = kzalloc(sizeof(*c->next), GFP_KERNEL);
	c = c->next;
  }
  if (!c) {
	return -ENOMEM;
  }
  c->pid = pid;
  return 0;
}

static void qemugl_deleteclient(pid_t pid)
{
  struct qemugl_client *c = qemugl_clients;
  if (c->pid == pid) {
	qemugl_clients = c->next;
  } else {
	for (; c; c = c->next) {
	  if (c->next && c->next->pid == pid) {
		struct qemugl_client *d = c->next;
		c->next = d->next;
		c = d;
		break;
	  }
	}
  }
  if (c) {
	if (c->fb.data) {
	  kfree(c->fb.data);
	  c->fb.data = NULL;
	}
	kfree(c);
  }
}

static int qemugl_hw_status(struct qemugl_client *c, void __user *status)
{
  if (copy_to_user(status, &c->last_hw_result, sizeof(c->last_hw_result))) {
	return -EFAULT;
  }
  return 0;
}

static int qemugl_hw_command(struct qemugl_client *c, void __user *args)
{
  unsigned int x = 0;
  int i = QEMUGL_HWREG_FID;
  for (; i < QEMUGL_HWREG_CMD; i += 4) {
	// assume <=32bit pointers:
	if (copy_from_user(&x, args, sizeof(x))) {
	  return -EFAULT;
	}
	args += sizeof(x);
	QEMUGL_WRITE_REG(x, i);
  }
  QEMUGL_WRITE_REG(QEMUGL_HWCMD_GLCALL, QEMUGL_HWREG_CMD);
  // our hw is *fast*, command result can be read immediately...
  c->last_hw_result = QEMUGL_READ_REG(QEMUGL_HWREG_STA);
  return 0;
}

static int qemugl_realloc_framebuffer(struct qemugl_client *c, void __user *arg)
{
  if (c->fb.data) {
	kfree(c->fb.data);
	c->fb.data = NULL;
  }
  if (copy_from_user(&c->fb.width, arg, sizeof(int)) ||
	  copy_from_user(&c->fb.height, arg + sizeof(int), sizeof(int)) ||
	  copy_from_user(&c->fb.bytesperpixel, arg + 2 * sizeof(int), sizeof(int))||
	  copy_from_user(&c->fb.bytesperline, arg + 3 * sizeof(int), sizeof(int))) {
	return -EFAULT;
  }
  QEMUGL_TRACE("width = %d, height = %d, bytes/pixel = %d, bytes/line = %d",
			   c->fb.width, c->fb.height, c->fb.bytesperpixel,
			   c->fb.bytesperline);
  if (c->fb.width * c->fb.bytesperpixel < c->fb.bytesperline) {
	QEMUGL_ERROR("width(%d) * bytes/pixel(%d) < bytes/line(%d)!",
				 c->fb.width, c->fb.bytesperpixel, c->fb.bytesperline);
	return -EFAULT;
  }
#ifdef QEMUGL_IO_FRAMEBUFFER
  c->fb.data = kzalloc(c->fb.bytesperline, GFP_KERNEL);
#else
  c->fb.data = kzalloc(c->fb.height * c->fb.bytesperline, GFP_KERNEL);
  // assume <=32bit pointers:
  QEMUGL_WRITE_REG((unsigned int)c->fb.data, QEMUGL_HWREG_IAP);
  QEMUGL_WRITE_REG(c->fb.bytesperline, QEMUGL_HWREG_IAS);
  QEMUGL_WRITE_REG(QEMUGL_HWCMD_SETBUF, QEMUGL_HWREG_CMD);
#endif
  if (c->fb.data == NULL) {
	QEMUGL_ERROR("unable to allocate memory for frame buffer");
	return -EFAULT;
  }
  return 0;
}

static int qemugl_copy_framebuffer(struct qemugl_client *c, void __user *arg)
{
  int result = 0;
  if (!c->fb.data) {
	QEMUGL_ERROR("frame buffer dimensions not defined");
	result = -EIO;
  } else {
#ifdef QEMUGL_IO_FRAMEBUFFER
#define QEMUGL_COPYFRAME(type)											\
	{																	\
	  int extra = c->fb.bytesperline / c->fb.bytesperpixel - c->fb.width; \
	  int height = c->fb.height;										\
	  while (height--) {												\
		int n = c->fb.width;											\
		type *p = c->fb.data;											\
		for (; n > 3; n -= 4) {											\
		  *(p++) = (type)QEMUGL_READ_REG(QEMUGL_HWREG_BUF);				\
		  *(p++) = (type)QEMUGL_READ_REG(QEMUGL_HWREG_BUF);				\
		  *(p++) = (type)QEMUGL_READ_REG(QEMUGL_HWREG_BUF);				\
		  *(p++) = (type)QEMUGL_READ_REG(QEMUGL_HWREG_BUF);				\
		}																\
		while (n--) {													\
		  *(p++) = (type)QEMUGL_READ_REG(QEMUGL_HWREG_BUF);				\
		}																\
		for (n = extra; n--;) {											\
		  *(p++) = 0;													\
		}																\
		if (copy_to_user(arg, c->fb.data, c->fb.bytesperline)) {		\
		  result = -EFAULT;												\
		  break;														\
		}																\
		arg += c->fb.bytesperline;										\
	  }																	\
	}
	switch (c->fb.bytesperpixel) {
	case 1: QEMUGL_COPYFRAME(unsigned char); break;
	case 2: QEMUGL_COPYFRAME(unsigned short); break;
	case 4: QEMUGL_COPYFRAME(unsigned int); break;
	default:
	  QEMUGL_ERROR("unsupported pixel size (%d bytes)",
				   c->fb.bytesperpixel);
	  break;
	}
#else
	if (copy_to_user(arg, c->fb.data, c->fb.height * c->fb.bytesperline)) {
	  result = -EFAULT;
	}
#endif // QEMUGL_IO_FRAMEBUFFER
  }
  return result;
}

static long qemugl_ioctl(struct file *file,
						 unsigned int cmd, unsigned long arg)
{
  long result = -EINVAL;
  struct qemugl_client *c;
  mutex_lock(&qemugl_mutex);
  if ((c = qemugl_getclient(current->pid))) {
	QEMUGL_TRACE("cmd=0x%08x arg=0x%08lx", cmd, arg);
	switch (cmd) {
	case QEMUGL_FIORNCMD:
	  result = qemugl_hw_command(c, (void __user *)arg);
	  break;
	case QEMUGL_FIORDSTA:
	  result = qemugl_hw_status(c, (void __user *)arg);
	  break;
	case QEMUGL_FIOCPBUF:
	  result = qemugl_copy_framebuffer(c, (void __user *)arg);
	  break;
	case QEMUGL_FIOSTBUF:
	  result = qemugl_realloc_framebuffer(c, (void __user *)arg);
	  break;
	default:
	  QEMUGL_TRACE("unknown command");
	  break;
	}
  }
  mutex_unlock(&qemugl_mutex);
  return result;
}

#ifdef QEMUGL_ENABLE_READ
static ssize_t qemugl_read(struct file *file, char __user *buf,
						   size_t count, loff_t *pos)
{
  struct qemugl_client *c;
  int result;
  if (count < 1) {
	return 0;
  }
  mutex_lock(&qemugl_mutex);
  if (!(c = qemugl_getclient(current->pid))) {
	QEMUGL_ERROR("unknown client pid %d", current->pid);
	result = -EIO;
  } else {
	if (count != c->fb.height * c->fb.bytesperline) {
	  QEMUGL_ERROR("read length (%d) != frame buffer size (%d)",
				   count, c->fb.height * c->fb.bytesperline);
	  result = -EIO;
	} else {
	  qemugl_copy_framebuffer(c, buf);
	  result = count;
	}
  }
  mutex_unlock(&qemugl_mutex);
  return result;
}
#endif // QEMUGL_ENABLE_READ

static int qemugl_open(struct inode *inode, struct file *file)
{
  int result;
  mutex_lock(&qemugl_mutex);
  QEMUGL_TRACE("client pid=%d", current->pid);
  result = qemugl_addclient(current->pid) ? -EACCES : 0;
  mutex_unlock(&qemugl_mutex);
  return result;
}

static int qemugl_release(struct inode *inode, struct file *file)
{
  mutex_lock(&qemugl_mutex);
  QEMUGL_TRACE("client pid=%d", current->pid);
  QEMUGL_WRITE_REG(current->pid, QEMUGL_HWREG_PID);
  QEMUGL_WRITE_REG(QEMUGL_HWCMD_RESET, QEMUGL_HWREG_CMD);
  qemugl_deleteclient(current->pid);
  mutex_unlock(&qemugl_mutex);
  return 0;
}

static const struct file_operations qemugl_fops = {
  .owner = THIS_MODULE,
  .unlocked_ioctl = qemugl_ioctl,
  .open = qemugl_open,
  .release = qemugl_release,
#ifdef QEMUGL_ENABLE_READ
  .read = qemugl_read,
#endif
};

static int __init qemugl_init(void)
{
  int r;
  mutex_init(&qemugl_mutex);
  if (!(qemugl_hw_base = ioremap(QEMUGL_HWREG_REGIONBASE,
								 QEMUGL_HWREG_REGIONSIZE))) {
	QEMUGL_ERROR("ioremap failed");
	return -ENODEV;
  }
  if ((r = register_chrdev_region(qemugl_dev, 1, QEMUGL_DEVICE_NAME)) < 0) {
	QEMUGL_ERROR("failed to register device (%d)", r);
	return -ENODEV;
  }
  if ((qemugl_cdev = cdev_alloc()) == NULL) {
	QEMUGL_ERROR("failed to allocate cdev");
	return -ENOMEM;
  }
  cdev_init(qemugl_cdev, &qemugl_fops);
  if ((r = cdev_add(qemugl_cdev, qemugl_dev, 1)) < 0) {
	QEMUGL_ERROR("failed to add cdev (%d)", r);
	cdev_del(qemugl_cdev);
	qemugl_cdev = NULL;
	return -ENODEV;
  }
  qemugl_class = class_create(THIS_MODULE, QEMUGL_DEVICE_NAME);
  qemugl_device = device_create(qemugl_class, NULL, qemugl_dev, NULL,
								QEMUGL_DEVICE_NAME);
  return 0;
}

static void __exit qemugl_exit(void)
{
  while (qemugl_clients != NULL) {
	qemugl_deleteclient(qemugl_clients->pid);
  }
  if (qemugl_device != NULL) {
	device_destroy(qemugl_class, qemugl_dev);
	qemugl_device = NULL;
  }
  if (qemugl_class != NULL) {
	class_destroy(qemugl_class);
	qemugl_class = NULL;
  }
  if (qemugl_cdev != NULL) {
	cdev_del(qemugl_cdev);
	qemugl_cdev = NULL;
  }
  unregister_chrdev_region(qemugl_dev, 1);
  if (qemugl_hw_base != NULL) {
	iounmap(qemugl_hw_base);
	qemugl_hw_base = NULL;
  }
}

module_init(qemugl_init);
module_exit(qemugl_exit);

MODULE_AUTHOR("Juha Riihimäki");
MODULE_DESCRIPTION("QEMU OpenGL driver");
MODULE_LICENSE("GPL v2");
