TIMEOUT=$(get_config timeout 60)

guest -w /bin/sh -c 'reboot; exit' > /dev/null

hello=$(guest -n -t $TIMEOUT echo 'hello world')
if test "$?" -ne 0 ; then
    echo "Guest did not boot in $TIMEOUT seconds"
    exit 1
fi

if test "$hello" != "hello world" ; then
    echo "Unexpected output $hello"
    exit 1
fi
