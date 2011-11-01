from qtest import inb, outb
import qtest, time, calendar

base = 0x70

def bcd2dec(value):
    return (((value >> 4) & 0x0F) * 10) + (value & 0x0F)

def dec2bcd(value):
    return ((value / 10) << 4) | (value % 10)

def cmos_read(reg):
    outb(base + 0, reg)
    return inb(base + 1)

def cmos_write(reg, val):
    outb(base + 0, reg)
    outb(base + 1, val)

def cmos_get_date_time():
    base_year = 2000

    sec = cmos_read(0x00)
    min = cmos_read(0x02)
    hour = cmos_read(0x04)
    mday = cmos_read(0x07)
    mon = cmos_read(0x08)
    year = cmos_read(0x09)

    if (cmos_read(0x0B) & 4) == 0:
        sec = bcd2dec(sec)
        min = bcd2dec(min)
        hour = bcd2dec(hour)
        mday = bcd2dec(mday)
        mon = bcd2dec(mon)
        year = bcd2dec(year)
        hour_offset = 80
    else:
        hour_offset = 0x80

    if (cmos_read(0x0B) & 2) == 0:
        if hour >= hour_offset:
            hour -= hour_offset
            hour += 12

    return time.gmtime(calendar.timegm(((base_year + year), mon, mday, hour, min, sec)))

def check_time():
    # This check assumes a few things.  First, we cannot guarantee that we get
    # a consistent reading from the wall clock because we may hit an edge of
    # the clock while reading.  To work around this, we read four clock readings
    # such that at least two of them should match.  We need to assume that one
    # reading is corrupt so we need four readings to ensure that we have at
    # least two consecutive identical readings
    #
    # It's also possible that we'll cross an edge reading the host clock so
    # simply check to make sure that the clock reading is within the period of
    # when we expect it to be.

    start = time.gmtime()
    date1 = cmos_get_date_time()
    date2 = cmos_get_date_time()
    date3 = cmos_get_date_time()
    date4 = cmos_get_date_time()
    end = time.gmtime()

    if date1 == date2:
        date = date1
    elif date2 == date3:
        date = date2
    elif date3 == date4:
        date = date4
    else:
        print 'Could not read RTC fast enough'
        return False

    if not start <= date <= end:
        t = calendar.timegm(date)
        s = calendar.timegm(start)
        if t < s:
            print 'RTC is %d second(s) behind wall-clock' % (s - t)
        else:
            print 'RTC is %d second(s) ahead of wall-clock' % (t - s)
        return False

    return True

def main(args):
    qtest.init(args[0])

    # Set BCD mode
    cmos_write(0x0B, cmos_read(0x0B) | 0x02)
    if not check_time():
        return 1

    # Set DEC mode
    cmos_write(0x0B, cmos_read(0x0B) & ~0x02)
    if not check_time():
        return 1

    return 0

if __name__ == '__main__':
    import sys
    sys.exit(main(sys.argv[1:]))
