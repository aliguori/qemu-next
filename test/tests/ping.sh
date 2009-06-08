PING_COUNT=$(get_config count 5)
PING_HOST=$(get_config hostname www.qemu.org)
TIMEOUT=$(get_config timeout 30)

STAT=$(guest -t $TIMEOUT ping -c $PING_COUNT $PING_HOST | grep 'packets transmitted')

if test "$?" = "2" ; then
    echo 'Ping test timed out'
    exit 1
fi

if test "$?" != "0" ; then
    echo "Ping test failed"
    exit 1
fi


xmit=$(echo $STAT | cut -f1 -d' ')
recv=$(echo $STAT | cut -f4 -d' ')

if test "$xmit" != "$PING_COUNT" ; then
    echo "Tried to transmit $PING_COUNT packets, only transmitted $xmit"
    exit 1
fi

if test "$xmit" != "$recv" ; then
    echo "Transmitted $xmit packets, but only received $recv responses"
    exit 1
fi
