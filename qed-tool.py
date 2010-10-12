#!/usr/bin/env python
#
# Tool to manipulate QED image files
#
# Copyright (C) 2010 IBM, Corp.
#
# Authors:
#  Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.

import sys
import struct
import random
import optparse

QED_F_NEED_CHECK = 0x02

header_fmt = '<IIIIQQQQII'
header_size = struct.calcsize(header_fmt)
field_names = ['magic', 'cluster_size', 'table_size',
               'first_cluster', 'features', 'compat_features',
               'l1_table_offset', 'image_size',
               'backing_filename_offset', 'backing_filename_size']
table_elem_fmt = '<Q'
table_elem_size = struct.calcsize(table_elem_fmt)

def err(msg):
    sys.stderr.write(msg + '\n')
    sys.exit(1)

def unpack_header(s):
    fields = struct.unpack(header_fmt, s)
    return dict((field_names[idx], val) for idx, val in enumerate(fields))

def pack_header(header):
    fields = tuple(header[x] for x in field_names)
    return struct.pack(header_fmt, *fields)

def unpack_table_elem(s):
    return struct.unpack(table_elem_fmt, s)[0]

def pack_table_elem(elem):
    return struct.pack(table_elem_fmt, elem)

class QED(object):
    def __init__(self, f):
        self.f = f

        self.f.seek(0, 2)
        self.filesize = f.tell()

        self.load_header()
        self.load_l1_table()

    def load_header(self):
        self.f.seek(0)
        self.header = unpack_header(self.f.read(header_size))

    def store_header(self):
        self.f.seek(0)
        self.f.write(pack_header(self.header))

    def read_table(self, offset):
        self.f.seek(offset)
        size = self.header['table_size'] * self.header['cluster_size']
        s = self.f.read(size)
        table = [unpack_table_elem(s[i:i + table_elem_size]) for i in xrange(0, size, table_elem_size)]
        return table

    def load_l1_table(self):
        self.l1_table = self.read_table(self.header['l1_table_offset'])
        self.table_nelems = self.header['table_size'] * self.header['cluster_size'] / table_elem_size

    def write_table(self, offset, table):
        s = ''.join(pack_table_elem(x) for x in table)
        self.f.seek(offset)
        self.f.write(s)

def random_table_item(table):
    return random.choice([(index, offset) for index, offset in enumerate(table) if offset != 0])

def corrupt_table_duplicate(table):
    '''Corrupt a table by introducing a duplicate offset'''
    _, dup_victim = random_table_item(table)

    for i in xrange(len(table)):
        dup_target = random.randint(0, len(table) - 1)
        if table[dup_target] != dup_victim:
            table[dup_target] = dup_victim
            return
    raise Exception('no duplication corruption possible in table')

def corrupt_table_invalidate(qed, table):
    '''Corrupt a table by introducing an invalid offset'''
    index, _ = random_table_item(table)
    table[index] = qed.filesize + random.randint(0, 100 * 1024 * 1024 * 1024 * 1024)

def cmd_show(qed, *args):
    '''show - Show header and l1 table'''
    if not args or args[0] == 'header':
        print qed.header
    elif args[0] == 'l1':
        print qed.l1_table
    else:
        err('unrecognized sub-command')

def cmd_duplicate(qed, table_level):
    '''duplicate l1|l2 - Duplicate a table element'''
    if table_level == 'l1':
        offset = qed.header['l1_table_offset']
        table = qed.l1_table
    elif table_level == 'l2':
        _, offset = random_table_item(qed.l1_table)
        table = qed.read_table(l2_offset)
    else:
        err('unrecognized sub-command')
    corrupt_table_duplicate(table)
    qed.write_table(offset, table)

def cmd_invalidate(qed, table_level):
    '''invalidate l1|l2 - Plant an invalid table element'''
    if table_level == 'l1':
        offset = qed.header['l1_table_offset']
        table = qed.l1_table
    elif table_level == 'l2':
        _, offset = random_table_item(qed.l1_table)
        table = qed.read_table(l2_offset)
    else:
        err('unrecognized sub-command')
    corrupt_table_invalidate(qed, table)
    qed.write_table(offset, table)

def cmd_need_check(qed, *args):
    '''need_check [on|off] - Test, set, or clear the QED_F_NEED_CHECK header bit'''
    if not args:
        print bool(qed.header['features'] & QED_F_NEED_CHECK)
        return

    if args[0] == 'on':
        qed.header['features'] |= QED_F_NEED_CHECK
    elif args[1] == 'off':
        qed.header['features'] &= ~QED_F_NEED_CHECK
    else:
        err('unrecognized sub-command')
    qed.store_header()

def usage():
    sys.stderr.write('usage: %s <filename> <command> [<args...>]\n\n' % sys.argv[0])
    for cmd in sorted(x for x in globals() if x.startswith('cmd_')):
        sys.stderr.write(globals()[cmd].__doc__ + '\n')
    sys.exit(1)

if len(sys.argv) < 3:
    usage()
filename, cmd = sys.argv[1:3]

cmd = 'cmd_' + cmd
if cmd not in globals():
    usage()

qed = QED(open(filename, 'r+b'))
try:
    globals()[cmd](qed, *sys.argv[3:])
except TypeError:
    sys.stderr.write(globals()[cmd].__doc__ + '\n')
    sys.exit(1)
