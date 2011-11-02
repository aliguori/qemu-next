#include "libqtest.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

static uint8_t base = 0x70;

static int bcd2dec(int value)
{
    return (((value >> 4) & 0x0F) * 10) + (value & 0x0F);
}

#if 0
static int dec2bcd(int value)
{
    return ((value / 10) << 4) | (value % 10);
}
#endif

static uint8_t cmos_read(uint8_t reg)
{
    outb(base + 0, reg);
    return inb(base + 1);
}

static void cmos_write(uint8_t reg, uint8_t val)
{
    outb(base + 0, reg);
    outb(base + 1, val);
}

static int tm_cmp(struct tm *lhs, struct tm *rhs)
{
    time_t a, b;
    struct tm d1, d2;

    memcpy(&d1, lhs, sizeof(d1));
    memcpy(&d2, rhs, sizeof(d2));

    a = mktime(&d1);
    b = mktime(&d2);

    if (a < b) {
        return -1;
    } else if (a > b) {
        return 1;
    }

    return 0;
}

#if 0
static void print_tm(struct tm *tm)
{
    printf("%04d-%02d-%02d %02d:%02d:%02d\n",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, 
           tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_gmtoff);
}
#endif

static void cmos_get_date_time(struct tm *date)
{
    int base_year = 2000, hour_offset;
    int sec, min, hour, mday, mon, year;
    time_t ts;
    struct tm dummy;

    sec = cmos_read(0x00);
    min = cmos_read(0x02);
    hour = cmos_read(0x04);
    mday = cmos_read(0x07);
    mon = cmos_read(0x08);
    year = cmos_read(0x09);

    if ((cmos_read(0x0B) & 4) == 0) {
        sec = bcd2dec(sec);
        min = bcd2dec(min);
        hour = bcd2dec(hour);
        mday = bcd2dec(mday);
        mon = bcd2dec(mon);
        year = bcd2dec(year);
        hour_offset = 80;
    } else {
        hour_offset = 0x80;
    }

    if ((cmos_read(0x0B) & 2) == 0) {
        if (hour >= hour_offset) {
            hour -= hour_offset;
            hour += 12;
        }
    }

    ts = time(NULL);
    localtime_r(&ts, &dummy);

    date->tm_isdst = dummy.tm_isdst;
    date->tm_sec = sec;
    date->tm_min = min;
    date->tm_hour = hour;
    date->tm_mday = mday;
    date->tm_mon = mon - 1;
    date->tm_year = base_year + year - 1900;
    date->tm_gmtoff = 0;

    ts = mktime(date);
}

static bool check_time(int wiggle)
{
    struct tm start, date[4], end;
    struct tm *datep;
    time_t ts;

    /*
     * This check assumes a few things.  First, we cannot guarantee that we get
     * a consistent reading from the wall clock because we may hit an edge of
     * the clock while reading.  To work around this, we read four clock readings
     * such that at least two of them should match.  We need to assume that one
     * reading is corrupt so we need four readings to ensure that we have at
     * least two consecutive identical readings
     *
     * It's also possible that we'll cross an edge reading the host clock so
     * simply check to make sure that the clock reading is within the period of
     * when we expect it to be.
     */

    ts = time(NULL);
    gmtime_r(&ts, &start);

    cmos_get_date_time(&date[0]);
    cmos_get_date_time(&date[1]);
    cmos_get_date_time(&date[2]);
    cmos_get_date_time(&date[3]);

    ts = time(NULL);
    gmtime_r(&ts, &end);

    if (tm_cmp(&date[0], &date[1]) == 0) {
        datep = &date[0];
    } else if (tm_cmp(&date[1], &date[2]) == 0) {
        datep = &date[1];
    } else if (tm_cmp(&date[2], &date[3]) == 0) {
        datep = &date[2];
    } else {
        g_assert_not_reached();
        return false;
    }

    if (!(tm_cmp(&start, datep) <= 0 && tm_cmp(datep, &end) <= 0)) {
        time_t t, s;

        start.tm_isdst = datep->tm_isdst;

        t = mktime(datep);
        s = mktime(&start);
        if (t < s) {
            fprintf(stderr, "RTC is %ld second(s) behind wall-clock\n", (s - t));
        } else {
            fprintf(stderr, "RTC is %ld second(s) ahead of wall-clock\n", (t - s));
        }

        if (ABS(t - s) <= wiggle) {
            return true;
        }

        return false;
    }

    return true;
}

int main(int argc, char **argv)
{
    // We update the RTC based on a timer that fires every second.  But there's
    // no guarantee that we'll get the timer at any point in time so when we
    // read the CMOS time, the timer may be pending but not yet fired.  That
    // means we can be a few seconds behind.
    //
    // We really should do a gettimeofday() when CMOS time is read to get an
    // accurate clock time but until then, give ourselves a little bit of
    // wiggle room.
    int wiggle = 2;

    qtest_start(argv[1]);

    // Set BCD mode
    cmos_write(0x0B, cmos_read(0x0B) | 0x02);
    if (!check_time(wiggle)) {
        return 1;
    }

    // Set DEC mode
    cmos_write(0x0B, cmos_read(0x0B) & ~0x02);
    if (!check_time(wiggle)) {
        return 1;
    }

    return 0;
}
