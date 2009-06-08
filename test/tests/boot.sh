# requires: -S

TIMEOUT=$(get_config timeout 30)

status=$(monitor info status)

if test "$status" != "VM status: paused" ; then
    echo "Guest is not in stopped state, skipping"
    exit 3
fi

monitor cont > /dev/null
hello=$(guest -n -t $TIMEOUT echo 'hello world')
if test "$?" -ne 0 ; then
    echo "Guest did not boot in $TIMEOUT seconds"
    exit 1
fi

if test "$hello" != "hello world" ; then
    echo "Unexpected output $hello"
    exit 1
fi
