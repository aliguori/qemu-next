CONFIG_POSIX=y
srcdir=/home/anthony/git/qemu
VPATH=$(srcdir)/block

CFLAGS=-fPIC -Wall -Werror -O -g -I.. -I$(srcdir) -I$(srcdir)/block
#CFLAGS=-Wall -Werror -O -g -I.. -I$(srcdir) -I$(srcdir)/block
CFLAGS+=-D_XOPEN_SOURCE=500 -D_BSD_SOURCE

obj-y += raw.o cow.o qcow.o vdi.o vmdk.o cloop.o dmg.o bochs.o vpc.o vvfat.o
obj-y += qcow2.o qcow2-refcount.o qcow2-cluster.o qcow2-snapshot.o
obj-y += parallels.o nbd.o blkdebug.o sheepdog.o core.o nbd-protocol.o
obj-y += aes.o util.o
obj-y += ../osdep.o ../qemu-malloc.o
obj-$(CONFIG_WIN32) += raw-win32.o
obj-$(CONFIG_POSIX) += raw-posix.o posix-aio-compat.o
obj-$(CONFIG_CURL) += curl.o

all: libqemu-block.so qemu-block-test

libqemu-block.a: $(obj-y)
	$(RM) $@
	ar cq $@ $^

libqemu-block.so: libqemu-block.so.1
	$(RM) $@
	ln -s $^ $@

libqemu-block.so.1: libqemu-block.so.1.0
	$(RM) $@
	ln -s $^ $@

libqemu-block.so.1.0: $(obj-y)
	gcc -shared -Wl,-soname,libqemu-block.so.1 -ldl \
	    -o libqemu-block.so.1.0 $(obj-y)

qemu-block-test: test.o
	$(CC) -o $@ $^ -L . -lqemu-block -lz

clean:
	$(RM) *.o *.so.* *.a *.so
