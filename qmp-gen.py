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

def qmp_type_to_qobj_ctor(typename):
    return 'qmp_marshal_type_%s' % typename

def qmp_type_from_qobj(typename):
    return 'qmp_unmarshal_type_%s' % typename

def print_lib_decl(name, required, optional, retval, suffix=''):
    args = ['QmpSession *qmp__session']
    for key in required:
        args.append('%s %s' % (qmp_type_to_c(required[key]), c_var(key)))

    for key in optional:
        if optional[key] == '**':
            args.append('KeyValues * %s' % c_var(key))
        else:
            args.append('bool has_%s' % c_var(key))
            args.append('%s %s' % (qmp_type_to_c(optional[key]), c_var(key)))

    args.append('Error **qmp__err')

    print '%s libqmp_%s(%s)%s' % (qmp_type_to_c(retval, True), c_var(name), ', '.join(args), suffix)

def print_lib_declaration(name, required, optional, retval):
    print_lib_decl(name, required, optional, retval, ';')

def print_lib_definition(name, required, optional, retval):
    print
    print_lib_decl(name, required, optional, retval)
    print '''{
    QDict *qmp__args = qdict_new();
    Error *qmp__local_err = NULL;'''

    if retval != 'none':
        print '    QObject *qmp__retval = NULL;'
        print '    %s qmp__native_retval = 0;' % (qmp_type_to_c(retval))
    print

    for key in required:
        argname = key
        argtype = required[key]
        print '    qdict_put_obj(qmp__args, "%s", %s(%s));' % (key, qmp_type_to_qobj_ctor(argtype), c_var(argname))
    if required:
        print

    for key in optional:
        argname = key
        argtype = optional[key]
        if argtype.startswith('**'):
            print '''    {
        KeyValues *qmp__i;
        for (qmp__i = %s; qmp__i; qmp__i = qmp__i->next) {
            qdict_put(qmp__args, qmp__i->key, qstring_from_str(qmp__i->value));
        }
    }''' % c_var(argname)
            continue
        print '    if (has_%s) {' % c_var(argname)
        print '        qdict_put_obj(qmp__args, "%s", %s(%s));' % (key, qmp_type_to_qobj_ctor(argtype), c_var(argname))
        print '    }'
        print

    if retval == 'none':
        print '    qmp__session->dispatch(qmp__session, "%s", qmp__args, &qmp__local_err);' % name
    else:
        print '    qmp__retval = qmp__session->dispatch(qmp__session, "%s", qmp__args, &qmp__local_err);' % name

    print
    print '    QDECREF(qmp__args);'

    if type(retval) == list:
        print '''
    if (!qmp__local_err) {
        QList *qmp__list_retval = qobject_to_qlist(qmp__retval);
        QListEntry *qmp__i;
        QLIST_FOREACH_ENTRY(qmp__list_retval, qmp__i) {
            %s qmp__native_i = %s(qmp__i->value);
            qmp__native_i->next = qmp__native_retval;
            qmp__native_retval = qmp__native_i;
        }
        qobject_decref(qmp__retval);
    }
    error_propagate(qmp__err, qmp__local_err);
    return qmp__native_retval;''' % (qmp_type_to_c(retval[0]), qmp_type_from_qobj(retval[0]))
    elif type(retval) == dict:
        print '    // FIXME (using an anonymous dict as return value'
    elif retval != 'none':
        print '''
    if (!qmp__local_err) {
        qmp__native_retval = %s(qmp__retval);
        qobject_decref(qmp__retval);
    }
    error_propagate(qmp__err, qmp__local_err);
    return qmp__native_retval;''' % qmp_type_from_qobj(retval)
    else:
        print '    error_propagate(qmp__err, qmp__local_err);'

    print '}'

def print_declaration(name, required, optional, retval):
    args = []
    for key in required:
        args.append('%s %s' % (qmp_type_to_c(required[key]), c_var(key)))

    for key in optional:
        if optional[key] == '**':
            args.append('KeyValues * %s' % c_var(key))
        else:
            args.append('bool has_%s' % c_var(key))
            args.append('%s %s' % (qmp_type_to_c(optional[key]), c_var(key)))

    args.append('Error **err')

    print '%s qmp_%s(%s);' % (qmp_type_to_c(retval, True), c_var(name), ', '.join(args))

