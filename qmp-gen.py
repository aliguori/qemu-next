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

def qmp_is_proxy_cmd(name):
    return name.startswith('guest-')

def qmp_is_async_cmd(name):
    return name.startswith('guest-')

def qmp_is_stateful_cmd(name):
    return name in ['qmp_capabilities', 'put-event', 'getfd', 'closefd']

def c_var(name):
    return '_'.join(name.split('-'))

def genindent(count):
    ret = ""
    for i in range(count):
        ret += " "
    return ret

indent_level = 0

def push_indent():
    global indent_level
    indent_level += 4

def pop_indent():
    global indent_level
    indent_level -= 4

def cgen(code, **kwds):
    indent = genindent(indent_level)
    lines = code.split('\n')
    lines = map(lambda x: indent + x, lines)
    return '\n'.join(lines) % kwds + '\n'

def mcgen(code, **kwds):
    return cgen('\n'.join(code.split('\n')[1:-1]), **kwds)

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
        for argname, argtype, optional in parse_args(typename):
            if optional:
                string += "%sbool has_%s;\n" % (genindent(indent + 4), c_var(argname))
            string += "%s%s %s;\n" % (genindent(indent + 4),
                                      qmp_type_to_c(argtype, True,
                                                    indent=(indent + 4)),
                                      c_var(argname))
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

def qmp_type_to_qobj(typename):
    return 'qmp_marshal_type_%s' % typename

def qmp_type_from_qobj(typename):
    return 'qmp_unmarshal_type_%s' % typename

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
        if ch in ['_', '-']:
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

def enum_abbreviation(name):
    ab = ''
    for ch in name:
        if ch.isupper():
            ab += ch
    return ab

def gen_type_declaration(name, typeinfo):
    ret = ''
    if type(typeinfo) == str:
        ret += mcgen('''

typedef %(type)s %(name)s;
''',
                     type=qmp_type_to_c(typeinfo),
                     name=name)
    elif is_dict(typeinfo) and not name.isupper():
        ret += mcgen('''

typedef struct %(name)s %(name)s;
struct %(name)s {
''', name=name)
        for argname, argtype, optional in parse_args(typeinfo):
            if optional:
                ret += cgen('    bool has_%(c_name)s;',
                            c_name=c_var(argname))
            ret += cgen('    %(type)s %(c_name)s;',
                        type=qmp_type_to_c(argtype, True, indent=4),
                        c_name=c_var(argname))
        ret += mcgen('''
    %(c_name)s *next;
};

%(name)s *qmp_alloc_%(dcc_name)s(void);
void qmp_free_%(dcc_name)s(%(name)s *obj);
''',
                     c_name=c_var(name), name=name,
                     dcc_name=de_camel_case(name))
    elif is_dict(typeinfo) and name.isupper():
        arglist = ['void *opaque']
        for argname, argtype, optional in parse_args(typeinfo):
            arglist.append('%s %s' % (qmp_type_to_c(argtype), argname))
        ret += mcgen('''

typedef void (%(event_func)s)(%(args)s);

typedef struct %(c_event)s {
    QmpSignal *signal;
    %(event_func)s *func;
} %(c_event)s;
''',
                     event_func=qmp_event_func_to_c(name),
                     args=', '.join(arglist),
                     c_event=qmp_event_to_c(name))
    return ret

def gen_metatype_free(typeinfo, prefix):
    ret = ''

    for argname, argtype, optional in parse_args(typeinfo):
        if type(argtype) == list:
            argtype = argtype[0]

        if is_dict(argtype):
            if optional:
                ret += cgen('    if (%(prefix)shas_%(c_name)s) {',
                            prefix=prefix, c_name=c_var(argname))
                push_indent()
            ret += gen_metatype_free(argtype, '%s%s.' % (prefix, argname))
            if optional:
                pop_indent()
                ret += cgen('    }')
        elif qmp_type_should_free(argtype):
            if optional:
                ret += mcgen('''
    if (%(prefix)shas_%(c_name)s) {
        %(free)s(%(prefix)s%(c_name)s);
    }
''',
                             prefix=prefix, c_name=c_var(argname),
                             free=qmp_free_func(argtype))
            else:
                ret += mcgen('''
    %(free)s(%(prefix)s%(c_name)s);
''',
                             prefix=prefix, c_name=c_var(argname),
                             free=qmp_free_func(argtype))

    return ret

