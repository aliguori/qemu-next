##
# Virtio Support
#
# Copyright IBM, Corp. 2007
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

def qmp_type_to_qobj_ctor(typename):
    return 'qmp_marshal_type_%s' % typename

def qmp_type_from_qobj(typename):
    return qobj_to_c(typename)

def print_lib_decl(name, options, retval, suffix='', guest=False):
    if guest:
        args = []
    else:
        args = ['QmpSession *qmp__session']
    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            args.append('KeyValues * %s' % c_var(argname))
        else:
            if optional:
                args.append('bool has_%s' % c_var(argname))
            args.append('%s %s' % (qmp_type_to_c(argtype), c_var(argname)))

    args.append('Error **qmp__err')

    if guest:
        prefix = 'qmp'
    else:
        prefix = 'libqmp'

    print '%s %s_%s(%s)%s' % (qmp_type_to_c(retval, True), prefix, c_var(name), ', '.join(args), suffix)

def print_lib_event_decl(name, end=''):
    print 'static void libqmp_notify_%s(QDict *qmp__args, void *qmp__fn, void *qmp__opaque, Error **qmp__errp)%s' % (de_camel_case(qmp_event_to_c(name)), end)

def print_lib_declaration(name, options, retval, guest=False):
    print_lib_decl(name, options, retval, ';')

def parse_args(typeinfo):
    for member in typeinfo:
        argname = member
        argtype = typeinfo[member]
        optional = False
        if member.startswith('*'):
            argname = member[1:]
            optional = True
        yield (argname, argtype, optional)

def print_lib_event_definition(name, typeinfo):
    print
    print_lib_event_decl(name)
    print '{'
    print '    %s *qmp__native_fn = qmp__fn;' % (qmp_event_func_to_c(name))
    print '    Error *qmp__err = NULL;'
    for argname, argtype, optional in parse_args(typeinfo):
        if optional:
            print '    bool has_%s;' % (c_var(argname))
        print '    %s %s = 0;' % (qmp_type_to_c(argtype, True), c_var(argname))

    print
    print '    (void)qmp__err;'

    for argname, argtype, optional in parse_args(typeinfo):
        if optional:
            print '    BUILD_BUG()'
        print '''
    if (!qdict_haskey(qmp__args, "%s")) {
        error_set(qmp__errp, QERR_MISSING_PARAMETER, "%s");
        goto qmp__out;
    }

    %s = %s(qdict_get(qmp__args, "%s"), &qmp__err);
    if (qmp__err) {
        if (error_is_type(qmp__err, QERR_INVALID_PARAMETER_TYPE)) {
            error_set(qmp__errp, QERR_INVALID_PARAMETER_TYPE, "%s",
                      error_get_field(qmp__err, "expected"));
            error_free(qmp__err);
            qmp__err = NULL;
        } else {
            error_propagate(qmp__errp, qmp__err);
        }
        goto qmp__out;
    }''' % (argname, argname, c_var(argname), qmp_type_from_qobj(argtype), argname, argname)

    arglist = ['qmp__opaque']
    for argname, argtype, optional in parse_args(typeinfo):
        arglist.append(c_var(argname))
    print
    print '    qmp__native_fn(%s);' % (', '.join(arglist))

    has_label = False
    for argname, argtype, optional in parse_args(typeinfo):
        if not has_label:
            print
            print 'qmp__out:'
            has_label = True

        if qmp_type_should_free(argtype):
            print '    %s(%s);' % (qmp_free_func(argtype), c_var(argname))
    print '    return;'
    print '}'