def print_definition(name, required, optional, retval):
    print '''
static void qmp_marshal_%s(const QDict *qdict, QObject **ret_data, Error **err)
{''' % c_var(name)

    for key in required:
        print '    %s %s;' % (qmp_type_to_c(required[key]), c_var(key))

    for key in optional:
        if optional[key] == '**':
            print '    KeyValues * %s;' % c_var(key)
        else:
            print '    bool has_%s = false;' % (c_var(key))
            print '    %s %s = 0;' % (qmp_type_to_c(optional[key]), c_var(key))

    if retval != 'none':
        print '    %s qmp_retval;' % (qmp_type_to_c(retval, True))

    for key in required:
        print '''
    if (!qdict_haskey(qdict, "%s")) {
        error_set(err, QERR_MISSING_PARAMETER, "%s");
        return;
    }
    /* FIXME validate type and send QERR_INVALID_PARAMETER */
    %s = %s(qdict_get(qdict, "%s"));
''' % (key, key, c_var(key), qmp_type_from_qobj(required[key]), key)

    for key in optional:
        if optional[key] == '**':
            print '''
    {
        const QDictEntry *qmp__qdict_i;

        %s = NULL;
        for (qmp__qdict_i = qdict_first(qdict); qmp__qdict_i; qmp__qdict_i = qdict_next(qdict, qmp__qdict_i)) {
            KeyValues *qmp__i;''' % c_var(key)
            for key1 in required.keys() + optional.keys():
                if key1 == key:
                    continue
                print '''            if (strcmp(qmp__qdict_i->key, "%s") == 0) {
                continue;
            }''' % key1
            print '''
            qmp__i = qmp_alloc_KeyValues();
            qmp__i->key = qemu_strdup(qmp__qdict_i->key);
            qmp__i->value = qobject_as_string(qmp__qdict_i->value);
            qmp__i->next = %s;
            %s = qmp__i;
        }
    }''' % (c_var(key), c_var(key))
        else:
            print '''
    if (qdict_haskey(qdict, "%s")) {
        %s = %s(qdict_get(qdict, "%s"));
        has_%s = true;
    }
''' % (key, c_var(key), qmp_type_from_qobj(optional[key]), key, c_var(key))

    args = []
    for key in required:
        args.append(c_var(key))
    for key in optional:
        if optional[key] == '**':
            args.append(c_var(key))
        else:
            args.append('has_%s' % c_var(key))
            args.append(c_var(key))
    args.append('err')

    arglist = ', '.join(args)
    fn = 'qmp_%s' % c_var(name)

    if retval == 'none':
        print '    %s(%s);' % (fn, arglist)
    else:
        print '    qmp_retval = %s(%s);' % (fn, arglist)

    print '''
    if (error_is_set(err)) {
        return;
    }'''

    if retval == 'none':
        pass
    elif type(retval) == str:
        print '    *ret_data = %s(qmp_retval);' % qmp_type_to_qobj_ctor(retval)
    elif type(retval) == list:
        print '''    *ret_data = QOBJECT(qlist_new());
    if (qmp_retval) {
        QList *list = qobject_to_qlist(*ret_data);
        %s i;
        for (i = qmp_retval; i != NULL; i = i->next) {
            QObject *obj = %s(i);
            qlist_append_obj(list, obj);
        }
    }''' % (qmp_type_to_c(retval[0]), qmp_type_to_qobj_ctor(retval[0]))

    print '''
}'''

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
        print "%s *qmp_alloc_%s(void);" % (name, name)
        print "void qmp_free_%s(%s *obj);" % (name, name)
        print 'QObject *qmp_marshal_type_%s(%s src);' % (name, qmp_type_to_c(name))
        print '%s qmp_unmarshal_type_%s(QObject *src);' % (qmp_type_to_c(name), name)
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

def qobj_to_c(typename):
    return 'qmp_unmarshal_type_%s' % typename

def print_metatype_undef(typeinfo, name, lhs, indent=0):
    if indent == 0:
        sep = '->'
    else:
        sep = '.'

    indent += 4

    if type(typeinfo) == str:
        inprint('%s = %s(%s);' % (lhs, qobj_to_c(typeinfo), name), indent)
    elif type(typeinfo) == dict:
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
        inprint('        %s qmp__node = %s(%s->value);' % (qmp_type_to_c(typeinfo[0]), qmp_type_from_qobj(typeinfo[0]), objname), indent)
        inprint('        qmp__node->next = %s;' % lhs, indent)
        inprint('        %s = qmp__node->next;' % lhs, indent)
        inprint('    }', indent)
        inprint('}', indent)

