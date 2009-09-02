SBCONF := $(wildcard /targets/links/scratchbox.config)
ifeq (,$(SBCONF))
KERNEL := $(shell uname -r)
KO_BUILD_DIR := /lib/modules/$(KERNEL)/build
else
KO_BUILD_DIR := /usr/src/kernel-headers
endif

obj-m += qemugl.o
qemugl-y := qemugl_main.o

.PHONY: all clean

all: libGL.so qemugl.ko

clean:
	rm -f *.so *.ko *.o parse_gl_h client_stub.c gl_func.h qemugl.mod.c Module* modules.order

parse_gl_h: parse_gl_h.c
	$(CC) -g -o $@ $<

client_stub.c: parse_gl_h
	rm -rf temp ; mkdir temp
	cd temp ; mkdir hw; ln -s ../.. hw/gl ; mkdir temp ; cd temp ; ../../$< ; cp $@ ../..
	rm -rf temp

gl_func.h: parse_gl_h
	rm -rf temp ; mkdir temp
	cd temp ; mkdir hw; ln -s ../.. hw/gl ; mkdir temp ; cd temp ; ../../$< ; cp $@ ../..
	rm -rf temp

libGL.so: client_stub.c opengl_client.c gl_func.h qemugl.h
	$(CC) -fPIC -Wall -g -O2 -fno-strict-aliasing opengl_client.c -shared -o $@ -I. -lXext -lX11 -lXfixes -lm -lpthread

qemugl.ko: qemugl_main.c qemugl.h
	make -C $(KO_BUILD_DIR) SUBDIRS=$$PWD SRCROOT=$$PWD
