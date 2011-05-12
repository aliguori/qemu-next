#!/usr/bin/python

import sys

data = sys.stdin.read()

docs = eval(data)

sys.stdout.write('''
#include "qdev-doc.h"

DeviceStateDocumentation device_docs[] = {''')

for item in docs:
    sys.stdout.write('''
    {
      .name = "%(device)s",
      .properties = (PropertyDocumentation[]){''' % item)
    for prop in item["properties"]:
        sys.stdout.write('''
        { "%s", "%s", "%s" },''' % (prop, item["properties"][prop]['type'], item["properties"][prop]['doc']))

    sys.stdout.write('''
        { } },
    },''')

sys.stdout.write('''
    { }
};
''')
