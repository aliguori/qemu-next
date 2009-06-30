#!/bin/sh

guest=linux

for i in virtio e1000 rtl8139 pcnet ne2k_pci; do
    ./run-guest $guest --net=user --qemu-dir=/home/anthony/obj/qemu-tmp --bios-dir=/home/anthony/git/qemu/pc-bios --nic=$i --daemonize=yes
    for t in boot netup sleep httpget reboot netup sleep httpget shutdown ; do
	echo -n "Running $t test with $i... "
	timeout=
	if test "$t" = "netup" ; then
	    timeout="--timeout=60"
	fi
	if test "$t" = "sleep" ; then
	    timeout="--timeout=10"
	fi

	if ./run-test $guest $t $timeout > /tmp/qemu-test.log; then
	    echo "Ok"
	    cat /tmp/qemu-test.log
	else
	    echo "Failed"
	    echo -n "  "
	    cat /tmp/qemu-test.log
	    ./run-test $guest shutdown
	    break
	fi
    done
done