def gen_type_definition(name, typeinfo):
    return mcgen('''

void qmp_free_%(dcc_name)s(%(name)s *obj)
{
    if (!obj) {
        return;
    }
%(type_free)s

    %(free)s(obj->next);
    qemu_free(obj);
}

%(name)s *qmp_alloc_%(dcc_name)s(void)
{
    BUILD_ASSERT(sizeof(%(name)s) < 512);
    return qemu_mallocz(512);
}
''',
                dcc_name=de_camel_case(name), name=name,
                free=qmp_free_func(name),
                type_free=gen_metatype_free(typeinfo, 'obj->'))

def gen_enum_declaration(name, entries):
    ret = mcgen('''

typedef enum %(name)s {
''', name=name)
    i = 0
    for entry in entries:
        ret += cgen('    %(abrev)s_%(name)s = %(value)d,',
                    abrev=enum_abbreviation(name),
                    name=entry.upper(), value=i)
        i += 1
    ret += mcgen('''
} %(name)s;

%(name)s qmp_type_%(dcc_name)s_from_str(const char *str, Error **errp);
const char *qmp_type_%(dcc_name)s_to_str(%(name)s value, Error **errp);
''',
                 name=name, dcc_name=de_camel_case(name))
    return ret

def gen_enum_definition(name, entries):
    ret = mcgen('''

%(name)s qmp_type_%(dcc_name)s_from_str(const char *str, Error **errp)
{
''',
                name=name,
                dcc_name=de_camel_case(name))
    first = True
    for entry in entries:
        prefix = '} else '
        if first:
            prefix = ''
            first = False
        ret += mcgen('''
    %(prefix)sif (strcmp(str, "%(entry)s") == 0) {
        return %(abrev)s_%(value)s;
''',
                     prefix=prefix, entry=entry,
                     abrev=enum_abbreviation(name), value=entry.upper())

    ret += mcgen('''
    } else {
        error_set(errp, QERR_ENUM_VALUE_INVALID, "%(name)s", str);
        return %(abrev)s_%(value)s;
    }
}

const char *qmp_type_%(dcc_name)s_to_str(%(name)s value, Error **errp)
{
''',
                 name=name, abrev=enum_abbreviation(name),
                 value=entries[0].upper(), dcc_name=de_camel_case(name))

    first = True
    for entry in entries:
        enum = '%s_%s' % (enum_abbreviation(name), entry.upper())
        prefix = '} else '
        if first:
            prefix = ''
            first = False
        ret += mcgen('''
    %(prefix)sif (value == %(enum)s) {
        return "%(entry)s";
''',
                     entry=entry, prefix=prefix, enum=enum)
    ret += mcgen('''
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%%d", value);
        error_set(errp, QERR_ENUM_VALUE_INVALID, "%(name)s", buf);
        return NULL;
    }
}
''',
                 name=name)
    return ret

def gen_type_marshal_declaration(name, typeinfo):
    if is_dict(typeinfo):
        return mcgen('''

QObject *qmp_marshal_type_%(name)s(%(type)s src);
%(type)s qmp_unmarshal_type_%(name)s(QObject *src, Error **errp);
''',
                     name=name, type=qmp_type_to_c(name))

    return ''

