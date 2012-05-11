#!/bin/bash
#
# QEMU Launcher
#
# This script enables simple use of the KVM and QEMU tool stack for
# easy kernel testing. It allows to pass either a host directory to
# the guest or a disk image. Example usage:
#
# Run the host root fs inside a VM:
#
# $ ./tools/testing/run-qemu/run-qemu.sh -r /
#
# Run the same with SDL:
#
# $ ./tools/testing/run-qemu/run-qemu.sh -r / --sdl
#
# Or with a PPC build:
#
# $ ARCH=ppc ./tools/testing/run-qemu/run-qemu.sh -r /
#
# PPC with a mac99 model by passing options to QEMU:
#
# $ ARCH=ppc ./tools/testing/run-qemu/run-qemu.sh -r / -- -M mac99
#

USE_SDL=
USE_VNC=
USE_GDB=1
KERNEL_BIN=arch/x86/boot/bzImage
MON_STDIO=
KERNEL_APPEND2=
SERIAL=ttyS0
SERIAL_KCONFIG=SERIAL_8250
BASENAME=$(basename "$0")

function usage() {
	echo "
$BASENAME allows you to execute a virtual machine with the Linux kernel
that you just built. To only execute a simple VM, you can just run it
on your root fs with \"-r / -a init=/bin/bash\"

	-a, --append parameters
		Append the given parameters to the kernel command line.

	-d, --disk image
		Add the image file as disk into the VM.

	-D, --no-gdb
		Don't run an xterm with gdb attached to the guest.

	-r, --root directory
		Use the specified directory as root directory inside the guest.

	-s, --sdl
		Enable SDL graphical output.

	-S, --smp cpus
		Set number of virtual CPUs.

	-v, --vnc
		Enable VNC graphical output.

Examples:

	Run the host root fs inside a VM:
	$ ./tools/testing/run-qemu/run-qemu.sh -r /

	Run the same with SDL:
	$ ./tools/testing/run-qemu/run-qemu.sh -r / --sdl

	Or with a PPC build:
	$ ARCH=ppc ./tools/testing/run-qemu/run-qemu.sh -r /

	PPC with a mac99 model by passing options to QEMU:
	$ ARCH=ppc ./tools/testing/run-qemu/run-qemu.sh -r / -- -M mac99
"
}

function require_config() {
	if [ "$(grep CONFIG_$1=y .config)" ]; then
		return
	fi

	echo "You need to enable CONFIG_$1 for run-qemu to work properly"
	exit 1
}

function has_config() {
	grep -q "CONFIG_$1=y" .config
}

function drive_if() {
	if has_config VIRTIO_BLK; then
		echo virtio
	elif has_config ATA_PIIX; then
		require_config "BLK_DEV_SD"
		echo ide
	else
		echo "\
Your kernel must have either VIRTIO_BLK or ATA_PIIX
enabled for block device assignment" >&2
		exit 1
	fi
}

function verify_qemu() {
	QEMU="$(which $1 2>/dev/null)"

	# binary exists?
	[ -x "$QEMU" ] || exit 1

	# we need a version that knows -machine
	if ! "$QEMU" --help | grep -q -- '-machine'; then
		exit 1
	fi

	echo "$QEMU"
	exit 0
}

GETOPT=`getopt -o a:d:Dhr:sS:v --long append,disk:,no-gdb,help,root:,sdl,smp:,vnc \
	-n "$(basename \"$0\")" -- "$@"`

if [ $? != 0 ]; then
	echo "Terminating..." >&2
	exit 1
fi

eval set -- "$GETOPT"

while true; do
	case "$1" in
	-a|--append)
		KERNEL_APPEND2="$KERNEL_APPEND2 $2"
		shift
		;;
	-d|--disk)
		set -e
		QEMU_OPTIONS="$QEMU_OPTIONS -drive \
			file=$2,if=$(drive_if),cache=unsafe"
		set +e
		USE_DISK=1
		shift
		;;
	-D|--no-gdb)
		USE_GDB=
		;;
	-h|--help)
		usage
		exit 0
		;;
	-r|--root)
		ROOTFS="$2"
		shift
		;;
	-s|--sdl)
		USE_SDL=1
		;;
	-S|--smp)
		SMP="$2"
		shift
		;;
	-v|--vnc)
		USE_VNC=1
		;;
	--)
		shift
		break
		;;
	*)
		echo "Could not parse option: $1" >&2
		exit 1
		;;
	esac
	shift
done

if [ ! "$ROOTFS" -a ! "$USE_DISK" ]; then
	echo "\
Error: Please specify at least -r or -d with a target \
FS to run off of" >&2
	exit 1
fi

# Try to find the KVM accelerated QEMU binary

[ "$ARCH" ] || ARCH=$(uname -m)
case $ARCH in
x86_64)
	# SUSE and Red Hat call the binary qemu-kvm
	[ "$QEMU_BIN" ] || QEMU_BIN=$(verify_qemu qemu-kvm)

	# Debian and Gentoo call it kvm
	[ "$QEMU_BIN" ] || QEMU_BIN=$(verify_qemu kvm)

	# QEMU's own build system calls it qemu-system-x86_64
	[ "$QEMU_BIN" ] || QEMU_BIN=$(verify_qemu qemu-system-x86_64)
	;;
i*86)
	# SUSE and Red Hat call the binary qemu-kvm
	[ "$QEMU_BIN" ] || QEMU_BIN=$(verify_qemu qemu-kvm)

	# Debian and Gentoo call it kvm
	[ "$QEMU_BIN" ] || QEMU_BIN=$(verify_qemu kvm)

	# new i386 version of QEMU
	[ "$QEMU_BIN" ] || QEMU_BIN=$(verify_qemu qemu-system-i386)

	# i386 version of QEMU
	[ "$QEMU_BIN" ] || QEMU_BIN=$(verify_qemu qemu)
	;;
