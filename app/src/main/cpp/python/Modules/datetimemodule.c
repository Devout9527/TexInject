#define PY_SSIZE_T_CLEAN

#include "Python.h"
#include "modsupport.h"
#include "structmember.h"

#include <time.h>

#include "timefuncs.h"

#ifndef Py_BUILD_CORE
#define Py_BUILD_CORE
#endif
#include "datetime.h"
#undef Py_BUILD_CORE

#if SIZEOF_INT < 4
#       error "datetime.c requires that C int have at least 32 bits"
#endif

#define MINYEAR 1
#define MAXYEAR 9999
#define MAXORDINAL 3652059

#define MAX_DELTA_DAYS 999999999

#define GET_YEAR                PyDateTime_GET_YEAR
#define GET_MONTH               PyDateTime_GET_MONTH
#define GET_DAY                 PyDateTime_GET_DAY
#define DATE_GET_HOUR           PyDateTime_DATE_GET_HOUR
#define DATE_GET_MINUTE         PyDateTime_DATE_GET_MINUTE
#define DATE_GET_SECOND         PyDateTime_DATE_GET_SECOND
#define DATE_GET_MICROSECOND    PyDateTime_DATE_GET_MICROSECOND

#define SET_YEAR(o, v)          (((o)->data[0] = ((v) & 0xff00) >> 8), \
                 ((o)->data[1] = ((v) & 0x00ff)))
#define SET_MONTH(o, v)         (PyDateTime_GET_MONTH(o) = (v))
#define SET_DAY(o, v)           (PyDateTime_GET_DAY(o) = (v))

#define DATE_SET_HOUR(o, v)     (PyDateTime_DATE_GET_HOUR(o) = (v))
#define DATE_SET_MINUTE(o, v)   (PyDateTime_DATE_GET_MINUTE(o) = (v))
#define DATE_SET_SECOND(o, v)   (PyDateTime_DATE_GET_SECOND(o) = (v))
#define DATE_SET_MICROSECOND(o, v)      \
    (((o)->data[7] = ((v) & 0xff0000) >> 16), \
     ((o)->data[8] = ((v) & 0x00ff00) >> 8), \
     ((o)->data[9] = ((v) & 0x0000ff)))

#define TIME_GET_HOUR           PyDateTime_TIME_GET_HOUR
#define TIME_GET_MINUTE         PyDateTime_TIME_GET_MINUTE
#define TIME_GET_SECOND         PyDateTime_TIME_GET_SECOND
#define TIME_GET_MICROSECOND    PyDateTime_TIME_GET_MICROSECOND
#define TIME_SET_HOUR(o, v)     (PyDateTime_TIME_GET_HOUR(o) = (v))
#define TIME_SET_MINUTE(o, v)   (PyDateTime_TIME_GET_MINUTE(o) = (v))
#define TIME_SET_SECOND(o, v)   (PyDateTime_TIME_GET_SECOND(o) = (v))
#define TIME_SET_MICROSECOND(o, v)      \
    (((o)->data[3] = ((v) & 0xff0000) >> 16), \
     ((o)->data[4] = ((v) & 0x00ff00) >> 8), \
     ((o)->data[5] = ((v) & 0x0000ff)))

#define GET_TD_DAYS(o)          (((PyDateTime_Delta *)(o))->days)
#define GET_TD_SECONDS(o)       (((PyDateTime_Delta *)(o))->seconds)
#define GET_TD_MICROSECONDS(o)  (((PyDateTime_Delta *)(o))->microseconds)

#define SET_TD_DAYS(o, v)       ((o)->days = (v))
#define SET_TD_SECONDS(o, v)    ((o)->seconds = (v))
#define SET_TD_MICROSECONDS(o, v) ((o)->microseconds = (v))

#define HASTZINFO(p)            (((_PyDateTime_BaseTZInfo *)(p))->hastzinfo)

#define MONTH_IS_SANE(M) ((unsigned int)(M) - 1 < 12)

static PyTypeObject PyDateTime_DateType;
static PyTypeObject PyDateTime_DateTimeType;
static PyTypeObject PyDateTime_DeltaType;
static PyTypeObject PyDateTime_TimeType;
static PyTypeObject PyDateTime_TZInfoType;

#define SIGNED_ADD_OVERFLOWED(RESULT, I, J) \
    ((((RESULT) ^ (I)) & ((RESULT) ^ (J))) < 0)

static int
divmod(int x, int y, int *r)
{
    int quo;

    assert(y > 0);
    quo = x / y;
    *r = x - quo * y;
    if (*r < 0) {
        --quo;
        *r += y;
    }
    assert(0 <= *r && *r < y);
    return quo;
}

static long
round_to_long(double x)
{
    if (x >= 0.0)
        x = floor(x + 0.5);
    else
        x = ceil(x - 0.5);
    return (long)x;
}

