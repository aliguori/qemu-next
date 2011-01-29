import sys

meta_types = {}

def c_var(name):
    return '_'.join(name.split('-'))

def genindent(count):
    ret = ""
    for i in range(count):
        ret += " "
    return ret

def qmp_array_type_to_c(typename):
    if type(typename) in [list, dict]:
        return qmp_type_to_c(typename)
    elif typename == 'int':
        return 'IntArray *'
    elif typename == 'str':
        return 'StringArray *'
    elif typename == 'bool':
        return 'BoolArray *'
    elif typename == 'double':
        return 'DoubleArray *'
    else:
        return qmp_type_to_c(typename)

def qmp_type_to_c(typename, retval=False, indent=0):
    if type(typename) == list:
        return qmp_array_type_to_c(typename[0])
    elif type(typename) == dict:
        string = 'struct {\n'
        for key in typename:
            name = key
            if key.startswith('*'):
                name = key[1:]
                string += "%sbool has_%s;\n" % (genindent(indent + 4), c_var(name))
            string += "%s%s %s;\n" % (genindent(indent + 4),
                                      qmp_type_to_c(typename[key],
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
    else:
        return 'struct %s *' % typename

def qmp_type_to_qdict(typename):
    if typename == 'int':
        return 'int'
    elif typename == 'str':
        return 'str'
    elif typename == 'bool':
        return 'bool'
    elif typename == 'number':
        return 'double'

def qmp_type_to_qobj_ctor(typename):
    if typename == 'int':
        return 'qobj_from_int'
    elif typename == 'str':
        return 'qobj_from_str'
    elif typename == 'bool':
        return 'qobj_from_bool'
    elif typename == 'number':
        return 'qobj_from_double'
    else:
        return 'qmp_marshal_type_%s' % typename

def print_declaration(name, required, optional, retval):
    args = ['Monitor *mon']
    for key in required:
        args.append('%s %s' % (qmp_type_to_c(required[key]), c_var(key)))

    for key in optional:
        if key == '**':
            args.append('const QDict *qdict')
        else:
            args.append('bool has_%s' % c_var(key))
            args.append('%s %s' % (qmp_type_to_c(optional[key]), c_var(key)))

    print '%s qmp_%s(%s);' % (qmp_type_to_c(retval, True), c_var(name), ', '.join(args))

def print_definition(name, required, optional, retval):
    print '''
static int qmp_marshal_%s(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
''' % c_var(name)

    for key in required:
        print '    %s %s;' % (qmp_type_to_c(required[key]), c_var(key))

    for key in optional:
        if key == '**':
            continue

        print '    bool has_%s = false;' % (c_var(key))
        print '    %s %s = 0;' % (qmp_type_to_c(optional[key]), c_var(key))

    if retval != 'none':
        print '    %s qmp_retval;' % (qmp_type_to_c(retval, True))

    for key in required:
        print '''
    if (!qdict_haskey(qdict, "%s")) {
        qerror_report(QERR_MISSING_PARAMETER, "%s");
        return -1;
    }
    /* FIXME validate type and send QERR_INVALID_PARAMETER */
    %s = qdict_get_%s(qdict, "%s");
''' % (key, key, c_var(key), qmp_type_to_qdict(required[key]), key)

    for key in optional:
        if key == '**':
            continue

        print '''
    if (qdict_haskey(qdict, "%s")) {
        %s = qdict_get_%s(qdict, "%s");
        has_%s = true;
    }
''' % (key, c_var(key), qmp_type_to_qdict(optional[key]), key, c_var(key))

    args = ['mon']
    for key in required:
        args.append(c_var(key))
    for key in optional:
        if key == '**':
            args.append('qdict')
        else:
            args.append('has_%s' % c_var(key))
            args.append(c_var(key))

    if retval == 'none':
        print '    qmp_%s(%s);' % (c_var(name), ', '.join(args))
    elif type(retval) == str:
        print '    qmp_retval = qmp_%s(%s);' % (c_var(name), ', '.join(args))
        print '    *ret_data = %s(qmp_retval);' % qmp_type_to_qobj_ctor(retval)
    elif type(retval) == list:
        print '    qmp_retval = qmp_%s(%s);' % (c_var(name), ', '.join(args))
        print '''
    *ret_data = QOBJECT(qlist_new());
    if (qmp_retval) {
        QList *list = qobject_to_qlist(*ret_data);
        %s i;
        for (i = qmp_retval; i != NULL; i = i->next) {
            QObject *obj = %s(i);
            qlist_append_obj(list, obj);
        }
    }''' % (qmp_type_to_c(retval[0]), qmp_type_to_qobj_ctor(retval[0]))

    print '''
    return 0;
}'''

def print_metatype_fwd_decl(name, typeinfo):
    print 'static QObject *qmp_marshal_type_%s(%s src);' % (name, qmp_type_to_c(name))

def print_metatype_declaration(name, typeinfo):
    if type(typeinfo) == str:
        print
        print "typedef %s %s;" % (qmp_type_to_c(typeinfo), name)
    elif type(typeinfo) == dict:
        print
        print "typedef struct %s %s;" % (name, name)
        print "struct %s {" % name
        for key in typeinfo:
            member = key
            if key.startswith('*'):
                member = key[1:]
                print "    bool has_%s;" % c_var(member)
            print "    %s %s;" % (qmp_type_to_c(typeinfo[key], indent=4), c_var(member))
        print "    %s *next;" % c_var(name)
        print "};"
        print
        print "void qmp_free_%s(%s *obj);" % (name, name)
        print

def inprint(string, indent):
    print '%s%s' % (genindent(indent), string)

def print_metatype_def(typeinfo, name, lhs, indent=0):
    if indent == 0:
        sep = '->'
    else:
        sep = '.'
    new_lhs = 'qmp__member%d' % (indent / 4)

    if type(typeinfo) == str:
        inprint('    %s = %s(%s);' % (lhs, qmp_type_to_qobj_ctor(typeinfo), name), indent)
    elif type(typeinfo) == dict:
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
        inprint('        %s %s_i;' % (qmp_type_to_c(typeinfo[0]), new_lhs), indent)
        print
        inprint('        for (%s_i = %s; %s_i != NULL; %s_i = %s_i->next) {' % (new_lhs, name, new_lhs, new_lhs, new_lhs), indent)
        inprint('            QObject *qmp__member = %s(%s_i);' % (qmp_type_to_qobj_ctor(typeinfo[0]), new_lhs), indent)
        inprint('            qlist_append_obj(qmp__list, qmp__member);', indent)
        inprint('        }', indent)
        inprint('        %s = QOBJECT(qmp__list);' % lhs, indent)
        inprint('    }', indent)

def print_metatype_definition(name, typeinfo):
    print '''
static QObject *qmp_marshal_type_%s(%s src)
{''' % (name, qmp_type_to_c(name))
    print '    QObject *qmp__retval;'
    print_metatype_def(typeinfo, 'src', 'qmp__retval')
    print '    return qmp__retval;'
    print '}'

if __name__ == '__main__':
    kind = 'body'
    if len(sys.argv) == 2:
        if sys.argv[1] == '--body':
            kind = 'body'
        elif sys.argv[1] == '--header':
            kind = 'header'

if kind == 'header':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#ifndef QMP_H
#define QMP_H

#include "qemu-common.h"
#include "qemu-objects.h"
'''
else:
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */

#include "qmp.h"
#include "monitor.h"
#include "qmp-core.h"

#define qobj_from_int(value) QOBJECT(qint_from_int(value))
#define qobj_from_str(value) QOBJECT(qstring_from_str(value))
#define qobj_from_bool(value) QOBJECT(qbool_from_int(value))
#define qobj_from_double(value) QOBJECT(qfloat_from_double(value))
'''

exprs = []

for line in sys.stdin:
    line = line.strip()
    if not line or line.startswith('#'):
        continue

    s = eval(line)
    exprs.append(s)

for s in exprs:
    if type(s) == dict:
        key = s.keys()[0]
        if kind == 'body':
            print_metatype_fwd_decl(key, s[key])

for s in exprs:
    if type(s) == dict:
        key = s.keys()[0]
        meta_types[key] = s[key]
        if kind == 'body':
            print_metatype_definition(key, meta_types[key])
        else:
            print_metatype_declaration(key, meta_types[key])
        continue

    name, required, optional, retval = s
    if kind == 'body':
        print_definition(name, required, optional, retval)
    else:
        print_declaration(name, required, optional, retval)

if kind == 'header':
    print '#endif'
else:
    print
    print 'static void qmp_init_marshal(void)'
    print '{'
    for s in exprs:
        if type(s) == list:
            print '    qmp_register_command("%s", &qmp_marshal_%s);' % (s[0], c_var(s[0]))
    print '};'
    print
    print 'qapi_init(qmp_init_marshal);'
        
