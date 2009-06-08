TIMEOUT=$(get_config timeout 15)
IFACE=$(get_config interface eth0)

t=0
while test "$t" -lt "$TIMEOUT" ; do
    guest ifconfig $IFACE | grep 'inet addr:' >/dev/null
    if test "$?" != "0" ; then
	sleep 1
	t=$(($t + 1))
    else
	break
    fi
done

guest ifconfig $IFACE | grep 'inet addr:' >/dev/null
if test "$?" != "0" ; then
    echo "After $TIMEOUT seconds, $IFACE is still not available."
    exit 2
fi