static int _days_in_month[] = {
    0,
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int _days_before_month[] = {
    0,
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

static int
is_leap(int year)
{

    const unsigned int ayear = (unsigned int)year;
    return ayear % 4 == 0 && (ayear % 100 != 0 || ayear % 400 == 0);
}

static int
days_in_month(int year, int month)
{
    assert(month >= 1);
    assert(month <= 12);
    if (month == 2 && is_leap(year))
        return 29;
    else
        return _days_in_month[month];
}

static int
days_before_month(int year, int month)
{
    int days;

    assert(month >= 1);
    assert(month <= 12);
    days = _days_before_month[month];
    if (month > 2 && is_leap(year))
        ++days;
    return days;
}

static int
days_before_year(int year)
{
    int y = year - 1;

    assert (year >= 0);
    if (y >= 0)
        return y*365 + y/4 - y/100 + y/400;
    else {
        assert(y == -1);
        return -366;
    }
}

#define DI4Y    1461
#define DI100Y  36524
#define DI400Y  146097

static void
ord_to_ymd(int ordinal, int *year, int *month, int *day)
{
    int n, n1, n4, n100, n400, leapyear, preceding;

    assert(ordinal >= 1);
    --ordinal;
    n400 = ordinal / DI400Y;
    n = ordinal % DI400Y;
    *year = n400 * 400 + 1;

    n100 = n / DI100Y;
    n = n % DI100Y;

    n4 = n / DI4Y;
    n = n % DI4Y;

    n1 = n / 365;
    n = n % 365;

    *year += n100 * 100 + n4 * 4 + n1;
    if (n1 == 4 || n100 == 4) {
        assert(n == 0);
        *year -= 1;
        *month = 12;
        *day = 31;
        return;
    }

    leapyear = n1 == 3 && (n4 != 24 || n100 == 3);
    assert(leapyear == is_leap(*year));
    *month = (n + 50) >> 5;
    preceding = (_days_before_month[*month] + (*month > 2 && leapyear));
    if (preceding > n) {

        *month -= 1;
        preceding -= days_in_month(*year, *month);
    }
    n -= preceding;
    assert(0 <= n);
    assert(n < days_in_month(*year, *month));

    *day = n + 1;
}

static int
ymd_to_ord(int year, int month, int day)
{
    return days_before_year(year) + days_before_month(year, month) + day;
}

static int
weekday(int year, int month, int day)
{
    return (ymd_to_ord(year, month, day) + 6) % 7;
}

static int
iso_week1_monday(int year)
{
    int first_day = ymd_to_ord(year, 1, 1);

    int first_weekday = (first_day + 6) % 7;

    int week1_monday  = first_day - first_weekday;

    if (first_weekday > 3)
        week1_monday += 7;
    return week1_monday;
}

static int
check_delta_day_range(int days)
{
    if (-MAX_DELTA_DAYS <= days && days <= MAX_DELTA_DAYS)
        return 0;
    PyErr_Format(PyExc_OverflowError,
                 "days=%d; must have magnitude <= %d",
                 days, MAX_DELTA_DAYS);
    return -1;
}

static int
check_date_args(int year, int month, int day)
{

    if (year < MINYEAR || year > MAXYEAR) {
        PyErr_SetString(PyExc_ValueError,
                        "year is out of range");
        return -1;
    }
    if (month < 1 || month > 12) {
        PyErr_SetString(PyExc_ValueError,
                        "month must be in 1..12");
        return -1;
    }
    if (day < 1 || day > days_in_month(year, month)) {
        PyErr_SetString(PyExc_ValueError,
                        "day is out of range for month");
        return -1;
    }
    return 0;
}

static int
check_time_args(int h, int m, int s, int us)
{
    if (h < 0 || h > 23) {
        PyErr_SetString(PyExc_ValueError,
                        "hour must be in 0..23");
        return -1;
    }
    if (m < 0 || m > 59) {
        PyErr_SetString(PyExc_ValueError,
                        "minute must be in 0..59");
        return -1;
    }
    if (s < 0 || s > 59) {
        PyErr_SetString(PyExc_ValueError,
                        "second must be in 0..59");
        return -1;
    }
    if (us < 0 || us > 999999) {
        PyErr_SetString(PyExc_ValueError,
                        "microsecond must be in 0..999999");
        return -1;
    }
    return 0;
}

static void
normalize_pair(int *hi, int *lo, int factor)
{
    assert(factor > 0);
    assert(lo != hi);
    if (*lo < 0 || *lo >= factor) {
        const int num_hi = divmod(*lo, factor, lo);
        const int new_hi = *hi + num_hi;
        assert(! SIGNED_ADD_OVERFLOWED(new_hi, *hi, num_hi));
        *hi = new_hi;
    }
    assert(0 <= *lo && *lo < factor);
}

static void
normalize_d_s_us(int *d, int *s, int *us)
{
    if (*us < 0 || *us >= 1000000) {
        normalize_pair(s, us, 1000000);

    }
    if (*s < 0 || *s >= 24*3600) {
        normalize_pair(d, s, 24*3600);

    }
    assert(0 <= *s && *s < 24*3600);
    assert(0 <= *us && *us < 1000000);
}

static int
normalize_y_m_d(int *y, int *m, int *d)
{
    int dim;

    if (*m < 1 || *m > 12) {
        --*m;
        normalize_pair(y, m, 12);
        ++*m;

    }
    assert(1 <= *m && *m <= 12);

    dim = days_in_month(*y, *m);
    if (*d < 1 || *d > dim) {

        if (*d == 0) {
            --*m;
            if (*m > 0)
                *d = days_in_month(*y, *m);
            else {
                --*y;
                *m = 12;
                *d = 31;
            }
        }
        else if (*d == dim + 1) {

            ++*m;
            *d = 1;
            if (*m > 12) {
                *m = 1;
                ++*y;
            }
        }
        else {
            int ordinal = ymd_to_ord(*y, *m, 1) +
                                      *d - 1;
            if (ordinal < 1 || ordinal > MAXORDINAL) {
                goto error;
            } else {
                ord_to_ymd(ordinal, y, m, d);
                return 0;
            }
        }
    }
    assert(*m > 0);
    assert(*d > 0);
    if (MINYEAR <= *y && *y <= MAXYEAR)
        return 0;
 error:
    PyErr_SetString(PyExc_OverflowError,
            "date value out of range");
    return -1;

}

static int
normalize_date(int *year, int *month, int *day)
{
    return normalize_y_m_d(year, month, day);
}

static int
normalize_datetime(int *year, int *month, int *day,
                   int *hour, int *minute, int *second,
                   int *microsecond)
{
    normalize_pair(second, microsecond, 1000000);
    normalize_pair(minute, second, 60);
    normalize_pair(hour, minute, 60);
    normalize_pair(day, hour, 24);
    return normalize_date(year, month, day);
}

static PyObject *
time_alloc(PyTypeObject *type, Py_ssize_t aware)
{
    PyObject *self;

    self = (PyObject *)
        PyObject_MALLOC(aware ?
                        sizeof(PyDateTime_Time) :
                sizeof(_PyDateTime_BaseTime));
    if (self == NULL)
        return (PyObject *)PyErr_NoMemory();
    PyObject_INIT(self, type);
    return self;
}

static PyObject *
datetime_alloc(PyTypeObject *type, Py_ssize_t aware)
{
    PyObject *self;

    self = (PyObject *)
        PyObject_MALLOC(aware ?
                        sizeof(PyDateTime_DateTime) :
                sizeof(_PyDateTime_BaseDateTime));
    if (self == NULL)
        return (PyObject *)PyErr_NoMemory();
    PyObject_INIT(self, type);
    return self;
}

static void
set_date_fields(PyDateTime_Date *self, int y, int m, int d)
{
    self->hashcode = -1;
    SET_YEAR(self, y);
    SET_MONTH(self, m);
    SET_DAY(self, d);
}

static PyObject *
new_date_ex(int year, int month, int day, PyTypeObject *type)
{
    PyDateTime_Date *self;

    self = (PyDateTime_Date *) (type->tp_alloc(type, 0));
    if (self != NULL)
        set_date_fields(self, year, month, day);
    return (PyObject *) self;
}

#define new_date(year, month, day) \
    new_date_ex(year, month, day, &PyDateTime_DateType)

static PyObject *
new_datetime_ex(int year, int month, int day, int hour, int minute,
             int second, int usecond, PyObject *tzinfo, PyTypeObject *type)
{
    PyDateTime_DateTime *self;
    char aware = tzinfo != Py_None;

    self = (PyDateTime_DateTime *) (type->tp_alloc(type, aware));
    if (self != NULL) {
        self->hastzinfo = aware;
        set_date_fields((PyDateTime_Date *)self, year, month, day);
        DATE_SET_HOUR(self, hour);
        DATE_SET_MINUTE(self, minute);
        DATE_SET_SECOND(self, second);
        DATE_SET_MICROSECOND(self, usecond);
        if (aware) {
            Py_INCREF(tzinfo);
            self->tzinfo = tzinfo;
        }
    }
    return (PyObject *)self;
}

#define new_datetime(y, m, d, hh, mm, ss, us, tzinfo)           \
    new_datetime_ex(y, m, d, hh, mm, ss, us, tzinfo,            \
                    &PyDateTime_DateTimeType)

static PyObject *
new_time_ex(int hour, int minute, int second, int usecond,
            PyObject *tzinfo, PyTypeObject *type)
{
    PyDateTime_Time *self;
    char aware = tzinfo != Py_None;

    self = (PyDateTime_Time *) (type->tp_alloc(type, aware));
    if (self != NULL) {
        self->hastzinfo = aware;
        self->hashcode = -1;
        TIME_SET_HOUR(self, hour);
        TIME_SET_MINUTE(self, minute);
        TIME_SET_SECOND(self, second);
        TIME_SET_MICROSECOND(self, usecond);
        if (aware) {
            Py_INCREF(tzinfo);
            self->tzinfo = tzinfo;
        }
    }
    return (PyObject *)self;
}

#define new_time(hh, mm, ss, us, tzinfo)                \
    new_time_ex(hh, mm, ss, us, tzinfo, &PyDateTime_TimeType)

static PyObject *
new_delta_ex(int days, int seconds, int microseconds, int normalize,
             PyTypeObject *type)
{
    PyDateTime_Delta *self;

    if (normalize)
        normalize_d_s_us(&days, &seconds, &microseconds);
    assert(0 <= seconds && seconds < 24*3600);
    assert(0 <= microseconds && microseconds < 1000000);

    if (check_delta_day_range(days) < 0)
        return NULL;

    self = (PyDateTime_Delta *) (type->tp_alloc(type, 0));
    if (self != NULL) {
        self->hashcode = -1;
        SET_TD_DAYS(self, days);
        SET_TD_SECONDS(self, seconds);
        SET_TD_MICROSECONDS(self, microseconds);
    }
    return (PyObject *) self;
}

#define new_delta(d, s, us, normalize)  \
    new_delta_ex(d, s, us, normalize, &PyDateTime_DeltaType)

static int
check_tzinfo_subclass(PyObject *p)
{
    if (p == Py_None || PyTZInfo_Check(p))
        return 0;
    PyErr_Format(PyExc_TypeError,
                 "tzinfo argument must be None or of a tzinfo subclass, "
                 "not type '%s'",
                 Py_TYPE(p)->tp_name);
    return -1;
}

static PyObject *
call_tzinfo_method(PyObject *tzinfo, char *methname, PyObject *tzinfoarg)
{
    PyObject *result;

    assert(tzinfo && methname && tzinfoarg);
    assert(check_tzinfo_subclass(tzinfo) >= 0);
    if (tzinfo == Py_None) {
        result = Py_None;
        Py_INCREF(result);
    }
    else
        result = PyObject_CallMethod(tzinfo, methname, "O", tzinfoarg);
    return result;
}

static PyObject *
get_tzinfo_member(PyObject *self)
{
    PyObject *tzinfo = NULL;

    if (PyDateTime_Check(self) && HASTZINFO(self))
        tzinfo = ((PyDateTime_DateTime *)self)->tzinfo;
    else if (PyTime_Check(self) && HASTZINFO(self))
        tzinfo = ((PyDateTime_Time *)self)->tzinfo;

    return tzinfo;
}

static int
call_utc_tzinfo_method(PyObject *tzinfo, char *name, PyObject *tzinfoarg,
                       int *none)
{
    PyObject *u;
    int result = -1;

    assert(tzinfo != NULL);
    assert(PyTZInfo_Check(tzinfo));
    assert(tzinfoarg != NULL);

    *none = 0;
    u = call_tzinfo_method(tzinfo, name, tzinfoarg);
    if (u == NULL)
        return -1;

    else if (u == Py_None) {
        result = 0;
        *none = 1;
    }
    else if (PyDelta_Check(u)) {
        const int days = GET_TD_DAYS(u);
        if (days < -1 || days > 0)
            result = 24*60;
        else {

            int ss = days * 24 * 3600 + GET_TD_SECONDS(u);
            result = divmod(ss, 60, &ss);
            if (ss || GET_TD_MICROSECONDS(u)) {
                PyErr_Format(PyExc_ValueError,
                             "tzinfo.%s() must return a "
                             "whole number of minutes",
                             name);
                result = -1;
            }
        }
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "tzinfo.%s() must return None or "
                     "timedelta, not '%s'",
                     name, Py_TYPE(u)->tp_name);
    }

    Py_DECREF(u);
    if (result < -1439 || result > 1439) {
        PyErr_Format(PyExc_ValueError,
                     "tzinfo.%s() returned %d; must be in "
                     "-1439 .. 1439",
                     name, result);
        result = -1;
    }
    return result;
}

static int
call_utcoffset(PyObject *tzinfo, PyObject *tzinfoarg, int *none)
{
    return call_utc_tzinfo_method(tzinfo, "utcoffset", tzinfoarg, none);
}

static PyObject *
offset_as_timedelta(PyObject *tzinfo, char *name, PyObject *tzinfoarg) {
    PyObject *result;

    assert(tzinfo && name && tzinfoarg);
    if (tzinfo == Py_None) {
        result = Py_None;
        Py_INCREF(result);
    }
    else {
        int none;
        int offset = call_utc_tzinfo_method(tzinfo, name, tzinfoarg,
                                            &none);
        if (offset < 0 && PyErr_Occurred())
            return NULL;
        if (none) {
            result = Py_None;
            Py_INCREF(result);
        }
        else
            result = new_delta(0, offset * 60, 0, 1);
    }
    return result;
}

static int
call_dst(PyObject *tzinfo, PyObject *tzinfoarg, int *none)
{
    return call_utc_tzinfo_method(tzinfo, "dst", tzinfoarg, none);
}

static PyObject *
call_tzname(PyObject *tzinfo, PyObject *tzinfoarg)
{
    PyObject *result;

    assert(tzinfo != NULL);
    assert(check_tzinfo_subclass(tzinfo) >= 0);
    assert(tzinfoarg != NULL);

    if (tzinfo == Py_None) {
        result = Py_None;
        Py_INCREF(result);
    }
    else
        result = PyObject_CallMethod(tzinfo, "tzname", "O", tzinfoarg);

    if (result != NULL && result != Py_None && ! PyString_Check(result)) {
        PyErr_Format(PyExc_TypeError, "tzinfo.tzname() must "
                     "return None or a string, not '%s'",
                     Py_TYPE(result)->tp_name);
        Py_DECREF(result);
        result = NULL;
    }
    return result;
}

typedef enum {

          OFFSET_ERROR,

          OFFSET_UNKNOWN,

          OFFSET_NAIVE,

          OFFSET_AWARE
} naivety;

static naivety
classify_utcoffset(PyObject *op, PyObject *tzinfoarg, int *offset)
{
    int none;
    PyObject *tzinfo;

    assert(tzinfoarg != NULL);
    *offset = 0;
    tzinfo = get_tzinfo_member(op);
    if (tzinfo == Py_None)
        return OFFSET_NAIVE;
    if (tzinfo == NULL) {

        return (PyTime_Check(op) || PyDate_Check(op)) ?
               OFFSET_NAIVE : OFFSET_UNKNOWN;
    }
    *offset = call_utcoffset(tzinfo, tzinfoarg, &none);
    if (*offset == -1 && PyErr_Occurred())
        return OFFSET_ERROR;
    return none ? OFFSET_NAIVE : OFFSET_AWARE;
}

static int
classify_two_utcoffsets(PyObject *o1, int *offset1, naivety *n1,
                        PyObject *tzinfoarg1,
                        PyObject *o2, int *offset2, naivety *n2,
                        PyObject *tzinfoarg2)
{
    if (get_tzinfo_member(o1) == get_tzinfo_member(o2)) {
        *offset1 = *offset2 = 0;
        *n1 = *n2 = OFFSET_NAIVE;
    }
    else {
        *n1 = classify_utcoffset(o1, tzinfoarg1, offset1);
        if (*n1 == OFFSET_ERROR)
            return -1;
        *n2 = classify_utcoffset(o2, tzinfoarg2, offset2);
        if (*n2 == OFFSET_ERROR)
            return -1;
    }
    return 0;
}

static PyObject *
append_keyword_tzinfo(PyObject *repr, PyObject *tzinfo)
{
    PyObject *temp;

    assert(PyString_Check(repr));
    assert(tzinfo);
    if (tzinfo == Py_None)
        return repr;

    assert(PyString_AsString(repr)[PyString_Size(repr)-1] == ')');
    temp = PyString_FromStringAndSize(PyString_AsString(repr),
                                      PyString_Size(repr) - 1);
    Py_DECREF(repr);
    if (temp == NULL)
        return NULL;
    repr = temp;

    PyString_ConcatAndDel(&repr, PyString_FromString(", tzinfo="));

    PyString_ConcatAndDel(&repr, PyObject_Repr(tzinfo));

    PyString_ConcatAndDel(&repr, PyString_FromString(")"));
    return repr;
}

static PyObject *
format_ctime(PyDateTime_Date *date, int hours, int minutes, int seconds)
{
    static const char *DayNames[] = {
        "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
    };
    static const char *MonthNames[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    char buffer[128];
    int wday = weekday(GET_YEAR(date), GET_MONTH(date), GET_DAY(date));

    PyOS_snprintf(buffer, sizeof(buffer), "%s %s %2d %02d:%02d:%02d %04d",
                  DayNames[wday], MonthNames[GET_MONTH(date) - 1],
                  GET_DAY(date), hours, minutes, seconds,
                  GET_YEAR(date));
    return PyString_FromString(buffer);
}

static int
format_utcoffset(char *buf, size_t buflen, const char *sep,
                PyObject *tzinfo, PyObject *tzinfoarg)
{
    int offset;
    int hours;
    int minutes;
    char sign;
    int none;

    assert(buflen >= 1);

    offset = call_utcoffset(tzinfo, tzinfoarg, &none);
    if (offset == -1 && PyErr_Occurred())
        return -1;
    if (none) {
        *buf = '\0';
        return 0;
    }
    sign = '+';
    if (offset < 0) {
        sign = '-';
        offset = - offset;
    }
    hours = divmod(offset, 60, &minutes);
    PyOS_snprintf(buf, buflen, "%c%02d%s%02d", sign, hours, sep, minutes);
    return 0;
}

static PyObject *
make_freplacement(PyObject *object)
{
    char freplacement[64];
    if (PyTime_Check(object))
        sprintf(freplacement, "%06d", TIME_GET_MICROSECOND(object));
    else if (PyDateTime_Check(object))
        sprintf(freplacement, "%06d", DATE_GET_MICROSECOND(object));
    else
        sprintf(freplacement, "%06d", 0);

    return PyString_FromStringAndSize(freplacement, strlen(freplacement));
}

static PyObject *
wrap_strftime(PyObject *object, const char *format, size_t format_len,
                PyObject *timetuple, PyObject *tzinfoarg)
{
    PyObject *result = NULL;

    PyObject *zreplacement = NULL;
    PyObject *Zreplacement = NULL;
    PyObject *freplacement = NULL;

    const char *pin;
    char ch;

    PyObject *newfmt = NULL;
    char *pnew;
    size_t totalnew;

    size_t usednew;

    const char *ptoappend;
    size_t ntoappend;

    assert(object && format && timetuple);

    {
        long year;
        PyObject *pyyear = PySequence_GetItem(timetuple, 0);
        if (pyyear == NULL) return NULL;
        assert(PyInt_Check(pyyear));
        year = PyInt_AsLong(pyyear);
        Py_DECREF(pyyear);
        if (year < 1900) {
            PyErr_Format(PyExc_ValueError, "year=%ld is before "
                         "1900; the datetime strftime() "
                         "methods require year >= 1900",
                         year);
            return NULL;
        }
    }

    if (format_len > INT_MAX - 1) {
        PyErr_NoMemory();
        goto Done;
    }

    totalnew = format_len + 1;
    newfmt = PyString_FromStringAndSize(NULL, totalnew);
    if (newfmt == NULL) goto Done;
    pnew = PyString_AsString(newfmt);
    usednew = 0;

    pin = format;
    while ((ch = *pin++) != '\0') {
        if (ch != '%') {
            ptoappend = pin - 1;
            ntoappend = 1;
        }
        else if ((ch = *pin++) == '\0') {

            PyErr_SetString(PyExc_ValueError, "strftime format "
                            "ends with raw %");
            goto Done;
        }

        else if (ch == 'z') {
            if (zreplacement == NULL) {

                char buf[100];
                PyObject *tzinfo = get_tzinfo_member(object);
                zreplacement = PyString_FromString("");
                if (zreplacement == NULL) goto Done;
                if (tzinfo != Py_None && tzinfo != NULL) {
                    assert(tzinfoarg != NULL);
                    if (format_utcoffset(buf,
                                         sizeof(buf),
                                         "",
                                         tzinfo,
                                         tzinfoarg) < 0)
                        goto Done;
                    Py_DECREF(zreplacement);
                    zreplacement = PyString_FromString(buf);
                    if (zreplacement == NULL) goto Done;
                }
            }
            assert(zreplacement != NULL);
            ptoappend = PyString_AS_STRING(zreplacement);
            ntoappend = PyString_GET_SIZE(zreplacement);
        }
        else if (ch == 'Z') {

            if (Zreplacement == NULL) {
                PyObject *tzinfo = get_tzinfo_member(object);
                Zreplacement = PyString_FromString("");
                if (Zreplacement == NULL) goto Done;
                if (tzinfo != Py_None && tzinfo != NULL) {
                    PyObject *temp;
                    assert(tzinfoarg != NULL);
                    temp = call_tzname(tzinfo, tzinfoarg);
                    if (temp == NULL) goto Done;
                    if (temp != Py_None) {
                        assert(PyString_Check(temp));

                        Py_DECREF(Zreplacement);
                        Zreplacement = PyObject_CallMethod(
                            temp, "replace",
                            "ss", "%", "%%");
                        Py_DECREF(temp);
                        if (Zreplacement == NULL)
                            goto Done;
                        if (!PyString_Check(Zreplacement)) {
                            PyErr_SetString(PyExc_TypeError, "tzname.replace() did not return a string");
                            goto Done;
                        }
                    }
                    else
                        Py_DECREF(temp);
                }
            }
            assert(Zreplacement != NULL);
            ptoappend = PyString_AS_STRING(Zreplacement);
            ntoappend = PyString_GET_SIZE(Zreplacement);
        }
        else if (ch == 'f') {

            if (freplacement == NULL) {
                freplacement = make_freplacement(object);
                if (freplacement == NULL)
                    goto Done;
            }
            assert(freplacement != NULL);
            assert(PyString_Check(freplacement));
            ptoappend = PyString_AS_STRING(freplacement);
            ntoappend = PyString_GET_SIZE(freplacement);
        }
        else {

            ptoappend = pin - 2;
            ntoappend = 2;
        }

        assert(ptoappend != NULL);
        assert(ntoappend >= 0);
        if (ntoappend == 0)
            continue;
        while (usednew + ntoappend > totalnew) {
            size_t bigger = totalnew << 1;
            if ((bigger >> 1) != totalnew) {
                PyErr_NoMemory();
                goto Done;
            }
            if (_PyString_Resize(&newfmt, bigger) < 0)
                goto Done;
            totalnew = bigger;
            pnew = PyString_AsString(newfmt) + usednew;
        }
        memcpy(pnew, ptoappend, ntoappend);
        pnew += ntoappend;
        usednew += ntoappend;
        assert(usednew <= totalnew);
    }

    if (_PyString_Resize(&newfmt, usednew) < 0)
        goto Done;
    {
        PyObject *time = PyImport_ImportModuleNoBlock("time");
        if (time == NULL)
            goto Done;
        result = PyObject_CallMethod(time, "strftime", "OO",
                                     newfmt, timetuple);
        Py_DECREF(time);
    }
 Done:
    Py_XDECREF(freplacement);
    Py_XDECREF(zreplacement);
    Py_XDECREF(Zreplacement);
    Py_XDECREF(newfmt);
    return result;
}

static char *
isoformat_date(PyDateTime_Date *dt, char buffer[], int bufflen)
{
    int x;
    x = PyOS_snprintf(buffer, bufflen,
                      "%04d-%02d-%02d",
                      GET_YEAR(dt), GET_MONTH(dt), GET_DAY(dt));
    assert(bufflen >= x);
    return buffer + x;
}

static char *
isoformat_time(PyDateTime_DateTime *dt, char buffer[], int bufflen)
{
    int x;
    int us = DATE_GET_MICROSECOND(dt);

    x = PyOS_snprintf(buffer, bufflen,
                      "%02d:%02d:%02d",
                      DATE_GET_HOUR(dt),
                      DATE_GET_MINUTE(dt),
                      DATE_GET_SECOND(dt));
    assert(bufflen >= x);
    if (us)
        x += PyOS_snprintf(buffer + x, bufflen - x, ".%06d", us);
    assert(bufflen >= x);
    return buffer + x;
}

static PyObject *
time_time(void)
{
    PyObject *result = NULL;
    PyObject *time = PyImport_ImportModuleNoBlock("time");

    if (time != NULL) {
        result = PyObject_CallMethod(time, "time", "()");
        Py_DECREF(time);
    }
    return result;
}

static PyObject *
build_struct_time(int y, int m, int d, int hh, int mm, int ss, int dstflag)
{
    PyObject *time;
    PyObject *result = NULL;

    time = PyImport_ImportModuleNoBlock("time");
    if (time != NULL) {
        result = PyObject_CallMethod(time, "struct_time",
                                     "((iiiiiiiii))",
                                     y, m, d,
                                     hh, mm, ss,
                                     weekday(y, m, d),
                                     days_before_month(y, m) + d,
                                     dstflag);
        Py_DECREF(time);
    }
    return result;
}

static PyObject *
diff_to_bool(int diff, int op)
{
    PyObject *result;
    int istrue;

    switch (op) {
        case Py_EQ: istrue = diff == 0; break;
        case Py_NE: istrue = diff != 0; break;
        case Py_LE: istrue = diff <= 0; break;
        case Py_GE: istrue = diff >= 0; break;
        case Py_LT: istrue = diff < 0; break;
        case Py_GT: istrue = diff > 0; break;
        default:
            assert(! "op unknown");
            istrue = 0;
    }
    result = istrue ? Py_True : Py_False;
    Py_INCREF(result);
    return result;
}

static PyObject *
cmperror(PyObject *a, PyObject *b)
{
    PyErr_Format(PyExc_TypeError,
                 "can't compare %s to %s",
                 Py_TYPE(a)->tp_name, Py_TYPE(b)->tp_name);
    return NULL;
}

static PyObject *us_per_us = NULL;
static PyObject *us_per_ms = NULL;
static PyObject *us_per_second = NULL;
static PyObject *us_per_minute = NULL;
static PyObject *us_per_hour = NULL;
static PyObject *us_per_day = NULL;
static PyObject *us_per_week = NULL;
static PyObject *seconds_per_day = NULL;

static PyObject *
delta_to_microseconds(PyDateTime_Delta *self)
{
    PyObject *x1 = NULL;
    PyObject *x2 = NULL;
    PyObject *x3 = NULL;
    PyObject *result = NULL;

    x1 = PyInt_FromLong(GET_TD_DAYS(self));
    if (x1 == NULL)
        goto Done;
    x2 = PyNumber_Multiply(x1, seconds_per_day);
    if (x2 == NULL)
        goto Done;
    Py_DECREF(x1);
    x1 = NULL;

    x1 = PyInt_FromLong(GET_TD_SECONDS(self));
    if (x1 == NULL)
        goto Done;
    x3 = PyNumber_Add(x1, x2);
    if (x3 == NULL)
        goto Done;
    Py_DECREF(x1);
    Py_DECREF(x2);
    x2 = NULL;

    x1 = PyNumber_Multiply(x3, us_per_second);
    if (x1 == NULL)
        goto Done;
    Py_DECREF(x3);
    x3 = NULL;

    x2 = PyInt_FromLong(GET_TD_MICROSECONDS(self));
    if (x2 == NULL)
        goto Done;
    result = PyNumber_Add(x1, x2);

Done:
    Py_XDECREF(x1);
    Py_XDECREF(x2);
    Py_XDECREF(x3);
    return result;
}

static PyObject *
microseconds_to_delta_ex(PyObject *pyus, PyTypeObject *type)
{
    int us;
    int s;
    int d;
    long temp;

    PyObject *tuple = NULL;
    PyObject *num = NULL;
    PyObject *result = NULL;

    tuple = PyNumber_Divmod(pyus, us_per_second);
    if (tuple == NULL)
        goto Done;

    num = PyTuple_GetItem(tuple, 1);
    if (num == NULL)
        goto Done;
    temp = PyLong_AsLong(num);
    num = NULL;
    if (temp == -1 && PyErr_Occurred())
        goto Done;
    assert(0 <= temp && temp < 1000000);
    us = (int)temp;
    if (us < 0) {

        assert(PyErr_Occurred());
        goto Done;
    }

    num = PyTuple_GetItem(tuple, 0);
    if (num == NULL)
        goto Done;
    Py_INCREF(num);
    Py_DECREF(tuple);

    tuple = PyNumber_Divmod(num, seconds_per_day);
    if (tuple == NULL)
        goto Done;
    Py_DECREF(num);

    num = PyTuple_GetItem(tuple, 1);
    if (num == NULL)
        goto Done;
    temp = PyLong_AsLong(num);
    num = NULL;
    if (temp == -1 && PyErr_Occurred())
        goto Done;
    assert(0 <= temp && temp < 24*3600);
    s = (int)temp;

    if (s < 0) {

        assert(PyErr_Occurred());
        goto Done;
    }

    num = PyTuple_GetItem(tuple, 0);
    if (num == NULL)
        goto Done;
    Py_INCREF(num);
    temp = PyLong_AsLong(num);
    if (temp == -1 && PyErr_Occurred())
        goto Done;
    d = (int)temp;
    if ((long)d != temp) {
        PyErr_SetString(PyExc_OverflowError, "normalized days too "
                        "large to fit in a C int");
        goto Done;
    }
    result = new_delta_ex(d, s, us, 0, type);

Done:
    Py_XDECREF(tuple);
    Py_XDECREF(num);
    return result;
}

#define microseconds_to_delta(pymicros) \
    microseconds_to_delta_ex(pymicros, &PyDateTime_DeltaType)

static PyObject *
multiply_int_timedelta(PyObject *intobj, PyDateTime_Delta *delta)
{
    PyObject *pyus_in;
    PyObject *pyus_out;
    PyObject *result;

    pyus_in = delta_to_microseconds(delta);
    if (pyus_in == NULL)
        return NULL;

    pyus_out = PyNumber_Multiply(pyus_in, intobj);
    Py_DECREF(pyus_in);
    if (pyus_out == NULL)
        return NULL;

    result = microseconds_to_delta(pyus_out);
    Py_DECREF(pyus_out);
    return result;
}

static PyObject *
divide_timedelta_int(PyDateTime_Delta *delta, PyObject *intobj)
{
    PyObject *pyus_in;
    PyObject *pyus_out;
    PyObject *result;

    pyus_in = delta_to_microseconds(delta);
    if (pyus_in == NULL)
        return NULL;

    pyus_out = PyNumber_FloorDivide(pyus_in, intobj);
    Py_DECREF(pyus_in);
    if (pyus_out == NULL)
        return NULL;

    result = microseconds_to_delta(pyus_out);
    Py_DECREF(pyus_out);
    return result;
}

static PyObject *
delta_add(PyObject *left, PyObject *right)
{
    PyObject *result = Py_NotImplemented;

    if (PyDelta_Check(left) && PyDelta_Check(right)) {

        int days = GET_TD_DAYS(left) + GET_TD_DAYS(right);
        int seconds = GET_TD_SECONDS(left) + GET_TD_SECONDS(right);
        int microseconds = GET_TD_MICROSECONDS(left) +
                           GET_TD_MICROSECONDS(right);
        result = new_delta(days, seconds, microseconds, 1);
    }

    if (result == Py_NotImplemented)
        Py_INCREF(result);
    return result;
}

static PyObject *
delta_negative(PyDateTime_Delta *self)
{
    return new_delta(-GET_TD_DAYS(self),
                     -GET_TD_SECONDS(self),
                     -GET_TD_MICROSECONDS(self),
                     1);
}

static PyObject *
delta_positive(PyDateTime_Delta *self)
{

    return new_delta(GET_TD_DAYS(self),
                     GET_TD_SECONDS(self),
                     GET_TD_MICROSECONDS(self),
                     0);
}

static PyObject *
delta_abs(PyDateTime_Delta *self)
{
    PyObject *result;

    assert(GET_TD_MICROSECONDS(self) >= 0);
    assert(GET_TD_SECONDS(self) >= 0);

    if (GET_TD_DAYS(self) < 0)
        result = delta_negative(self);
    else
        result = delta_positive(self);

    return result;
}

static PyObject *
delta_subtract(PyObject *left, PyObject *right)
{
    PyObject *result = Py_NotImplemented;

    if (PyDelta_Check(left) && PyDelta_Check(right)) {

        int days = GET_TD_DAYS(left) - GET_TD_DAYS(right);
        int seconds = GET_TD_SECONDS(left) - GET_TD_SECONDS(right);
        int microseconds = GET_TD_MICROSECONDS(left) -
                           GET_TD_MICROSECONDS(right);
        result = new_delta(days, seconds, microseconds, 1);
    }

    if (result == Py_NotImplemented)
        Py_INCREF(result);
    return result;
}

static PyObject *
delta_richcompare(PyDateTime_Delta *self, PyObject *other, int op)
{
    int diff = 42;

    if (PyDelta_Check(other)) {
        diff = GET_TD_DAYS(self) - GET_TD_DAYS(other);
        if (diff == 0) {
            diff = GET_TD_SECONDS(self) - GET_TD_SECONDS(other);
            if (diff == 0)
                diff = GET_TD_MICROSECONDS(self) -
                       GET_TD_MICROSECONDS(other);
        }
    }
    else if (op == Py_EQ || op == Py_NE)
        diff = 1;

    else
        return cmperror((PyObject *)self, other);

    return diff_to_bool(diff, op);
}

static PyObject *delta_getstate(PyDateTime_Delta *self);

static long
delta_hash(PyDateTime_Delta *self)
{
    if (self->hashcode == -1) {
        PyObject *temp = delta_getstate(self);
        if (temp != NULL) {
            self->hashcode = PyObject_Hash(temp);
            Py_DECREF(temp);
        }
    }
    return self->hashcode;
}

static PyObject *
delta_multiply(PyObject *left, PyObject *right)
{
    PyObject *result = Py_NotImplemented;

    if (PyDelta_Check(left)) {

        if (PyInt_Check(right) || PyLong_Check(right))
            result = multiply_int_timedelta(right,
                            (PyDateTime_Delta *) left);
    }
    else if (PyInt_Check(left) || PyLong_Check(left))
        result = multiply_int_timedelta(left,
                                        (PyDateTime_Delta *) right);

    if (result == Py_NotImplemented)
        Py_INCREF(result);
    return result;
}

static PyObject *
delta_divide(PyObject *left, PyObject *right)
{
    PyObject *result = Py_NotImplemented;

    if (PyDelta_Check(left)) {

        if (PyInt_Check(right) || PyLong_Check(right))
            result = divide_timedelta_int(
                            (PyDateTime_Delta *)left,
                            right);
    }

    if (result == Py_NotImplemented)
        Py_INCREF(result);
    return result;
}

static PyObject *
accum(const char* tag, PyObject *sofar, PyObject *num, PyObject *factor,
      double *leftover)
{
    PyObject *prod;
    PyObject *sum;

    assert(num != NULL);

    if (PyInt_Check(num) || PyLong_Check(num)) {
        prod = PyNumber_Multiply(num, factor);
        if (prod == NULL)
            return NULL;
        sum = PyNumber_Add(sofar, prod);
        Py_DECREF(prod);
        return sum;
    }

    if (PyFloat_Check(num)) {
        double dnum;
        double fracpart;
        double intpart;
        PyObject *x;
        PyObject *y;

        dnum = PyFloat_AsDouble(num);
        if (dnum == -1.0 && PyErr_Occurred())
            return NULL;
        fracpart = modf(dnum, &intpart);
        x = PyLong_FromDouble(intpart);
        if (x == NULL)
            return NULL;

        prod = PyNumber_Multiply(x, factor);
        Py_DECREF(x);
        if (prod == NULL)
            return NULL;

        sum = PyNumber_Add(sofar, prod);
        Py_DECREF(prod);
        if (sum == NULL)
            return NULL;

        if (fracpart == 0.0)
            return sum;

        assert(PyInt_Check(factor) || PyLong_Check(factor));
        if (PyInt_Check(factor))
            dnum = (double)PyInt_AsLong(factor);
        else
            dnum = PyLong_AsDouble(factor);

        dnum *= fracpart;
        fracpart = modf(dnum, &intpart);
        x = PyLong_FromDouble(intpart);
        if (x == NULL) {
            Py_DECREF(sum);
            return NULL;
        }

        y = PyNumber_Add(sum, x);
        Py_DECREF(sum);
        Py_DECREF(x);
        *leftover += fracpart;
        return y;
    }

    PyErr_Format(PyExc_TypeError,
                 "unsupported type for timedelta %s component: %s",
                 tag, Py_TYPE(num)->tp_name);
    return NULL;
}

static PyObject *
delta_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    PyObject *self = NULL;

    PyObject *day = NULL;
    PyObject *second = NULL;
    PyObject *us = NULL;
    PyObject *ms = NULL;
    PyObject *minute = NULL;
    PyObject *hour = NULL;
    PyObject *week = NULL;

    PyObject *x = NULL;
    PyObject *y = NULL;
    double leftover_us = 0.0;

    static char *keywords[] = {
        "days", "seconds", "microseconds", "milliseconds",
        "minutes", "hours", "weeks", NULL
    };

    if (PyArg_ParseTupleAndKeywords(args, kw, "|OOOOOOO:__new__",
                                    keywords,
                                    &day, &second, &us,
                                    &ms, &minute, &hour, &week) == 0)
        goto Done;

    x = PyInt_FromLong(0);
    if (x == NULL)
        goto Done;

#define CLEANUP         \
    Py_DECREF(x);       \
    x = y;              \
    if (x == NULL)      \
        goto Done

    if (us) {
        y = accum("microseconds", x, us, us_per_us, &leftover_us);
        CLEANUP;
    }
    if (ms) {
        y = accum("milliseconds", x, ms, us_per_ms, &leftover_us);
        CLEANUP;
    }
    if (second) {
        y = accum("seconds", x, second, us_per_second, &leftover_us);
        CLEANUP;
    }
    if (minute) {
        y = accum("minutes", x, minute, us_per_minute, &leftover_us);
        CLEANUP;
    }
    if (hour) {
        y = accum("hours", x, hour, us_per_hour, &leftover_us);
        CLEANUP;
    }
    if (day) {
        y = accum("days", x, day, us_per_day, &leftover_us);
        CLEANUP;
    }
    if (week) {
        y = accum("weeks", x, week, us_per_week, &leftover_us);
        CLEANUP;
    }
    if (leftover_us) {

        PyObject *temp = PyLong_FromLong(round_to_long(leftover_us));
        if (temp == NULL) {
            Py_DECREF(x);
            goto Done;
        }
        y = PyNumber_Add(x, temp);
        Py_DECREF(temp);
        CLEANUP;
    }

    self = microseconds_to_delta_ex(x, type);
    Py_DECREF(x);
Done:
    return self;

#undef CLEANUP
}

static int
delta_nonzero(PyDateTime_Delta *self)
{
    return (GET_TD_DAYS(self) != 0
        || GET_TD_SECONDS(self) != 0
        || GET_TD_MICROSECONDS(self) != 0);
}

static PyObject *
delta_repr(PyDateTime_Delta *self)
{
    if (GET_TD_MICROSECONDS(self) != 0)
        return PyString_FromFormat("%s(%d, %d, %d)",
                                   Py_TYPE(self)->tp_name,
                                   GET_TD_DAYS(self),
                                   GET_TD_SECONDS(self),
                                   GET_TD_MICROSECONDS(self));
    if (GET_TD_SECONDS(self) != 0)
        return PyString_FromFormat("%s(%d, %d)",
                                   Py_TYPE(self)->tp_name,
                                   GET_TD_DAYS(self),
                                   GET_TD_SECONDS(self));

    return PyString_FromFormat("%s(%d)",
                               Py_TYPE(self)->tp_name,
                               GET_TD_DAYS(self));
}

static PyObject *
delta_str(PyDateTime_Delta *self)
{
    int days = GET_TD_DAYS(self);
    int seconds = GET_TD_SECONDS(self);
    int us = GET_TD_MICROSECONDS(self);
    int hours;
    int minutes;
    char buf[100];
    char *pbuf = buf;
    size_t buflen = sizeof(buf);
    int n;

    minutes = divmod(seconds, 60, &seconds);
    hours = divmod(minutes, 60, &minutes);

    if (days) {
        n = PyOS_snprintf(pbuf, buflen, "%d day%s, ", days,
                          (days == 1 || days == -1) ? "" : "s");
        if (n < 0 || (size_t)n >= buflen)
            goto Fail;
        pbuf += n;
        buflen -= (size_t)n;
    }

    n = PyOS_snprintf(pbuf, buflen, "%d:%02d:%02d",
                      hours, minutes, seconds);
    if (n < 0 || (size_t)n >= buflen)
        goto Fail;
    pbuf += n;
    buflen -= (size_t)n;

    if (us) {
        n = PyOS_snprintf(pbuf, buflen, ".%06d", us);
        if (n < 0 || (size_t)n >= buflen)
            goto Fail;
        pbuf += n;
    }

    return PyString_FromStringAndSize(buf, pbuf - buf);

 Fail:
    PyErr_SetString(PyExc_SystemError, "goofy result from PyOS_snprintf");
    return NULL;
}

static PyObject *
delta_getstate(PyDateTime_Delta *self)
{
    return Py_BuildValue("iii", GET_TD_DAYS(self),
                                GET_TD_SECONDS(self),
                                GET_TD_MICROSECONDS(self));
}

static PyObject *
delta_total_seconds(PyObject *self)
{
    PyObject *total_seconds;
    PyObject *total_microseconds;
    PyObject *one_million;

    total_microseconds = delta_to_microseconds((PyDateTime_Delta *)self);
    if (total_microseconds == NULL)
        return NULL;

    one_million = PyLong_FromLong(1000000L);
    if (one_million == NULL) {
        Py_DECREF(total_microseconds);
        return NULL;
    }

    total_seconds = PyNumber_TrueDivide(total_microseconds, one_million);

    Py_DECREF(total_microseconds);
    Py_DECREF(one_million);
    return total_seconds;
}

static PyObject *
delta_reduce(PyDateTime_Delta* self)
{
    return Py_BuildValue("ON", Py_TYPE(self), delta_getstate(self));
}

#define OFFSET(field)  offsetof(PyDateTime_Delta, field)

static PyMemberDef delta_members[] = {

    {"days",         T_INT, OFFSET(days),         READONLY,
     PyDoc_STR("Number of days.")},

    {"seconds",      T_INT, OFFSET(seconds),      READONLY,
     PyDoc_STR("Number of seconds (>= 0 and less than 1 day).")},

    {"microseconds", T_INT, OFFSET(microseconds), READONLY,
     PyDoc_STR("Number of microseconds (>= 0 and less than 1 second).")},
    {NULL}
};

static PyMethodDef delta_methods[] = {
    {"total_seconds", (PyCFunction)delta_total_seconds, METH_NOARGS,
     PyDoc_STR("Total seconds in the duration.")},

    {"__reduce__", (PyCFunction)delta_reduce, METH_NOARGS,
     PyDoc_STR("__reduce__() -> (cls, state)")},

    {NULL,      NULL},
};

static char delta_doc[] =
PyDoc_STR("Difference between two datetime values.");

static PyNumberMethods delta_as_number = {
    delta_add,
    delta_subtract,
    delta_multiply,
    delta_divide,
    0,
    0,
    0,
    (unaryfunc)delta_negative,
    (unaryfunc)delta_positive,
    (unaryfunc)delta_abs,
    (inquiry)delta_nonzero,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    delta_divide,
    0,
    0,
    0,
};

static PyTypeObject PyDateTime_DeltaType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "datetime.timedelta",
    sizeof(PyDateTime_Delta),
    0,
    0,
    0,
    0,
    0,
    0,
    (reprfunc)delta_repr,
    &delta_as_number,
    0,
    0,
    (hashfunc)delta_hash,
    0,
    (reprfunc)delta_str,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
        Py_TPFLAGS_BASETYPE,
    delta_doc,
    0,
    0,
    (richcmpfunc)delta_richcompare,
    0,
    0,
    0,
    delta_methods,
    delta_members,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    delta_new,
    0,
};

