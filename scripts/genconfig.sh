#!/bin/sh
#
# Simple Qconfig parser
#
# Copyright IBM, Corp. 2011
#
# Authors:
#  Anthony Liguori   <aliguori@us.ibm.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

# This is just intended as a placeholder.  Please do not enhance this script.
# Instead, please try to port the Linux kernel's Kconfig parser.

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
