#!/bin/sh

process() {
    local basedir=`dirname $1`
    cat "$1" | while read LINE; do
	local first_word=`echo $LINE | cut -f1 -d' '`
	if test "$first_word" = "config"; then
	    echo $LINE | sed -e 's:^config \(.*\):CONFIG_\1=y:g'
	elif test "$first_word" = "source"; then
	    local rest=`echo $LINE | cut -f2- -d' '`
	    process "$basedir/$rest"
	fi
    done
}

process "$1"