static PyObject *
date_year(PyDateTime_Date *self, void *unused)
{
    return PyInt_FromLong(GET_YEAR(self));
}

static PyObject *
date_month(PyDateTime_Date *self, void *unused)
{
    return PyInt_FromLong(GET_MONTH(self));
}

static PyObject *
date_day(PyDateTime_Date *self, void *unused)
{
    return PyInt_FromLong(GET_DAY(self));
}

static PyGetSetDef date_getset[] = {
    {"year",        (getter)date_year},
    {"month",       (getter)date_month},
    {"day",         (getter)date_day},
    {NULL}
};

static char *date_kws[] = {"year", "month", "day", NULL};

static PyObject *
date_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    PyObject *self = NULL;
    PyObject *state;
    int year;
    int month;
    int day;

    if (PyTuple_GET_SIZE(args) == 1 &&
        PyString_Check(state = PyTuple_GET_ITEM(args, 0)) &&
        PyString_GET_SIZE(state) == _PyDateTime_DATE_DATASIZE &&
        MONTH_IS_SANE(PyString_AS_STRING(state)[2]))
    {
        PyDateTime_Date *me;

        me = (PyDateTime_Date *) (type->tp_alloc(type, 0));
        if (me != NULL) {
            char *pdata = PyString_AS_STRING(state);
            memcpy(me->data, pdata, _PyDateTime_DATE_DATASIZE);
            me->hashcode = -1;
        }
        return (PyObject *)me;
    }

    if (PyArg_ParseTupleAndKeywords(args, kw, "iii", date_kws,
                                    &year, &month, &day)) {
        if (check_date_args(year, month, day) < 0)
            return NULL;
        self = new_date_ex(year, month, day, type);
    }
    return self;
}

