ISO=$(get_config filename) || die "--filename required"
TIMEOUT=$(get_config timeout 30)

find_first_cdrom() {
    monitor info block | grep type=cdrom | head -1
}

check_if_mounted() {
    guest cat /etc/mtab | grep " $1 " >/dev/null
}

cdrom=$(find_first_cdrom | cut -f1 -d:)
if test "$?" != "0" ; then
    die "Could not find CDROM for guest"
fi

if find_first_cdrom | grep '[not inserted]' >/dev/null ; then
    :
else
    die "First CDROM in guest is already attached"
fi

monitor change $cdrom "$ISO" || \
    die "Could not change CDROM in guest"

# Mount disk
t=0
while test "$t" -lt "$TIMEOUT" ; do
    guest mount /dev/cdrom /mnt >/dev/null
    if test "$?" = "0" ; then
	break
    else
	sleep 1
	t=$(($t + 1))
    fi
done

# Wait for mount point to appear
while test "$t" -lt "$TIMEOUT" ; do
    if check_if_mounted /mnt ; then
	break
    else
	sleep 1
	t=$(($t + 1))
    fi
done

check_if_mounted /mnt || die "Failed to mount disk image"    

if guest test -e /mnt/md5sum.txt ; then
    guest cd /mnt 
    guest md5sum -c md5sum.txt >/dev/null || die "Failed to validate md5sums in image"
    guest cd /
fi

guest umount /mnt
guest eject /dev/cdrom

find_first_cdrom | grep '[not inserted]' >/dev/null || die 'Disk is still inserted after guest eject'

