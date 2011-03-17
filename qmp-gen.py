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

def qmp_type_is_simple(typename):
    return typename in ['str', 'int', 'number']

def qapi_free_func(typename):
    if type(typename) == list:
        return qapi_free_func(typename[0])
    elif typename == 'str':
        return 'qemu_free'
    else:
        return 'qapi_free_%s' % (de_camel_case(typename))

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

def qcfg_unmarshal_type(name):
    return 'qcfg_unmarshal_type_%s' % name

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

def gen_lib_decl(name, options, retval, suffix='', proxy=False, async=False):
    if proxy:
        async=True

    if proxy:
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

    if proxy:
        prefix = 'qmp'
    else:
        prefix = 'libqmp'

    if proxy:
        qmp_retval = 'void'
        args.append('%sCompletionFunc *qmp__cc' % camel_case(name))
        args.append('void *qmp__opaque')
    else:
        qmp_retval = qmp_type_to_c(retval, True)

    return mcgen('''
%(ret)s %(prefix)s_%(name)s(%(args)s)%(suffix)s
''', ret=qmp_retval, prefix=prefix, name=c_var(name), args=', '.join(args), suffix=suffix)

def gen_lib_declaration(name, options, retval):
    return gen_lib_decl(name, options, retval, ';')

def gen_lib_event_definition(name, typeinfo):
    args = ''
    for argname, argtype, optional in parse_args(typeinfo):
        if optional:
            args += cgen('    bool has_%(name)s;', name=c_var(argname))
        args += cgen('    %(type)s %(name)s = 0;', type=qmp_type_to_c(argtype, True), name=c_var(argname))

    ret = mcgen('''

static void libqmp_notify_%(fn_name)s(QmpSession *qmp__sess, QDict *qmp__args, void *qmp__fn, void *qmp__opaque, Error **qmp__errp)
{
    %(fn_ret)s *qmp__native_fn = qmp__fn;
    Error *qmp__err = NULL;
    QmpMarshalState *qmp__mstate;
%(args)s

    (void)qmp__err;
    qmp__mstate = &qmp__sess->mstate;
''',
                fn_name=de_camel_case(qmp_event_to_c(name)),
                fn_ret=qmp_event_func_to_c(name), args=args)

    for argname, argtype, optional in parse_args(typeinfo):
        if optional:
            ret += cgen('    BUILD_BUG()')
        ret += mcgen('''

    if (!qdict_haskey(qmp__args, "%(name)s")) {
        error_set(qmp__errp, QERR_MISSING_PARAMETER, "%(name)s");
        goto qmp__out;
    }

    %(c_name)s = %(unmarshal)s(qmp__mstate, qdict_get(qmp__args, "%(name)s"), &qmp__err);
    if (qmp__err) {
        if (error_is_type(qmp__err, QERR_INVALID_PARAMETER_TYPE)) {
            error_set(qmp__errp, QERR_INVALID_PARAMETER_TYPE, "%(name)s",
                      error_get_field(qmp__err, "expected"));
            error_free(qmp__err);
            qmp__err = NULL;
        } else {
            error_propagate(qmp__errp, qmp__err);
        }
        goto qmp__out;
    }
''', name=argname, c_name=c_var(argname), unmarshal=qmp_type_from_qobj(argtype))

    arglist = ['qmp__opaque']
    for argname, argtype, optional in parse_args(typeinfo):
        arglist.append(c_var(argname))
    ret += mcgen('''

    qmp__native_fn(%(args)s);
''', args=', '.join(arglist))

    has_label = False
    for argname, argtype, optional in parse_args(typeinfo):
        if not has_label:
            ret += mcgen('''
qmp__out:
''')
            has_label = True

        if qmp_type_should_free(argtype):
            ret += cgen('    %(free)s(%(name)s);', free=qapi_free_func(argtype), name=c_var(argname))
    ret += mcgen('''
    return;
}
''')
    return ret

def gen_async_lib_definition(name, options, retval):
    ret = mcgen('''

typedef struct %(cc_name)sCompletionCB
{
    %(cc_name)sCompletionFunc *cb;
    void *opaque;
} %(cc_name)sCompletionCB;

static void qmp_%(c_name)s_cb(void *qmp__opaque, QObject *qmp__retval, Error *qmp__err)
{
    %(cc_name)sCompletionCB *qmp__cb = qmp__opaque;
    QmpMarshalState qmp__mstate_obj = {}, *qmp__mstate = &qmp__mstate_obj;
''',
                cc_name=camel_case(name), c_name=c_var(name))

    if retval != 'none':
        ret += cgen('    %(ret_type)s qmp__native_retval = 0;',
                           ret_type=qmp_type_to_c(retval, True))

    ret += cgen('    (void)qmp__mstate;')

    if type(retval) == list:
        ret += mcgen('''

    if (!qmp__err) {
        // FIXME need to validate the type here
        QList *qmp__list_retval = qobject_to_qlist(qmp__retval);
        QListEntry *qmp__i;
        QLIST_FOREACH_ENTRY(qmp__list_retval, qmp__i) {
            %(ret_type)s qmp__native_i = %(unmarshal)s(qmp__mstate, qmp__i->value, &qmp__err);
            if (qmp__err) {
                %(free)s(qmp__native_retval);
                break;
            }
            qmp__native_i->next = qmp__native_retval;
            qmp__native_retval = qmp__native_i;
        }
    }
''',
                            ret_type=qmp_type_to_c(retval[0], True),
                            unmarshal=qmp_type_from_qobj(retval[0]),
                            free=qapi_free_func(retval[0]))
    elif is_dict(retval):
        ret += mcgen('''
    // FIXME (using an anonymous dict as return value)')
    BUILD_BUG();
''')
    elif retval != 'none':
        ret += mcgen('''

    if (!qmp__err) {
        qmp__native_retval = %(unmarshal)s(qmp__mstate, qmp__retval, &qmp__err);
    }
''',
                            unmarshal=qmp_type_from_qobj(retval))
    ret += cgen('')
    if retval == 'none':
        ret += cgen('    qmp__cb->cb(qmp__cb->opaque, qmp__err);')
    else:
        ret += cgen('    qmp__cb->cb(qmp__cb->opaque, qmp__native_retval, qmp__err);')
    ret += cgen('}')

    return ret