static PyObject *
date_local_from_time_t(PyObject *cls, double ts)
{
    struct tm *tm;
    time_t t;
    PyObject *result = NULL;

    t = _PyTime_DoubleToTimet(ts);
    if (t == (time_t)-1 && PyErr_Occurred())
        return NULL;
    tm = localtime(&t);
    if (tm)
        result = PyObject_CallFunction(cls, "iii",
                                       tm->tm_year + 1900,
                                       tm->tm_mon + 1,
                                       tm->tm_mday);
    else
        PyErr_SetString(PyExc_ValueError,
                        "timestamp out of range for "
                        "platform localtime() function");
    return result;
}

static PyObject *
date_today(PyObject *cls, PyObject *dummy)
{
    PyObject *time;
    PyObject *result;

    time = time_time();
    if (time == NULL)
        return NULL;

    result = PyObject_CallMethod(cls, "fromtimestamp", "O", time);
    Py_DECREF(time);
    return result;
}

static PyObject *
date_fromtimestamp(PyObject *cls, PyObject *args)
{
    double timestamp;
    PyObject *result = NULL;

    if (PyArg_ParseTuple(args, "d:fromtimestamp", &timestamp))
        result = date_local_from_time_t(cls, timestamp);
    return result;
}

static PyObject *
date_fromordinal(PyObject *cls, PyObject *args)
{
    PyObject *result = NULL;
    int ordinal;

    if (PyArg_ParseTuple(args, "i:fromordinal", &ordinal)) {
        int year;
        int month;
        int day;

        if (ordinal < 1)
            PyErr_SetString(PyExc_ValueError, "ordinal must be "
                                              ">= 1");
        else {
            ord_to_ymd(ordinal, &year, &month, &day);
            result = PyObject_CallFunction(cls, "iii",
                                           year, month, day);
        }
    }
    return result;
}

static PyObject *
add_date_timedelta(PyDateTime_Date *date, PyDateTime_Delta *delta, int negate)
{
    PyObject *result = NULL;
    int year = GET_YEAR(date);
    int month = GET_MONTH(date);
    int deltadays = GET_TD_DAYS(delta);

    int day = GET_DAY(date) + (negate ? -deltadays : deltadays);

    if (normalize_date(&year, &month, &day) >= 0)
        result = new_date(year, month, day);
    return result;
}

