#!/usr/bin/python

import sys

data = sys.stdin.read()

docs = eval(data)

sys.stdout.write('''
<html>
<head>
<title>QEMU device documentation</title>
</head>
<body>
''')

for item in docs:
    sys.stdout.write('''
<h2>%(device)s :: %(parent)s</h2>

<table border="1">
<tr>
<th>Name</th><th>Type</th><th>Comments</th>
</tr>
''' % item)
    for prop in item["properties"]:
        sys.stdout.write('''
<tr>
<td>%s</td><td>%s</td><td>%s</td>
</tr>
''' % (prop, item["properties"][prop]['type'], item["properties"][prop]['doc']))

    sys.stdout.write('''
</table>
''')

sys.stdout.write('''
</body>
</html>
''')