def gen_lib_definition(name, options, retval, proxy=False, async=False):
    if proxy:
        async = True

    ret = ''
    if proxy:
        ret += gen_async_lib_definition(name, options, retval)

    fn_decl = gen_lib_decl(name, options, retval, proxy=proxy, async=async)
    ret += mcgen('''

%(fn_decl)s
{
    QDict *qmp__args = qdict_new();
''',
                        fn_decl=fn_decl)
    if async:
        ret += mcgen('''
    QmpMarshalState qmp__mstate_obj = {}, *qmp__mstate = &qmp__mstate_obj;
    %(cc_name)sCompletionCB *qmp__cb = qemu_mallocz(sizeof(*qmp__cb));

    qmp__cb->cb = qmp__cc;
    qmp__cb->opaque = qmp__opaque;
''',
                            cc_name=camel_case(name))
    else:
        ret += mcgen('''
    Error *qmp__local_err = NULL;
    QObject *qmp__retval = NULL;
    QmpMarshalState *qmp__mstate = &qmp__session->mstate;
''')
        if retval != 'none':
            ret += cgen('    %(ret_type)s qmp__native_retval = 0;',
                               ret_type=qmp_type_to_c(retval, True))
        if qmp_type_is_event(retval):
            ret += cgen('    int qmp__global_handle = 0;')
    ret += mcgen('''

    (void)qmp__mstate;
''')

    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            ret += mcgen('''
    {
        KeyValues *qmp__i;
        for (qmp__i = %(name)s; qmp__i; qmp__i = qmp__i->next) {
            qdict_put(qmp__args, qmp__i->key, qstring_from_str(qmp__i->value));
        }
    }
''',
                         name=c_var(argname))
        else:
            if optional:
                ret += mcgen('''
    if (has_%(c_name)s) {
''',
                                    c_name=c_var(argname))
                push_indent()
            ret += mcgen('''
    qdict_put_obj(qmp__args, "%(name)s", %(marshal)s(qmp__mstate, %(c_name)s));
''',
                                name=argname, c_name=c_var(argname),
                                marshal=qmp_type_to_qobj(argtype))
            if optional:
                pop_indent()
                ret += mcgen('''
    }
''')

    if proxy:
        ret += mcgen('''
    qmp_guest_dispatch("%(name)s", qmp__args, qmp__err, qmp_%(c_name)s_cb, qmp__cb);
''',
                            name=name, c_name=c_var(name))
    else:
        ret += mcgen('''
    qmp__retval = qmp__session->dispatch(qmp__session, "%(name)s", qmp__args, &qmp__local_err);
''',
                            name=name)
    ret += mcgen('''

    QDECREF(qmp__args);
''')

    if async:
        pass
    elif type(retval) == list:
        ret += mcgen('''

    if (!qmp__local_err) {
        // FIXME need to validate the type here
        QList *qmp__list_retval = qobject_to_qlist(qmp__retval);
        QListEntry *qmp__i;
        QLIST_FOREACH_ENTRY(qmp__list_retval, qmp__i) {
            %(type)s qmp__native_i = %(unmarshal)s(qmp__mstate, qmp__i->value, &qmp__local_err);
            if (qmp__local_err) {
                %(free)s(qmp__native_retval);
                break;
            }
            qmp__native_i->next = qmp__native_retval;
            qmp__native_retval = qmp__native_i;
        }
        qobject_decref(qmp__retval);
    }
    error_propagate(qmp__err, qmp__local_err);
    return qmp__native_retval;
''',
                     type=qmp_type_to_c(retval[0], True),
                     unmarshal=qmp_type_from_qobj(retval[0]),
                     free=qapi_free_func(retval[0]))
    elif is_dict(retval):
        ret += mcgen('''
    // FIXME (using an anonymous dict as return value)
    BUILD_BUG();
''')
    elif qmp_type_is_event(retval):
        if proxy:
            ret += cgen('    BUILD_BUG();')
        ret += mcgen('''
    if (!qmp__local_err) {
        qmp__global_handle = %(unmarshal)s(qmp__mstate, qmp__retval, &qmp__local_err);
        qobject_decref(qmp__retval);
        qmp__retval = NULL;
    }
    if (!qmp__local_err) {
        qmp__native_retval = libqmp_signal_init(qmp__session, %(type)s, qmp__global_handle);
    }
    error_propagate(qmp__err, qmp__local_err);
    return qmp__native_retval;
''',
                            unmarshal=qmp_type_from_qobj('int'),
                            type=qmp_event_to_c(retval))
    elif retval != 'none':
        ret += mcgen('''

    if (!qmp__local_err) {
        qmp__native_retval = %(unmarshal)s(qmp__mstate, qmp__retval, &qmp__local_err);
        qobject_decref(qmp__retval);
    }
    error_propagate(qmp__err, qmp__local_err);
    return qmp__native_retval;
''',
                            unmarshal=qmp_type_from_qobj(retval))
    else:
        ret += mcgen('''
    qobject_decref(qmp__retval);
    error_propagate(qmp__err, qmp__local_err);
''')

    ret += cgen('}')

    return ret

def gen_async_fn_decl(name, options, retval):
    if retval == 'none':
        return mcgen('''
typedef void (%(cc_name)sCompletionFunc)(void *qmp__opaque, Error *qmp__err);
''',
                    cc_name=camel_case(name))
    else:
        return mcgen('''
typedef void (%(cc_name)sCompletionFunc)(void *qmp__opaque, %(ret_type)s qmp__retval, Error *qmp__err);
''', cc_name=camel_case(name), ret_type=qmp_type_to_c(retval))

def gen_declaration(name, options, retval, async=False, prefix='qmp'):
    args = []
    ret = ''

    if async:
        ret += gen_async_fn_decl(name, options, retval)

    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            args.append('KeyValues * %s' % c_var(argname))
        else:
            if optional:
                args.append('bool has_%s' % c_var(argname))
            args.append('%s %s' % (qmp_type_to_c(argtype), c_var(argname)))
    args.append('Error **err')

    if async:
        args.append('%sCompletionFunc *qmp__cc' % camel_case(name))
        args.append('void *qmp__opaque')
        qmp_retval = 'void'
    else:
        qmp_retval = qmp_type_to_c(retval, True)

    if qmp_is_stateful_cmd(name):
        args = ['QmpState *qmp__state'] + args

    ret += cgen('%(ret_type)s %(prefix)s_%(c_name)s(%(args)s);',
                ret_type=qmp_retval, prefix=prefix,
                c_name=c_var(name), args=', '.join(args))

    return ret

def gen_async_definition(name, options, retval):
    if retval == 'none':
        ret = mcgen('''

static void qmp_async_completion_%(c_name)s(void *qmp__opaque, Error *qmp__err)
{
''',
                    c_name=c_var(name))
    else:
        ret = mcgen('''

static void qmp_async_completion_%(c_name)s(void *qmp__opaque, %(ret_type)s qmp__retval, Error *qmp__err)
{
''',
                    c_name=c_var(name), ret_type=qmp_type_to_c(retval))
    if retval != 'none':
        ret += cgen('    QObject *qmp__ret_data;')
    ret += mcgen('''
    QmpCommandState *qmp__cmd = qmp__opaque;
    QmpMarshalState *qmp__mstate;

    qmp__mstate = qmp_state_get_mstate(qmp__cmd->state);

    if (qmp__err) {
        qmp_async_complete_command(qmp__cmd, NULL, qmp__err);
        return;
    }
''')
    if retval == 'none':
        pass
    elif type(retval) == str:
        ret += mcgen('''

    qmp__ret_data = %(marshal)s(qmp__mstate, qmp__retval);
''',
                     marshal=qmp_type_to_qobj(retval))
    elif type(retval) == list:
        ret += mcgen('''

    qmp__ret_data = QOBJECT(qlist_new());
    if (qmp__retval) {
        // FIXME need to validate the type here
        QList *list = qobject_to_qlist(qmp__ret_data);
        %(type)s i;
        for (i = qmp__retval; i != NULL; i = i->next) {
            QObject *obj = %(marshal)s(qmp__mstate, i);
            qlist_append_obj(list, obj);
        }
    }
''',
              type=qmp_type_to_c(retval[0], True),
              marshal=qmp_type_to_qobj(retval[0]))

    ret_data = 'qmp__ret_data'
    if retval == 'none':
        ret_data = 'NULL'
    ret += mcgen('''

    qmp_async_complete_command(qmp__cmd, %(ret_var)s, NULL);
}
''', ret_var=ret_data)

    return ret

