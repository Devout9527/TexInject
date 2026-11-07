#include "Python.h"
#include "structseq.h"
#include "timefuncs.h"

#ifdef __APPLE__
#if defined(HAVE_GETTIMEOFDAY) && defined(HAVE_FTIME)

# undef HAVE_FTIME
#endif
#endif

#include <ctype.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef QUICKWIN
#include <io.h>
#endif

#ifdef HAVE_FTIME
#include <sys/timeb.h>
#if !defined(MS_WINDOWS) && !defined(PYOS_OS2)
extern int ftime(struct timeb *);
#endif
#endif

#if defined(__WATCOMC__) && !defined(__QNX__)
#include <i86.h>
#else
#ifdef MS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "pythread.h"

static HANDLE hInterruptEvent = NULL;
static BOOL WINAPI PyCtrlHandler(DWORD dwCtrlType)
{
    SetEvent(hInterruptEvent);

    return FALSE;
}
static long main_thread;

#if defined(__BORLANDC__)

#define timezone _timezone
#define tzname _tzname
#define daylight _daylight
#endif
#endif
#endif

#if defined(MS_WINDOWS) && !defined(__BORLANDC__)

#undef HAVE_CLOCK
#endif

#if defined(PYOS_OS2)
#define INCL_DOS
#define INCL_ERRORS
#include <os2.h>
#endif

#if defined(PYCC_VACPP)
#include <sys/time.h>
#endif

#ifdef __BEOS__
#include <time.h>

#include <support/SupportDefs.h>
#include <kernel/OS.h>
#endif

#ifdef RISCOS
extern int riscos_sleep(double);
#endif

static int floatsleep(double);
static double floattime(void);

static PyObject *moddict = NULL;

time_t
_PyTime_DoubleToTimet(double x)
{
    time_t result;
    double diff;

    result = (time_t)x;

    diff = x - (double)result;
    if (diff <= -1.0 || diff >= 1.0) {
        PyErr_SetString(PyExc_ValueError,
                        "timestamp out of range for platform time_t");
        result = (time_t)-1;
    }
    return result;
}

static PyObject *
time_time(PyObject *self, PyObject *unused)
{
    double secs;
    secs = floattime();
    if (secs == 0.0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }
    return PyFloat_FromDouble(secs);
}

PyDoc_STRVAR(time_doc,
"time() -> floating point number\n\
\n\
Return the current time in seconds since the Epoch.\n\
Fractions of a second may be present if the system clock provides them.");

#ifdef HAVE_CLOCK

#ifndef CLOCKS_PER_SEC
#ifdef CLK_TCK
#define CLOCKS_PER_SEC CLK_TCK
#else
#define CLOCKS_PER_SEC 1000000
#endif
#endif

static PyObject *
time_clock(PyObject *self, PyObject *unused)
{
    return PyFloat_FromDouble(((double)clock()) / CLOCKS_PER_SEC);
}
#endif

#if defined(MS_WINDOWS) && !defined(__BORLANDC__)

static PyObject *
time_clock(PyObject *self, PyObject *unused)
{
    static LARGE_INTEGER ctrStart;
    static double divisor = 0.0;
    LARGE_INTEGER now;
    double diff;

    if (divisor == 0.0) {
        LARGE_INTEGER freq;
        QueryPerformanceCounter(&ctrStart);
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {

            return PyFloat_FromDouble(((double)clock()) /
                                      CLOCKS_PER_SEC);
        }
        divisor = (double)freq.QuadPart;
    }
    QueryPerformanceCounter(&now);
    diff = (double)(now.QuadPart - ctrStart.QuadPart);
    return PyFloat_FromDouble(diff / divisor);
}

#define HAVE_CLOCK
#endif

#ifdef HAVE_CLOCK
PyDoc_STRVAR(clock_doc,
"clock() -> floating point number\n\
\n\
Return the CPU time or real time since the start of the process or since\n\
the first call to clock().  This has as much precision as the system\n\
records.");
#endif