def print_lib_definition(name, options, retval, guest=False):
    print
    print_lib_decl(name, options, retval, guest=guest)
    print '''{
    QDict *qmp__args = qdict_new();
    Error *qmp__local_err = NULL;'''

    print '    QObject *qmp__retval = NULL;'
    if retval != 'none':
        print '    %s qmp__native_retval = 0;' % (qmp_type_to_c(retval, True))
    if qmp_type_is_event(retval):
        print '    int qmp__global_handle = 0;'
    print

    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            print '''    {
        KeyValues *qmp__i;
        for (qmp__i = %s; qmp__i; qmp__i = qmp__i->next) {
            qdict_put(qmp__args, qmp__i->key, qstring_from_str(qmp__i->value));
        }
    }''' % c_var(argname)
        else:
            indent = 4
            if optional:
                inprint('if (has_%s) {' % c_var(argname), indent)
                indent += 4
            inprint('qdict_put_obj(qmp__args, "%s", %s(%s));' % (argname, qmp_type_to_qobj_ctor(argtype), c_var(argname)), indent)
            if optional:
                indent -= 4
                inprint('}', indent)

    if guest:
        print '    qmp__retval = qmp_guest_dispatch("%s", qmp__args, &qmp__local_err);' % (name)
    else:
        print '    qmp__retval = qmp__session->dispatch(qmp__session, "%s", qmp__args, &qmp__local_err);' % (name)
    print
    print '    QDECREF(qmp__args);'

    if type(retval) == list:
        print '''
    if (!qmp__local_err) {
        QList *qmp__list_retval = qobject_to_qlist(qmp__retval);
        QListEntry *qmp__i;
        QLIST_FOREACH_ENTRY(qmp__list_retval, qmp__i) {
            %s qmp__native_i = %s(qmp__i->value, &qmp__local_err);
            if (qmp__local_err) {
                %s(qmp__native_retval);
                break;
            }
            qmp__native_i->next = qmp__native_retval;
            qmp__native_retval = qmp__native_i;
        }
        qobject_decref(qmp__retval);
    }
    error_propagate(qmp__err, qmp__local_err);
    return qmp__native_retval;''' % (qmp_type_to_c(retval[0], True), qmp_type_from_qobj(retval[0]), qmp_free_func(retval[0]))
    elif is_dict(retval):
        print '    // FIXME (using an anonymous dict as return value)'
        print '    BUILD_BUG();'
    elif qmp_type_is_event(retval):
        if guest:
            print '    BUILD_BUG();'
        print '''    if (!qmp__local_err) {
        qmp__global_handle = %s(qmp__retval, &qmp__local_err);
        qobject_decref(qmp__retval);
        qmp__retval = NULL;
    }
    if (!qmp__local_err) {
        qmp__native_retval = libqmp_signal_init(qmp__session, %s, qmp__global_handle);
    }
    error_propagate(qmp__err, qmp__local_err);
    return qmp__native_retval;''' % (qmp_type_from_qobj('int'), qmp_event_to_c(retval))
    elif retval != 'none':
        print '''
    if (!qmp__local_err) {
        qmp__native_retval = %s(qmp__retval, &qmp__local_err);
        qobject_decref(qmp__retval);
    }
    error_propagate(qmp__err, qmp__local_err);
    return qmp__native_retval;''' % qmp_type_from_qobj(retval)
    else:
        print '    qobject_decref(qmp__retval);'
        print '    error_propagate(qmp__err, qmp__local_err);'

    print '}'

def print_declaration(name, options, retval):
    args = []
    if name in ['qmp_capabilities', 'put-event']:
        return
    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            args.append('KeyValues * %s' % c_var(argname))
        else:
            if optional:
                args.append('bool has_%s' % c_var(argname))
            args.append('%s %s' % (qmp_type_to_c(argtype), c_var(argname)))
    args.append('Error **err')

    print '%s qmp_%s(%s);' % (qmp_type_to_c(retval, True), c_var(name), ', '.join(args))