s390*)
	KERNEL_BIN=arch/s390/boot/image
	[ "$QEMU_BIN" ] || QEMU_BIN=$(verify_qemu qemu-system-s390x)
	;;
ppc*)
	KERNEL_BIN=vmlinux

	IS_64BIT=
	has_config PPC64 && IS_64BIT=64
	if has_config PPC_85xx; then
		QEMU_OPTIONS="$QEMU_OPTIONS -M mpc8544ds"
	elif has_config PPC_PSERIES; then
		QEMU_OPTIONS="$QEMU_OPTIONS -M pseries"
		SERIAL=hvc0
		SERIAL_KCONFIG=HVC_CONSOLE
	elif has_config PPC_PMAC; then
		has_config SERIAL_PMACZILOG_TTYS || SERIAL=ttyPZ0
		SERIAL_KCONFIG=SERIAL_PMACZILOG
	else
		echo "Unknown PPC board" >&2
		exit 1
	fi

	[ "$QEMU_BIN" ] || QEMU_BIN=$(which qemu-system-ppc${IS_64BIT} 2>/dev/null)
	;;
esac

if [ ! -e "$QEMU_BIN" ]; then
	echo "\
Could not find a usable QEMU binary. Please install one from \
your distro or from source code using:

  $ git clone git://git.qemu.org/qemu.git
  $ cd qemu
  $ ./configure
  $ make -j
  $ sudo make install

or point this script to a working version of qemu using

  $ export QEMU_BIN=/path/to/qemu-kvm
" >&2
	exit 1
fi

# The binaries without kvm in their name can be too old to support KVM, so
# check for that before the user gets confused
if [ ! "$(echo $QEMU_BIN | grep kvm)" -a \
     ! "$($QEMU_BIN --help | egrep '^-machine')" ]; then
	echo "Your QEMU binary is too old, please update to at least 0.15." >&2
	exit 1
fi
QEMU_OPTIONS="$QEMU_OPTIONS -machine accel=kvm:tcg"

# We need to check some .config variables to make sure we actually work
# on the respective kernel.
if [ ! -e .config ]; then
	echo "\
Please run this script on a fully compiled and configured
Linux kernel build directory" >&2
	exit 1
fi

if [ ! -e "$KERNEL_BIN" ]; then
	echo "Could not find kernel binary: $KERNEL_BIN" >&2
	exit 1
fi

QEMU_OPTIONS="$QEMU_OPTIONS -kernel $KERNEL_BIN"

if [ "$USE_SDL" ]; then
	# SDL is the default, so nothing to do
	:
elif [ "$USE_VNC" ]; then
	QEMU_OPTIONS="$QEMU_OPTIONS -vnc :5"
else
	# When emulating a serial console, tell the kernel to use it as well
	QEMU_OPTIONS="$QEMU_OPTIONS -nographic"
	KERNEL_APPEND="$KERNEL_APPEND console=$SERIAL earlyprintk=serial"
	MON_STDIO=1
	require_config "$SERIAL_KCONFIG"
fi

if [ "$ROOTFS" ]; then
	# Using rootfs with 9p
	require_config "NET_9P_VIRTIO"
	require_config "9P_FS"
	KERNEL_APPEND="$KERNEL_APPEND \
root=/dev/root rootflags=rw,trans=virtio,version=9p2000.L rootfstype=9p"

#Usage: -virtfs fstype,path=/share_path/,security_model=[mapped|passthrough|none],mount_tag=tag.


	QEMU_OPTIONS="$QEMU_OPTIONS \
-virtfs local,id=root,path=$ROOTFS,mount_tag=root,security_model=passthrough \
-device virtio-9p-pci,fsdev=root,mount_tag=/dev/root"
fi

[ "$SMP" ] || SMP=1

# User append args come last
KERNEL_APPEND="$KERNEL_APPEND $KERNEL_APPEND2"

############### Execution #################

QEMU_OPTIONS="$QEMU_OPTIONS -smp $SMP"

echo "
	################# Linux QEMU launcher #################

This script executes your currently built Linux kernel using QEMU. If KVM is
available, it will also use KVM for fast virtualization of your guest.

The intent is to make it very easy to run your kernel. If you need to do more
advanced things, such as passing through real devices, please use QEMU command
line options and add them to the $BASENAME command line using --.

This tool is for simplicity, not world dominating functionality coverage.
(just a hobby, won't be big and professional like libvirt)

"

if [ "$MON_STDIO" ]; then
	echo "\
### Your guest is bound to the current foreground shell. To quit the guest, ###
### please use Ctrl-A x                                                     ###
"
fi

echo -n "  Executing: $QEMU_BIN $QEMU_OPTIONS -append \"$KERNEL_APPEND\" "
for i in "$@"; do
	echo -n "\"$i\" "
done
echo
echo

GDB_PID=
if [ "$USE_GDB" -a "$DISPLAY" -a -x "$(which xterm)" -a -e "$(which gdb)" ]; then
	# Run a gdb console in parallel to the kernel

	# XXX find out if port is in use
	PORT=$(( $$ + 1024 ))
	xterm -T "$BASENAME" -e "sleep 2; gdb vmlinux -ex 'target remote localhost:$PORT' -ex c" &
	GDB_PID=$!
	QEMU_OPTIONS="$QEMU_OPTIONS -gdb tcp::$PORT"
fi

$QEMU_BIN $QEMU_OPTIONS -append "$KERNEL_APPEND" "$@"
wait $GDB_PID &>/dev/null

