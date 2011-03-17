from qmp.schema import __qmp_commands__
import socket, json

class QmpException(Exception):
    def __init__(self, klass, data):
        self.klass = klass
        self.data = data

    def __str__(self):
        return repr(self.data)

class ServerProxy(object):
    def __init__(self, path):
        s = socket.socket(socket.AF_UNIX)
        s.connect(path)
        self.__s = s
        self.__f = s.makefile()
        self.__greeting = json.loads(self.__f.readline())

    def __event(self, obj):
        print 'got event %s' % obj['event']

    def __dispatch(self, cmd):
        string = json.dumps(cmd)
        self.__f.write('%s\n' % string)
        self.__f.flush()

        while True:
            obj = json.loads(self.__f.readline())
            if obj.has_key('event'):
                self.__event(obj)
            else:
                return obj

    def __getattr__(self, name):
        if name in __qmp_commands__:
            idl = __qmp_commands__[name]
            def trampoline(*args, **kwds):
                i = 0
                cmd = {}
                cmd['execute'] = idl[0]
                for arg in args:
                    if i >= len(idl[1:]):
                        raise TypeError("make me pretty")
                    n, opt = idl[i + 1]
                    cmd[n] = arg
                    i += 1
                for key in kwds:
                    cmd[key] = kwds[key]

                result = self.__dispatch(cmd)
                if result.has_key('error'):
                    raise QmpException(result['error']['class'],
                                       result['error']['data'])
                return result['return']
            return trampoline
        raise AttributeError("make me prettier")
