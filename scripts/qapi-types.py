from ordereddict import OrderedDict
from qapi import *
import sys

def generate_fwd_struct(name, members):
    return mcgen('''

typedef struct %(name)s %(name)s;

typedef struct %(name)sList
{
    %(name)s *value;
    struct %(name)sList *next;
} %(name)sList;
''',
                 name=name)

def generate_struct(name, members):
    ret = mcgen('''

struct %(name)s
{
''',
          name=name)

    for argname, argtype, optional in parse_args(members):
        if optional:
            ret += mcgen('''
    bool has_%(c_name)s;
''',
                         c_name=c_var(argname))
        ret += mcgen('''
    %(c_type)s %(c_name)s;
''',
                     c_type=c_type(argtype), c_name=c_var(argname))

    ret += mcgen('''
};
''')

    return ret

def generate_handle(name, typeinfo):
    return mcgen('''

typedef struct %(name)s
{
    %(c_type)s handle;
} %(name)s;

typedef struct %(name)sList
{
    %(name)s *value;
    struct %(name)sList *next;
} %(name)sList;
''',
                 name=name, c_type=c_type(typeinfo))

def generate_enum(name, values):
    ret = mcgen('''

typedef enum %(name)s
{
''',
                name=name)

    i = 1
    for value in values:
        ret += mcgen('''
    %(abbrev)s_%(value)s = %(i)d,
''',
                     abbrev=de_camel_case(name).upper(),
                     value=c_var(value).upper(),
                     i=i)
        i += 1

    ret += mcgen('''
} %(name)s;
''',
                 name=name)

    return ret

def generate_union(name, typeinfo):
    ret = mcgen('''

struct %(name)s
{
    %(name)sKind kind;
    union {
''',
                name=name)

    for key in typeinfo:
        ret += mcgen('''
        %(c_type)s %(c_name)s;
''',
                     c_type=c_type(typeinfo[key]),
                     c_name=c_var(key))

    ret += mcgen('''
    };
};
''')

    return ret

fdecl = open('qapi-types.h', 'w')

exprs = parse_schema(sys.stdin)

fdecl.write('''/* AUTOMATICALLY GENERATED, DO NOT MODIFY */
#ifndef QAPI_TYPES_H
#define QAPI_TYPES_H

#include "qapi-types-core.h"
''')

for expr in exprs:
    ret = ''
    if expr.has_key('type'):
        ret += generate_fwd_struct(expr['type'], expr['data'])
    elif expr.has_key('enum'):
        add_enum(expr['enum'])
        ret += generate_enum(expr['enum'], expr['data'])
    elif expr.has_key('union'):
        add_enum('%sKind' % expr['union'])
        ret += generate_fwd_struct(expr['union'], expr['data'])
        ret += generate_enum('%sKind' % expr['union'], expr['data'].keys())
    fdecl.write(ret)

for expr in exprs:
    ret = ''
    if expr.has_key('type'):
        ret += generate_struct(expr['type'], expr['data'])
    elif expr.has_key('handle'):
        ret += generate_handle(expr['handle'], expr['data'])
    elif expr.has_key('union'):
        ret += generate_union(expr['union'], expr['data'])
    fdecl.write(ret)

fdecl.write('''
#endif
''')

fdecl.flush()
fdecl.close()