static PyObject *
date_add(PyObject *left, PyObject *right)
{
    if (PyDateTime_Check(left) || PyDateTime_Check(right)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    if (PyDate_Check(left)) {

        if (PyDelta_Check(right))

            return add_date_timedelta((PyDateTime_Date *) left,
                                      (PyDateTime_Delta *) right,
                                      0);
    }
    else {

        if (PyDelta_Check(left))

            return add_date_timedelta((PyDateTime_Date *) right,
                                      (PyDateTime_Delta *) left,
                                      0);
    }
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

static PyObject *
date_subtract(PyObject *left, PyObject *right)
{
    if (PyDateTime_Check(left) || PyDateTime_Check(right)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    if (PyDate_Check(left)) {
        if (PyDate_Check(right)) {

            int left_ord = ymd_to_ord(GET_YEAR(left),
                                      GET_MONTH(left),
                                      GET_DAY(left));
            int right_ord = ymd_to_ord(GET_YEAR(right),
                                       GET_MONTH(right),
                                       GET_DAY(right));
            return new_delta(left_ord - right_ord, 0, 0, 0);
        }
        if (PyDelta_Check(right)) {

            return add_date_timedelta((PyDateTime_Date *) left,
                                      (PyDateTime_Delta *) right,
                                      1);
        }
    }
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

static PyObject *
date_repr(PyDateTime_Date *self)
{
    char buffer[1028];
    const char *type_name;

    type_name = Py_TYPE(self)->tp_name;
    PyOS_snprintf(buffer, sizeof(buffer), "%s(%d, %d, %d)",
                  type_name,
                  GET_YEAR(self), GET_MONTH(self), GET_DAY(self));

    return PyString_FromString(buffer);
}

static PyObject *
date_isoformat(PyDateTime_Date *self)
{
    char buffer[128];

    isoformat_date(self, buffer, sizeof(buffer));
    return PyString_FromString(buffer);
}

static PyObject *
date_str(PyDateTime_Date *self)
{
    return PyObject_CallMethod((PyObject *)self, "isoformat", "()");
}

static PyObject *
date_ctime(PyDateTime_Date *self)
{
    return format_ctime(self, 0, 0, 0);
}

static PyObject *
date_strftime(PyDateTime_Date *self, PyObject *args, PyObject *kw)
{

    PyObject *result;
    PyObject *tuple;
    const char *format;
    Py_ssize_t format_len;
    static char *keywords[] = {"format", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kw, "s#:strftime", keywords,
                                      &format, &format_len))
        return NULL;

    tuple = PyObject_CallMethod((PyObject *)self, "timetuple", "()");
    if (tuple == NULL)
        return NULL;
    result = wrap_strftime((PyObject *)self, format, format_len, tuple,
                           (PyObject *)self);
    Py_DECREF(tuple);
    return result;
}

static PyObject *
date_format(PyDateTime_Date *self, PyObject *args)
{
    PyObject *format;

    if (!PyArg_ParseTuple(args, "O:__format__", &format))
        return NULL;

    if (PyString_Check(format)) {

        if (PyString_GET_SIZE(format) == 0)
            return PyObject_Str((PyObject *)self);
    } else if (PyUnicode_Check(format)) {

        if (PyUnicode_GET_SIZE(format) == 0)
            return PyObject_Unicode((PyObject *)self);
    } else {
        PyErr_Format(PyExc_ValueError,
                     "__format__ expects str or unicode, not %.200s",
                     Py_TYPE(format)->tp_name);
        return NULL;
    }
    return PyObject_CallMethod((PyObject *)self, "strftime", "O", format);
}

static PyObject *
date_isoweekday(PyDateTime_Date *self)
{
    int dow = weekday(GET_YEAR(self), GET_MONTH(self), GET_DAY(self));

    return PyInt_FromLong(dow + 1);
}

static PyObject *
date_isocalendar(PyDateTime_Date *self)
{
    int  year         = GET_YEAR(self);
    int  week1_monday = iso_week1_monday(year);
    int today         = ymd_to_ord(year, GET_MONTH(self), GET_DAY(self));
    int  week;
    int  day;

    week = divmod(today - week1_monday, 7, &day);
    if (week < 0) {
        --year;
        week1_monday = iso_week1_monday(year);
        week = divmod(today - week1_monday, 7, &day);
    }
    else if (week >= 52 && today >= iso_week1_monday(year + 1)) {
        ++year;
        week = 0;
    }
    return Py_BuildValue("iii", year, week + 1, day + 1);
}

static PyObject *
date_richcompare(PyDateTime_Date *self, PyObject *other, int op)
{
    int diff = 42;

    if (PyDate_Check(other))
        diff = memcmp(self->data, ((PyDateTime_Date *)other)->data,
                      _PyDateTime_DATE_DATASIZE);

    else if (PyObject_HasAttrString(other, "timetuple")) {

        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    else if (op == Py_EQ || op == Py_NE)
        diff = 1;

    else
        return cmperror((PyObject *)self, other);

    return diff_to_bool(diff, op);
}

static PyObject *
date_timetuple(PyDateTime_Date *self)
{
    return build_struct_time(GET_YEAR(self),
                             GET_MONTH(self),
                             GET_DAY(self),
                             0, 0, 0, -1);
}

static PyObject *
date_replace(PyDateTime_Date *self, PyObject *args, PyObject *kw)
{
    PyObject *clone;
    PyObject *tuple;
    int year = GET_YEAR(self);
    int month = GET_MONTH(self);
    int day = GET_DAY(self);

    if (! PyArg_ParseTupleAndKeywords(args, kw, "|iii:replace", date_kws,
                                      &year, &month, &day))
        return NULL;
    tuple = Py_BuildValue("iii", year, month, day);
    if (tuple == NULL)
        return NULL;
    clone = date_new(Py_TYPE(self), tuple, NULL);
    Py_DECREF(tuple);
    return clone;
}

static PyObject *date_getstate(PyDateTime_Date *self);

static long
date_hash(PyDateTime_Date *self)
{
    if (self->hashcode == -1) {
        PyObject *temp = date_getstate(self);
        if (temp != NULL) {
            self->hashcode = PyObject_Hash(temp);
            Py_DECREF(temp);
        }
    }
    return self->hashcode;
}

static PyObject *
date_toordinal(PyDateTime_Date *self)
{
    return PyInt_FromLong(ymd_to_ord(GET_YEAR(self), GET_MONTH(self),
                                     GET_DAY(self)));
}

static PyObject *
date_weekday(PyDateTime_Date *self)
{
    int dow = weekday(GET_YEAR(self), GET_MONTH(self), GET_DAY(self));

    return PyInt_FromLong(dow);
}

static PyObject *
date_getstate(PyDateTime_Date *self)
{
    return Py_BuildValue(
        "(N)",
        PyString_FromStringAndSize((char *)self->data,
                                   _PyDateTime_DATE_DATASIZE));
}

static PyObject *
date_reduce(PyDateTime_Date *self, PyObject *arg)
{
    return Py_BuildValue("(ON)", Py_TYPE(self), date_getstate(self));
}

static PyMethodDef date_methods[] = {

    {"fromtimestamp", (PyCFunction)date_fromtimestamp, METH_VARARGS |
                                                       METH_CLASS,
     PyDoc_STR("timestamp -> local date from a POSIX timestamp (like "
               "time.time()).")},

    {"fromordinal", (PyCFunction)date_fromordinal,      METH_VARARGS |
                                                    METH_CLASS,
     PyDoc_STR("int -> date corresponding to a proleptic Gregorian "
               "ordinal.")},

    {"today",         (PyCFunction)date_today,   METH_NOARGS | METH_CLASS,
     PyDoc_STR("Current date or datetime:  same as "
               "self.__class__.fromtimestamp(time.time()).")},

    {"ctime",       (PyCFunction)date_ctime,        METH_NOARGS,
     PyDoc_STR("Return ctime() style string.")},

    {"strftime",        (PyCFunction)date_strftime,     METH_VARARGS | METH_KEYWORDS,
     PyDoc_STR("format -> strftime() style string.")},

    {"__format__",      (PyCFunction)date_format,       METH_VARARGS,
     PyDoc_STR("Formats self with strftime.")},

    {"timetuple",   (PyCFunction)date_timetuple,    METH_NOARGS,
     PyDoc_STR("Return time tuple, compatible with time.localtime().")},

    {"isocalendar", (PyCFunction)date_isocalendar,  METH_NOARGS,
     PyDoc_STR("Return a 3-tuple containing ISO year, week number, and "
               "weekday.")},

    {"isoformat",   (PyCFunction)date_isoformat,        METH_NOARGS,
     PyDoc_STR("Return string in ISO 8601 format, YYYY-MM-DD.")},

    {"isoweekday",  (PyCFunction)date_isoweekday,   METH_NOARGS,
     PyDoc_STR("Return the day of the week represented by the date.\n"
               "Monday == 1 ... Sunday == 7")},

    {"toordinal",   (PyCFunction)date_toordinal,    METH_NOARGS,
     PyDoc_STR("Return proleptic Gregorian ordinal.  January 1 of year "
               "1 is day 1.")},

    {"weekday",     (PyCFunction)date_weekday,      METH_NOARGS,
     PyDoc_STR("Return the day of the week represented by the date.\n"
               "Monday == 0 ... Sunday == 6")},

    {"replace",     (PyCFunction)date_replace,      METH_VARARGS | METH_KEYWORDS,
     PyDoc_STR("Return date with new specified fields.")},

    {"__reduce__", (PyCFunction)date_reduce,        METH_NOARGS,
     PyDoc_STR("__reduce__() -> (cls, state)")},

    {NULL,      NULL}
};

static char date_doc[] =
PyDoc_STR("date(year, month, day) --> date object");

static PyNumberMethods date_as_number = {
    date_add,
    date_subtract,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

static PyTypeObject PyDateTime_DateType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "datetime.date",
    sizeof(PyDateTime_Date),
    0,
    0,
    0,
    0,
    0,
    0,
    (reprfunc)date_repr,
    &date_as_number,
    0,
    0,
    (hashfunc)date_hash,
    0,
    (reprfunc)date_str,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
    Py_TPFLAGS_BASETYPE,
    date_doc,
    0,
    0,
    (richcmpfunc)date_richcompare,
    0,
    0,
    0,
    date_methods,
    0,
    date_getset,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    date_new,
    0,
};

static PyObject *
tzinfo_nogo(const char* methodname)
{
    PyErr_Format(PyExc_NotImplementedError,
                 "a tzinfo subclass must implement %s()",
                 methodname);
    return NULL;
}

static PyObject *
tzinfo_tzname(PyDateTime_TZInfo *self, PyObject *dt)
{
    return tzinfo_nogo("tzname");
}

static PyObject *
tzinfo_utcoffset(PyDateTime_TZInfo *self, PyObject *dt)
{
    return tzinfo_nogo("utcoffset");
}

static PyObject *
tzinfo_dst(PyDateTime_TZInfo *self, PyObject *dt)
{
    return tzinfo_nogo("dst");
}

static PyObject *
tzinfo_fromutc(PyDateTime_TZInfo *self, PyDateTime_DateTime *dt)
{
    int y, m, d, hh, mm, ss, us;

    PyObject *result;
    int off, dst;
    int none;
    int delta;

    if (! PyDateTime_Check(dt)) {
        PyErr_SetString(PyExc_TypeError,
                        "fromutc: argument must be a datetime");
        return NULL;
    }
    if (! HASTZINFO(dt) || dt->tzinfo != (PyObject *)self) {
        PyErr_SetString(PyExc_ValueError, "fromutc: dt.tzinfo "
                        "is not self");
        return NULL;
    }

    off = call_utcoffset(dt->tzinfo, (PyObject *)dt, &none);
    if (off == -1 && PyErr_Occurred())
        return NULL;
    if (none) {
        PyErr_SetString(PyExc_ValueError, "fromutc: non-None "
                        "utcoffset() result required");
        return NULL;
    }

    dst = call_dst(dt->tzinfo, (PyObject *)dt, &none);
    if (dst == -1 && PyErr_Occurred())
        return NULL;
    if (none) {
        PyErr_SetString(PyExc_ValueError, "fromutc: non-None "
                        "dst() result required");
        return NULL;
    }

    y = GET_YEAR(dt);
    m = GET_MONTH(dt);
    d = GET_DAY(dt);
    hh = DATE_GET_HOUR(dt);
    mm = DATE_GET_MINUTE(dt);
    ss = DATE_GET_SECOND(dt);
    us = DATE_GET_MICROSECOND(dt);

    delta = off - dst;
    mm += delta;
    if ((mm < 0 || mm >= 60) &&
        normalize_datetime(&y, &m, &d, &hh, &mm, &ss, &us) < 0)
        return NULL;
    result = new_datetime(y, m, d, hh, mm, ss, us, dt->tzinfo);
    if (result == NULL)
        return result;

    dst = call_dst(dt->tzinfo, result, &none);
    if (dst == -1 && PyErr_Occurred())
        goto Fail;
    if (none)
        goto Inconsistent;
    if (dst == 0)
        return result;

    mm += dst;
    if ((mm < 0 || mm >= 60) &&
        normalize_datetime(&y, &m, &d, &hh, &mm, &ss, &us) < 0)
        goto Fail;
    Py_DECREF(result);
    result = new_datetime(y, m, d, hh, mm, ss, us, dt->tzinfo);
    return result;

Inconsistent:
    PyErr_SetString(PyExc_ValueError, "fromutc: tz.dst() gave"
                    "inconsistent results; cannot convert");

Fail:
    Py_DECREF(result);
    return NULL;
}

static PyObject *
tzinfo_reduce(PyObject *self)
{
    PyObject *args, *state, *tmp;
    PyObject *getinitargs, *getstate;

    tmp = PyTuple_New(0);
    if (tmp == NULL)
        return NULL;

    getinitargs = PyObject_GetAttrString(self, "__getinitargs__");
    if (getinitargs != NULL) {
        args = PyObject_CallObject(getinitargs, tmp);
        Py_DECREF(getinitargs);
        if (args == NULL) {
            Py_DECREF(tmp);
            return NULL;
        }
    }
    else {
        PyErr_Clear();
        args = tmp;
        Py_INCREF(args);
    }

    getstate = PyObject_GetAttrString(self, "__getstate__");
    if (getstate != NULL) {
        state = PyObject_CallObject(getstate, tmp);
        Py_DECREF(getstate);
        if (state == NULL) {
            Py_DECREF(args);
            Py_DECREF(tmp);
            return NULL;
        }
    }
    else {
        PyObject **dictptr;
        PyErr_Clear();
        state = Py_None;
        dictptr = _PyObject_GetDictPtr(self);
        if (dictptr && *dictptr && PyDict_Size(*dictptr))
            state = *dictptr;
        Py_INCREF(state);
    }

    Py_DECREF(tmp);

    if (state == Py_None) {
        Py_DECREF(state);
        return Py_BuildValue("(ON)", Py_TYPE(self), args);
    }
    else
        return Py_BuildValue("(ONN)", Py_TYPE(self), args, state);
}

static PyMethodDef tzinfo_methods[] = {

    {"tzname",          (PyCFunction)tzinfo_tzname,             METH_O,
     PyDoc_STR("datetime -> string name of time zone.")},

    {"utcoffset",       (PyCFunction)tzinfo_utcoffset,          METH_O,
     PyDoc_STR("datetime -> minutes east of UTC (negative for "
               "west of UTC).")},

    {"dst",             (PyCFunction)tzinfo_dst,                METH_O,
     PyDoc_STR("datetime -> DST offset in minutes east of UTC.")},

    {"fromutc",         (PyCFunction)tzinfo_fromutc,            METH_O,
     PyDoc_STR("datetime in UTC -> datetime in local time.")},

    {"__reduce__",  (PyCFunction)tzinfo_reduce,             METH_NOARGS,
     PyDoc_STR("-> (cls, state)")},

    {NULL, NULL}
};

static char tzinfo_doc[] =
PyDoc_STR("Abstract base class for time zone info objects.");

statichere PyTypeObject PyDateTime_TZInfoType = {
    PyObject_HEAD_INIT(NULL)
    0,
    "datetime.tzinfo",
    sizeof(PyDateTime_TZInfo),
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
    Py_TPFLAGS_BASETYPE,
    tzinfo_doc,
    0,
    0,
    0,
    0,
    0,
    0,
    tzinfo_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    PyType_GenericNew,
    0,
};

static PyObject *
time_hour(PyDateTime_Time *self, void *unused)
{
    return PyInt_FromLong(TIME_GET_HOUR(self));
}

static PyObject *
time_minute(PyDateTime_Time *self, void *unused)
{
    return PyInt_FromLong(TIME_GET_MINUTE(self));
}

static PyObject *
py_time_second(PyDateTime_Time *self, void *unused)
{
    return PyInt_FromLong(TIME_GET_SECOND(self));
}

static PyObject *
time_microsecond(PyDateTime_Time *self, void *unused)
{
    return PyInt_FromLong(TIME_GET_MICROSECOND(self));
}

static PyObject *
time_tzinfo(PyDateTime_Time *self, void *unused)
{
    PyObject *result = HASTZINFO(self) ? self->tzinfo : Py_None;
    Py_INCREF(result);
    return result;
}

static PyGetSetDef time_getset[] = {
    {"hour",        (getter)time_hour},
    {"minute",      (getter)time_minute},
    {"second",      (getter)py_time_second},
    {"microsecond", (getter)time_microsecond},
    {"tzinfo",          (getter)time_tzinfo},
    {NULL}
};

static char *time_kws[] = {"hour", "minute", "second", "microsecond",
                           "tzinfo", NULL};

static PyObject *
time_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    PyObject *self = NULL;
    PyObject *state;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int usecond = 0;
    PyObject *tzinfo = Py_None;

    if (PyTuple_GET_SIZE(args) >= 1 &&
        PyTuple_GET_SIZE(args) <= 2 &&
        PyString_Check(state = PyTuple_GET_ITEM(args, 0)) &&
        PyString_GET_SIZE(state) == _PyDateTime_TIME_DATASIZE &&
        ((unsigned char) (PyString_AS_STRING(state)[0])) < 24)
    {
        PyDateTime_Time *me;
        char aware;

        if (PyTuple_GET_SIZE(args) == 2) {
            tzinfo = PyTuple_GET_ITEM(args, 1);
            if (check_tzinfo_subclass(tzinfo) < 0) {
                PyErr_SetString(PyExc_TypeError, "bad "
                    "tzinfo state arg");
                return NULL;
            }
        }
        aware = (char)(tzinfo != Py_None);
        me = (PyDateTime_Time *) (type->tp_alloc(type, aware));
        if (me != NULL) {
            char *pdata = PyString_AS_STRING(state);

            memcpy(me->data, pdata, _PyDateTime_TIME_DATASIZE);
            me->hashcode = -1;
            me->hastzinfo = aware;
            if (aware) {
                Py_INCREF(tzinfo);
                me->tzinfo = tzinfo;
            }
        }
        return (PyObject *)me;
    }

    if (PyArg_ParseTupleAndKeywords(args, kw, "|iiiiO", time_kws,
                                    &hour, &minute, &second, &usecond,
                                    &tzinfo)) {
        if (check_time_args(hour, minute, second, usecond) < 0)
            return NULL;
        if (check_tzinfo_subclass(tzinfo) < 0)
            return NULL;
        self = new_time_ex(hour, minute, second, usecond, tzinfo,
                           type);
    }
    return self;
}

static void
time_dealloc(PyDateTime_Time *self)
{
    if (HASTZINFO(self)) {
        Py_XDECREF(self->tzinfo);
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
time_utcoffset(PyDateTime_Time *self, PyObject *unused) {
    return offset_as_timedelta(HASTZINFO(self) ? self->tzinfo : Py_None,
                               "utcoffset", Py_None);
}

static PyObject *
time_dst(PyDateTime_Time *self, PyObject *unused) {
    return offset_as_timedelta(HASTZINFO(self) ? self->tzinfo : Py_None,
                               "dst", Py_None);
}

static PyObject *
time_tzname(PyDateTime_Time *self, PyObject *unused) {
    return call_tzname(HASTZINFO(self) ? self->tzinfo : Py_None,
                       Py_None);
}

static PyObject *
time_repr(PyDateTime_Time *self)
{
    char buffer[100];
    const char *type_name = Py_TYPE(self)->tp_name;
    int h = TIME_GET_HOUR(self);
    int m = TIME_GET_MINUTE(self);
    int s = TIME_GET_SECOND(self);
    int us = TIME_GET_MICROSECOND(self);
    PyObject *result = NULL;

    if (us)
        PyOS_snprintf(buffer, sizeof(buffer),
                      "%s(%d, %d, %d, %d)", type_name, h, m, s, us);
    else if (s)
        PyOS_snprintf(buffer, sizeof(buffer),
                      "%s(%d, %d, %d)", type_name, h, m, s);
    else
        PyOS_snprintf(buffer, sizeof(buffer),
                      "%s(%d, %d)", type_name, h, m);
    result = PyString_FromString(buffer);
    if (result != NULL && HASTZINFO(self))
        result = append_keyword_tzinfo(result, self->tzinfo);
    return result;
}

static PyObject *
time_str(PyDateTime_Time *self)
{
    return PyObject_CallMethod((PyObject *)self, "isoformat", "()");
}

static PyObject *
time_isoformat(PyDateTime_Time *self, PyObject *unused)
{
    char buf[100];
    PyObject *result;

    PyDateTime_DateTime datetime;
    PyDateTime_DateTime *pdatetime = &datetime;

    memcpy(pdatetime->data + _PyDateTime_DATE_DATASIZE,
           self->data,
           _PyDateTime_TIME_DATASIZE);

    isoformat_time(pdatetime, buf, sizeof(buf));
    result = PyString_FromString(buf);
    if (result == NULL || ! HASTZINFO(self) || self->tzinfo == Py_None)
        return result;

    if (format_utcoffset(buf, sizeof(buf), ":", self->tzinfo,
                         Py_None) < 0) {
        Py_DECREF(result);
        return NULL;
    }
    PyString_ConcatAndDel(&result, PyString_FromString(buf));
    return result;
}

static PyObject *
time_strftime(PyDateTime_Time *self, PyObject *args, PyObject *kw)
{
    PyObject *result;
    PyObject *tuple;
    const char *format;
    Py_ssize_t format_len;
    static char *keywords[] = {"format", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kw, "s#:strftime", keywords,
                                      &format, &format_len))
        return NULL;

    tuple = Py_BuildValue("iiiiiiiii",
                          1900, 1, 1,
                  TIME_GET_HOUR(self),
                  TIME_GET_MINUTE(self),
                  TIME_GET_SECOND(self),
                  0, 1, -1);
    if (tuple == NULL)
        return NULL;
    assert(PyTuple_Size(tuple) == 9);
    result = wrap_strftime((PyObject *)self, format, format_len, tuple,
                           Py_None);
    Py_DECREF(tuple);
    return result;
}

static PyObject *
time_richcompare(PyDateTime_Time *self, PyObject *other, int op)
{
    int diff;
    naivety n1, n2;
    int offset1, offset2;

    if (! PyTime_Check(other)) {
        if (op == Py_EQ || op == Py_NE) {
            PyObject *result = op == Py_EQ ? Py_False : Py_True;
            Py_INCREF(result);
            return result;
        }

        return cmperror((PyObject *)self, other);
    }
    if (classify_two_utcoffsets((PyObject *)self, &offset1, &n1, Py_None,
                                 other, &offset2, &n2, Py_None) < 0)
        return NULL;
    assert(n1 != OFFSET_UNKNOWN && n2 != OFFSET_UNKNOWN);

    if (n1 == n2 && offset1 == offset2) {
        diff = memcmp(self->data, ((PyDateTime_Time *)other)->data,
                      _PyDateTime_TIME_DATASIZE);
        return diff_to_bool(diff, op);
    }

    if (n1 == OFFSET_AWARE && n2 == OFFSET_AWARE) {
        assert(offset1 != offset2);

        offset1 = TIME_GET_HOUR(self) * 3600 +
                  (TIME_GET_MINUTE(self) - offset1) * 60 +
                  TIME_GET_SECOND(self);
        offset2 = TIME_GET_HOUR(other) * 3600 +
                  (TIME_GET_MINUTE(other) - offset2) * 60 +
                  TIME_GET_SECOND(other);
        diff = offset1 - offset2;
        if (diff == 0)
            diff = TIME_GET_MICROSECOND(self) -
                   TIME_GET_MICROSECOND(other);
        return diff_to_bool(diff, op);
    }

    assert(n1 != n2);
    PyErr_SetString(PyExc_TypeError,
                    "can't compare offset-naive and "
                    "offset-aware times");
    return NULL;
}

static long
time_hash(PyDateTime_Time *self)
{
    if (self->hashcode == -1) {
        naivety n;
        int offset;
        PyObject *temp;

        n = classify_utcoffset((PyObject *)self, Py_None, &offset);
        assert(n != OFFSET_UNKNOWN);
        if (n == OFFSET_ERROR)
            return -1;

        if (offset == 0)
            temp = PyString_FromStringAndSize((char *)self->data,
                                    _PyDateTime_TIME_DATASIZE);
        else {
            int hour;
            int minute;

            assert(n == OFFSET_AWARE);
            assert(HASTZINFO(self));
            hour = divmod(TIME_GET_HOUR(self) * 60 +
                            TIME_GET_MINUTE(self) - offset,
                          60,
                          &minute);
            if (0 <= hour && hour < 24)
                temp = new_time(hour, minute,
                                TIME_GET_SECOND(self),
                                TIME_GET_MICROSECOND(self),
                                Py_None);
            else
                temp = Py_BuildValue("iiii",
                           hour, minute,
                           TIME_GET_SECOND(self),
                           TIME_GET_MICROSECOND(self));
        }
        if (temp != NULL) {
            self->hashcode = PyObject_Hash(temp);
            Py_DECREF(temp);
        }
    }
    return self->hashcode;
}

static PyObject *
time_replace(PyDateTime_Time *self, PyObject *args, PyObject *kw)
{
    PyObject *clone;
    PyObject *tuple;
    int hh = TIME_GET_HOUR(self);
    int mm = TIME_GET_MINUTE(self);
    int ss = TIME_GET_SECOND(self);
    int us = TIME_GET_MICROSECOND(self);
    PyObject *tzinfo = HASTZINFO(self) ? self->tzinfo : Py_None;

    if (! PyArg_ParseTupleAndKeywords(args, kw, "|iiiiO:replace",
                                      time_kws,
                                      &hh, &mm, &ss, &us, &tzinfo))
        return NULL;
    tuple = Py_BuildValue("iiiiO", hh, mm, ss, us, tzinfo);
    if (tuple == NULL)
        return NULL;
    clone = time_new(Py_TYPE(self), tuple, NULL);
    Py_DECREF(tuple);
    return clone;
}

static int
time_nonzero(PyDateTime_Time *self)
{
    int offset;
    int none;

    if (TIME_GET_SECOND(self) || TIME_GET_MICROSECOND(self)) {

        return 1;
    }
    offset = 0;
    if (HASTZINFO(self) && self->tzinfo != Py_None) {
        offset = call_utcoffset(self->tzinfo, Py_None, &none);
        if (offset == -1 && PyErr_Occurred())
            return -1;
    }
    return (TIME_GET_MINUTE(self) - offset + TIME_GET_HOUR(self)*60) != 0;
}

static PyObject *
time_getstate(PyDateTime_Time *self)
{
    PyObject *basestate;
    PyObject *result = NULL;

    basestate =  PyString_FromStringAndSize((char *)self->data,
                                            _PyDateTime_TIME_DATASIZE);
    if (basestate != NULL) {
        if (! HASTZINFO(self) || self->tzinfo == Py_None)
            result = PyTuple_Pack(1, basestate);
        else
            result = PyTuple_Pack(2, basestate, self->tzinfo);
        Py_DECREF(basestate);
    }
    return result;
}

static PyObject *
time_reduce(PyDateTime_Time *self, PyObject *arg)
{
    return Py_BuildValue("(ON)", Py_TYPE(self), time_getstate(self));
}

static PyMethodDef time_methods[] = {

    {"isoformat",   (PyCFunction)time_isoformat,        METH_NOARGS,
     PyDoc_STR("Return string in ISO 8601 format, HH:MM:SS[.mmmmmm]"
               "[+HH:MM].")},

    {"strftime",        (PyCFunction)time_strftime,     METH_VARARGS | METH_KEYWORDS,
     PyDoc_STR("format -> strftime() style string.")},

    {"__format__",      (PyCFunction)date_format,       METH_VARARGS,
     PyDoc_STR("Formats self with strftime.")},

    {"utcoffset",       (PyCFunction)time_utcoffset,    METH_NOARGS,
     PyDoc_STR("Return self.tzinfo.utcoffset(self).")},

    {"tzname",          (PyCFunction)time_tzname,       METH_NOARGS,
     PyDoc_STR("Return self.tzinfo.tzname(self).")},

    {"dst",             (PyCFunction)time_dst,          METH_NOARGS,
     PyDoc_STR("Return self.tzinfo.dst(self).")},

    {"replace",     (PyCFunction)time_replace,          METH_VARARGS | METH_KEYWORDS,
     PyDoc_STR("Return time with new specified fields.")},

    {"__reduce__", (PyCFunction)time_reduce,        METH_NOARGS,
     PyDoc_STR("__reduce__() -> (cls, state)")},

    {NULL,      NULL}
};

static char time_doc[] =
PyDoc_STR("time([hour[, minute[, second[, microsecond[, tzinfo]]]]]) --> a time object\n\
\n\
All arguments are optional. tzinfo may be None, or an instance of\n\
a tzinfo subclass. The remaining arguments may be ints or longs.\n");

static PyNumberMethods time_as_number = {
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    (inquiry)time_nonzero,
};

statichere PyTypeObject PyDateTime_TimeType = {
    PyObject_HEAD_INIT(NULL)
    0,
    "datetime.time",
    sizeof(PyDateTime_Time),
    0,
    (destructor)time_dealloc,
    0,
    0,
    0,
    0,
    (reprfunc)time_repr,
    &time_as_number,
    0,
    0,
    (hashfunc)time_hash,
    0,
    (reprfunc)time_str,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
    Py_TPFLAGS_BASETYPE,
    time_doc,
    0,
    0,
    (richcmpfunc)time_richcompare,
    0,
    0,
    0,
    time_methods,
    0,
    time_getset,
    0,
    0,
    0,
    0,
    0,
    0,
    time_alloc,
    time_new,
    0,
};

static PyObject *
datetime_hour(PyDateTime_DateTime *self, void *unused)
{
    return PyInt_FromLong(DATE_GET_HOUR(self));
}

static PyObject *
datetime_minute(PyDateTime_DateTime *self, void *unused)
{
    return PyInt_FromLong(DATE_GET_MINUTE(self));
}

static PyObject *
datetime_second(PyDateTime_DateTime *self, void *unused)
{
    return PyInt_FromLong(DATE_GET_SECOND(self));
}

static PyObject *
datetime_microsecond(PyDateTime_DateTime *self, void *unused)
{
    return PyInt_FromLong(DATE_GET_MICROSECOND(self));
}

static PyObject *
datetime_tzinfo(PyDateTime_DateTime *self, void *unused)
{
    PyObject *result = HASTZINFO(self) ? self->tzinfo : Py_None;
    Py_INCREF(result);
    return result;
}

static PyGetSetDef datetime_getset[] = {
    {"hour",        (getter)datetime_hour},
    {"minute",      (getter)datetime_minute},
    {"second",      (getter)datetime_second},
    {"microsecond", (getter)datetime_microsecond},
    {"tzinfo",          (getter)datetime_tzinfo},
    {NULL}
};

static char *datetime_kws[] = {
    "year", "month", "day", "hour", "minute", "second",
    "microsecond", "tzinfo", NULL
};

static PyObject *
datetime_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    PyObject *self = NULL;
    PyObject *state;
    int year;
    int month;
    int day;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int usecond = 0;
    PyObject *tzinfo = Py_None;

    if (PyTuple_GET_SIZE(args) >= 1 &&
        PyTuple_GET_SIZE(args) <= 2 &&
        PyString_Check(state = PyTuple_GET_ITEM(args, 0)) &&
        PyString_GET_SIZE(state) == _PyDateTime_DATETIME_DATASIZE &&
        MONTH_IS_SANE(PyString_AS_STRING(state)[2]))
    {
        PyDateTime_DateTime *me;
        char aware;

        if (PyTuple_GET_SIZE(args) == 2) {
            tzinfo = PyTuple_GET_ITEM(args, 1);
            if (check_tzinfo_subclass(tzinfo) < 0) {
                PyErr_SetString(PyExc_TypeError, "bad "
                    "tzinfo state arg");
                return NULL;
            }
        }
        aware = (char)(tzinfo != Py_None);
        me = (PyDateTime_DateTime *) (type->tp_alloc(type , aware));
        if (me != NULL) {
            char *pdata = PyString_AS_STRING(state);

            memcpy(me->data, pdata, _PyDateTime_DATETIME_DATASIZE);
            me->hashcode = -1;
            me->hastzinfo = aware;
            if (aware) {
                Py_INCREF(tzinfo);
                me->tzinfo = tzinfo;
            }
        }
        return (PyObject *)me;
    }

    if (PyArg_ParseTupleAndKeywords(args, kw, "iii|iiiiO", datetime_kws,
                                    &year, &month, &day, &hour, &minute,
                                    &second, &usecond, &tzinfo)) {
        if (check_date_args(year, month, day) < 0)
            return NULL;
        if (check_time_args(hour, minute, second, usecond) < 0)
            return NULL;
        if (check_tzinfo_subclass(tzinfo) < 0)
            return NULL;
        self = new_datetime_ex(year, month, day,
                                hour, minute, second, usecond,
                                tzinfo, type);
    }
    return self;
}

typedef struct tm *(*TM_FUNC)(const time_t *timer);

static PyObject *
datetime_from_timet_and_us(PyObject *cls, TM_FUNC f, time_t timet, int us,
                           PyObject *tzinfo)
{
    struct tm *tm;
    PyObject *result = NULL;

    tm = f(&timet);
    if (tm) {

        if (tm->tm_sec > 59)
            tm->tm_sec = 59;
        result = PyObject_CallFunction(cls, "iiiiiiiO",
                                       tm->tm_year + 1900,
                                       tm->tm_mon + 1,
                                       tm->tm_mday,
                                       tm->tm_hour,
                                       tm->tm_min,
                                       tm->tm_sec,
                                       us,
                                       tzinfo);
    }
    else
        PyErr_SetString(PyExc_ValueError,
                        "timestamp out of range for "
                        "platform localtime()/gmtime() function");
    return result;
}

static PyObject *
datetime_from_timestamp(PyObject *cls, TM_FUNC f, double timestamp,
                        PyObject *tzinfo)
{
    time_t timet;
    double fraction;
    int us;

    timet = _PyTime_DoubleToTimet(timestamp);
    if (timet == (time_t)-1 && PyErr_Occurred())
        return NULL;
    fraction = timestamp - (double)timet;
    us = (int)round_to_long(fraction * 1e6);
    if (us < 0) {

        timet -= 1;
        us += 1000000;
    }

    if (us == 1000000) {
        timet += 1;
        us = 0;
    }
    return datetime_from_timet_and_us(cls, f, timet, us, tzinfo);
}

static PyObject *
datetime_best_possible(PyObject *cls, TM_FUNC f, PyObject *tzinfo)
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval t;

#ifdef GETTIMEOFDAY_NO_TZ
    gettimeofday(&t);
#else
    gettimeofday(&t, (struct timezone *)NULL);
#endif
    return datetime_from_timet_and_us(cls, f, t.tv_sec, (int)t.tv_usec,
                                      tzinfo);

#else

    PyObject *time;
    double dtime;

    time = time_time();
    if (time == NULL)
        return NULL;
    dtime = PyFloat_AsDouble(time);
    Py_DECREF(time);
    if (dtime == -1.0 && PyErr_Occurred())
        return NULL;
    return datetime_from_timestamp(cls, f, dtime, tzinfo);
#endif
}

static PyObject *
datetime_now(PyObject *cls, PyObject *args, PyObject *kw)
{
    PyObject *self;
    PyObject *tzinfo = Py_None;
    static char *keywords[] = {"tz", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kw, "|O:now", keywords,
                                      &tzinfo))
        return NULL;
    if (check_tzinfo_subclass(tzinfo) < 0)
        return NULL;

    self = datetime_best_possible(cls,
                                  tzinfo == Py_None ? localtime : gmtime,
                                  tzinfo);
    if (self != NULL && tzinfo != Py_None) {

        PyObject *temp = self;
        self = PyObject_CallMethod(tzinfo, "fromutc", "O", self);
        Py_DECREF(temp);
    }
    return self;
}

static PyObject *
datetime_utcnow(PyObject *cls, PyObject *dummy)
{
    return datetime_best_possible(cls, gmtime, Py_None);
}

static PyObject *
datetime_fromtimestamp(PyObject *cls, PyObject *args, PyObject *kw)
{
    PyObject *self;
    double timestamp;
    PyObject *tzinfo = Py_None;
    static char *keywords[] = {"timestamp", "tz", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kw, "d|O:fromtimestamp",
                                      keywords, &timestamp, &tzinfo))
        return NULL;
    if (check_tzinfo_subclass(tzinfo) < 0)
        return NULL;

    self = datetime_from_timestamp(cls,
                                   tzinfo == Py_None ? localtime : gmtime,
                                   timestamp,
                                   tzinfo);
    if (self != NULL && tzinfo != Py_None) {

        PyObject *temp = self;
        self = PyObject_CallMethod(tzinfo, "fromutc", "O", self);
        Py_DECREF(temp);
    }
    return self;
}

