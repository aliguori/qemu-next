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

def print_metatype_def(typeinfo, name, lhs, indent=0):
    if indent == 0:
        sep = '->'
    else:
        sep = '.'
    new_lhs = 'qmp__member%d' % (indent / 4)

    if type(typeinfo) == str:
        inprint('    %s = %s(%s);' % (lhs, qmp_type_to_qobj_ctor(typeinfo), name), indent)
    elif is_dict(typeinfo):
        inprint('    {', indent)
        inprint('        QDict *qmp__dict = qdict_new();', indent)
        inprint('        QObject *%s;' % new_lhs, indent)
        print
        for key in typeinfo:
            member = key
            if key.startswith('*'):
                member = key[1:]
                inprint('        if (%s%shas_%s) {' % (name, sep, c_var(member)), indent)
                print_metatype_def(typeinfo[key], '%s%s%s' % (name, sep, c_var(member)), new_lhs, indent + 8)
                inprint('            qdict_put_obj(qmp__dict, "%s", %s);' % (member, new_lhs), indent)
                inprint('        }', indent)
            else:
                print_metatype_def(typeinfo[key], '%s%s%s' % (name, sep, c_var(member)), new_lhs, indent + 4)
                inprint('        qdict_put_obj(qmp__dict, "%s", %s);' % (member, new_lhs), indent)
            print
        inprint('        %s = QOBJECT(qmp__dict);' % lhs, indent)
        inprint('    }', indent)
    elif type(typeinfo) == list:
        inprint('    {', indent)
        inprint('        QList *qmp__list = qlist_new();', indent)
        inprint('        %s %s_i;' % (qmp_type_to_c(typeinfo[0], True), new_lhs), indent)
        print
        inprint('        for (%s_i = %s; %s_i != NULL; %s_i = %s_i->next) {' % (new_lhs, name, new_lhs, new_lhs, new_lhs), indent)
        inprint('            QObject *qmp__member = %s(%s_i);' % (qmp_type_to_qobj_ctor(typeinfo[0]), new_lhs), indent)
        inprint('            qlist_append_obj(qmp__list, qmp__member);', indent)
        inprint('        }', indent)
        inprint('        %s = QOBJECT(qmp__list);' % lhs, indent)
        inprint('    }', indent)

def qobj_to_c(typename):
    return 'qmp_unmarshal_type_%s' % typename

def print_metatype_undef(typeinfo, name, lhs, indent=0):
    if indent == 0:
        sep = '->'
    else:
        sep = '.'

    indent += 4

    if type(typeinfo) == str:
        inprint('%s = %s(%s, &qmp__err);' % (lhs, qobj_to_c(typeinfo), name), indent)
        inprint('if (qmp__err) {', indent)
        inprint('    goto qmp__err_out;', indent)
        inprint('}', indent)
    elif is_dict(typeinfo):
        objname = 'qmp__object%d' % ((indent - 4) / 4)
        inprint('{', indent)
        inprint('    QDict *qmp__dict = qobject_to_qdict(%s);' % c_var(name), indent)
        inprint('    QObject *%s;' % objname, indent)
        for key in typeinfo:
            member = key
            optional = False
            if key.startswith('*'):
                member = key[1:]
                optional = True
            if optional:
                inprint('if (qdict_haskey(qmp__dict, "%s")) {' % (member), indent + 4)
                inprint('    %s = qdict_get(qmp__dict, "%s");' % (objname, member), indent + 4)
                inprint('    %s%shas_%s = true;' % (lhs, sep, c_var(member)), indent + 4)
                print_metatype_undef(typeinfo[key], objname, '%s%s%s' % (lhs, sep, c_var(member)), indent + 4)
                inprint('} else {', indent + 4)
                inprint('    %s%shas_%s = false;' % (lhs, sep, c_var(member)), indent + 4)
                inprint('}', indent + 4)
            else:
                inprint('%s = qdict_get(qmp__dict, "%s");' % (objname, key), indent + 4)
                print_metatype_undef(typeinfo[key], objname, '%s%s%s' % (lhs, sep, c_var(member)), indent)
        inprint('}', indent)
    elif type(typeinfo) == list:
        objname = 'qmp__object%d' % ((indent - 4) / 4)
        inprint('{', indent)
        inprint('    QList *qmp__list = qobject_to_qlist(%s);' % c_var(name), indent)
        inprint('    QListEntry *%s;' % objname, indent)
        inprint('    QLIST_FOREACH_ENTRY(qmp__list, %s) {' % objname, indent)
        inprint('        %s qmp__node = %s(%s->value, &qmp__err);' % (qmp_type_to_c(typeinfo[0], True), qmp_type_from_qobj(typeinfo[0]), objname), indent)
        inprint('        if (qmp__err) {', indent)
        inprint('            goto qmp__err_out;', indent)
        inprint('        }', indent)
        inprint('        qmp__node->next = %s;' % lhs, indent)
        inprint('        %s = qmp__node;' % lhs, indent)
        inprint('    }', indent)
        inprint('}', indent)