def print_metatype_definition(name, typeinfo):
    print '''
QObject *qmp_marshal_type_%s(%s src)
{''' % (name, qmp_type_to_c(name))
    print '    QObject *qmp__retval;'
    print_metatype_def(typeinfo, 'src', 'qmp__retval')
    print '''    return qmp__retval;
}

%s qmp_unmarshal_type_%s(QObject *src)
{''' % (qmp_type_to_c(name), name)
    print '    %s qmp__retval = qmp_alloc_%s();' % (qmp_type_to_c(name), name)
    print_metatype_undef(typeinfo, 'src', 'qmp__retval')
    print '''    return qmp__retval;
}

void qmp_free_%s(%s *obj)
{
    qemu_free(obj);
}

%s *qmp_alloc_%s(void)
{
    return qemu_mallocz(sizeof(%s));
}''' % (name, name, name, name, name)


kind = 'body'
if len(sys.argv) == 2:
    if sys.argv[1] == '--body':
        kind = 'body'
    elif sys.argv[1] == '--header':
        kind = 'header'
    elif sys.argv[1] == '--lib-header':
        kind = 'lib-header'
    elif sys.argv[1] == '--lib-body':
        kind = 'lib-body'
    elif sys.argv[1] == '--types-body':
        kind = 'types-body'
    elif sys.argv[1] == '--types-header':
        kind = 'types-header'

if kind == 'types-header':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#ifndef QMP_TYPES_H
#define QMP_TYPES_H

#include "qemu-common.h"
#include "qemu-objects.h"
#include "error.h"

#define qmp_marshal_type_int(value) QOBJECT(qint_from_int(value))
#define qmp_marshal_type_str(value) QOBJECT(qstring_from_str(value))
#define qmp_marshal_type_bool(value) QOBJECT(qbool_from_int(value))
#define qmp_marshal_type_number(value) QOBJECT(qfloat_from_double(value))

#define qmp_unmarshal_type_int(value) qint_get_int(qobject_to_qint(value))
#define qmp_unmarshal_type_str(value) qemu_strdup(qstring_get_str(qobject_to_qstring(value))) // FIXME mem life cycle
#define qmp_unmarshal_type_bool(value) qbool_get_int(qobject_to_qbool(value))
#define qmp_unmarshal_type_number(value) qfloat_get_double(qobject_to_qfloat(value))
'''
elif kind == 'types-body':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */

#include "qmp-types.h"
'''
elif kind == 'lib-header':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#ifndef LIBQMP_H
#define LIBQMP_H

#include "libqmp-core.h"
'''
elif kind == 'lib-body':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */

#include "libqmp.h"
#include "libqmp-internal.h"
'''
elif kind == 'header':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#ifndef QMP_H
#define QMP_H

#include "qemu-common.h"
#include "qemu-objects.h"
#include "qmp-types.h"
#include "error.h"
'''
elif kind == 'body':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */

#include "qmp.h"
#include "qmp-core.h"
'''

exprs = []
expr = ''

for line in sys.stdin:
    if line.startswith('#') or line == '\n':
        continue

    if line.startswith(' '):
        expr += line
    elif expr:
        s = eval(expr)
        exprs.append(s)
        expr = line
    else:
        expr += line

if expr:
    s = eval(expr)
    exprs.append(s)

for s in exprs:
    if type(s) == dict:
        key = s.keys()[0]
        meta_types[key] = s[key]
        if kind == 'types-body':
            print_metatype_definition(key, meta_types[key])
        elif kind == 'types-header':
            print_metatype_declaration(key, meta_types[key])
    else:
        name, required, optional, retval = s
        if kind == 'body':
            print_definition(name, required, optional, retval)
        elif kind == 'header':
            print_declaration(name, required, optional, retval)
        elif kind == 'lib-body':
            print_lib_definition(name, required, optional, retval)
        elif kind == 'lib-header':
            print_lib_declaration(name, required, optional, retval)

if kind.endswith('header'):
    print '#endif'
elif kind == 'body':
    print
    print 'static void qmp_init_marshal(void)'
    print '{'
    for s in exprs:
        if type(s) == list:
            print '    qmp_register_command("%s", &qmp_marshal_%s);' % (s[0], c_var(s[0]))
    print '};'
    print
    print 'qapi_init(qmp_init_marshal);'
        
