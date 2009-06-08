# We expect QEMU_GUEST_DEV to be set and QEMU_MONITOR to be set
# we also expect MONITOR_CMD to be set

guest() {
    w=""
    n="-n"

    if test "$1" = "-w" ; then
	w="-w"
	shift;
    fi

    if test "$1" = "-n" ; then
	n=""
	shift
    fi
    if test "$1" = "-t" ; then
	timeout="--timeout=$2"
	shift
	shift
    else
	timeout=""
    fi

    "$MONITOR_CMD" $timeout $n $w "$QEMU_GUEST_DEV" "$*"
    if test "$?" = "2" ; then
	return 2
    fi

    if test -z "$n" -a -z "$w" ; then
	status=$("$MONITOR_CMD" -n "$QEMU_GUEST_DEV" 'echo $?')
	return $status
    fi
}

monitor() {
    w=""

    if test "$1" = "-w" ; then
	w="-w"
	shift
    fi

    "$MONITOR_CMD" $w "$QEMU_MONITOR" "$*"
}

monitor_is_paused() {
    status=$(monitor info status)
    if test "$status" = "VM status: paused" ; then
	return 0
    else
	return 1
    fi
}

get_config() {
    var="$1"
    val="$2"

    required="no"
    if [ -z "$val" ] ; then
	required="yes"
    fi
    found="no"

    for ((i = 0; i < $n_opts; i++)) ; do
	if [ "${opts[$i]}" = "$var" ] ; then
	    val="${vals[$i]}"
	    found="yes"
	fi
    done

    if [ "$found" = "no" -a "$required" = "yes" ] ; then
	return 1
    fi

    echo "$val"
}

set_config() {
    section="$1"
    var="$2"
    val="$3"

    util/parse-cfg --section=$section "$cfg" "$var" "$val"
}

die() {
    echo "$@"
    exit 1
}
