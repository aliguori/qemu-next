#!/bin/sh

fail() {
    echo "FAILED"
    exit 1
}

for model in ne2k_pci rtl8139 pcnet virtio e1000 ; do
    echo -n "Testing networking for $model... "
    ./run-guest linux --qemu-path=/home/anthony/obj/qemu-tmp --nic=$model \
                      --daemonize=yes
    ./run-test linux boot || fail
    ./run-test linux netup || fail
    ./run-test linux ping || fail
    ./run-test linux reboot || fail
    ./run-test linux netup || fail
    ./run-test linux ping || fail
    ./run-test linux shutdown || fail
    echo "OK"
done