def gen_definition(name, options, retval, async=False, prefix='qmp'):
    ret = ''
    if async:
        ret = gen_async_definition(name, options, retval)

    if qmp_type_is_event(retval):
        arglist = ['void *opaque']
        for argname, argtype, optional in parse_args(event_types[retval]):
            if optional:
                arglist.append('bool has_%s' % c_var(argname))
            arglist.append('%s %s' % (qmp_type_to_c(argtype), c_var(argname)))
        ret += mcgen('''

static void qmp_marshal_%(c_name)s(%(args)s)
{
    QDict *qmp__args = qdict_new();
    QmpConnection *qmp__conn = opaque;
    QmpMarshalState *qmp__mstate;

    qmp__mstate = qmp_state_get_mstate(qmp__conn->state);
''',
                     c_name=qmp_event_to_c(retval),
                     args=', '.join(arglist))

        for argname, argtype, optional in parse_args(event_types[retval]):
            if optional:
                ret += cgen('    if (has_%(c_name)s) {', c_name=c_var(argname))
                push_indent()
            ret += mcgen('''
    qdict_put_obj(qmp__args, "%(name)s", %(marshal)s(qmp__mstate, %(c_name)s));
''',
                         name=argname,  c_name=c_var(argname),
                         marshal=qmp_type_to_qobj(argtype))
            if optional:
                pop_indent()
                ret += cgen('    }')

        ret += mcgen('''

    qmp_state_event(qmp__conn, QOBJECT(qmp__args));
    QDECREF(qmp__args);
}

static void qmp_marshal_%(c_name)s(QmpState *qmp__sess, const QDict *qdict, QObject **ret_data, Error **err)
{
    int qmp__handle;
    QmpConnection *qmp__connection = qemu_mallocz(sizeof(QmpConnection));
''',
                     c_name=c_var(name))
    elif async:
        ret += mcgen('''

static void qmp_marshal_%(c_name)s(const QDict *qdict, Error **err, QmpCommandState *qmp__cmd)
{
''',
                     c_name=c_var(name))
    else:
        ret += mcgen('''

static void qmp_marshal_%(c_name)s(QmpState *qmp__sess, const QDict *qdict, QObject **ret_data, Error **err)
{
''',
                     c_name=c_var(name))

    ret += mcgen('''
    Error *qmp__err = NULL;
    QmpMarshalState *qmp__mstate;
''')

    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            ret += cgen('    KeyValues * %(c_name)s = 0;', c_name=c_var(argname))
        else:
            if optional:
                ret += cgen('    bool has_%(c_name)s = false;', c_name=c_var(argname))
            ret += cgen('    %(type)s %(c_name)s = 0;',
                        type=qmp_type_to_c(argtype, True), c_name=c_var(argname))

    if retval != 'none' and not async:
        ret += mcgen('''
    %(type)s qmp_retval = 0;
''',
                     type=qmp_type_to_c(retval, True))

    if async:
        ret += mcgen('''

    (void)qmp__err;
    qmp__mstate = qmp_state_get_mstate(qmp__cmd->state);
''')
    else:
        ret += mcgen('''

    (void)qmp__err;
    qmp__mstate = qmp_state_get_mstate(qmp__sess);
''')

    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            ret += mcgen('''
    {
        const QDictEntry *qmp__qdict_i;

        %(c_name)s = NULL;
        for (qmp__qdict_i = qdict_first(qdict); qmp__qdict_i; qmp__qdict_i = qdict_next(qdict, qmp__qdict_i)) {
            KeyValues *qmp__i;
''',
                         c_name=c_var(argname))
            for argname1, argtype1, optional1 in parse_args(options):
                if argname1 == argname:
                    continue
                ret += mcgen('''
            if (strcmp(qmp__qdict_i->key, "%(name)s") == 0) {
                continue;
            }
''',
                             name=argname1)
            ret += mcgen('''

            qmp__i = qapi_alloc_key_values();
            qmp__i->key = qemu_strdup(qmp__qdict_i->key);
            qmp__i->value = qobject_as_string(qmp__qdict_i->value);
            qmp__i->next = %(c_name)s;
            %(c_name)s = qmp__i;
        }
    }
''',
                         c_name=c_var(argname))
        elif optional:
            ret += mcgen('''

    if (qdict_haskey(qdict, "%(name)s")) {
        %(c_name)s = %(unmarshal)s(qmp__mstate, qdict_get(qdict, "%(name)s"), &qmp__err);
        if (qmp__err) {
            if (error_is_type(qmp__err, QERR_INVALID_PARAMETER_TYPE)) {
                error_set(err, QERR_INVALID_PARAMETER_TYPE, "%(name)s",
                          error_get_field(qmp__err, "expected"));
                error_free(qmp__err);
                qmp__err = NULL;
            } else {
                error_propagate(err, qmp__err);
            }
            goto qmp__out;
        }
        has_%(c_name)s = true;
    }
''',
                         name=argname, c_name=c_var(argname),
                         unmarshal=qmp_type_from_qobj(argtype))
        else:
            ret += mcgen('''

    if (!qdict_haskey(qdict, "%(name)s")) {
        error_set(err, QERR_MISSING_PARAMETER, "%(name)s");
        goto qmp__out;
    }

    %(c_name)s = %(unmarshal)s(qmp__mstate, qdict_get(qdict, "%(name)s"), &qmp__err);
    if (qmp__err) {
        if (error_is_type(qmp__err, QERR_INVALID_PARAMETER_TYPE)) {
            error_set(err, QERR_INVALID_PARAMETER_TYPE, "%(name)s",
                      error_get_field(qmp__err, "expected"));
            error_free(qmp__err);
            qmp__err = NULL;
        } else {
            error_propagate(err, qmp__err);
        }
        goto qmp__out;
    }
''',
                         name=argname, c_name=c_var(argname),
                         unmarshal=qmp_type_from_qobj(argtype))

    args = []
    for argname, argtype, optional in parse_args(options):
        if optional and argtype != '**':
            args.append('has_%s' % c_var(argname))
        args.append(c_var(argname))
    args.append('err')

    if qmp_is_stateful_cmd(name):
        args = ['qmp__sess'] + args

    if async:
        args.append('qmp_async_completion_%s' % c_var(name))
        args.append('qmp__cmd')

    arglist = ', '.join(args)
    fn = '%s_%s' % (prefix, c_var(name))

    if retval == 'none' or async:
        ret += cgen('    %(fn)s(%(args)s);', fn=fn, args=arglist)
    else:
        ret += cgen('    qmp_retval = %(fn)s(%(args)s);', fn=fn, args=arglist)

    ret += mcgen('''

    if (error_is_set(err)) {
        goto qmp__out;
    }
''')

    if retval == 'none' or async:
        pass
    elif qmp_type_is_event(retval):
        ret += mcgen('''
    qmp__handle = signal_connect(qmp_retval, qmp_marshal_%(event_name)s, qmp__connection);
    qmp_state_add_connection(qmp__sess, "%(ret_name)s", qmp_retval->signal, qmp__handle, qmp__connection);
    *ret_data = QOBJECT(qint_from_int(qmp__connection->global_handle));
    qmp__connection = NULL;
''',
                     event_name=qmp_event_to_c(retval),
                     ret_name=retval)
    elif type(retval) == str:
        ret += mcgen('''
    *ret_data = %(marshal)s(qmp__mstate, qmp_retval);
''',
                     marshal=qmp_type_to_qobj(retval))
    elif type(retval) == list:
        ret += mcgen('''
    *ret_data = QOBJECT(qlist_new());
    if (qmp_retval) {
        // FIXME need to validate the type here
        QList *list = qobject_to_qlist(*ret_data);
        %(ret_type)s i;
        for (i = qmp_retval; i != NULL; i = i->next) {
            QObject *obj = %(marshal)s(qmp__mstate, i);
            qlist_append_obj(list, obj);
        }
    }
''',
                     ret_type=qmp_type_to_c(retval[0], True),
                     marshal=qmp_type_to_qobj(retval[0]))

    ret += mcgen('''

qmp__out:
''')

    if qmp_type_is_event(retval):
        ret += cgen('    qemu_free(qmp__connection);')
    ret += cgen('')
    args = []
    for argname, argtype, optional in parse_args(options):
        if argtype == '**':
            ret += cgen('    %(free)s(%(c_name)s);',
                        free=qapi_free_func('KeyValues'),
                        c_name=c_var(argname))
        elif qmp_type_should_free(argtype):
            if optional:
                ret += cgen('    if (has_%(c_name)s) {', c_name=c_var(argname))
                push_indent()
            ret += cgen('    %(free)s(%(c_name)s);',
                        free=qapi_free_func(argtype), c_name=c_var(argname))
            if optional:
                pop_indent()
                ret += cgen('    }')
    if retval != 'none' and not async:
        if qmp_type_should_free(retval):
            ret += cgen('    %(free)s(%(c_name)s);',
                        free=qapi_free_func(retval), c_name='qmp_retval')

    ret += mcgen('''

    return;
}
''')

    return ret