static PyObject *
datetime_utcfromtimestamp(PyObject *cls, PyObject *args)
{
    double timestamp;
    PyObject *result = NULL;

    if (PyArg_ParseTuple(args, "d:utcfromtimestamp", &timestamp))
        result = datetime_from_timestamp(cls, gmtime, timestamp,
                                         Py_None);
    return result;
}

static PyObject *
datetime_strptime(PyObject *cls, PyObject *args)
{
    static PyObject *module = NULL;
    PyObject *result = NULL, *obj, *st = NULL, *frac = NULL;
    const char *string, *format;

    if (!PyArg_ParseTuple(args, "ss:strptime", &string, &format))
        return NULL;

    if (module == NULL &&
        (module = PyImport_ImportModuleNoBlock("_strptime")) == NULL)
        return NULL;

    obj = PyObject_CallMethod(module, "_strptime", "ss", string, format);
    if (obj != NULL) {
        int i, good_timetuple = 1;
        long int ia[7];
        if (PySequence_Check(obj) && PySequence_Size(obj) == 2) {
            st = PySequence_GetItem(obj, 0);
            frac = PySequence_GetItem(obj, 1);
            if (st == NULL || frac == NULL)
                good_timetuple = 0;

            if (good_timetuple &&
                PySequence_Check(st) &&
                PySequence_Size(st) >= 6) {
                for (i=0; i < 6; i++) {
                    PyObject *p = PySequence_GetItem(st, i);
                    if (p == NULL) {
                        good_timetuple = 0;
                        break;
                    }
                    if (PyInt_Check(p))
                        ia[i] = PyInt_AsLong(p);
                    else
                        good_timetuple = 0;
                    Py_DECREF(p);
                }
            }
            else
                good_timetuple = 0;

            if (good_timetuple && PyInt_Check(frac))
                ia[6] = PyInt_AsLong(frac);
            else
                good_timetuple = 0;
        }
        else
            good_timetuple = 0;
        if (good_timetuple)
            result = PyObject_CallFunction(cls, "iiiiiii",
                                           ia[0], ia[1], ia[2],
                                           ia[3], ia[4], ia[5],
                                           ia[6]);
        else
            PyErr_SetString(PyExc_ValueError,
                "unexpected value from _strptime._strptime");
    }
    Py_XDECREF(obj);
    Py_XDECREF(st);
    Py_XDECREF(frac);
    return result;
}

