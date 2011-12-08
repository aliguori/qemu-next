import sys

info = 'SSISlaveInfo'
klass = 'SSISlaveClass'
cast = 'SSI_SLAVE_CLASS'

lines = sys.stdin.read().split('\n')

i = 0
while i < len(lines):
    line = lines[i]
    i += 1

    if line.startswith('static %s ' % info):
        if not line.endswith('info = {'):
            raise Exception('Cannot process this form "%s"' % line)

        name = line.split()[2][:-5]

        items = []
        processed_lines = []
        while i < len(lines) and lines[i] != '};':
            line = lines[i]
            i += 1
            processed_lines.append(line)

            if line.strip() == '' or line.strip().startswith('/*'):
                continue

            try:
                key, value = map(lambda x: x.strip(), line.split('=', 1))
                if value.endswith(','):
                    value = value[:-1]
            except:
                sys.stdout.write('\n'.join(processed_lines))
                raise

            if key == '.qdev.props' and value.startswith('('):
                properties = []
                while i < len(lines) and lines[i].strip() not in ['},', '}']:
                    line = lines[i]
                    i += 1

                    line = line.strip()
                    if line.endswith(','):
                        line = line[:-1]

                    properties.append(line)

                if i == len(lines):
                    raise Exception('Cannot find end of properties')

                i += 1
                value = properties

            items.append((key, value))

        if i == len(lines):
            raise Exception('Cannot find end of type info')

        i += 1

        props = filter(lambda (x,y): x == '.qdev.props', items)
        if len(props) and type(props[0][1]) == list:
            print 'static Property %s_properties[] = {' % name
            for prop in props[0][1]:
                print '    %s,' % prop
            print '};'
            print

        print '''static void %s_class_init(ObjectClass *klass, void *data)
{
    %s *k = %s(klass);
''' % (name, klass, cast)
        for key, value in items:
            if key.startswith('.qdev.'):
                continue

            print '    k->%s = %s;' % (key[1:], value)
        print '''}

static DeviceInfo %s_info = {''' % name
        for key, value in items:
            if not key.startswith('.qdev.'):
                continue

            if key == '.qdev.props' and type(value) == list:
                print '    .props = %s_properties,' % name
            else:
                print '    %s = %s,' % (key[5:], value)
        print '    .class_init = %s_class_init,' % (name)
        print '};'
    elif i < len(lines):
        print line

