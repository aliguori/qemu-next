#!/bin/bash
#
# Create a large QED image file for consistency check testing
#
# Copyright (C) 2010 IBM, Corp.
#
# Authors:
#  Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.

# Image size (in MB)
size_mb=$((1 * 1024 * 1024))

# Amount of data to write between metadata updates (in MB)
stride_mb=16

file=$1
cluster_size=65536
table_size=4
table_nelems=$((table_size * cluster_size / 8))
l2_size_mb=$((cluster_size * table_nelems / 1024 / 1024))

if [ -z "$file" ]; then
	echo "usage: $0 <filename>" >&2
	exit 1
fi

# Create a large image file
./qemu-img create -f qed "$file" "$size_mb"M

# Write data in each L2 table
for ((offset = 0; offset < size_mb; offset += l2_size_mb)); do
	./qemu-io -c "write -q ${offset}M ${stride_mb}M" "$file"
done

# Mark image dirty
./qed-tool.py "$file" need_check on
