#!/bin/sh

echo "const char *$1 = "
sed -e 's:":\\":g;s:^.*$:  "\0\\n":g'
echo '    ;'
