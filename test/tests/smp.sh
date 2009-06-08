smp_cpus=$(monitor info cpus | wc -l)
guest_cpus=$(guest cat /proc/cpuinfo | grep '^processor' | wc -l)

if test "$smp_cpus" != "$guest_cpus" ; then
    echo "Configured $smp_cpus CPUs, guest sees $guest_cpus CPUs"
    exit 1
fi