def gen_metatype_def(typeinfo, name, lhs, sep='->', level=0):
    new_lhs = 'qmp__member%d' % level

    ret = ''

    if type(typeinfo) == str:
        ret += cgen('    %(lhs)s = %(marshal)s(%(name)s);',
                    lhs=lhs, name=name,
                    marshal=qmp_type_to_qobj(typeinfo))
    elif is_dict(typeinfo):
        ret += mcgen('''
    {
        QDict *qmp__dict = qdict_new();
        QObject *%(new_lhs)s;
''',
                     new_lhs=new_lhs)
        for argname, argtype, optional in parse_args(typeinfo):
            ret += cgen('')
            if optional:
                ret += mcgen('''
        if (%(name)s%(sep)shas_%(c_name)s) {
''',
                             name=name, sep=sep,
                             c_name=c_var(argname))
                push_indent()
            push_indent()
            ret += gen_metatype_def(argtype, '%s%s%s' % (name, sep, c_var(argname)), new_lhs, '.', level + 1)
            pop_indent()
            ret += mcgen('''
        qdict_put_obj(qmp__dict, "%(name)s", %(new_lhs)s);
''',
                         name=argname, new_lhs=new_lhs)
            if optional:
                pop_indent()
                ret += mcgen('''
        }
''')
        ret += mcgen('''
        %(lhs)s = QOBJECT(qmp__dict);
    }
''',
                     lhs=lhs)
    elif type(typeinfo) == list:
        ret += mcgen('''
    {
        QList *qmp__list = qlist_new();
        %(type)s %(new_lhs)s_i;

        for (%(new_lhs)s_i = %(name)s; %(new_lhs)s_i != NULL; %(new_lhs)s_i = %(new_lhs)s_i->next) {
            QObject *qmp__member = %(marshal)s(%(new_lhs)s_i);
            qlist_append_obj(qmp__list, qmp__member);
        }
        %(lhs)s = QOBJECT(qmp__list);
    }
''',
                     type=qmp_type_to_c(typeinfo[0], True),
                     new_lhs=new_lhs, name=name, lhs=lhs,
                     marshal=qmp_type_to_qobj(typeinfo[0]))

    return ret

def gen_metatype_undef(typeinfo, name, lhs, sep='->', level=0):
    ret = ''
    if type(typeinfo) == str:
        ret += mcgen('''
    %(lhs)s = %(unmarshal)s(%(c_name)s, &qmp__err);
    if (qmp__err) {
        goto qmp__err_out;
    }
''',
                     lhs=lhs, c_name=c_var(name),
                     unmarshal=qmp_type_from_qobj(typeinfo))
    elif is_dict(typeinfo):
        objname = 'qmp__object%d' % level
        ret += mcgen('''
    {
        QDict *qmp__dict = qobject_to_qdict(%(c_name)s);
        QObject *%(objname)s;

''',
                     c_name=c_var(name), objname=objname)
        for argname, argtype, optional in parse_args(typeinfo):
            if optional:
                ret += mcgen('''
        if (qdict_haskey(qmp__dict, "%(name)s")) {
''',
                             name=argname)
                push_indent()
            ret += mcgen('''
        %(objname)s = qdict_get(qmp__dict, "%(name)s");
''',
                         name=argname, objname=objname)
            push_indent()
            ret += gen_metatype_undef(argtype, objname, '%s%s%s' % (lhs, sep, c_var(argname)), '.', level + 1)
            pop_indent()
            if optional:
                pop_indent()
                ret += mcgen('''
            %(lhs)s%(sep)shas_%(c_name)s = true;
        } else {
            %(lhs)s%(sep)shas_%(c_name)s = false;
        }
''',
                             lhs=lhs, sep=sep, c_name=c_var(argname))

        ret += mcgen('''
    }
''')

    elif type(typeinfo) == list:
        objname = 'qmp__object%d' % level
        ret += mcgen('''
    {
        QList *qmp__list = qobject_to_qlist(%(c_name)s);
        QListEntry *%(objname)s;
        QLIST_FOREACH_ENTRY(qmp__list, %(objname)s) {
            %(type)s qmp__node = %(unmarshal)s(%(objname)s->value, &qmp__err);
            if (qmp__err) {
                goto qmp__err_out;
            }
            qmp__node->next = %(lhs)s;
            %(lhs)s = qmp__node;
        }
    }
''',
                     c_name=c_var(name), objname=objname, lhs=lhs,
                     type=qmp_type_to_c(typeinfo[0], True),
                     unmarshal=qmp_type_from_qobj(typeinfo[0]))

    return ret

