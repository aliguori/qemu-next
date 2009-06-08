# requires: -no-shutdown

TIMEOUT=$(get_config timeout 30)

guest shutdown -h now >/dev/null
if test "$?" != "0" ; then
    echo "Could not execute shutdown command"
    exit 1
fi

t=0
while test "$t" -lt "$TIMEOUT" ; do
    status=$(monitor info status)
    if monitor_is_paused ; then
	break
    else
	sleep 1
	t=$(($t + 1))
    fi
done

if monitor_is_paused ; then
    monitor -w quit
else
    echo "After $TIMEOUT seconds, guest is still running."
    exit 2
fi
