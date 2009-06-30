URL=$(get_config url http://www.qemu.org/qemu-0.9.1.tar.gz)
MD5SUM=$(get_config md5sum 6591df8e9270eb358c881de4ebea1262)
TIMEOUT=$(get_config timeout 30)

if guest which curl >/dev/null ; then
    md5sum=$(guest -t $TIMEOUT /bin/bash -c "wget -O - -o /dev/null $URL | md5sum" | cut -f 1 -d' ')
else
    md5sum=$(guest -t $TIMEOUT /bin/sh -c "curl -s $URL | md5sum" | cut -f 1 -d' ')
fi

if test "$?" = "2" ; then
    echo 'HTTP get test timed out'
    exit 1
fi

if test "$?" != "0" ; then
    echo "HTTP get test failed"
    exit 1
fi

if test "$md5sum" != "$MD5SUM" ; then
    echo "Checksum mismatch between files"
    exit 1
fi