def print_type_marshal_definition(name, typeinfo):
    c_var_name = de_camel_case(name)
    if qmp_type_is_event(name):
        return

    print '''
QObject *qmp_marshal_type_%s(%s src)
{''' % (name, qmp_type_to_c(name))
    print '    QObject *qmp__retval;'
    print_metatype_def(typeinfo, 'src', 'qmp__retval')
    print '''    return qmp__retval;
}

%s qmp_unmarshal_type_%s(QObject *src, Error **errp)
{''' % (qmp_type_to_c(name), name)
    print '    Error *qmp__err = NULL;'
    print '    %s qmp__retval = qmp_alloc_%s();' % (qmp_type_to_c(name), c_var_name)
    print_metatype_undef(typeinfo, 'src', 'qmp__retval')
    print '''    return qmp__retval;
qmp__err_out:
    error_propagate(errp, qmp__err);
    %s(qmp__retval);
    return NULL;
}''' % (qmp_free_func(name))

def print_type_marshal_declaration(name, typeinfo):
    if qmp_type_is_event(name):
        return

    if is_dict(typeinfo):
        print
        print 'QObject *qmp_marshal_type_%s(%s src);' % (name, qmp_type_to_c(name))
        print '%s qmp_unmarshal_type_%s(QObject *src, Error **errp);' % (qmp_type_to_c(name), name)

def print_enum_marshal_declaration(name, entries):
    print
    print 'QObject *qmp_marshal_type_%s(%s value);' % (name, name)
    print '%s qmp_unmarshal_type_%s(QObject *obj, Error **errp);' % (name, name)

def print_enum_marshal_definition(name, entries):
    print '''
QObject *qmp_marshal_type_%s(%s value)
{
    return QOBJECT(qint_from_int(value));
}
''' % (name, name)

    print '''
%s qmp_unmarshal_type_%s(QObject *obj, Error **errp)
{
    return (%s)qint_get_int(qobject_to_qint(obj));
}
''' % (name, name, name)

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
    elif sys.argv[1] == '--marshal-body':
        kind = 'marshal-body'
    elif sys.argv[1] == '--marshal-header':
        kind = 'marshal-header'

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
elif kind == 'marshal-header':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#ifndef QMP_MARSHAL_TYPES_H
#define QMP_MARSHAL_TYPES_H

#include "qmp-marshal-types-core.h"

'''
elif kind == 'marshal-body':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */

#include "qmp-marshal-types.h"
#include "qerror.h"
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
            elif kind == 'marshal-body':
                print_type_marshal_definition(key, s[key])
            elif kind == 'marshal-header':
                print_type_marshal_declaration(key, s[key])
        else:
            enum_types.append(key)
            if kind == 'types-header':
                print_enum_declaration(key, s[key])
            elif kind == 'types-body':
                print_enum_definition(key, s[key])
            elif kind == 'marshal-header':
                print_enum_marshal_declaration(key, s[key])
            elif kind == 'marshal-body':
                print_enum_marshal_definition(key, s[key])

if kind.endswith('header'):
    print '#endif'