def gen_enum_declaration(name, entries):
    ret = mcgen('''

typedef enum %(name)s {
''', name=name)
    i = 0
    for entry in entries:
        ret += cgen('    %(abrev)s_%(name)s = %(value)d,',
                    abrev=enum_abbreviation(name),
                    name=c_var(entry).upper(), value=i)
        i += 1
    ret += mcgen('''
} %(name)s;

%(name)s qmp_type_%(dcc_name)s_from_str(const char *str, Error **errp);
const char *qmp_type_%(dcc_name)s_to_str(%(name)s value, Error **errp);
''',
                 name=name, dcc_name=de_camel_case(name))
    return ret

def gen_enum_marshal_declaration(name, entries):
    return mcgen('''

QObject *qmp_marshal_type_%(name)s(QmpMarshalState *qmp__mstate, %(name)s value);
%(name)s qmp_unmarshal_type_%(name)s(QmpMarshalState *qmp__mstate, QObject *obj, Error **errp);
''',
                name=name)

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
                     abrev=enum_abbreviation(name), value=c_var(entry).upper())

    ret += mcgen('''
    } else {
        error_set(errp, QERR_ENUM_VALUE_INVALID, "", "%(name)s", str);
        return 0;
    }
}

const char *qmp_type_%(dcc_name)s_to_str(%(name)s value, Error **errp)
{
''',
                 name=name, dcc_name=de_camel_case(name))

    first = True
    for entry in entries:
        enum = '%s_%s' % (enum_abbreviation(name), c_var(entry).upper())
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
        error_set(errp, QERR_ENUM_VALUE_INVALID, "", "%(name)s", buf);
        return NULL;
    }
}
''',
                 name=name)
    return ret

def gen_enum_marshal_definition(name, entries):
    return mcgen('''

QObject *qmp_marshal_type_%(name)s(QmpMarshalState *qmp__mstate, %(name)s value)
{
    return QOBJECT(qint_from_int(value));
}

%(name)s qmp_unmarshal_type_%(name)s(QmpMarshalState *qmp__mstate, QObject *obj, Error **errp)
{
    // FIXME need to validate the type here
    return (%(name)s)qint_get_int(qobject_to_qint(obj));
}
''',
                name=name)

def gen_qdev_declaration(name, entries):
    return mcgen('''

extern PropertyInfo qdev_prop_%(name)s;

#define DEFINE_PROP_%(upper_name)s(_n, _s, _f, _d) \\
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_%(name)s, %(name)s)
''',
                 name=name,
                 upper_name=de_camel_case(name).upper())

def gen_qdev_definition(name, entries):
    ret = mcgen('''

static int parse_%(name)s(DeviceState *dev, Property *prop, const char *str)
{
    %(name)s *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;
''',
                name=name)
    first = True
    for entry in entries:
        prefix='} else '
        if first:
            prefix=''
            first = False
        ret += mcgen('''
    %(prefix)sif (strcmp(str, "%(entry)s") == 0) {
        *ptr = %(abrev)s_%(value)s;
''',
                     prefix=prefix, entry=entry,
                     abrev=enum_abbreviation(name),
                     value=c_var(entry).upper())
    ret += mcgen('''
    } else {
        *ptr = strtoul(str, &end, 0);
        if ((*end != '\\0') || (end == str)) {
            return -EINVAL;
        }
        if (*ptr > %(abrev)s_%(value)s) {
            return -EINVAL;
        }
    }
    return 0;
}

static int print_%(name)s(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    %(name)s *ptr = qdev_get_prop_ptr(dev, prop);
''',
                 abrev=enum_abbreviation(name),
                 value=c_var(entries[-1]).upper(),
                 name=name)

    first = True
    for entry in entries:
        enum = '%s_%s' % (enum_abbreviation(name), c_var(entry).upper())
        prefix = '} else '
        if first:
            prefix=''
            first = False
        ret += mcgen('''
    %(prefix)sif (*ptr == %(enum)s) {
        return snprintf(dest, len, "%%s", "%(name)s");
''',
                     prefix=prefix, enum=enum, name=entry)
    ret += mcgen('''
    }

    return -EINVAL;
}

PropertyInfo qdev_prop_%(name)s = {
    .name = "%(name)s",
    .type = PROP_TYPE_STRING,
    .size = sizeof(%(name)s),
    .parse = parse_%(name)s,
    .print = print_%(name)s,
};
''',
                 name=name)
    return ret

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

%(name)s *qapi_alloc_%(dcc_name)s(void);
void qapi_free_%(dcc_name)s(%(name)s *obj);
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

def gen_type_marshal_declaration(name, typeinfo):
    return mcgen('''

QObject *qmp_marshal_type_%(name)s(QmpMarshalState *qmp__mstate, %(type)s src);
%(type)s qmp_unmarshal_type_%(name)s(QmpMarshalState *qmp__mstate, QObject *src, Error **errp);
''',
                     name=name, type=qmp_type_to_c(name))

    return ''

def gen_metatype_def(typeinfo, name, lhs, sep='->', level=0):
    new_lhs = 'qmp__member%d' % level

    ret = ''

    if type(typeinfo) == str:
        ret += cgen('    %(lhs)s = %(marshal)s(qmp__mstate, %(name)s);',
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
            QObject *qmp__member = %(marshal)s(qmp__mstate, %(new_lhs)s_i);
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
    %(lhs)s = %(unmarshal)s(qmp__mstate, %(c_name)s, &qmp__err);
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
        QDict *qmp__dict;
        QObject *%(objname)s;

        if (qobject_type(%(c_name)s) != QTYPE_QDICT) {
            error_set(&qmp__err, QERR_INVALID_PARAMETER_TYPE, "<unknown>", "JSON object");
            goto qmp__err_out;
        }

        qmp__dict = qobject_to_qdict(%(c_name)s);

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
        // FIXME need to validate the type here
        QList *qmp__list = qobject_to_qlist(%(c_name)s);
        QListEntry *%(objname)s;
        QLIST_FOREACH_ENTRY(qmp__list, %(objname)s) {
            %(type)s qmp__node = %(unmarshal)s(qmp__mstate, %(objname)s->value, &qmp__err);
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
                             free=qapi_free_func(argtype))
            else:
                ret += mcgen('''
    %(free)s(%(prefix)s%(c_name)s);