def print_definition(name, options, retval):
    if qmp_type_is_event(retval):
        arglist = ['void *opaque']
        for argname, argtype, optional in parse_args(event_types[retval]):
            if optional:
                arglist.append('bool has_%s' % c_var(argname))
            arglist.append('%s %s' % (qmp_type_to_c(argtype), c_var(argname)))
        print '''
static void qmp_marshal_%s(%s)
{
    QDict *qmp__args = qdict_new();
    QmpConnection *qmp__conn = opaque;
''' % (qmp_event_to_c(retval), ', '.join(arglist))

        indent = 4
        for argname, argtype, optional in parse_args(event_types[retval]):
            if optional:
                inprint('if (has_%s) {' % c_var(argname), indent)
                indent += 4
            inprint('qdict_put_obj(qmp__args, "%s", %s(%s));' % (argname, qmp_type_to_qobj_ctor(argtype), c_var(argname)), indent)
            if optional:
                indent -= 4
                inprint('}', indent)

        print '''
    qmp_state_event(qmp__conn, QOBJECT(qmp__args));
    QDECREF(qmp__args);
}'''
        print '''
static void qmp_marshal_%s(QmpState *qmp__sess, const QDict *qdict, QObject **ret_data, Error **err)
{
    int qmp__handle;
    QmpConnection *qmp__connection = qemu_mallocz(sizeof(QmpConnection));''' % c_var(name)
    elif name in ['qmp_capabilities', 'put-event']:
        print '''
static void qmp_marshal_%s(QmpState *qmp__sess, const QDict *qdict, QObject **ret_data, Error **err)
{''' % c_var(name)
    else:
        print '''
static void qmp_marshal_%s(const QDict *qdict, QObject **ret_data, Error **err)
{''' % c_var(name)
    print '    Error *qmp__err = NULL;'

    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            print '    KeyValues * %s = 0;' % c_var(argname)
        else:
            if optional:
                print '    bool has_%s = false;' % (c_var(argname))
            print '    %s %s = 0;' % (qmp_type_to_c(argtype, True), c_var(argname))

    if retval != 'none':
        print '    %s qmp_retval = 0;' % (qmp_type_to_c(retval, True))

    print '''
    (void)qmp__err;'''

    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            print '''
    {
        const QDictEntry *qmp__qdict_i;

        %s = NULL;
        for (qmp__qdict_i = qdict_first(qdict); qmp__qdict_i; qmp__qdict_i = qdict_next(qdict, qmp__qdict_i)) {
            KeyValues *qmp__i;''' % c_var(argname)
            for argname1, argtype1, optional1 in parse_args(options):
                if argname1 == argname:
                    continue
                print '''            if (strcmp(qmp__qdict_i->key, "%s") == 0) {
                continue;
            }''' % argname1
            print '''
            qmp__i = qmp_alloc_key_values();
            qmp__i->key = qemu_strdup(qmp__qdict_i->key);
            qmp__i->value = qobject_as_string(qmp__qdict_i->value);
            qmp__i->next = %s;
            %s = qmp__i;
        }
    }''' % (c_var(argname), c_var(argname))
        elif optional:
            print '''
    if (qdict_haskey(qdict, "%s")) {
        %s = %s(qdict_get(qdict, "%s"), &qmp__err);
        if (qmp__err) {
            if (error_is_type(qmp__err, QERR_INVALID_PARAMETER_TYPE)) {
                error_set(err, QERR_INVALID_PARAMETER_TYPE, "%s",
                          error_get_field(qmp__err, "expected"));
                error_free(qmp__err);
                qmp__err = NULL;
            } else {
                error_propagate(err, qmp__err);
            }
            goto qmp__out;
        }
        has_%s = true;
    }
''' % (argname, c_var(argname), qmp_type_from_qobj(argtype), argname, argname, c_var(argname))
        else:
            print '''
    if (!qdict_haskey(qdict, "%s")) {
        error_set(err, QERR_MISSING_PARAMETER, "%s");
        goto qmp__out;
    }

    %s = %s(qdict_get(qdict, "%s"), &qmp__err);
    if (qmp__err) {
        if (error_is_type(qmp__err, QERR_INVALID_PARAMETER_TYPE)) {
            error_set(err, QERR_INVALID_PARAMETER_TYPE, "%s",
                      error_get_field(qmp__err, "expected"));
            error_free(qmp__err);
            qmp__err = NULL;
        } else {
            error_propagate(err, qmp__err);
        }
        goto qmp__out;
    }
''' % (argname, argname, c_var(argname), qmp_type_from_qobj(argtype), argname, argname)

    args = []
    for argname, argtype, optional in parse_args(options):
        if optional and argtype != '**':
            args.append('has_%s' % c_var(argname))
        args.append(c_var(argname))
    args.append('err')

    if name in ['qmp_capabilities', 'put-event']:
        args = ['qmp__sess'] + args

    arglist = ', '.join(args)
    fn = 'qmp_%s' % c_var(name)

    if name == 'put-event':
        print '    qmp_state_del_connection(%s);' % arglist
    elif retval == 'none':
        print '    %s(%s);' % (fn, arglist)
    else:
        print '    qmp_retval = %s(%s);' % (fn, arglist)

    print '''
    if (error_is_set(err)) {
        goto qmp__out;
    }'''

    if retval == 'none':
        pass
    elif qmp_type_is_event(retval):
        print '    qmp__handle = signal_connect(qmp_retval, qmp_marshal_%s, qmp__connection);' % (qmp_event_to_c(retval))
        print '    qmp_state_add_connection(qmp__sess, "%s", qmp_retval->signal, qmp__handle, qmp__connection);' % retval
        print '    *ret_data = QOBJECT(qint_from_int(qmp__connection->global_handle));'
        print '    qmp__connection = NULL;'
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
    }''' % (qmp_type_to_c(retval[0], True), qmp_type_to_qobj_ctor(retval[0]))

    print
    print 'qmp__out:'
    if qmp_type_is_event(retval):
        print '    qemu_free(qmp__connection);'
    print
    args = []
    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            print '    %s(%s);' % (qmp_free_func('KeyValues'), c_var(argname))
        elif qmp_type_should_free(argtype):
            indent = 4
            if optional:
                inprint('if (has_%s) {' % c_var(argname), indent)
                indent += 4
            inprint('%s(%s);' % (qmp_free_func(argtype), c_var(argname)), indent)
            if optional:
                indent -= 4
                inprint('}', indent)
    if retval != 'none':
        if qmp_type_should_free(retval):
            print '    %s(%s);' % (qmp_free_func(retval), 'qmp_retval')
    print '    return;'

    print '}'

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

def print_enum_marshal_declaration(name, entries):
    print
    print 'QObject *qmp_marshal_type_%s(%s value);' % (name, name)
    print '%s qmp_unmarshal_type_%s(QObject *obj, Error **errp);' % (name, name)

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

def print_qdev_declaration(name, entries):
    print '''
