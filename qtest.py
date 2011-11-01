import socket

q = None

class QTest(object):
    def __init__(self, path):
        self._sock = socket.socket(socket.AF_UNIX)
        self._sock.connect(path)
        self.inbuf = ''
        self.irqs = {}
        for i in range(16):
            self.irqs[i] = False

    def _recv(self):
        while self.inbuf.find('\n') == -1:
            self.inbuf += self._sock.recv(1024)

        rsp, self.inbuf = self.inbuf.split('\n', 1)
        return rsp.split()

    def _send(self, command, *args):
        outbuf = ' '.join([command] + map(str, args))
        self._sock.sendall(outbuf + '\n')

    def _cmd(self, command, *args):
        self._send(command, *args)
        while True:
            rsp = self._recv()
            if rsp[0] in ['IRQ']:
                num = int(rsp[2], 0)
                if rsp[1] in ['raise']:
                    self.irqs[num] = True
                else:
                    self.irqs[num] = False
                continue
            if rsp[0] != 'OK':
                raise Exception('Bad response')
            break
        return rsp[1:]

    def get_irq(self, num):
        return self.irqs[num]

# Helpers to add expected platform functions in the current namespace

def init(path):
    global q
    q = QTest(path)

def outb(addr, value):
    q._cmd('outb', addr, value)

def outw(addr, value):
    q._cmd('outw', addr, value)

def outl(addr, value):
    q._cmd('outl', addr, value)

def inb(addr):
    return int(q._cmd('inb', addr)[0], 0)

def inw(addr):
    return int(q._cmd('inw', addr)[0], 0)

def inl(addr):
    return int(q._cmd('inl', addr)[0], 0)

def get_irq(num):
    return q.get_irq(num)
