#!/bin/sh

echo 'const char *vmstate_schema_json = '
sed -e "s:\":\':g;s:^\(.*\)$:    \"\1\":g"
echo '    ;'
