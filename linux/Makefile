KERNELDIR ?= /lib/modules/$(shell uname -r)/build

# Basic types/annotations that Linux headers use
CORE_HDRS=linux/types.h linux/posix_types.h linux/stddef.h linux/compiler.h
CORE_HDRS+=linux/byteorder/little_endian.h linux/byteorder/big_endian.h
CORE_HDRS+=linux/swab.h linux/ioctl.h

CORE_HDRS+=asm-generic/int-ll64.h asm-generic/int-l64.h asm-generic/ioctl.h

CORE_HDRS+=asm-x86/types.h asm-x86/posix_types.h
CORE_HDRS+=asm-x86/posix_types_32.h asm-x86/posix_types_64.h
CORE_HDRS+=asm-x86/byteorder.h asm-x86/swab.h asm-x86/ioctl.h

CORE_HDRS+=asm-powerpc/types.h asm-powerpc/posix_types.h
CORE_HDRS+=asm-powerpc/byteorder.h asm-powerpc/swab.h asm-powerpc/ioctl.h

CORE_HDRS+=asm-sparc/types.h asm-sparc/posix_types.h
CORE_HDRS+=asm-sparc/byteorder.h asm-sparc/swab.h asm-sparc/ioctl.h
CORE_HDRS+=asm-sparc/asi.h 

CORE_HDRS+=asm-arm/types.h asm-arm/posix_types.h
CORE_HDRS+=asm-arm/byteorder.h asm-arm/swab.h asm-arm/ioctl.h

CORE_HDRS+=asm-parisc/types.h asm-parisc/posix_types.h
CORE_HDRS+=asm-parisc/byteorder.h asm-parisc/swab.h asm-parisc/ioctl.h

# Kernel Virtual Machine interface
KVM_HDRS=linux/kvm.h linux/kvm_para.h
KVM_HDRS+=asm-x86/kvm.h asm-x86/kvm_para.h
KVM_HDRS+=asm-powerpc/kvm.h asm-powerpc/kvm_para.h

# VirtIO paravirtual IO framework
VIRTIO_HDRS=linux/virtio_config.h linux/virtio_net.h linux/virtio_blk.h
VIRTIO_HDRS+=linux/virtio_console.h linux/virtio_balloon.h

# tun/tap interfaces
TUN_HDRS=linux/if_tun.h linux/if_ether.h

# timers
TIMER_HDRS=linux/rtc.h linux/hpet.h

# USB pass through
USB_HDRS=linux/usbdevice_fs.h linux/magic.h

# IDE/FD
DISK_HDRS=linux/cdrom.h linux/fd.h

# Parallel port
PPORT_HDRS=linux/ppdev.h linux/parport.h

sync: sync-kvm sync-virtio sync-tun sync-timer sync-usb sync-disk sync-pport

sync-core: $(CORE_HDRS)

sync-kvm: sync-core $(KVM_HDRS)

sync-virtio: sync-core $(VIRTIO_HDRS)

sync-tun: sync-core $(TUN_HDRS)

sync-timer: sync-core $(TIMER_HDRS)

sync-usb: sync-core $(USB_HDRS)

sync-disk: sync-core $(DISK_HDRS)

sync-pport: sync-core $(PPORT_HDRS)

clean:
	@$(RM) -r linux asm-x86 asm-powerpc asm-generic asm-sparc asm-arm \
	          asm-parisc

linux/%: $(KERNELDIR)/include/linux/%
	@mkdir -p $(shell dirname $@)
	@echo "  IMPORT    $@"
	@sed -f fixup.sed $< > $@

asm-generic/%: $(KERNELDIR)/include/asm-generic/%
	@mkdir -p $(shell dirname $@)
	@echo "  IMPORT    $@"
	@sed -f fixup.sed $< > $@

asm-x86/%: $(KERNELDIR)/arch/x86/include/asm/%
	@mkdir -p $(shell dirname $@)
	@echo "  IMPORT    $@"
	@sed -f fixup.sed $< > $@

asm-powerpc/%: $(KERNELDIR)/arch/powerpc/include/asm/%
	@mkdir -p $(shell dirname $@)
	@echo "  IMPORT    $@"
	@sed -f fixup.sed $< > $@

asm-sparc/%: $(KERNELDIR)/arch/sparc/include/asm/%
	@mkdir -p $(shell dirname $@)
	@echo "  IMPORT    $@"
	@sed -f fixup.sed $< > $@

asm-arm/%: $(KERNELDIR)/arch/arm/include/asm/%
	@mkdir -p $(shell dirname $@)
	@echo "  IMPORT    $@"
	@sed -f fixup.sed $< > $@

asm-parisc/%: $(KERNELDIR)/arch/parisc/include/asm/%
	@mkdir -p $(shell dirname $@)
	@echo "  IMPORT    $@"
	@sed -f fixup.sed $< > $@

.PHONY: clean