extern PropertyInfo qdev_prop_%s;

#define DEFINE_PROP_%s(_n, _s, _f, _d) \\
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_%s, %s)
''' % (name, de_camel_case(name).upper(), name, name)

def print_qdev_definition(name, entries):
    print '''
static int parse_%s(DeviceState *dev, Property *prop, const char *str)
{
    %s *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;
''' % (name, name)
    first = True
    for entry in entries:
        if first:
            first = False
            print '    if (strcmp(str, "%s") == 0) {' % entry
        else:
            print '    } else if (strcmp(str, "%s") == 0) {' % entry
        print '        *ptr = %s_%s;' % (enum_abbreviation(name), entry.upper())
    print '''    } else {
        *ptr = strtoul(str, &end, 0);
        if ((*end != '\\0') || (end == str)) {
            return -EINVAL;
        }
        if (*ptr > %s_%s) {
            return -EINVAL;
        }
    }
    return 0;
}''' % (enum_abbreviation(name), entries[-1].upper())

    print '''
static int print_%s(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    %s *ptr = qdev_get_prop_ptr(dev, prop);
''' % (name, name)
    first = True
    for entry in entries:
        enum = '%s_%s' % (enum_abbreviation(name), entry.upper())
        if first:
            first = False
            print '    if (*ptr == %s) {' % enum
        else:
            print '    } else if (*ptr == %s) {' % enum
        print '        return snprintf(dest, len, "%%s", "%s");' % entry
    print '''    }
    return -EINVAL;
}

