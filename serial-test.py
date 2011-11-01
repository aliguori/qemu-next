from qtest import outb, inb
import qtest, sys

def main(args):
    if len(args) != 1:
        raise Exception('Missing argument')

    qtest.init(args[0])

    base = 0x3f8

    # disable THRE and RDA interrupt
    outb(base + 1, 0x00)

    for ch in "Hello, World!\r\n":
        # wait for THRE
        while (inb(base + 5) & 0x20) == 0:
            pass

        outb(base + 0, ord(ch))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