def gen_type_marshal_definition(name, typeinfo):
    ret = mcgen('''

QObject *qmp_marshal_type_%(name)s(%(type)s src)
{
    QObject *qmp__retval;

%(marshal)s

    return qmp__retval;
}

%(type)s qmp_unmarshal_type_%(name)s(QObject *src, Error **errp)
{
    Error *qmp__err = NULL;
    %(type)s qmp__retval = qmp_alloc_%(dcc_name)s();

%(unmarshal)s

    return qmp__retval;

qmp__err_out:
    error_propagate(errp, qmp__err);
    %(free)s(qmp__retval);
    return NULL;
}
''',
                 name=name, type=qmp_type_to_c(name),
                 marshal=gen_metatype_def(typeinfo, 'src', 'qmp__retval'),
                 dcc_name=de_camel_case(name), free=qmp_free_func(name),
                 unmarshal=gen_metatype_undef(typeinfo, 'src', 'qmp__retval'))

    return ret

def gen_enum_marshal_declaration(name, entries):
    return mcgen('''

QObject *qmp_marshal_type_%(name)s(%(name)s value);
%(name)s qmp_unmarshal_type_%(name)s(QObject *obj, Error **errp);
''',
                name=name)

def gen_enum_marshal_definition(name, entries):
    return mcgen('''

QObject *qmp_marshal_type_%(name)s(%(name)s value)
{
    return QOBJECT(qint_from_int(value));
}

%(name)s qmp_unmarshal_type_%(name)s(QObject *obj, Error **errp)
{
    return (%(name)s)qint_get_int(qobject_to_qint(obj));
}
''',
                name=name)

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
#    return eval(string)

def generate(kind):
    global enum_types
    global event_types
    global indent_level

    enum_types = []
    event_types = {}
    indent_level = 0

    ret = mcgen('''
/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
''')

    if kind == 'types-header':
        ret += mcgen('''
#ifndef QMP_TYPES_H
#define QMP_TYPES_H

#include "qmp-types-core.h"
''')
    elif kind == 'types-body':
        ret += mcgen('''
#include "qmp-types.h"
#include "qmp-marshal-types.h"
''')
    elif kind == 'marshal-header':
        ret += mcgen('''
#ifndef QMP_MARSHAL_TYPES_H
#define QMP_MARSHAL_TYPES_H

#include "qmp-marshal-types-core.h"
''')
    elif kind == 'marshal-body':
        ret += mcgen('''
#include "qmp-marshal-types.h"
#include "qerror.h"
''')
    
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
       if s.has_key('type'):
           name = s['type']
           data = s['data']

           if kind == 'types-body':
               ret += gen_type_definition(name, data)
           elif kind == 'types-header':
               ret += gen_type_declaration(name, data)
           elif kind == 'marshal-body':
               ret += gen_type_marshal_definition(name, data)
           elif kind == 'marshal-header':
               ret += gen_type_marshal_declaration(name, data)
       elif s.has_key('enum'):
           name = s['enum']
           data = s['data']

           enum_types.append(s['enum'])
           if kind == 'types-header':
               ret += gen_enum_declaration(name, data)
           elif kind == 'types-body':
               ret += gen_enum_definition(name, data)
           elif kind == 'marshal-header':
               ret += gen_enum_marshal_declaration(name, data)
           elif kind == 'marshal-body':
               ret += gen_enum_marshal_definition(name, data)
       elif s.has_key('event'):
           name = s['event']
           data = {}
           if s.has_key('data'):
               data = s['data']

           event_types[name] = data
           if kind == 'types-header':
               ret += gen_type_declaration(name, data)
       elif s.has_key('command'):
            name = s['command']
            options = {}
            if s.has_key('data'):
                options = s['data']
            retval = 'none'
            if s.has_key('returns'):
                retval = s['returns']

    if kind.endswith('header'):
        ret += cgen('#endif')

    return ret

def main(args):
    if len(args) != 1:
        return 1
    if not args[0].startswith('--'):
        return 1

    kind = args[0][2:]

    ret = generate(kind)

    sys.stdout.write(ret)

    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
