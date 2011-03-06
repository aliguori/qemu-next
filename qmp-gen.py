##
# QAPI Code Generator
#
# Copyright IBM, Corp. 2011
#
# Authors:
#  Anthony Liguori   <aliguori@us.ibm.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
##
import sys
from ordereddict import OrderedDict

enum_types = []
event_types = {}

def c_var(name):
    return '_'.join(name.split('-'))

def genindent(count):
    ret = ""
    for i in range(count):
        ret += " "
    return ret

def is_dict(obj):
    if type(obj) in [dict, OrderedDict]:
        return True
    return False

def qmp_array_type_to_c(typename):
    if type(typename) == list or is_dict(typename):
        return qmp_type_to_c(typename)
    elif typename == 'int':
        return 'IntArray *'
    elif typename == 'str':
        return 'StringArray *'
    elif typename == 'bool':
        return 'BoolArray *'
    elif typename == 'number':
        return 'DoubleArray *'
    else:
        return qmp_type_to_c(typename)

def qmp_type_should_free(typename):
    if (type(typename) == list or
        typename == 'str' or
        (typename not in ['int', 'bool', 'number'] and
         typename not in enum_types and not typename.isupper())):
        return True
    return False

def qmp_free_func(typename):
    if type(typename) == list:
        return qmp_free_func(typename[0])
    elif typename == 'str':
        return 'qemu_free'
    else:
        return 'qmp_free_%s' % (de_camel_case(typename))

def qmp_type_is_event(typename):
    if type(typename) == str and typename.isupper():
        return True
    return False

def qmp_type_to_c(typename, retval=False, indent=0):
    if type(typename) == list:
        return qmp_array_type_to_c(typename[0])
    elif is_dict(typename):
        string = 'struct {\n'
        for key in typename:
            name = key
            if key.startswith('*'):
                name = key[1:]
                string += "%sbool has_%s;\n" % (genindent(indent + 4), c_var(name))
            string += "%s%s %s;\n" % (genindent(indent + 4),
                                      qmp_type_to_c(typename[key],
                                                    True,
                                                    indent=(indent + 4)),
                                      c_var(name))
        string += "%s}" % genindent(indent)
        return string
    elif typename == 'int':
        return 'int64_t'
    elif not retval and typename == 'str':
        return 'const char *'
    elif retval and typename == 'str':
        return 'char *'
    elif typename == 'bool':
        return 'bool'
    elif typename == 'number':
        return 'double'
    elif typename == 'none':
        return 'void'
    elif typename in enum_types:
        return typename
    elif qmp_type_is_event(typename):
        return 'struct %s *' % qmp_event_to_c(typename)
    else:
        return 'struct %s *' % typename

def qmp_type_to_qobj_ctor(typename):
    return 'qmp_marshal_type_%s' % typename

def qmp_type_from_qobj(typename):
    return qobj_to_c(typename)

def parse_args(typeinfo):
    for member in typeinfo:
        argname = member
        argtype = typeinfo[member]
        optional = False
        if member.startswith('*'):
            argname = member[1:]
            optional = True
        yield (argname, argtype, optional)

def de_camel_case(name):
    new_name = ''
    for ch in name:
        if ch.isupper() and new_name:
            new_name += '_'
        new_name += ch.lower()
    return new_name

def camel_case(name):
    new_name = ''
    first = True
    for ch in name:
        if ch == '_':
            first = True
        elif first:
            new_name += ch.upper()
            first = False
        else:
            new_name += ch.lower()
    return new_name

def qmp_event_to_c(name):
    return '%sEvent' % camel_case(name)

def qmp_event_func_to_c(name):
    return '%sFunc' % camel_case(name)

def inprint(string, indent):
    print '%s%s' % (genindent(indent), string)

def enum_abbreviation(name):
    ab = ''
    for ch in name:
        if ch.isupper():
            ab += ch
    return ab

def print_enum_declaration(name, entries):
    print
    print 'typedef enum %s {' % name
    i = 0
    for entry in entries:
        print '    %s_%s = %d,' % (enum_abbreviation(name), entry.upper(), i)
        i += 1
    print '} %s;' % name
    print
    print '%s qmp_type_%s_from_str(const char *str, Error **errp);' % (name, de_camel_case(name))
    print 'const char *qmp_type_%s_to_str(%s value, Error **errp);' % (de_camel_case(name), name)

def print_enum_definition(name, entries):
    print '''
%s qmp_type_%s_from_str(const char *str, Error **errp)
{''' % (name, de_camel_case(name))
    first = True
    for entry in entries:
        if first:
            print '    if (strcmp(str, "%s") == 0) {' % entry
            first = False
        else:
            print '    } else if (strcmp(str, "%s") == 0) {' % entry
        print '        return %s_%s;' % (enum_abbreviation(name), entry.upper())
    print '''    } else {
        error_set(errp, QERR_ENUM_VALUE_INVALID, "%s", str);
        return %s_%s;
    }
}''' % (name, enum_abbreviation(name), entries[0].upper())

    print '''
const char *qmp_type_%s_to_str(%s value, Error **errp)
{''' % (de_camel_case(name), name)
    first = True
    for entry in entries:
        enum = '%s_%s' % (enum_abbreviation(name), entry.upper())
        if first:
            print '    if (value == %s) {' % enum
            first = False
        else:
            print '    } else if (value == %s) {' % enum
        print '        return "%s";' % entry
    print '''    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%%d", value);
        error_set(errp, QERR_ENUM_VALUE_INVALID, "%s", buf);
        return NULL;
    }
}''' % name

