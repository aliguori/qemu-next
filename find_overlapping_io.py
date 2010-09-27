#!/usr/bin/env python
import sys

def trace_filter(fobj, event, keys):
    for line in fobj:
        fields = line.strip().split()
        if fields[0] != event:
            continue

        attrs = dict([(k, v) for k, v in (x.split('=') for x in fields[2:])])
        match = True
        for k, v in keys.iteritems():
            if k not in attrs:
                match = False
                break
            if attrs[k] != v:
                match = False
                break

        if match:
            yield attrs

def intersection(a_sector_num, a_nb_sectors, b_sector_num, b_nb_sectors):
    return not (a_sector_num + a_nb_sectors <= b_sector_num or \
                b_sector_num + b_nb_sectors <= a_sector_num)

bs, sector_num, nb_sectors = sys.argv[1:]
sector_num = int(sector_num, 0)
nb_sectors = int(nb_sectors, 0)

for req in trace_filter(sys.stdin, 'bdrv_aio_writev', {'bs': bs}):
    if intersection(sector_num, nb_sectors, int(req['sector_num'], 0), int(req['nb_sectors'], 0)):
        print req