''',
                             prefix=prefix, c_name=c_var(argname),
                             free=qapi_free_func(argtype))

    return ret

def gen_type_marshal_definition(name, typeinfo):
    ret = mcgen('''

QObject *qmp_marshal_type_%(name)s(QmpMarshalState *qmp__mstate, %(type)s src)
{
    QObject *qmp__retval;

%(marshal)s

    return qmp__retval;
}

%(type)s qmp_unmarshal_type_%(name)s(QmpMarshalState *qmp__mstate, QObject *src, Error **errp)
{
    Error *qmp__err = NULL;
    %(type)s qmp__retval = qapi_alloc_%(dcc_name)s();

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
                 dcc_name=de_camel_case(name), free=qapi_free_func(name),
                 unmarshal=gen_metatype_undef(typeinfo, 'src', 'qmp__retval'))

    return ret

def gen_type_definition(name, typeinfo):
    return mcgen('''

void qapi_free_%(dcc_name)s(%(name)s *obj)
{
    if (!obj) {
        return;
    }
%(type_free)s

    %(free)s(obj->next);
    qemu_free(obj);
}

%(name)s *qapi_alloc_%(dcc_name)s(void)
{
    BUILD_ASSERT(sizeof(%(name)s) < 512);
    return qemu_mallocz(512);
}
''',
                dcc_name=de_camel_case(name), name=name,
                free=qapi_free_func(name),
                type_free=gen_metatype_free(typeinfo, 'obj->'))

def gen_union_marshal_definition(name, typeinfo):
    ret = mcgen('''

QObject *qmp_marshal_type_%(name)s(QmpMarshalState *qmp__mstate, %(type)s src)
{
    QDict *qmp__retval = qdict_new();

    qdict_put(qmp__retval, "kind", qint_from_int(src->kind));

    switch (src->kind) {
''',
                name=name, type=qmp_type_to_c(name))

    for argname, argtype, optional in parse_args(typeinfo):
        if optional or type(argtype) in [list, dict]:
            ret += cgen('        BUILD_BUG();')
            continue
        ret += mcgen('''
    case %(abrev)s_%(uname)s:
        qdict_put_obj(qmp__retval, "%(name)s", %(marshal)s(qmp__mstate, src->%(c_name)s));
        break;
''',
                     abrev=enum_abbreviation('%sKind' % name),
                     uname=c_var(argname).upper(), name=argname,
                     c_name=c_var(argname), marshal=qmp_type_to_qobj(argtype))

    ret += mcgen('''
    }

    return QOBJECT(qmp__retval);
}

%(type)s qmp_unmarshal_type_%(name)s(QmpMarshalState *qmp__mstate, QObject *src, Error **errp)
{
    %(type)s qmp__retval = qapi_alloc_%(dcc_name)s();
    Error *local_err = NULL;
    QDict *dict;

    if (qobject_type(src) != QTYPE_QDICT) {
        error_set(&local_err, QERR_INVALID_PARAMETER_TYPE, "<unknown>", "JSON object");
        goto out;
    }

    dict = qobject_to_qdict(src);
    if (!qdict_haskey(dict, "kind")) {
        error_set(&local_err, QERR_MISSING_PARAMETER, "kind");
        goto out;
    }

    if (qobject_type(qdict_get(dict, "kind")) != QTYPE_QINT) {
        error_set(&local_err, QERR_INVALID_PARAMETER_TYPE, "kind", "integer");
        goto out;
    }

    switch (qdict_get_int(dict, "kind")) {    
''',
                 name=name, type=qmp_type_to_c(name),
                 dcc_name=de_camel_case(name))

    for argname, argtype, optional in parse_args(typeinfo):
        ret += mcgen('''
    case %(abrev)s_%(uname)s:
        if (!qdict_haskey(dict, "%(name)s")) {
            error_set(&local_err, QERR_MISSING_PARAMETER, "%(name)s");
            goto out;
        }
        qmp__retval->kind = %(abrev)s_%(uname)s;
        qmp__retval->%(c_name)s = %(unmarshal)s(qmp__mstate, qdict_get(dict, "%(name)s"), &local_err);
        break;
''',
                     abrev=enum_abbreviation('%sKind' % name), name=argname,
                     uname=c_var(argname).upper(), c_name=c_var(argname),
                     unmarshal=qmp_type_from_qobj(argtype))

    ret += mcgen('''
    default:
        error_set(&local_err, QERR_INVALID_PARAMETER_VALUE, "kind", "%(name)sKind");
        goto out;
    }

    if (local_err) {
        goto out;
    }

    return qmp__retval;

out:
    %(free)s(qmp__retval);
    error_propagate(errp, local_err);
    return NULL;
}
''',
                 name=name, free=qapi_free_func(name))
    return ret

def gen_union_declaration(name, typeinfo):
    kind_name = "%sKind" % name
    entries = map(lambda (x, y, z): x, parse_args(typeinfo))
    ret = gen_enum_declaration(kind_name, entries)

    ret += mcgen('''
typedef struct %(name)s {
    %(kind)s kind;
    union {
''',
                 kind=kind_name, name=name)

    for argname, argtype, optional in parse_args(typeinfo):
        ret += cgen('        %(c_type)s %(c_name)s;',
                    c_type=qmp_type_to_c(argtype, True),
                    c_name=c_var(argname))

    ret += mcgen('''
    };
    struct %(name)s * next;
} %(name)s;

%(name)s *qapi_alloc_%(dcc_name)s(void);
void qapi_free_%(dcc_name)s(%(name)s *obj);
''',
                 name=name, dcc_name=de_camel_case(name))
                 
    return ret

def gen_union_definition(name, typeinfo):
    kind_name = "%sKind" % name
    entries = map(lambda (x, y, z): x, parse_args(typeinfo))
    ret = gen_enum_definition(kind_name, entries)

    ret += mcgen('''

%(name)s *qapi_alloc_%(dcc_name)s(void)
{
    BUILD_ASSERT(sizeof(%(name)s) < 512);
    return qemu_mallocz(512);
}

void qapi_free_%(dcc_name)s(%(name)s *obj)
{
    if (!obj) {
        return;
    }

    switch (obj->kind) {
''',
                 name=name, dcc_name=de_camel_case(name))

    for argname, argtype, optional in parse_args(typeinfo):
        ret += mcgen('''
    case %(abrev)s_%(uname)s:
        %(free)s(obj->%(c_name)s);
        break;
''',
                     free=qapi_free_func(argtype), c_name=c_var(argname),
                     abrev=enum_abbreviation(kind_name),
                     uname=c_var(argname).upper())
        

    ret += mcgen('''
    }
             
    qapi_free_%(dcc_name)s(obj->next);
    qemu_free(obj);
}
''',
                 name=name, dcc_name=de_camel_case(name))


    return ret

def gen_qcfg_marshal_declaration(name, data):
    ret = mcgen('''
%(c_type)s qcfg_unmarshal_type_%(name)s(KeyValues *kvs, Error **errp);
''',
                c_type=qmp_type_to_c(name), name=name)
    return ret;