def print_type_declaration(name, typeinfo):
    if type(typeinfo) == str:
        print
        print "typedef %s %s;" % (qmp_type_to_c(typeinfo), name)
    elif is_dict(typeinfo) and not name.isupper():
        print
        print "typedef struct %s %s;" % (name, name)
        print "struct %s {" % name
        for key in typeinfo:
            member = key
            if key.startswith('*'):
                member = key[1:]
                print "    bool has_%s;" % c_var(member)
            print "    %s %s;" % (qmp_type_to_c(typeinfo[key], True, indent=4), c_var(member))
        print "    %s *next;" % c_var(name)
        print "};"
        print
        print "%s *qmp_alloc_%s(void);" % (name, de_camel_case(name))
        print "void qmp_free_%s(%s *obj);" % (de_camel_case(name), name)
    elif is_dict(typeinfo) and name.isupper():
        arglist = ['void *opaque']
        for argname, argtype, optional in parse_args(typeinfo):
            arglist.append('%s %s' % (qmp_type_to_c(argtype), argname))
        print
        print 'typedef void (%s)(%s);' % (qmp_event_func_to_c(name), ', '.join(arglist))
        print
        print 'typedef struct %s {' % qmp_event_to_c(name)
        print '    QmpSignal *signal;'
        print '    %s *func;' % qmp_event_func_to_c(name)
        print '} %s;' % qmp_event_to_c(name)

def print_type_free(typeinfo, prefix, indent=4):
    for argname, argtype, optional in parse_args(typeinfo):
        if type(argtype) == list:
            argtype = argtype[0]

        if is_dict(argtype):
            if optional:
                inprint('if (%shas_%s) {' % (prefix, argname), indent)
                print_type_free(argtype, '%s%s.' % (prefix, argname), indent + 4)
                inprint('}', indent)
            else:
                print_type_free(argtype, '%s%s.' % (prefix, argname), indent)
        elif qmp_type_should_free(argtype):
            if optional:
                inprint('if (%shas_%s) {' % (prefix, argname), indent)
                inprint('    %s(%s%s);' % (qmp_free_func(argtype), prefix, argname), indent)
                inprint('}', indent)
            else:
                inprint('%s(%s%s);' % (qmp_free_func(argtype), prefix, argname), indent)

def print_type_definition(name, typeinfo):
    if qmp_type_is_event(name):
        return

    c_var_name = de_camel_case(name)

    print '''
void qmp_free_%s(%s *obj)
{
    if (!obj) {
        return;
    }''' % (c_var_name, name)

    print_type_free(typeinfo, 'obj->')

    print '''
    %s(obj->next);
    qemu_free(obj);
}''' % (qmp_free_func(name))

    print '''
%s *qmp_alloc_%s(void)
{
    BUILD_ASSERT(sizeof(%s) < 512);
    return qemu_mallocz(512);
}''' % (name, c_var_name, name)

def tokenize(data):
    while len(data):
        if data[0] in ['{', '}', ':', ',', '[', ']']:
            yield data[0]
            data = data[1:]
        elif data[0] in ' \n':
            data = data[1:]
        elif data[0] == "'":
            data = data[1:]
            string = ''
            while data[0] != "'":
                string += data[0]
                data = data[1:]
            data = data[1:]
            yield string

def parse_value(tokens):
    if tokens[0] == '{':
        ret = OrderedDict()
        tokens = tokens[1:]
        while tokens[0] != '}':
            key = tokens[0]
            tokens = tokens[1:] 

            tokens = tokens[1:] # :

            value, tokens = parse_value(tokens)

            if tokens[0] == ',':
                tokens = tokens[1:]

            ret[key] = value
        tokens = tokens[1:]
        return ret, tokens
    elif tokens[0] == '[':
        ret = []
        tokens = tokens[1:]
        while tokens[0] != ']':
            value, tokens = parse_value(tokens)
            if tokens[0] == ',':
                tokens = tokens[1:]
            ret.append(value)
        tokens = tokens[1:]
        return ret, tokens
    else:
        return tokens[0], tokens[1:]

def ordered_eval(string):
    return parse_value(map(lambda x: x, tokenize(string)))[0]

if len(sys.argv) == 2:
    if sys.argv[1] == '--types-body':
        kind = 'types-body'
    elif sys.argv[1] == '--types-header':
        kind = 'types-header'

if kind == 'types-header':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#ifndef QMP_TYPES_H
#define QMP_TYPES_H

#include "qmp-types-core.h"

'''
elif kind == 'types-body':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */

#include "qmp-types.h"
#include "qemu-common.h"
'''

exprs = []
expr = ''

for line in sys.stdin:
    if line.startswith('#') or line == '\n':
        continue

    if line.startswith(' '):
        expr += line
    elif expr:
        s = ordered_eval(expr)
        exprs.append(s)
        expr = line
    else:
        expr += line

if expr:
    s = ordered_eval(expr)
    exprs.append(s)

for s in exprs:
    if is_dict(s):
        key = s.keys()[0]
        if is_dict(s[key]):
            if qmp_type_is_event(key):
                event_types[key] = s[key]
            if kind == 'types-body':
                print_type_definition(key, s[key])
            elif kind == 'types-header':
                print_type_declaration(key, s[key])
        else:
            enum_types.append(key)
            if kind == 'types-header':
                print_enum_declaration(key, s[key])
            elif kind == 'types-body':
                print_enum_definition(key, s[key])

if kind.endswith('header'):
    print '#endif'