PropertyInfo qdev_prop_%s = {
    .name = "%s",
    .type = PROP_TYPE_STRING,
    .size = sizeof(%s),
    .parse = parse_%s,
    .print = print_%s,
};''' % (name, name, name, name, name)

def print_type_declaration(name, typeinfo):
    if type(typeinfo) == str:
        print
        print "typedef %s %s;" % (qmp_type_to_c(typeinfo), name)
    elif is_dict(typeinfo) and not name.isupper():
        print
        print "typedef struct %s %s;" % (name, name)
        print "struct %s {" % name
        for argname, argtype, optional in parse_args(typeinfo):
            if optional:
                print "    bool has_%s;" % c_var(argname)
            print "    %s %s;" % (qmp_type_to_c(argtype, True, indent=4), c_var(argname))
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

def print_type_marshal_declaration(name, typeinfo):
    if qmp_type_is_event(name):
        return

    if is_dict(typeinfo):
        print
        print 'QObject *qmp_marshal_type_%s(%s src);' % (name, qmp_type_to_c(name))
        print '%s qmp_unmarshal_type_%s(QObject *src, Error **errp);' % (qmp_type_to_c(name), name)

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
    elif is_dict(typeinfo):
        inprint('    {', indent)
        inprint('        QDict *qmp__dict = qdict_new();', indent)
        inprint('        QObject *%s;' % new_lhs, indent)
        print
        for argname, argtype, optional in parse_args(typeinfo):
            if optional:
                inprint('        if (%s%shas_%s) {' % (name, sep, c_var(argname)), indent)
                indent += 4
            print_metatype_def(argtype, '%s%s%s' % (name, sep, c_var(argname)), new_lhs, indent + 4)
            inprint('            qdict_put_obj(qmp__dict, "%s", %s);' % (argname, new_lhs), indent)
            if optional:
                indent -= 4
                inprint('        }', indent)
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
        for argname, argtype, optional in parse_args(typeinfo):
            if optional:
                inprint('if (qdict_haskey(qmp__dict, "%s")) {' % (argname), indent + 4)
                indent += 4
            inprint('    %s = qdict_get(qmp__dict, "%s");' % (objname, argname), indent)
            print_metatype_undef(argtype, objname, '%s%s%s' % (lhs, sep, c_var(argname)), indent)
            if optional:
                indent -= 4
                inprint('    %s%shas_%s = true;' % (lhs, sep, c_var(argname)), indent + 4)
                inprint('} else {', indent + 4)
                inprint('    %s%shas_%s = false;' % (lhs, sep, c_var(argname)), indent + 4)
                inprint('}', indent + 4)
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

def print_metatype_free(typeinfo, prefix, indent=4):
    for argname, argtype, optional in parse_args(typeinfo):
        if type(argtype) == list:
            argtype = argtype[0]

        if is_dict(argtype):
            if optional:
                inprint('if (%shas_%s) {' % (prefix, argname), indent)
                print_metatype_free(argtype, '%s%s.' % (prefix, argname), indent + 4)
                inprint('}', indent)
            else:
                print_metatype_free(argtype, '%s%s.' % (prefix, argname), indent)
        elif qmp_type_should_free(argtype):
            if optional:
                inprint('if (%shas_%s) {' % (prefix, argname), indent)
                inprint('    %s(%s%s);' % (qmp_free_func(argtype), prefix, argname), indent)
                inprint('}', indent)
            else:
                inprint('%s(%s%s);' % (qmp_free_func(argtype), prefix, argname), indent)

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

    print_metatype_free(typeinfo, 'obj->')

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
#    return eval(string)

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
    elif sys.argv[1] == '--marshal-body':
        kind = 'marshal-body'
    elif sys.argv[1] == '--marshal-header':
        kind = 'marshal-header'
    elif sys.argv[1] == '--qdev-header':
        kind = 'qdev-header'
    elif sys.argv[1] == '--qdev-body':
        kind = 'qdev-body'

if kind == 'marshal-header':
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
elif kind == 'types-header':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#ifndef QMP_TYPES_H
#define QMP_TYPES_H

#include "qmp-types-core.h"

'''
elif kind == 'types-body':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */

#include "qmp-types.h"
#include "qmp-marshal-types.h"
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
elif kind == 'qdev-header':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#ifndef QDEV_MARSHAL_H
#define QDEV_MARSHAL_H

#include "qemu-common.h"
#include "qmp-types.h"
#include "hw/qdev.h"
'''
elif kind == 'qdev-body':
    print '''/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
#include "qdev-marshal.h"
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
            elif kind == 'lib-body':
                if qmp_type_is_event(key):
                    print_lib_event_definition(key, event_types[key])
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
            elif kind == 'qdev-header':
                print_qdev_declaration(key, s[key])
            elif kind == 'qdev-body':
                print_qdev_definition(key, s[key])
    else:
        name, options, retval = s
        if kind == 'body':
            if name.startswith('guest-'):
                print_lib_definition(name, options, retval, guest=True)
            print_definition(name, options, retval)
        elif kind == 'header':
            print_declaration(name, options, retval)
        elif kind == 'lib-body':
            print_lib_definition(name, options, retval)
        elif kind == 'lib-header':
            print_lib_declaration(name, options, retval)

if kind.endswith('header'):
    print '#endif'
elif kind == 'body':
    print
    print 'static void qmp_init_marshal(void)'
    print '{'
    for s in exprs:
        if type(s) != list:
            continue
        if qmp_type_is_event(s[2]) or s[0] in ['qmp_capabilities', 'put-event']:
            print '    qmp_register_stateful_command("%s", &qmp_marshal_%s);' % (s[0], c_var(s[0]))
        else:
            print '    qmp_register_command("%s", &qmp_marshal_%s);' % (s[0], c_var(s[0]))
    print '};'
    print
    print 'qapi_init(qmp_init_marshal);'
elif kind == 'lib-body':
    print
    print 'void libqmp_init_events(QmpSession *sess)'
    print '{'
    for event in event_types:
        print '    libqmp_register_event(sess, "%s", &libqmp_notify_%s);' % (event, de_camel_case(qmp_event_to_c(event)))
    print '}'
        