def gen_qcfg_union_marshal_definition(name, typeinfo):
    kind_name = '%sKind' % name

    ret = mcgen('''

%(c_type)s qcfg_unmarshal_type_%(name)s(KeyValues *kvs, Error **errp)
{
    %(c_type)s obj;
    Error *local_err = NULL;
    KeyValues *kv;
    bool has_value = false;

    obj = qapi_alloc_%(dcc_name)s();
''',
                c_type=qmp_type_to_c(name), name=name,
                dcc_name=de_camel_case(name))

    for argname, argtype, optional in parse_args(typeinfo):
        if optional or type(argtype) in [list, dict]:
            print '    BUILD_BUG();'
            continue
        ret += mcgen('''

    kv = qcfg_find_key(kvs, "%(name)s");
    if (kv) {
        if (has_value) {
            error_set(&local_err, QERR_UNION_MULTIPLE_ENTRIES, "", "%(type_name)s",
                      qmp_type_%(dcc_name)s_to_str(obj->kind, NULL), "%(name)s");
            goto qmp__out;
        }
        has_value = true;
        obj->kind = %(abrev)s_%(uname)s;
        obj->%(c_name)s = %(unmarshal)s(kv, &local_err);
        if (local_err) {
            qcfg_enhance_error(&local_err, "%(name)s");
            goto qmp__out;
        }
        qapi_free_key_values(kv);
        kv = NULL;
    }
''',
                     name=argname, abrev=enum_abbreviation(kind_name),
                     uname=c_var(argname).upper(), c_name=c_var(argname),
                     unmarshal=qcfg_unmarshal_type(argtype),
                     dcc_name=de_camel_case(kind_name), type_name=name)

    first = True
    ret += mcgen('''

    for (kv = kvs; kv; kv = kv->next) {
''')
    for argname, argtype, optional in parse_args(typeinfo):
        if first:
            ret += mcgen('''
        if (!(qcfg_iskey(kv->key, "%(name)s") ||
''',
                         name=argname)
            first = False
        else:
            ret += mcgen('''
              qcfg_iskey(kv->key, "%(name)s") ||
''',
                         name=argname)
    if not first:
        ret += mcgen('''
              false)) {
            error_set(&local_err, QERR_INVALID_PARAMETER, kv->key);
            kv = NULL;
            goto qmp__out;
        }
''')
    ret += mcgen('''
    }
''')

    ret += mcgen('''

    if (!has_value) {
        error_set(&local_err, QERR_UNION_NO_VALUE, "", "%(name)s");
        goto qmp__out;
    }

    return obj;

qmp__out:
    %(free)s(obj);
    qapi_free_key_values(kv);
    error_propagate(errp, local_err);
    return NULL;
}
''',
                 free=qapi_free_func(name), name=name)

    return ret

def gen_qcfg_enum_marshal_definition(name, typeinfo):
    return mcgen('''

%(c_type)s qcfg_unmarshal_type_%(name)s(KeyValues *kvs, Error **errp)
{
    return qmp_type_%(dcc_name)s_from_str(kvs->value, errp);
}
''',
                c_type=qmp_type_to_c(name), name=name,
                dcc_name=de_camel_case(name))

def gen_qcfg_marshal_definition(name, typeinfo):
    ret = mcgen('''

%(c_type)s qcfg_unmarshal_type_%(name)s(KeyValues *kvs, Error **errp)
{
    %(c_type)s obj;
    Error *local_err = NULL;
    KeyValues *kv;

    obj = qapi_alloc_%(dcc_name)s();
''',
                c_type=qmp_type_to_c(name), name=name,
                dcc_name=de_camel_case(name))

    for argname, argtype, optional in parse_args(typeinfo):
        if type(argtype) in [dict, list]:
            ret += cgen('    BUILD_BUG();')
            continue
        ret += mcgen('''

    kv = qcfg_find_key(kvs, "%(name)s");
''',
                    name=argname)
        if optional:
            ret += mcgen('''
    if (kv) {
        obj->has_%(c_name)s = true;
''',
                         c_name=c_var(argname))
            push_indent()
        else:
            ret += mcgen('''
    if (!kv) {
        error_set(&local_err, QERR_MISSING_PARAMETER, "%(name)s");
        goto qmp__out;
    }
''',
                         name=argname)
        ret += mcgen('''

    obj->%(c_name)s = %(unmarshal)s(kv, &local_err);
    if (local_err) {
        qcfg_enhance_error(&local_err, "%(name)s");
        goto qmp__out;
    }
    qapi_free_key_values(kv);
    kv = NULL;
''',
                     c_name=c_var(argname), name=argname,
                     unmarshal=qcfg_unmarshal_type(argtype))
        if optional:
            pop_indent()
            ret += cgen('    }')

    first = True
    ret += mcgen('''

    for (kv = kvs; kv; kv = kv->next) {
''')
    for argname, argtype, optional in parse_args(typeinfo):
        if first:
            ret += mcgen('''
        if (!(qcfg_iskey(kv->key, "%(name)s") ||
''',
                         name=argname)
            first = False
        else:
            ret += mcgen('''
              qcfg_iskey(kv->key, "%(name)s") ||
''',
                         name=argname)
    if not first:
        ret += mcgen('''
              false)) {
            error_set(&local_err, QERR_INVALID_PARAMETER, kv->key);
            kv = NULL;
            goto qmp__out;
        }
''')
    ret += mcgen('''
    }
''')

    ret += mcgen('''

    return obj;

qmp__out:
    qapi_free_key_values(kv);
    %(free)s(obj);
    error_propagate(errp, local_err);
    return NULL;
}
''',
                 free=qapi_free_func(name))

    return ret

def gen_opts_declaration(name, data):
    if data == None:
        ret = mcgen('''
void qcfg_handle_%(c_name)s(Error **errp);
''',
                    c_name=c_var(name))
    else:
        ret = mcgen('''
void qcfg_handle_%(c_name)s(%(type)s config, Error **errp);
''',
                    c_name=c_var(name), type=qmp_type_to_c(data))
    return ret

def gen_opts_declaration(name, data):
    if data == None:
        ret = mcgen('''
void qcfg_handle_%(c_name)s(Error **errp);
''',
                    c_name=c_var(name))
    else:
        ret = mcgen('''
void qcfg_handle_%(c_name)s(%(type)s config, Error **errp);
''',
                    c_name=c_var(name), type=qmp_type_to_c(data))
    return ret

def gen_opts_definition(name, data, implicit_key):
    if data == None:
        ret = mcgen('''

static void qcfg_dispatch_%(c_name)s(Error **errp)
{
    qcfg_handle_%(c_name)s(errp);
}
''',
                    c_name=c_var(name))
    else:
        if implicit_key != None:
            implicit_key = '"%s"' % implicit_key
        else:
            implicit_key = 'NULL'

        ret = mcgen('''

static void qcfg_dispatch_%(c_name)s(const char *value, Error **errp)
{
    %(type)s config;
    Error *local_err = NULL;
    KeyValues *kvs;

    kvs = qcfg_parse(value, %(implicit_key)s);
    config = qcfg_unmarshal_type_%(type_name)s(kvs, &local_err);
    if (local_err) {
        goto out;
    } else {
        qcfg_handle_%(c_name)s(config, &local_err);
    }

out:
    error_propagate(errp, local_err);
    %(free)s(config);
    qapi_free_key_values(kvs);
}
''',
                    c_name=c_var(name), type=qmp_type_to_c(data),
                    implicit_key=implicit_key, free=qapi_free_func(data),
                    type_name=data)
    return ret