static PyObject *
time_sleep(PyObject *self, PyObject *args)
{
    double secs;
    if (!PyArg_ParseTuple(args, "d:sleep", &secs))
        return NULL;
    if (floatsleep(secs) != 0)
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(sleep_doc,
"sleep(seconds)\n\
\n\
Delay execution for a given number of seconds.  The argument may be\n\
a floating point number for subsecond precision.");

static PyStructSequence_Field struct_time_type_fields[] = {
    {"tm_year", "year, for example, 1993"},
    {"tm_mon", "month of year, range [1, 12]"},
    {"tm_mday", "day of month, range [1, 31]"},
    {"tm_hour", "hours, range [0, 23]"},
    {"tm_min", "minutes, range [0, 59]"},
    {"tm_sec", "seconds, range [0, 61])"},
    {"tm_wday", "day of week, range [0, 6], Monday is 0"},
    {"tm_yday", "day of year, range [1, 366]"},
    {"tm_isdst", "1 if summer time is in effect, 0 if not, and -1 if unknown"},
    {0}
};

static PyStructSequence_Desc struct_time_type_desc = {
    "time.struct_time",
    "The time value as returned by gmtime(), localtime(), and strptime(), and\n"
    " accepted by asctime(), mktime() and strftime().  May be considered as a\n"
    " sequence of 9 integers.\n\n"
    " Note that several fields' values are not the same as those defined by\n"
    " the C language standard for struct tm.  For example, the value of the\n"
    " field tm_year is the actual year, not year - 1900.  See individual\n"
    " fields' descriptions for details.",
    struct_time_type_fields,
    9,
};

static int initialized;
static PyTypeObject StructTimeType;

static PyObject *
tmtotuple(struct tm *p)
{
    PyObject *v = PyStructSequence_New(&StructTimeType);
    if (v == NULL)
        return NULL;

#define SET(i,val) PyStructSequence_SET_ITEM(v, i, PyInt_FromLong((long) val))

    SET(0, p->tm_year + 1900);
    SET(1, p->tm_mon + 1);
    SET(2, p->tm_mday);
    SET(3, p->tm_hour);
    SET(4, p->tm_min);
    SET(5, p->tm_sec);
    SET(6, (p->tm_wday + 6) % 7);
    SET(7, p->tm_yday + 1);
    SET(8, p->tm_isdst);
#undef SET
    if (PyErr_Occurred()) {
        Py_XDECREF(v);
        return NULL;
    }

    return v;
}

static PyObject *
time_convert(double when, struct tm * (*function)(const time_t *))
{
    struct tm *p;
    time_t whent = _PyTime_DoubleToTimet(when);

    if (whent == (time_t)-1 && PyErr_Occurred())
        return NULL;
    errno = 0;
    p = function(&whent);
    if (p == NULL) {
#ifdef EINVAL
        if (errno == 0)
            errno = EINVAL;
#endif
        return PyErr_SetFromErrno(PyExc_ValueError);
    }
    return tmtotuple(p);
}

static int
parse_time_double_args(PyObject *args, char *format, double *pwhen)
{
    PyObject *ot = NULL;

    if (!PyArg_ParseTuple(args, format, &ot))
        return 0;
    if (ot == NULL || ot == Py_None)
        *pwhen = floattime();
    else {
        double when = PyFloat_AsDouble(ot);
        if (PyErr_Occurred())
            return 0;
        *pwhen = when;
    }
    return 1;
}

static PyObject *
time_gmtime(PyObject *self, PyObject *args)
{
    double when;
    if (!parse_time_double_args(args, "|O:gmtime", &when))
        return NULL;
    return time_convert(when, gmtime);
}

PyDoc_STRVAR(gmtime_doc,
"gmtime([seconds]) -> (tm_year, tm_mon, tm_mday, tm_hour, tm_min,\n\
                       tm_sec, tm_wday, tm_yday, tm_isdst)\n\
\n\
Convert seconds since the Epoch to a time tuple expressing UTC (a.k.a.\n\
GMT).  When 'seconds' is not passed in, convert the current time instead.");

static PyObject *
time_localtime(PyObject *self, PyObject *args)
{
    double when;
    if (!parse_time_double_args(args, "|O:localtime", &when))
        return NULL;
    return time_convert(when, localtime);
}

PyDoc_STRVAR(localtime_doc,
"localtime([seconds]) -> (tm_year,tm_mon,tm_mday,tm_hour,tm_min,\n\
                          tm_sec,tm_wday,tm_yday,tm_isdst)\n\
\n\
Convert seconds since the Epoch to a time tuple expressing local time.\n\
When 'seconds' is not passed in, convert the current time instead.");

static int
gettmarg(PyObject *args, struct tm *p)
{
    int y;
    memset((void *) p, '\0', sizeof(struct tm));

    if (!PyArg_Parse(args, "(iiiiiiiii)",
                     &y,
                     &p->tm_mon,
                     &p->tm_mday,
                     &p->tm_hour,
                     &p->tm_min,
                     &p->tm_sec,
                     &p->tm_wday,
                     &p->tm_yday,
                     &p->tm_isdst))
        return 0;
    if (y < 1900) {
        PyObject *accept = PyDict_GetItemString(moddict,
                                                "accept2dyear");
        if (accept == NULL || !PyInt_Check(accept) ||
            PyInt_AsLong(accept) == 0) {
            PyErr_SetString(PyExc_ValueError,
                            "year >= 1900 required");
            return 0;
        }
        if (69 <= y && y <= 99)
            y += 1900;
        else if (0 <= y && y <= 68)
            y += 2000;
        else {
            PyErr_SetString(PyExc_ValueError,
                            "year out of range");
            return 0;
        }
    }
    p->tm_year = y - 1900;
    p->tm_mon--;
    p->tm_wday = (p->tm_wday + 1) % 7;
    p->tm_yday--;
    return 1;
}

#ifdef HAVE_STRFTIME
static PyObject *
time_strftime(PyObject *self, PyObject *args)
{
    PyObject *tup = NULL;
    struct tm buf;
    const char *fmt;
    size_t fmtlen, buflen;
    char *outbuf = 0;
    size_t i;

    memset((void *) &buf, '\0', sizeof(buf));

    if (!PyArg_ParseTuple(args, "s|O:strftime", &fmt, &tup))
        return NULL;

    if (tup == NULL) {
        time_t tt = time(NULL);
        buf = *localtime(&tt);
    } else if (!gettmarg(tup, &buf))
        return NULL;

    if (buf.tm_mon == -1)
        buf.tm_mon = 0;
    else if (buf.tm_mon < 0 || buf.tm_mon > 11) {
        PyErr_SetString(PyExc_ValueError, "month out of range");
            return NULL;
    }
    if (buf.tm_mday == 0)
        buf.tm_mday = 1;
    else if (buf.tm_mday < 0 || buf.tm_mday > 31) {
        PyErr_SetString(PyExc_ValueError, "day of month out of range");
            return NULL;
    }
    if (buf.tm_hour < 0 || buf.tm_hour > 23) {
        PyErr_SetString(PyExc_ValueError, "hour out of range");
        return NULL;
    }
    if (buf.tm_min < 0 || buf.tm_min > 59) {
        PyErr_SetString(PyExc_ValueError, "minute out of range");
        return NULL;
    }
    if (buf.tm_sec < 0 || buf.tm_sec > 61) {
        PyErr_SetString(PyExc_ValueError, "seconds out of range");
        return NULL;
    }

    if (buf.tm_wday < 0) {
        PyErr_SetString(PyExc_ValueError, "day of week out of range");
        return NULL;
    }
    if (buf.tm_yday == -1)
        buf.tm_yday = 0;
    else if (buf.tm_yday < 0 || buf.tm_yday > 365) {
        PyErr_SetString(PyExc_ValueError, "day of year out of range");
        return NULL;
    }

    if (buf.tm_isdst < -1)
        buf.tm_isdst = -1;
    else if (buf.tm_isdst > 1)
        buf.tm_isdst = 1;

#ifdef MS_WINDOWS

    for(outbuf = strchr(fmt, '%');
        outbuf != NULL;
        outbuf = strchr(outbuf+2, '%'))
    {
        if (outbuf[1]=='#')
            ++outbuf;
        if (outbuf[1]=='\0' ||
            !strchr("aAbBcdHIjmMpSUwWxXyYzZ%", outbuf[1]))
        {
            PyErr_SetString(PyExc_ValueError, "Invalid format string");
            return 0;
        }
    }
#endif

    fmtlen = strlen(fmt);

    for (i = 1024; ; i += i) {
        outbuf = (char *)malloc(i);
        if (outbuf == NULL) {
            return PyErr_NoMemory();
        }
        buflen = strftime(outbuf, i, fmt, &buf);
        if (buflen > 0 || i >= 256 * fmtlen) {

            PyObject *ret;
            ret = PyString_FromStringAndSize(outbuf, buflen);
            free(outbuf);
            return ret;
        }
        free(outbuf);
#if defined _MSC_VER && _MSC_VER >= 1400 && defined(__STDC_SECURE_LIB__)

        if (buflen == 0 && errno == EINVAL) {
            PyErr_SetString(PyExc_ValueError, "Invalid format string");
            return 0;
        }
#endif

    }
}

PyDoc_STRVAR(strftime_doc,
"strftime(format[, tuple]) -> string\n\
\n\
Convert a time tuple to a string according to a format specification.\n\
See the library reference manual for formatting codes. When the time tuple\n\
is not present, current time as returned by localtime() is used.");
#endif

static PyObject *
time_strptime(PyObject *self, PyObject *args)
{
    PyObject *strptime_module = PyImport_ImportModuleNoBlock("_strptime");
    PyObject *strptime_result;

    if (!strptime_module)
        return NULL;
    strptime_result = PyObject_CallMethod(strptime_module,
                                            "_strptime_time", "O", args);
    Py_DECREF(strptime_module);
    return strptime_result;
}

PyDoc_STRVAR(strptime_doc,
"strptime(string, format) -> struct_time\n\
\n\
Parse a string to a time tuple according to a format specification.\n\
See the library reference manual for formatting codes (same as strftime()).");

static PyObject *
time_asctime(PyObject *self, PyObject *args)
{
    PyObject *tup = NULL;
    struct tm buf;
    char *p;
    if (!PyArg_UnpackTuple(args, "asctime", 0, 1, &tup))
        return NULL;
    if (tup == NULL) {
        time_t tt = time(NULL);
        buf = *localtime(&tt);
    } else if (!gettmarg(tup, &buf))
        return NULL;
    p = asctime(&buf);
    if (p == NULL) {
        PyErr_SetString(PyExc_ValueError, "invalid time");
        return NULL;
    }
    if (p[24] == '\n')
        p[24] = '\0';
    return PyString_FromString(p);
}

PyDoc_STRVAR(asctime_doc,
"asctime([tuple]) -> string\n\
\n\
Convert a time tuple to a string, e.g. 'Sat Jun 06 16:26:11 1998'.\n\
When the time tuple is not present, current time as returned by localtime()\n\
is used.");

static PyObject *
time_ctime(PyObject *self, PyObject *args)
{
    PyObject *ot = NULL;
    time_t tt;
    char *p;

    if (!PyArg_UnpackTuple(args, "ctime", 0, 1, &ot))
        return NULL;
    if (ot == NULL || ot == Py_None)
        tt = time(NULL);
    else {
        double dt = PyFloat_AsDouble(ot);
        if (PyErr_Occurred())
            return NULL;
        tt = _PyTime_DoubleToTimet(dt);
        if (tt == (time_t)-1 && PyErr_Occurred())
            return NULL;
    }
    p = ctime(&tt);
    if (p == NULL) {
        PyErr_SetString(PyExc_ValueError, "unconvertible time");
        return NULL;
    }
    if (p[24] == '\n')
        p[24] = '\0';
    return PyString_FromString(p);
}

PyDoc_STRVAR(ctime_doc,
"ctime(seconds) -> string\n\
\n\
Convert a time in seconds since the Epoch to a string in local time.\n\
This is equivalent to asctime(localtime(seconds)). When the time tuple is\n\
not present, current time as returned by localtime() is used.");

#ifdef HAVE_MKTIME
static PyObject *
time_mktime(PyObject *self, PyObject *tup)
{
    struct tm buf;
    time_t tt;
    if (!gettmarg(tup, &buf))
        return NULL;
    buf.tm_wday = -1;
    tt = mktime(&buf);

    if (tt == (time_t)(-1) && buf.tm_wday == -1) {
        PyErr_SetString(PyExc_OverflowError,
                        "mktime argument out of range");
        return NULL;
    }
    return PyFloat_FromDouble((double)tt);
}

PyDoc_STRVAR(mktime_doc,
"mktime(tuple) -> floating point number\n\
\n\
Convert a time tuple in local time to seconds since the Epoch.");
#endif

#ifdef HAVE_WORKING_TZSET
static void inittimezone(PyObject *module);

static PyObject *
time_tzset(PyObject *self, PyObject *unused)
{
    PyObject* m;

    m = PyImport_ImportModuleNoBlock("time");
    if (m == NULL) {
        return NULL;
    }

    tzset();

    inittimezone(m);
    Py_DECREF(m);

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(tzset_doc,
"tzset()\n\
\n\
Initialize, or reinitialize, the local timezone to the value stored in\n\
os.environ['TZ']. The TZ environment variable should be specified in\n\
standard Unix timezone format as documented in the tzset man page\n\
(eg. 'US/Eastern', 'Europe/Amsterdam'). Unknown timezones will silently\n\
fall back to UTC. If the TZ environment variable is not set, the local\n\
timezone is set to the systems best guess of wallclock time.\n\
Changing the TZ environment variable without calling tzset *may* change\n\
the local timezone used by methods such as localtime, but this behaviour\n\
should not be relied on.");
#endif

static void
inittimezone(PyObject *m) {

#if defined(HAVE_TZNAME) && !defined(__GLIBC__) && !defined(__CYGWIN__)
    tzset();
#ifdef PYOS_OS2
    PyModule_AddIntConstant(m, "timezone", _timezone);
#else
    PyModule_AddIntConstant(m, "timezone", timezone);
#endif
#ifdef HAVE_ALTZONE
    PyModule_AddIntConstant(m, "altzone", altzone);
#else
#ifdef PYOS_OS2
    PyModule_AddIntConstant(m, "altzone", _timezone-3600);
#else
    PyModule_AddIntConstant(m, "altzone", timezone-3600);
#endif
#endif
    PyModule_AddIntConstant(m, "daylight", daylight);
    PyModule_AddObject(m, "tzname",
                       Py_BuildValue("(zz)", tzname[0], tzname[1]));
#else
#ifdef HAVE_STRUCT_TM_TM_ZONE
    {
#define YEAR ((time_t)((365 * 24 + 6) * 3600))
        time_t t;
        struct tm *p;
        long janzone, julyzone;
        char janname[10], julyname[10];
        t = (time((time_t *)0) / YEAR) * YEAR;
        p = localtime(&t);
        janzone = -p->tm_gmtoff;
        strncpy(janname, p->tm_zone ? p->tm_zone : "   ", 9);
        janname[9] = '\0';
        t += YEAR/2;
        p = localtime(&t);
        julyzone = -p->tm_gmtoff;
        strncpy(julyname, p->tm_zone ? p->tm_zone : "   ", 9);
        julyname[9] = '\0';

        if( janzone < julyzone ) {

            PyModule_AddIntConstant(m, "timezone", julyzone);
            PyModule_AddIntConstant(m, "altzone", janzone);
            PyModule_AddIntConstant(m, "daylight",
                                    janzone != julyzone);
            PyModule_AddObject(m, "tzname",
                               Py_BuildValue("(zz)",
                                             julyname, janname));
        } else {
            PyModule_AddIntConstant(m, "timezone", janzone);
            PyModule_AddIntConstant(m, "altzone", julyzone);
            PyModule_AddIntConstant(m, "daylight",
                                    janzone != julyzone);
            PyModule_AddObject(m, "tzname",
                               Py_BuildValue("(zz)",
                                             janname, julyname));
        }
    }
#else
#endif
#ifdef __CYGWIN__
    tzset();
    PyModule_AddIntConstant(m, "timezone", _timezone);
    PyModule_AddIntConstant(m, "altzone", _timezone-3600);
    PyModule_AddIntConstant(m, "daylight", _daylight);
    PyModule_AddObject(m, "tzname",
                       Py_BuildValue("(zz)", _tzname[0], _tzname[1]));
#endif
#endif
}

static PyMethodDef time_methods[] = {
    {"time",            time_time, METH_NOARGS, time_doc},
#ifdef HAVE_CLOCK
    {"clock",           time_clock, METH_NOARGS, clock_doc},
#endif
    {"sleep",           time_sleep, METH_VARARGS, sleep_doc},
    {"gmtime",          time_gmtime, METH_VARARGS, gmtime_doc},
    {"localtime",       time_localtime, METH_VARARGS, localtime_doc},
    {"asctime",         time_asctime, METH_VARARGS, asctime_doc},
    {"ctime",           time_ctime, METH_VARARGS, ctime_doc},
#ifdef HAVE_MKTIME
    {"mktime",          time_mktime, METH_O, mktime_doc},
#endif
#ifdef HAVE_STRFTIME
    {"strftime",        time_strftime, METH_VARARGS, strftime_doc},
#endif
    {"strptime",        time_strptime, METH_VARARGS, strptime_doc},
#ifdef HAVE_WORKING_TZSET
    {"tzset",           time_tzset, METH_NOARGS, tzset_doc},
#endif
    {NULL,              NULL}
};

PyDoc_STRVAR(module_doc,
"This module provides various functions to manipulate time values.\n\
\n\
There are two standard representations of time.  One is the number\n\
of seconds since the Epoch, in UTC (a.k.a. GMT).  It may be an integer\n\
or a floating point number (to represent fractions of seconds).\n\
The Epoch is system-defined; on Unix, it is generally January 1st, 1970.\n\
The actual value can be retrieved by calling gmtime(0).\n\
\n\
The other representation is a tuple of 9 integers giving local time.\n\
The tuple items are:\n\
  year (four digits, e.g. 1998)\n\
  month (1-12)\n\
  day (1-31)\n\
  hours (0-23)\n\
  minutes (0-59)\n\
  seconds (0-59)\n\
  weekday (0-6, Monday is 0)\n\
  Julian day (day in the year, 1-366)\n\
  DST (Daylight Savings Time) flag (-1, 0 or 1)\n\
If the DST flag is 0, the time is given in the regular time zone;\n\
if it is 1, the time is given in the DST time zone;\n\
if it is -1, mktime() should guess based on the date and time.\n\
\n\
Variables:\n\
\n\
timezone -- difference in seconds between UTC and local standard time\n\
altzone -- difference in  seconds between UTC and local DST time\n\
daylight -- whether local time should reflect DST\n\
tzname -- tuple of (standard time zone name, DST time zone name)\n\
\n\
Functions:\n\
\n\
time() -- return current time in seconds since the Epoch as a float\n\
clock() -- return CPU time since process start as a float\n\
sleep() -- delay for a number of seconds given as a float\n\
gmtime() -- convert seconds since Epoch to UTC tuple\n\
localtime() -- convert seconds since Epoch to local time tuple\n\
asctime() -- convert time tuple to string\n\
ctime() -- convert time in seconds to string\n\
mktime() -- convert local time tuple to seconds since Epoch\n\
strftime() -- convert time tuple to string according to format specification\n\
strptime() -- parse string to time tuple according to format specification\n\
tzset() -- change the local timezone");

PyMODINIT_FUNC
inittime(void)
{
    PyObject *m;
    char *p;
    m = Py_InitModule3("time", time_methods, module_doc);
    if (m == NULL)
        return;

    p = Py_GETENV("PYTHONY2K");
    PyModule_AddIntConstant(m, "accept2dyear", (long) (!p || !*p));

    Py_XDECREF(moddict);

    moddict = PyModule_GetDict(m);
    Py_INCREF(moddict);

    inittimezone(m);

#ifdef MS_WINDOWS

    main_thread = PyThread_get_thread_ident();
    hInterruptEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    SetConsoleCtrlHandler( PyCtrlHandler, TRUE);
#endif
    if (!initialized) {
        PyStructSequence_InitType(&StructTimeType,
                                  &struct_time_type_desc);
    }
    Py_INCREF(&StructTimeType);
    PyModule_AddObject(m, "struct_time", (PyObject*) &StructTimeType);
    initialized = 1;
}

static double
floattime(void)
{

#ifdef HAVE_GETTIMEOFDAY
    {
        struct timeval t;
#ifdef GETTIMEOFDAY_NO_TZ
        if (gettimeofday(&t) == 0)
            return (double)t.tv_sec + t.tv_usec*0.000001;
#else
        if (gettimeofday(&t, (struct timezone *)NULL) == 0)
            return (double)t.tv_sec + t.tv_usec*0.000001;
#endif
    }

#endif
    {
#if defined(HAVE_FTIME)
        struct timeb t;
        ftime(&t);
        return (double)t.time + (double)t.millitm * (double)0.001;
#else
        time_t secs;
        time(&secs);
        return (double)secs;
#endif
    }
}

static int
floatsleep(double secs)
{

#if defined(HAVE_SELECT) && !defined(__BEOS__) && !defined(__EMX__)
    struct timeval t;
    double frac;
    frac = fmod(secs, 1.0);
    secs = floor(secs);
    t.tv_sec = (long)secs;
    t.tv_usec = (long)(frac*1000000.0);
    Py_BEGIN_ALLOW_THREADS
    if (select(0, (fd_set *)0, (fd_set *)0, (fd_set *)0, &t) != 0) {
#ifdef EINTR
        if (errno != EINTR) {
#else
        if (1) {
#endif
            Py_BLOCK_THREADS
            PyErr_SetFromErrno(PyExc_IOError);
            return -1;
        }
    }
    Py_END_ALLOW_THREADS
#elif defined(__WATCOMC__) && !defined(__QNX__)

    Py_BEGIN_ALLOW_THREADS
    delay((int)(secs * 1000 + 0.5));
    Py_END_ALLOW_THREADS
#elif defined(MS_WINDOWS)
    {
        double millisecs = secs * 1000.0;
        unsigned long ul_millis;

        if (millisecs > (double)ULONG_MAX) {
            PyErr_SetString(PyExc_OverflowError,
                            "sleep length is too large");
            return -1;
        }
        Py_BEGIN_ALLOW_THREADS

        ul_millis = (unsigned long)millisecs;
        if (ul_millis == 0 ||
            main_thread != PyThread_get_thread_ident())
            Sleep(ul_millis);
        else {
            DWORD rc;
            ResetEvent(hInterruptEvent);
            rc = WaitForSingleObject(hInterruptEvent, ul_millis);
            if (rc == WAIT_OBJECT_0) {

                Sleep(1);
                Py_BLOCK_THREADS
                errno = EINTR;
                PyErr_SetFromErrno(PyExc_IOError);
                return -1;
            }
        }
        Py_END_ALLOW_THREADS
    }
#elif defined(PYOS_OS2)

    Py_BEGIN_ALLOW_THREADS
    if (DosSleep(secs * 1000) != NO_ERROR) {
        Py_BLOCK_THREADS
        PyErr_SetFromErrno(PyExc_IOError);
        return -1;
    }
    Py_END_ALLOW_THREADS
#elif defined(__BEOS__)

    {
        if( secs <= 0.0 ) {
            return;
        }

        Py_BEGIN_ALLOW_THREADS

        if( snooze( (bigtime_t)( secs * 1000.0 * 1000.0 ) ) == B_INTERRUPTED ) {
            Py_BLOCK_THREADS
            PyErr_SetFromErrno( PyExc_IOError );
            return -1;
        }
        Py_END_ALLOW_THREADS
    }
#elif defined(RISCOS)
    if (secs <= 0.0)
        return 0;
    Py_BEGIN_ALLOW_THREADS

    if ( riscos_sleep(secs) )
        return -1;
    Py_END_ALLOW_THREADS
#elif defined(PLAN9)
    {
        double millisecs = secs * 1000.0;
        if (millisecs > (double)LONG_MAX) {
            PyErr_SetString(PyExc_OverflowError, "sleep length is too large");
            return -1;
        }

        Py_BEGIN_ALLOW_THREADS
        if(sleep((long)millisecs) < 0){
            Py_BLOCK_THREADS
            PyErr_SetFromErrno(PyExc_IOError);
            return -1;
        }
        Py_END_ALLOW_THREADS
    }
#else

    Py_BEGIN_ALLOW_THREADS
    sleep((int)secs);
    Py_END_ALLOW_THREADS
#endif

    return 0;
}

PyAPI_FUNC(double)
_PyTime_FloatTime(void)
{
    return floattime();
}
