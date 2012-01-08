#######################################################################
# oslib-obj-y is code depending on the OS (win32 vs posix)
oslib-obj-y = osdep.o
oslib-obj-$(CONFIG_WIN32) += oslib-win32.o qemu-thread-win32.o os-win32.o
oslib-obj-$(CONFIG_POSIX) += oslib-posix.o qemu-thread-posix.o os-posix.o