def gen_handle_declaration(name, data):
    return mcgen('''

typedef struct %(name)s {
    %(ref_type)s info;

    struct %(name)s * next;
} %(name)s;

%(name)s *qapi_new_%(dcc_name)s(%(value_type)s info, Error **errp);
void qapi_free_%(dcc_name)s(%(name)s *obj);
''',
                 name=name, dcc_name=de_camel_case(name),
                 ref_type=qmp_type_to_c(data, True),
                 value_type=qmp_type_to_c(data))

def gen_handle_marshal_definition(name, typeinfo):
    return mcgen('''

QObject *qmp_marshal_type_%(name)s(QmpMarshalState *qmp__mstate, %(type)s src)
{
    QDict *dict = qdict_new();
    QString *handle = qstring_from_str("%(name)s");

    qdict_put(dict, "__handle__", handle);
    qdict_put_obj(dict, "data", %(marshal)s(qmp__mstate, src->info));
    
    return QOBJECT(dict);
}

%(type)s qmp_unmarshal_type_%(name)s(QmpMarshalState *qmp__mstate, QObject *src, Error **errp)
{
    QDict *dict;
    QObject *qobj;
    Error *local_err = NULL;
    %(data_type)s info;
    %(type)s retval;

    if (qobject_type(src) != QTYPE_QDICT) {
        error_set(&local_err, QERR_INVALID_PARAMETER_TYPE, "<unknown>", "JSON object");
        goto out;
    }

    dict = qobject_to_qdict(src);

    if (!qdict_haskey(dict, "__handle__")) {
        error_set(&local_err, QERR_MISSING_PARAMETER, "__handle__");
        goto out;
    }

    qobj = qdict_get(dict, "__handle__");

    if (qobject_type(qobj) != QTYPE_QSTRING) {
        error_set(&local_err, QERR_INVALID_PARAMETER_TYPE, "__handle__", "string");
        goto out;
    }

    if (strcmp(qstring_get_str(qobject_to_qstring(qobj)), "%(name)s") != 0) {
        error_set(&local_err, QERR_INVALID_PARAMETER_VALUE, "__handle__", "%(name)s");
        goto out;
    }

    if (!qdict_haskey(dict, "data")) {
        error_set(&local_err, QERR_MISSING_PARAMETER, "data");
        goto out;
    }

    qobj = qdict_get(dict, "data");
    info = %(data_unmarshal)s(qmp__mstate, qobj, &local_err);
    if (local_err) {
        goto out;
    }

    retval = qapi_new_%(dcc_name)s(info, errp);

    %(data_free)s(info);

    return retval;

out:
    error_propagate(errp, local_err);
    return NULL;
}
''',
                 name=name, type=qmp_type_to_c(name),
                 data_type=qmp_type_to_c(typeinfo, True),
                 dcc_name=de_camel_case(name),
                 marshal=qmp_type_to_qobj(typeinfo),
                 data_unmarshal=qmp_type_from_qobj(typeinfo),
                 data_free=qapi_free_func(typeinfo))

def gen_handle_lib_definition(name, typeinfo):
    ret = mcgen('''

%(name)s *qapi_new_%(dcc_name)s(%(value_type)s info, Error **errp)
{
    QmpMarshalState qmp__mstate = {};
    %(name)s *retval = qemu_mallocz(sizeof(*retval));
    /* a fancy pants way to do a generic object dup */
    QObject *obj = %(marshal)s(&qmp__mstate, info);
    retval->info = %(unmarshal)s(&qmp__mstate, obj, NULL);
    qobject_decref(obj);
    return retval;
}

void qapi_free_%(dcc_name)s(%(name)s *obj)
{
    if (!obj) {
        return;
    }

''',
                name=name, dcc_name=de_camel_case(name),
                value_type=qmp_type_to_c(typeinfo),
                marshal=qmp_type_to_qobj(typeinfo),
                unmarshal=qmp_type_from_qobj(typeinfo))

    if qmp_type_should_free(typeinfo):
        ret += cgen('    %(free)s(obj->info);',
                    free=qapi_free_func(typeinfo));

    ret += mcgen('''
    qapi_free_%(dcc_name)s(obj->next);
    qemu_free(obj);
}
''',
                 dcc_name=de_camel_case(name))

    return ret

def gen_python_bindings(name, data, retval):
    entries = ['"%s"' % name]
    for argname, argtype, optional in parse_args(data):
        entries.append('("%s", %s)' % (argname, optional))
    return '    "%s": [%s],\n' % (c_var(name), ', '.join(entries))

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

dependent_types = {}

def get_dependent_types(typeinfo):
    ret = []

    if type(typeinfo) == str:
        ret = [typeinfo]
        if dependent_types.has_key(typeinfo):
            ret += dependent_types[typeinfo]
    elif is_dict(typeinfo):
        ret = []
        for key in typeinfo:
            ret += get_dependent_types(typeinfo[key])
    elif type(typeinfo) == list:
        ret = get_dependent_types(typeinfo[0])

    return ret