static PyObject *
datetime_combine(PyObject *cls, PyObject *args, PyObject *kw)
{
    static char *keywords[] = {"date", "time", NULL};
    PyObject *date;
    PyObject *time;
    PyObject *result = NULL;

    if (PyArg_ParseTupleAndKeywords(args, kw, "O!O!:combine", keywords,
                                    &PyDateTime_DateType, &date,
                                    &PyDateTime_TimeType, &time)) {
        PyObject *tzinfo = Py_None;

        if (HASTZINFO(time))
            tzinfo = ((PyDateTime_Time *)time)->tzinfo;
        result = PyObject_CallFunction(cls, "iiiiiiiO",
                                        GET_YEAR(date),
                                        GET_MONTH(date),
                                        GET_DAY(date),
                                        TIME_GET_HOUR(time),
                                        TIME_GET_MINUTE(time),
                                        TIME_GET_SECOND(time),
                                        TIME_GET_MICROSECOND(time),
                                        tzinfo);
    }
    return result;
}

static void
datetime_dealloc(PyDateTime_DateTime *self)
{
    if (HASTZINFO(self)) {
        Py_XDECREF(self->tzinfo);
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
datetime_utcoffset(PyDateTime_DateTime *self, PyObject *unused) {
    return offset_as_timedelta(HASTZINFO(self) ? self->tzinfo : Py_None,
                               "utcoffset", (PyObject *)self);
}

static PyObject *
datetime_dst(PyDateTime_DateTime *self, PyObject *unused) {
    return offset_as_timedelta(HASTZINFO(self) ? self->tzinfo : Py_None,
                               "dst", (PyObject *)self);
}

static PyObject *
datetime_tzname(PyDateTime_DateTime *self, PyObject *unused) {
    return call_tzname(HASTZINFO(self) ? self->tzinfo : Py_None,
                       (PyObject *)self);
}

static PyObject *
add_datetime_timedelta(PyDateTime_DateTime *date, PyDateTime_Delta *delta,
                       int factor)
{

    int year = GET_YEAR(date);
    int month = GET_MONTH(date);
    int day = GET_DAY(date) + GET_TD_DAYS(delta) * factor;
    int hour = DATE_GET_HOUR(date);
    int minute = DATE_GET_MINUTE(date);
    int second = DATE_GET_SECOND(date) + GET_TD_SECONDS(delta) * factor;
    int microsecond = DATE_GET_MICROSECOND(date) +
                      GET_TD_MICROSECONDS(delta) * factor;

    assert(factor == 1 || factor == -1);
    if (normalize_datetime(&year, &month, &day,
                           &hour, &minute, &second, &microsecond) < 0)
        return NULL;
    else
        return new_datetime(year, month, day,
                            hour, minute, second, microsecond,
                            HASTZINFO(date) ? date->tzinfo : Py_None);
}

static PyObject *
datetime_add(PyObject *left, PyObject *right)
{
    if (PyDateTime_Check(left)) {

        if (PyDelta_Check(right))

            return add_datetime_timedelta(
                            (PyDateTime_DateTime *)left,
                            (PyDateTime_Delta *)right,
                            1);
    }
    else if (PyDelta_Check(left)) {

        return add_datetime_timedelta((PyDateTime_DateTime *) right,
                                      (PyDateTime_Delta *) left,
                                      1);
    }
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

static PyObject *
datetime_subtract(PyObject *left, PyObject *right)
{
    PyObject *result = Py_NotImplemented;

    if (PyDateTime_Check(left)) {

        if (PyDateTime_Check(right)) {

            naivety n1, n2;
            int offset1, offset2;
            int delta_d, delta_s, delta_us;

            if (classify_two_utcoffsets(left, &offset1, &n1, left,
                                        right, &offset2, &n2,
                                        right) < 0)
                return NULL;
            assert(n1 != OFFSET_UNKNOWN && n2 != OFFSET_UNKNOWN);
            if (n1 != n2) {
                PyErr_SetString(PyExc_TypeError,
                    "can't subtract offset-naive and "
                    "offset-aware datetimes");
                return NULL;
            }
            delta_d = ymd_to_ord(GET_YEAR(left),
                                 GET_MONTH(left),
                                 GET_DAY(left)) -
                      ymd_to_ord(GET_YEAR(right),
                                 GET_MONTH(right),
                                 GET_DAY(right));

            delta_s = (DATE_GET_HOUR(left) -
                       DATE_GET_HOUR(right)) * 3600 +
                      (DATE_GET_MINUTE(left) -
                       DATE_GET_MINUTE(right)) * 60 +
                      (DATE_GET_SECOND(left) -
                       DATE_GET_SECOND(right));
            delta_us = DATE_GET_MICROSECOND(left) -
                       DATE_GET_MICROSECOND(right);

            delta_s += (offset2 - offset1) * 60;
            result = new_delta(delta_d, delta_s, delta_us, 1);
        }
        else if (PyDelta_Check(right)) {

            result = add_datetime_timedelta(
                            (PyDateTime_DateTime *)left,
                            (PyDateTime_Delta *)right,
                            -1);
        }
    }

    if (result == Py_NotImplemented)
        Py_INCREF(result);
    return result;
}

static PyObject *
datetime_repr(PyDateTime_DateTime *self)
{
    char buffer[1000];
    const char *type_name = Py_TYPE(self)->tp_name;
    PyObject *baserepr;

    if (DATE_GET_MICROSECOND(self)) {
        PyOS_snprintf(buffer, sizeof(buffer),
                      "%s(%d, %d, %d, %d, %d, %d, %d)",
                      type_name,
                      GET_YEAR(self), GET_MONTH(self), GET_DAY(self),
                      DATE_GET_HOUR(self), DATE_GET_MINUTE(self),
                      DATE_GET_SECOND(self),
                      DATE_GET_MICROSECOND(self));
    }
    else if (DATE_GET_SECOND(self)) {
        PyOS_snprintf(buffer, sizeof(buffer),
                      "%s(%d, %d, %d, %d, %d, %d)",
                      type_name,
                      GET_YEAR(self), GET_MONTH(self), GET_DAY(self),
                      DATE_GET_HOUR(self), DATE_GET_MINUTE(self),
                      DATE_GET_SECOND(self));
    }
    else {
        PyOS_snprintf(buffer, sizeof(buffer),
                      "%s(%d, %d, %d, %d, %d)",
                      type_name,
                      GET_YEAR(self), GET_MONTH(self), GET_DAY(self),
                      DATE_GET_HOUR(self), DATE_GET_MINUTE(self));
    }
    baserepr = PyString_FromString(buffer);
    if (baserepr == NULL || ! HASTZINFO(self))
        return baserepr;
    return append_keyword_tzinfo(baserepr, self->tzinfo);
}

static PyObject *
datetime_str(PyDateTime_DateTime *self)
{
    return PyObject_CallMethod((PyObject *)self, "isoformat", "(s)", " ");
}

static PyObject *
datetime_isoformat(PyDateTime_DateTime *self, PyObject *args, PyObject *kw)
{
    char sep = 'T';
    static char *keywords[] = {"sep", NULL};
    char buffer[100];
    char *cp;
    PyObject *result;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "|c:isoformat", keywords,
                                     &sep))
        return NULL;
    cp = isoformat_date((PyDateTime_Date *)self, buffer, sizeof(buffer));
    assert(cp != NULL);
    *cp++ = sep;
    cp = isoformat_time(self, cp, sizeof(buffer) - (cp - buffer));
    result = PyString_FromStringAndSize(buffer, cp - buffer);
    if (result == NULL || ! HASTZINFO(self))
        return result;

    if (format_utcoffset(buffer, sizeof(buffer), ":", self->tzinfo,
                         (PyObject *)self) < 0) {
        Py_DECREF(result);
        return NULL;
    }
    PyString_ConcatAndDel(&result, PyString_FromString(buffer));
    return result;
}

static PyObject *
datetime_ctime(PyDateTime_DateTime *self)
{
    return format_ctime((PyDateTime_Date *)self,
                        DATE_GET_HOUR(self),
                        DATE_GET_MINUTE(self),
                        DATE_GET_SECOND(self));
}

static PyObject *
datetime_richcompare(PyDateTime_DateTime *self, PyObject *other, int op)
{
    int diff;
    naivety n1, n2;
    int offset1, offset2;

    if (! PyDateTime_Check(other)) {

        if (PyObject_HasAttrString(other, "timetuple") &&
            ! PyDate_Check(other)) {

            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        if (op == Py_EQ || op == Py_NE) {
            PyObject *result = op == Py_EQ ? Py_False : Py_True;
            Py_INCREF(result);
            return result;
        }

        return cmperror((PyObject *)self, other);
    }

    if (classify_two_utcoffsets((PyObject *)self, &offset1, &n1,
                                (PyObject *)self,
                                 other, &offset2, &n2,
                                 other) < 0)
        return NULL;
    assert(n1 != OFFSET_UNKNOWN && n2 != OFFSET_UNKNOWN);

    if (n1 == n2 && offset1 == offset2) {
        diff = memcmp(self->data, ((PyDateTime_DateTime *)other)->data,
                      _PyDateTime_DATETIME_DATASIZE);
        return diff_to_bool(diff, op);
    }

    if (n1 == OFFSET_AWARE && n2 == OFFSET_AWARE) {
        PyDateTime_Delta *delta;

        assert(offset1 != offset2);
        delta = (PyDateTime_Delta *)datetime_subtract((PyObject *)self,
                                                       other);
        if (delta == NULL)
            return NULL;
        diff = GET_TD_DAYS(delta);
        if (diff == 0)
            diff = GET_TD_SECONDS(delta) |
                   GET_TD_MICROSECONDS(delta);
        Py_DECREF(delta);
        return diff_to_bool(diff, op);
    }

    assert(n1 != n2);
    PyErr_SetString(PyExc_TypeError,
                    "can't compare offset-naive and "
                    "offset-aware datetimes");
    return NULL;
}

static long
datetime_hash(PyDateTime_DateTime *self)
{
    if (self->hashcode == -1) {
        naivety n;
        int offset;
        PyObject *temp;

        n = classify_utcoffset((PyObject *)self, (PyObject *)self,
                               &offset);
        assert(n != OFFSET_UNKNOWN);
        if (n == OFFSET_ERROR)
            return -1;

        if (n == OFFSET_NAIVE)
            temp = PyString_FromStringAndSize(
                            (char *)self->data,
                            _PyDateTime_DATETIME_DATASIZE);
        else {
            int days;
            int seconds;

            assert(n == OFFSET_AWARE);
            assert(HASTZINFO(self));
            days = ymd_to_ord(GET_YEAR(self),
                              GET_MONTH(self),
                              GET_DAY(self));
            seconds = DATE_GET_HOUR(self) * 3600 +
                      (DATE_GET_MINUTE(self) - offset) * 60 +
                      DATE_GET_SECOND(self);
            temp = new_delta(days,
                             seconds,
                             DATE_GET_MICROSECOND(self),
                             1);
        }
        if (temp != NULL) {
            self->hashcode = PyObject_Hash(temp);
            Py_DECREF(temp);
        }
    }
    return self->hashcode;
}

static PyObject *
datetime_replace(PyDateTime_DateTime *self, PyObject *args, PyObject *kw)
{
    PyObject *clone;
    PyObject *tuple;
    int y = GET_YEAR(self);
    int m = GET_MONTH(self);
    int d = GET_DAY(self);
    int hh = DATE_GET_HOUR(self);
    int mm = DATE_GET_MINUTE(self);
    int ss = DATE_GET_SECOND(self);
    int us = DATE_GET_MICROSECOND(self);
    PyObject *tzinfo = HASTZINFO(self) ? self->tzinfo : Py_None;

    if (! PyArg_ParseTupleAndKeywords(args, kw, "|iiiiiiiO:replace",
                                      datetime_kws,
                                      &y, &m, &d, &hh, &mm, &ss, &us,
                                      &tzinfo))
        return NULL;
    tuple = Py_BuildValue("iiiiiiiO", y, m, d, hh, mm, ss, us, tzinfo);
    if (tuple == NULL)
        return NULL;
    clone = datetime_new(Py_TYPE(self), tuple, NULL);
    Py_DECREF(tuple);
    return clone;
}

static PyObject *
datetime_astimezone(PyDateTime_DateTime *self, PyObject *args, PyObject *kw)
{
    int y, m, d, hh, mm, ss, us;
    PyObject *result;
    int offset, none;

    PyObject *tzinfo;
    static char *keywords[] = {"tz", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kw, "O!:astimezone", keywords,
                                      &PyDateTime_TZInfoType, &tzinfo))
        return NULL;

    if (!HASTZINFO(self) || self->tzinfo == Py_None)
        goto NeedAware;

    if (self->tzinfo == tzinfo) {
        Py_INCREF(self);
        return (PyObject *)self;
    }

    offset = call_utcoffset(self->tzinfo, (PyObject *)self, &none);
    if (offset == -1 && PyErr_Occurred())
        return NULL;
    if (none)
        goto NeedAware;

    y = GET_YEAR(self);
    m = GET_MONTH(self);
    d = GET_DAY(self);
    hh = DATE_GET_HOUR(self);
    mm = DATE_GET_MINUTE(self);
    ss = DATE_GET_SECOND(self);
    us = DATE_GET_MICROSECOND(self);

    mm -= offset;
    if ((mm < 0 || mm >= 60) &&
        normalize_datetime(&y, &m, &d, &hh, &mm, &ss, &us) < 0)
        return NULL;

    result = new_datetime(y, m, d, hh, mm, ss, us, tzinfo);
    if (result != NULL) {
        PyObject *temp = result;

        result = PyObject_CallMethod(tzinfo, "fromutc", "O", temp);
        Py_DECREF(temp);
    }
    return result;

NeedAware:
    PyErr_SetString(PyExc_ValueError, "astimezone() cannot be applied to "
                                      "a naive datetime");
    return NULL;
}

static PyObject *
datetime_timetuple(PyDateTime_DateTime *self)
{
    int dstflag = -1;

    if (HASTZINFO(self) && self->tzinfo != Py_None) {
        int none;

        dstflag = call_dst(self->tzinfo, (PyObject *)self, &none);
        if (dstflag == -1 && PyErr_Occurred())
            return NULL;

        if (none)
            dstflag = -1;
        else if (dstflag != 0)
            dstflag = 1;

    }
    return build_struct_time(GET_YEAR(self),
                             GET_MONTH(self),
                             GET_DAY(self),
                             DATE_GET_HOUR(self),
                             DATE_GET_MINUTE(self),
                             DATE_GET_SECOND(self),
                             dstflag);
}

static PyObject *
datetime_getdate(PyDateTime_DateTime *self)
{
    return new_date(GET_YEAR(self),
                    GET_MONTH(self),
                    GET_DAY(self));
}

static PyObject *
datetime_gettime(PyDateTime_DateTime *self)
{
    return new_time(DATE_GET_HOUR(self),
                    DATE_GET_MINUTE(self),
                    DATE_GET_SECOND(self),
                    DATE_GET_MICROSECOND(self),
                    Py_None);
}

static PyObject *
datetime_gettimetz(PyDateTime_DateTime *self)
{
    return new_time(DATE_GET_HOUR(self),
                    DATE_GET_MINUTE(self),
                    DATE_GET_SECOND(self),
                    DATE_GET_MICROSECOND(self),
                    HASTZINFO(self) ? self->tzinfo : Py_None);
}

static PyObject *
datetime_utctimetuple(PyDateTime_DateTime *self)
{
    int y = GET_YEAR(self);
    int m = GET_MONTH(self);
    int d = GET_DAY(self);
    int hh = DATE_GET_HOUR(self);
    int mm = DATE_GET_MINUTE(self);
    int ss = DATE_GET_SECOND(self);
    int us = 0;
    int offset = 0;

    if (HASTZINFO(self) && self->tzinfo != Py_None) {
        int none;

        offset = call_utcoffset(self->tzinfo, (PyObject *)self, &none);
        if (offset == -1 && PyErr_Occurred())
            return NULL;
    }

    if (offset) {

        int stat;

        mm -= offset;
        stat = normalize_datetime(&y, &m, &d, &hh, &mm, &ss, &us);
        if (stat < 0) {

            if (PyErr_ExceptionMatches(PyExc_OverflowError))
                PyErr_Clear();
            else
                return NULL;
        }
    }
    return build_struct_time(y, m, d, hh, mm, ss, 0);
}

static PyObject *
datetime_getstate(PyDateTime_DateTime *self)
{
    PyObject *basestate;
    PyObject *result = NULL;

    basestate = PyString_FromStringAndSize((char *)self->data,
                                      _PyDateTime_DATETIME_DATASIZE);
    if (basestate != NULL) {
        if (! HASTZINFO(self) || self->tzinfo == Py_None)
            result = PyTuple_Pack(1, basestate);
        else
            result = PyTuple_Pack(2, basestate, self->tzinfo);
        Py_DECREF(basestate);
    }
    return result;
}

static PyObject *
datetime_reduce(PyDateTime_DateTime *self, PyObject *arg)
{
    return Py_BuildValue("(ON)", Py_TYPE(self), datetime_getstate(self));
}

static PyMethodDef datetime_methods[] = {

    {"now",         (PyCFunction)datetime_now,
     METH_VARARGS | METH_KEYWORDS | METH_CLASS,
     PyDoc_STR("[tz] -> new datetime with tz's local day and time.")},

    {"utcnow",         (PyCFunction)datetime_utcnow,
     METH_NOARGS | METH_CLASS,
     PyDoc_STR("Return a new datetime representing UTC day and time.")},

    {"fromtimestamp", (PyCFunction)datetime_fromtimestamp,
     METH_VARARGS | METH_KEYWORDS | METH_CLASS,
     PyDoc_STR("timestamp[, tz] -> tz's local time from POSIX timestamp.")},

    {"utcfromtimestamp", (PyCFunction)datetime_utcfromtimestamp,
     METH_VARARGS | METH_CLASS,
     PyDoc_STR("timestamp -> UTC datetime from a POSIX timestamp "
               "(like time.time()).")},

    {"strptime", (PyCFunction)datetime_strptime,
     METH_VARARGS | METH_CLASS,
     PyDoc_STR("string, format -> new datetime parsed from a string "
               "(like time.strptime()).")},

    {"combine", (PyCFunction)datetime_combine,
     METH_VARARGS | METH_KEYWORDS | METH_CLASS,
     PyDoc_STR("date, time -> datetime with same date and time fields")},

    {"date",   (PyCFunction)datetime_getdate, METH_NOARGS,
     PyDoc_STR("Return date object with same year, month and day.")},

    {"time",   (PyCFunction)datetime_gettime, METH_NOARGS,
     PyDoc_STR("Return time object with same time but with tzinfo=None.")},

    {"timetz",   (PyCFunction)datetime_gettimetz, METH_NOARGS,
     PyDoc_STR("Return time object with same time and tzinfo.")},

    {"ctime",       (PyCFunction)datetime_ctime,        METH_NOARGS,
     PyDoc_STR("Return ctime() style string.")},

    {"timetuple",   (PyCFunction)datetime_timetuple, METH_NOARGS,
     PyDoc_STR("Return time tuple, compatible with time.localtime().")},

    {"utctimetuple",   (PyCFunction)datetime_utctimetuple, METH_NOARGS,
     PyDoc_STR("Return UTC time tuple, compatible with time.localtime().")},

    {"isoformat",   (PyCFunction)datetime_isoformat, METH_VARARGS | METH_KEYWORDS,
     PyDoc_STR("[sep] -> string in ISO 8601 format, "
               "YYYY-MM-DDTHH:MM:SS[.mmmmmm][+HH:MM].\n\n"
               "sep is used to separate the year from the time, and "
               "defaults to 'T'.")},

    {"utcoffset",       (PyCFunction)datetime_utcoffset, METH_NOARGS,
     PyDoc_STR("Return self.tzinfo.utcoffset(self).")},

    {"tzname",          (PyCFunction)datetime_tzname,   METH_NOARGS,
     PyDoc_STR("Return self.tzinfo.tzname(self).")},

    {"dst",             (PyCFunction)datetime_dst, METH_NOARGS,
     PyDoc_STR("Return self.tzinfo.dst(self).")},

    {"replace",     (PyCFunction)datetime_replace,      METH_VARARGS | METH_KEYWORDS,
     PyDoc_STR("Return datetime with new specified fields.")},

    {"astimezone",  (PyCFunction)datetime_astimezone, METH_VARARGS | METH_KEYWORDS,
     PyDoc_STR("tz -> convert to local time in new timezone tz\n")},

    {"__reduce__", (PyCFunction)datetime_reduce,     METH_NOARGS,
     PyDoc_STR("__reduce__() -> (cls, state)")},

    {NULL,      NULL}
};

static char datetime_doc[] =
PyDoc_STR("datetime(year, month, day[, hour[, minute[, second[, microsecond[,tzinfo]]]]])\n\
\n\
The year, month and day arguments are required. tzinfo may be None, or an\n\
instance of a tzinfo subclass. The remaining arguments may be ints or longs.\n");

static PyNumberMethods datetime_as_number = {
    datetime_add,
    datetime_subtract,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

statichere PyTypeObject PyDateTime_DateTimeType = {
    PyObject_HEAD_INIT(NULL)
    0,
    "datetime.datetime",
    sizeof(PyDateTime_DateTime),
    0,
    (destructor)datetime_dealloc,
    0,
    0,
    0,
    0,
    (reprfunc)datetime_repr,
    &datetime_as_number,
    0,
    0,
    (hashfunc)datetime_hash,
    0,
    (reprfunc)datetime_str,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
    Py_TPFLAGS_BASETYPE,
    datetime_doc,
    0,
    0,
    (richcmpfunc)datetime_richcompare,
    0,
    0,
    0,
    datetime_methods,
    0,
    datetime_getset,
    &PyDateTime_DateType,
    0,
    0,
    0,
    0,
    0,
    datetime_alloc,
    datetime_new,
    0,
};

static PyMethodDef module_methods[] = {
    {NULL, NULL}
};

static PyDateTime_CAPI CAPI = {
    &PyDateTime_DateType,
    &PyDateTime_DateTimeType,
    &PyDateTime_TimeType,
    &PyDateTime_DeltaType,
    &PyDateTime_TZInfoType,
    new_date_ex,
    new_datetime_ex,
    new_time_ex,
    new_delta_ex,
    datetime_fromtimestamp,
    date_fromtimestamp
};

PyMODINIT_FUNC
initdatetime(void)
{
    PyObject *m;
    PyObject *d;
    PyObject *x;

    m = Py_InitModule3("datetime", module_methods,
                       "Fast implementation of the datetime type.");
    if (m == NULL)
        return;

    if (PyType_Ready(&PyDateTime_DateType) < 0)
        return;
    if (PyType_Ready(&PyDateTime_DateTimeType) < 0)
        return;
    if (PyType_Ready(&PyDateTime_DeltaType) < 0)
        return;
    if (PyType_Ready(&PyDateTime_TimeType) < 0)
        return;
    if (PyType_Ready(&PyDateTime_TZInfoType) < 0)
        return;

    d = PyDateTime_DeltaType.tp_dict;

    x = new_delta(0, 0, 1, 0);
    if (x == NULL || PyDict_SetItemString(d, "resolution", x) < 0)
        return;
    Py_DECREF(x);

    x = new_delta(-MAX_DELTA_DAYS, 0, 0, 0);
    if (x == NULL || PyDict_SetItemString(d, "min", x) < 0)
        return;
    Py_DECREF(x);

    x = new_delta(MAX_DELTA_DAYS, 24*3600-1, 1000000-1, 0);
    if (x == NULL || PyDict_SetItemString(d, "max", x) < 0)
        return;
    Py_DECREF(x);

    d = PyDateTime_DateType.tp_dict;

    x = new_date(1, 1, 1);
    if (x == NULL || PyDict_SetItemString(d, "min", x) < 0)
        return;
    Py_DECREF(x);

    x = new_date(MAXYEAR, 12, 31);
    if (x == NULL || PyDict_SetItemString(d, "max", x) < 0)
        return;
    Py_DECREF(x);

    x = new_delta(1, 0, 0, 0);
    if (x == NULL || PyDict_SetItemString(d, "resolution", x) < 0)
        return;
    Py_DECREF(x);

    d = PyDateTime_TimeType.tp_dict;

    x = new_time(0, 0, 0, 0, Py_None);
    if (x == NULL || PyDict_SetItemString(d, "min", x) < 0)
        return;
    Py_DECREF(x);

    x = new_time(23, 59, 59, 999999, Py_None);
    if (x == NULL || PyDict_SetItemString(d, "max", x) < 0)
        return;
    Py_DECREF(x);

    x = new_delta(0, 0, 1, 0);
    if (x == NULL || PyDict_SetItemString(d, "resolution", x) < 0)
        return;
    Py_DECREF(x);

    d = PyDateTime_DateTimeType.tp_dict;

    x = new_datetime(1, 1, 1, 0, 0, 0, 0, Py_None);
    if (x == NULL || PyDict_SetItemString(d, "min", x) < 0)
        return;
    Py_DECREF(x);

    x = new_datetime(MAXYEAR, 12, 31, 23, 59, 59, 999999, Py_None);
    if (x == NULL || PyDict_SetItemString(d, "max", x) < 0)
        return;
    Py_DECREF(x);

    x = new_delta(0, 0, 1, 0);
    if (x == NULL || PyDict_SetItemString(d, "resolution", x) < 0)
        return;
    Py_DECREF(x);

    PyModule_AddIntConstant(m, "MINYEAR", MINYEAR);
    PyModule_AddIntConstant(m, "MAXYEAR", MAXYEAR);

    Py_INCREF(&PyDateTime_DateType);
    PyModule_AddObject(m, "date", (PyObject *) &PyDateTime_DateType);

    Py_INCREF(&PyDateTime_DateTimeType);
    PyModule_AddObject(m, "datetime",
                       (PyObject *)&PyDateTime_DateTimeType);

    Py_INCREF(&PyDateTime_TimeType);
    PyModule_AddObject(m, "time", (PyObject *) &PyDateTime_TimeType);

    Py_INCREF(&PyDateTime_DeltaType);
    PyModule_AddObject(m, "timedelta", (PyObject *) &PyDateTime_DeltaType);

    Py_INCREF(&PyDateTime_TZInfoType);
    PyModule_AddObject(m, "tzinfo", (PyObject *) &PyDateTime_TZInfoType);

    x = PyCapsule_New(&CAPI, PyDateTime_CAPSULE_NAME, NULL);
    if (x == NULL)
        return;
    PyModule_AddObject(m, "datetime_CAPI", x);

    assert(DI4Y == 4 * 365 + 1);
    assert(DI4Y == days_before_year(4+1));

    assert(DI400Y == 4 * DI100Y + 1);
    assert(DI400Y == days_before_year(400+1));

    assert(DI100Y == 25 * DI4Y - 1);
    assert(DI100Y == days_before_year(100+1));

    us_per_us = PyInt_FromLong(1);
    us_per_ms = PyInt_FromLong(1000);
    us_per_second = PyInt_FromLong(1000000);
    us_per_minute = PyInt_FromLong(60000000);
    seconds_per_day = PyInt_FromLong(24 * 3600);
    if (us_per_us == NULL || us_per_ms == NULL || us_per_second == NULL ||
        us_per_minute == NULL || seconds_per_day == NULL)
        return;

    us_per_hour = PyLong_FromDouble(3600000000.0);
    us_per_day = PyLong_FromDouble(86400000000.0);
    us_per_week = PyLong_FromDouble(604800000000.0);
    if (us_per_hour == NULL || us_per_day == NULL || us_per_week == NULL)
        return;
}