def generate(kind, output):
    global enum_types
    global event_types
    global indent_level

    enum_types = []
    event_types = {}
    indent_level = 0

    guard = '%s_H' % c_var(output[:-2]).upper()
    core = '%s-core.h' % output[:-2]
    header = '%s.h' % output[:-2]

    if kind.endswith('body') or kind.endswith('header'):
        ret = mcgen('''
/* THIS FILE IS AUTOMATICALLY GENERATED, DO NOT EDIT */
''')
    else:
        ret = ''

    if kind == 'lib-body':
        ret += mcgen('''
#include "%(header)s"
#include "libqmp-internal.h"
''',
                     header=header)
    elif kind == 'header':
        ret += mcgen('''
#ifndef %(guard)s
#define %(guard)s

#include "qemu-common.h"
#include "qemu-objects.h"
#include "qapi-types.h"
#include "error.h"
''',
                     guard=guard)
    elif kind == 'body':
        ret += mcgen('''
#include "qmp.h"
#include "qmp-core.h"
''')
    elif kind == 'qdev-header':
        ret += mcgen('''
#ifndef %(guard)s
#define %(guard)s

#include "qemu-common.h"
#include "qapi-types.h"
#include "hw/qdev.h"
''',
                     guard=guard)
    elif kind.endswith('-body'):
        ret += mcgen('''
#include "%(header)s"
#include "qemu-common.h"
#include "qerror.h"
''',
                     header=header)
    elif kind.endswith('-header'):
        ret += mcgen('''
#ifndef %(guard)s
#define %(guard)s

#include "%(core)s"
''',
                     guard=guard, core=core)
    elif kind == 'pybinding':
        ret += mcgen('''
__qmp_commands__ = {
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

    qobj_types = []
    qcfg_types = []

    for s in exprs:
        if s.has_key('type'):
            d = get_dependent_types(s['data'])
            dependent_types[s['type']] = d
        elif s.has_key('union'):
            d = get_dependent_types(s['data'])
            dependent_types[s['union']] = d
        elif s.has_key('enum'):
            dependent_types[s['enum']] = []
        elif s.has_key('command'):
            if s.has_key('data'):
                for argname, argtype, optional in parse_args(s['data']):
                    qobj_types += get_dependent_types(argtype)
            if s.has_key('returns'):
                qobj_types += get_dependent_types(s['returns'])
        elif s.has_key('event'):
            if s.has_key('data'):
                for argname, argtype, optional in parse_args(s['data']):
                    qobj_types += get_dependent_types(argtype)
        elif s.has_key('option'):
            if s.has_key('data'):
                qcfg_types += get_dependent_types(s['data'])
        elif s.has_key('handle'):
            if s.has_key('data'):
                qobj_types += get_dependent_types(s['data'])
    
    for s in exprs:
       if s.has_key('type'):
           name = s['type']
           data = s['data']

           if kind == 'types-body':
               ret += gen_type_definition(name, data)
           elif kind == 'types-header':
               ret += gen_type_declaration(name, data)
           elif kind == 'marshal-body':
               if name in qobj_types:
                   ret += gen_type_marshal_definition(name, data)
           elif kind == 'marshal-header':
               if name in qobj_types:
                   ret += gen_type_marshal_declaration(name, data)
           elif kind == 'qcfg-header':
               if name in qcfg_types:
                   ret += gen_qcfg_marshal_declaration(name, data)
           elif kind == 'qcfg-body':
               if name in qcfg_types:
                   ret += gen_qcfg_marshal_definition(name, data)
       elif s.has_key('enum'):
           name = s['enum']
           data = s['data']

           enum_types.append(s['enum'])
           if kind == 'types-header':
               ret += gen_enum_declaration(name, data)
           elif kind == 'types-body':
               ret += gen_enum_definition(name, data)
           elif kind == 'marshal-header':
               if name in qobj_types:
                   ret += gen_enum_marshal_declaration(name, data)
           elif kind == 'marshal-body':
               if name in qobj_types:
                   ret += gen_enum_marshal_definition(name, data)
           elif kind == 'qdev-header':
               ret += gen_qdev_declaration(name, data)
           elif kind == 'qdev-body':
               ret += gen_qdev_definition(name, data)
           elif kind == 'qcfg-header':
               if name in qcfg_types:
                   ret += gen_qcfg_marshal_declaration(name, data)
           elif kind == 'qcfg-body':
               if name in qcfg_types:
                   ret += gen_qcfg_enum_marshal_definition(name, data)
       elif s.has_key('union'):
           name = s['union']
           data = s['data']

           if kind == 'types-header':
               ret += gen_union_declaration(name, data)
           elif kind == 'types-body':
               ret += gen_union_definition(name, data)
           elif kind == 'marshal-header':
               if name in qobj_types:
                   ret += gen_type_marshal_declaration(name, data)
           elif kind == 'marshal-body':
               if name in qobj_types:
                   ret += gen_union_marshal_definition(name, data)
           elif kind == 'qcfg-header':
               if name in qcfg_types:
                   ret += gen_qcfg_marshal_declaration(name, data)
           elif kind == 'qcfg-body':
               if name in qcfg_types:
                   ret += gen_qcfg_union_marshal_definition(name, data)
       elif s.has_key('event'):
           name = s['event']
           data = {}
           if s.has_key('data'):
               data = s['data']

           event_types[name] = data
           if kind == 'types-header':
               ret += gen_type_declaration(name, data)
           elif kind == 'lib-body':
               ret += gen_lib_event_definition(name, data)
       elif s.has_key('command'):
            name = s['command']
            options = {}
            if s.has_key('data'):
                options = s['data']
            retval = 'none'
            if s.has_key('returns'):
                retval = s['returns']
            if kind == 'body':
                async = qmp_is_async_cmd(name)
                proxy = qmp_is_proxy_cmd(name)
                if proxy:
                    ret += gen_lib_definition(name, options, retval, proxy=True)
                ret += gen_definition(name, options, retval, async=async)
            elif kind == 'header':
                async = qmp_is_async_cmd(name)
                ret += gen_declaration(name, options, retval, async=async)
            elif kind == 'guest-body':
                if qmp_is_proxy_cmd(name):
                    ret += gen_definition(name, options, retval, prefix='qga')
            elif kind == 'guest-header':
                if qmp_is_proxy_cmd(name):
                    ret += gen_declaration(name, options, retval, prefix='qga')
            elif kind == 'lib-body':
                ret += gen_lib_definition(name, options, retval)
            elif kind == 'lib-header':
                ret += gen_lib_declaration(name, options, retval)
            elif kind == 'pybinding':
                ret += gen_python_bindings(name, options, retval)
       elif s.has_key('option'):
           name = s['option']
           data = None
           implicit_key = None
           if s.has_key('data'):
               data = s['data']
           if s.has_key('implicit'):
               implicit_key = s['implicit']
           if kind == 'opts-header':
               ret += gen_opts_declaration(name, data)
           elif kind == 'opts-body':
               ret += gen_opts_definition(name, data, implicit_key)
       elif s.has_key('handle'):
           name = s['handle']
           data = s['data']
           if kind == 'types-header':
               ret += gen_handle_declaration(name, data);
           elif kind == 'marshal-header':
               ret += gen_type_marshal_declaration(name, data)
           elif kind == 'marshal-body':
               ret += gen_handle_marshal_definition(name, data)
           elif kind == 'lib-body':
               ret += gen_handle_lib_definition(name, data)
    
    if kind.endswith('header'):
        ret += cgen('#endif')
    elif kind == 'body':
        ret += mcgen('''

static void qmp_init_marshal(void)
{
''')
        for s in exprs:
            if not s.has_key('command'):
                continue
            name = s['command']
            retval = 'none'
            if s.has_key('returns'):
                retval = s['returns']

            if qmp_is_async_cmd(name):
                ret += mcgen('''
    qmp_register_async_command("%(name)s", &qmp_marshal_%(c_name)s);
''',
                             name=name, c_name=c_var(name))
            else:
                ret += mcgen('''
    qmp_register_command("%(name)s", &qmp_marshal_%(c_name)s);
''',
                             name=name, c_name=c_var(name))
        ret += mcgen('''
}

qapi_init(qmp_init_marshal);
''')
    elif kind == 'guest-body':
        ret += mcgen('''

void qga_init_marshal(void)
{
''')
        for s in exprs:
            if not s.has_key('command'):
                continue
            name = s['command']
            if qmp_is_proxy_command(name):
                ret += mcgen('''
    qga_register_command("%(name)s", &qmp_marshal_%(c_name)s);
''',
                             name=name, c_name=c_var(name))
        ret += mcgen('''
}
''')
    elif kind == 'lib-body':
        ret += mcgen('''

void libqmp_init_events(QmpSession *sess)
{
''')
        for event in event_types:
            ret += mcgen('''
    libqmp_register_event(sess, "%(name)s", &libqmp_notify_%(c_event_name)s);
''',
                         name=event,
                         c_event_name=de_camel_case(qmp_event_to_c(event)))
        ret += mcgen('''
}
''')
    elif kind == 'opts-body':
        ret += mcgen('''

void qcfg_options_init(void)
{
''')
        for s in exprs:
            if not s.has_key('option'):
                continue
            name = s['option']
            if s.has_key('data'):
                ret += mcgen('''
     qcfg_register_option_arg("%(name)s", &qcfg_dispatch_%(c_name)s);
''',
                             name=name, c_name=c_var(name))
            else:
                ret += mcgen('''
     qcfg_register_option_noarg("%(name)s", &qcfg_dispatch_%(c_name)s);
''',
                             name=name, c_name=c_var(name))
                    
        ret += mcgen('''
}
''')
    elif kind == 'pybinding':
        ret += mcgen('''
}
''')

    return ret

def main(args):
    if len(args) != 2:
        return 1
    if not args[0].startswith('--'):
        return 1

    kind = args[0][2:]
    output = args[1]

    ret = generate(kind, output)

    f = open(output, 'w')
    f.write(ret)
    f.close()

    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
