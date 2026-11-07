#include "Python.h"
#include <ctype.h>
#include <float.h>

static PyObject *int_int(PyIntObject *v);

long
PyInt_GetMax(void)
{
    return LONG_MAX;
}

#define BLOCK_SIZE      1000
#define BHEAD_SIZE      8
#define N_INTOBJECTS    ((BLOCK_SIZE - BHEAD_SIZE) / sizeof(PyIntObject))

struct _intblock {
    struct _intblock *next;
    PyIntObject objects[N_INTOBJECTS];
};

typedef struct _intblock PyIntBlock;

static PyIntBlock *block_list = NULL;
static PyIntObject *free_list = NULL;

static PyIntObject *
fill_free_list(void)
{
    PyIntObject *p, *q;

    p = (PyIntObject *) PyMem_MALLOC(sizeof(PyIntBlock));
    if (p == NULL)
        return (PyIntObject *) PyErr_NoMemory();
    ((PyIntBlock *)p)->next = block_list;
    block_list = (PyIntBlock *)p;

    p = &((PyIntBlock *)p)->objects[0];
    q = p + N_INTOBJECTS;
    while (--q > p)
        Py_TYPE(q) = (struct _typeobject *)(q-1);
    Py_TYPE(q) = NULL;
    return p + N_INTOBJECTS - 1;
}

#ifndef NSMALLPOSINTS
#define NSMALLPOSINTS           257
#endif
#ifndef NSMALLNEGINTS
#define NSMALLNEGINTS           5
#endif
#if NSMALLNEGINTS + NSMALLPOSINTS > 0

static PyIntObject *small_ints[NSMALLNEGINTS + NSMALLPOSINTS];
#endif
#ifdef COUNT_ALLOCS
Py_ssize_t quick_int_allocs;
Py_ssize_t quick_neg_int_allocs;
#endif

PyObject *
PyInt_FromLong(long ival)
{
    register PyIntObject *v;
#if NSMALLNEGINTS + NSMALLPOSINTS > 0
    if (-NSMALLNEGINTS <= ival && ival < NSMALLPOSINTS) {
        v = small_ints[ival + NSMALLNEGINTS];
        Py_INCREF(v);
#ifdef COUNT_ALLOCS
        if (ival >= 0)
            quick_int_allocs++;
        else
            quick_neg_int_allocs++;
#endif
        return (PyObject *) v;
    }
#endif
    if (free_list == NULL) {
        if ((free_list = fill_free_list()) == NULL)
            return NULL;
    }

    v = free_list;
    free_list = (PyIntObject *)Py_TYPE(v);
    (void)PyObject_INIT(v, &PyInt_Type);
    v->ob_ival = ival;
    return (PyObject *) v;
}

PyObject *
PyInt_FromSize_t(size_t ival)
{
    if (ival <= LONG_MAX)
        return PyInt_FromLong((long)ival);
    return _PyLong_FromSize_t(ival);
}

PyObject *
PyInt_FromSsize_t(Py_ssize_t ival)
{
    if (ival >= LONG_MIN && ival <= LONG_MAX)
        return PyInt_FromLong((long)ival);
    return _PyLong_FromSsize_t(ival);
}

static void
int_dealloc(PyIntObject *v)
{
    if (PyInt_CheckExact(v)) {
        Py_TYPE(v) = (struct _typeobject *)free_list;
        free_list = v;
    }
    else
        Py_TYPE(v)->tp_free((PyObject *)v);
}

long
PyInt_AsLong(register PyObject *op)
{
    PyNumberMethods *nb;
    PyIntObject *io;
    long val;

    if (op && PyInt_Check(op))
        return PyInt_AS_LONG((PyIntObject*) op);

    if (op == NULL || (nb = Py_TYPE(op)->tp_as_number) == NULL ||
        nb->nb_int == NULL) {
        PyErr_SetString(PyExc_TypeError, "an integer is required");
        return -1;
    }

    io = (PyIntObject*) (*nb->nb_int) (op);
    if (io == NULL)
        return -1;
    if (!PyInt_Check(io)) {
        if (PyLong_Check(io)) {

            val = PyLong_AsLong((PyObject *)io);
            Py_DECREF(io);
            if ((val == -1) && PyErr_Occurred())
                return -1;
            return val;
        }
        else
        {
            Py_DECREF(io);
            PyErr_SetString(PyExc_TypeError,
                        "__int__ method should return an integer");
            return -1;
        }
    }

    val = PyInt_AS_LONG(io);
    Py_DECREF(io);

    return val;
}

int
_PyInt_AsInt(PyObject *obj)
{
    long result = PyInt_AsLong(obj);
    if (result == -1 && PyErr_Occurred())
        return -1;
    if (result > INT_MAX || result < INT_MIN) {
        PyErr_SetString(PyExc_OverflowError,
                        "Python int too large to convert to C int");
        return -1;
    }
    return (int)result;
}

Py_ssize_t
PyInt_AsSsize_t(register PyObject *op)
{
#if SIZEOF_SIZE_T != SIZEOF_LONG
    PyNumberMethods *nb;
    PyObject *io;
    Py_ssize_t val;
#endif

    if (op == NULL) {
        PyErr_SetString(PyExc_TypeError, "an integer is required");
        return -1;
    }

    if (PyInt_Check(op))
        return PyInt_AS_LONG((PyIntObject*) op);
    if (PyLong_Check(op))
        return _PyLong_AsSsize_t(op);
#if SIZEOF_SIZE_T == SIZEOF_LONG
    return PyInt_AsLong(op);
#else

    if ((nb = Py_TYPE(op)->tp_as_number) == NULL ||
        (nb->nb_int == NULL && nb->nb_long == 0)) {
        PyErr_SetString(PyExc_TypeError, "an integer is required");
        return -1;
    }

    if (nb->nb_long != 0)
        io = (*nb->nb_long)(op);
    else
        io = (*nb->nb_int)(op);
    if (io == NULL)
        return -1;
    if (!PyInt_Check(io)) {
        if (PyLong_Check(io)) {

            val = _PyLong_AsSsize_t(io);
            Py_DECREF(io);
            if ((val == -1) && PyErr_Occurred())
                return -1;
            return val;
        }
        else
        {
            Py_DECREF(io);
            PyErr_SetString(PyExc_TypeError,
                        "__int__ method should return an integer");
            return -1;
        }
    }

    val = PyInt_AS_LONG(io);
    Py_DECREF(io);

    return val;
#endif
}

unsigned long
PyInt_AsUnsignedLongMask(register PyObject *op)
{
    PyNumberMethods *nb;
    PyIntObject *io;
    unsigned long val;

    if (op && PyInt_Check(op))
        return PyInt_AS_LONG((PyIntObject*) op);
    if (op && PyLong_Check(op))
        return PyLong_AsUnsignedLongMask(op);

    if (op == NULL || (nb = Py_TYPE(op)->tp_as_number) == NULL ||
        nb->nb_int == NULL) {
        PyErr_SetString(PyExc_TypeError, "an integer is required");
        return (unsigned long)-1;
    }

    io = (PyIntObject*) (*nb->nb_int) (op);
    if (io == NULL)
        return (unsigned long)-1;
    if (!PyInt_Check(io)) {
        if (PyLong_Check(io)) {
            val = PyLong_AsUnsignedLongMask((PyObject *)io);
            Py_DECREF(io);
            if (PyErr_Occurred())
                return (unsigned long)-1;
            return val;
        }
        else
        {
            Py_DECREF(io);
            PyErr_SetString(PyExc_TypeError,
                        "__int__ method should return an integer");
            return (unsigned long)-1;
        }
    }

    val = PyInt_AS_LONG(io);
    Py_DECREF(io);

    return val;
}

#ifdef HAVE_LONG_LONG
unsigned PY_LONG_LONG
PyInt_AsUnsignedLongLongMask(register PyObject *op)
{
    PyNumberMethods *nb;
    PyIntObject *io;
    unsigned PY_LONG_LONG val;

    if (op && PyInt_Check(op))
        return PyInt_AS_LONG((PyIntObject*) op);
    if (op && PyLong_Check(op))
        return PyLong_AsUnsignedLongLongMask(op);

    if (op == NULL || (nb = Py_TYPE(op)->tp_as_number) == NULL ||
        nb->nb_int == NULL) {
        PyErr_SetString(PyExc_TypeError, "an integer is required");
        return (unsigned PY_LONG_LONG)-1;
    }

    io = (PyIntObject*) (*nb->nb_int) (op);
    if (io == NULL)
        return (unsigned PY_LONG_LONG)-1;
    if (!PyInt_Check(io)) {
        if (PyLong_Check(io)) {
            val = PyLong_AsUnsignedLongLongMask((PyObject *)io);
            Py_DECREF(io);
            if (PyErr_Occurred())
                return (unsigned PY_LONG_LONG)-1;
            return val;
        }
        else
        {
            Py_DECREF(io);
            PyErr_SetString(PyExc_TypeError,
                        "__int__ method should return an integer");
            return (unsigned PY_LONG_LONG)-1;
        }
    }

    val = PyInt_AS_LONG(io);
    Py_DECREF(io);

    return val;
}
#endif

PyObject *
PyInt_FromString(char *s, char **pend, int base)
{
    char *end;
    long x;
    Py_ssize_t slen;
    PyObject *sobj, *srepr;

    if ((base != 0 && base < 2) || base > 36) {
        PyErr_SetString(PyExc_ValueError,
                        "int() base must be >= 2 and <= 36");
        return NULL;
    }

    while (*s && isspace(Py_CHARMASK(*s)))
        s++;
    errno = 0;
    if (base == 0 && s[0] == '0') {
        x = (long) PyOS_strtoul(s, &end, base);
        if (x < 0)
            return PyLong_FromString(s, pend, base);
    }
    else
        x = PyOS_strtol(s, &end, base);
    if (end == s || !isalnum(Py_CHARMASK(end[-1])))
        goto bad;
    while (*end && isspace(Py_CHARMASK(*end)))
        end++;
    if (*end != '\0') {
  bad:
        slen = strlen(s) < 200 ? strlen(s) : 200;
        sobj = PyString_FromStringAndSize(s, slen);
        if (sobj == NULL)
            return NULL;
        srepr = PyObject_Repr(sobj);
        Py_DECREF(sobj);
        if (srepr == NULL)
            return NULL;
        PyErr_Format(PyExc_ValueError,
                     "invalid literal for int() with base %d: %s",
                     base, PyString_AS_STRING(srepr));
        Py_DECREF(srepr);
        return NULL;
    }
    else if (errno != 0)
        return PyLong_FromString(s, pend, base);
    if (pend)
        *pend = end;
    return PyInt_FromLong(x);
}

#ifdef Py_USING_UNICODE
PyObject *
PyInt_FromUnicode(Py_UNICODE *s, Py_ssize_t length, int base)
{
    PyObject *result;
    char *buffer = (char *)PyMem_MALLOC(length+1);

    if (buffer == NULL)
        return PyErr_NoMemory();

    if (PyUnicode_EncodeDecimal(s, length, buffer, NULL)) {
        PyMem_FREE(buffer);
        return NULL;
    }
    result = PyInt_FromString(buffer, NULL, base);
    PyMem_FREE(buffer);
    return result;
}
#endif

#define CONVERT_TO_LONG(obj, lng)               \
    if (PyInt_Check(obj)) {                     \
        lng = PyInt_AS_LONG(obj);               \
    }                                           \
    else {                                      \
        Py_INCREF(Py_NotImplemented);           \
        return Py_NotImplemented;               \
    }

static int
int_print(PyIntObject *v, FILE *fp, int flags)

{
    long int_val = v->ob_ival;
    Py_BEGIN_ALLOW_THREADS
    fprintf(fp, "%ld", int_val);
    Py_END_ALLOW_THREADS
    return 0;
}

static int
int_compare(PyIntObject *v, PyIntObject *w)
{
    register long i = v->ob_ival;
    register long j = w->ob_ival;
    return (i < j) ? -1 : (i > j) ? 1 : 0;
}

static long
int_hash(PyIntObject *v)
{

    long x = v -> ob_ival;
    if (x == -1)
        x = -2;
    return x;
}

static PyObject *
int_add(PyIntObject *v, PyIntObject *w)
{
    register long a, b, x;
    CONVERT_TO_LONG(v, a);
    CONVERT_TO_LONG(w, b);

    x = (long)((unsigned long)a + b);
    if ((x^a) >= 0 || (x^b) >= 0)
        return PyInt_FromLong(x);
    return PyLong_Type.tp_as_number->nb_add((PyObject *)v, (PyObject *)w);
}

static PyObject *
int_sub(PyIntObject *v, PyIntObject *w)
{
    register long a, b, x;
    CONVERT_TO_LONG(v, a);
    CONVERT_TO_LONG(w, b);

    x = (long)((unsigned long)a - b);
    if ((x^a) >= 0 || (x^~b) >= 0)
        return PyInt_FromLong(x);
    return PyLong_Type.tp_as_number->nb_subtract((PyObject *)v,
                                                 (PyObject *)w);
}

static PyObject *
int_mul(PyObject *v, PyObject *w)
{
    long a, b;
    long longprod;
    double doubled_longprod;
    double doubleprod;

    CONVERT_TO_LONG(v, a);
    CONVERT_TO_LONG(w, b);

    longprod = (long)((unsigned long)a * b);
    doubleprod = (double)a * (double)b;
    doubled_longprod = (double)longprod;

    if (doubled_longprod == doubleprod)
        return PyInt_FromLong(longprod);

    {
        const double diff = doubled_longprod - doubleprod;
        const double absdiff = diff >= 0.0 ? diff : -diff;
        const double absprod = doubleprod >= 0.0 ? doubleprod :
                              -doubleprod;

        if (32.0 * absdiff <= absprod)
            return PyInt_FromLong(longprod);
        else
            return PyLong_Type.tp_as_number->nb_multiply(v, w);
    }
}

#define UNARY_NEG_WOULD_OVERFLOW(x)     \
    ((x) < 0 && (unsigned long)(x) == 0-(unsigned long)(x))

enum divmod_result {
    DIVMOD_OK,
    DIVMOD_OVERFLOW,
    DIVMOD_ERROR
};

static enum divmod_result
i_divmod(register long x, register long y,
         long *p_xdivy, long *p_xmody)
{
    long xdivy, xmody;

    if (y == 0) {
        PyErr_SetString(PyExc_ZeroDivisionError,
                        "integer division or modulo by zero");
        return DIVMOD_ERROR;
    }

    if (y == -1 && UNARY_NEG_WOULD_OVERFLOW(x))
        return DIVMOD_OVERFLOW;
    xdivy = x / y;

    xmody = (long)(x - (unsigned long)xdivy * y);

    if (xmody && ((y ^ xmody) < 0)  ) {
        xmody += y;
        --xdivy;
        assert(xmody && ((y ^ xmody) >= 0));
    }
    *p_xdivy = xdivy;
    *p_xmody = xmody;
    return DIVMOD_OK;
}

static PyObject *
int_div(PyIntObject *x, PyIntObject *y)
{
    long xi, yi;
    long d, m;
    CONVERT_TO_LONG(x, xi);
    CONVERT_TO_LONG(y, yi);
    switch (i_divmod(xi, yi, &d, &m)) {
    case DIVMOD_OK:
        return PyInt_FromLong(d);
    case DIVMOD_OVERFLOW:
        return PyLong_Type.tp_as_number->nb_divide((PyObject *)x,
                                                   (PyObject *)y);
    default:
        return NULL;
    }
}

static PyObject *
int_classic_div(PyIntObject *x, PyIntObject *y)
{
    long xi, yi;
    long d, m;
    CONVERT_TO_LONG(x, xi);
    CONVERT_TO_LONG(y, yi);
    if (Py_DivisionWarningFlag &&
        PyErr_Warn(PyExc_DeprecationWarning, "classic int division") < 0)
        return NULL;
    switch (i_divmod(xi, yi, &d, &m)) {
    case DIVMOD_OK:
        return PyInt_FromLong(d);
    case DIVMOD_OVERFLOW:
        return PyLong_Type.tp_as_number->nb_divide((PyObject *)x,
                                                   (PyObject *)y);
    default:
        return NULL;
    }
}

static PyObject *
int_true_divide(PyIntObject *x, PyIntObject *y)
{
    long xi, yi;

    CONVERT_TO_LONG(x, xi);
    CONVERT_TO_LONG(y, yi);
    if (yi == 0) {
        PyErr_SetString(PyExc_ZeroDivisionError,
                        "division by zero");
        return NULL;
    }
    if (xi == 0)
        return PyFloat_FromDouble(yi < 0 ? -0.0 : 0.0);

#define WIDTH_OF_ULONG (CHAR_BIT*SIZEOF_LONG)
#if DBL_MANT_DIG < WIDTH_OF_ULONG
    if ((xi >= 0 ? 0UL + xi : 0UL - xi) >> DBL_MANT_DIG ||
        (yi >= 0 ? 0UL + yi : 0UL - yi) >> DBL_MANT_DIG)

        return PyLong_Type.tp_as_number->nb_true_divide(
            (PyObject *)x, (PyObject *)y);
    else
#endif

        return PyFloat_FromDouble((double)xi / (double)yi);
}

static PyObject *
int_mod(PyIntObject *x, PyIntObject *y)
{
    long xi, yi;
    long d, m;
    CONVERT_TO_LONG(x, xi);
    CONVERT_TO_LONG(y, yi);
    switch (i_divmod(xi, yi, &d, &m)) {
    case DIVMOD_OK:
        return PyInt_FromLong(m);
    case DIVMOD_OVERFLOW:
        return PyLong_Type.tp_as_number->nb_remainder((PyObject *)x,
                                                      (PyObject *)y);
    default:
        return NULL;
    }
}

static PyObject *
int_divmod(PyIntObject *x, PyIntObject *y)
{
    long xi, yi;
    long d, m;
    CONVERT_TO_LONG(x, xi);
    CONVERT_TO_LONG(y, yi);
    switch (i_divmod(xi, yi, &d, &m)) {
    case DIVMOD_OK:
        return Py_BuildValue("(ll)", d, m);
    case DIVMOD_OVERFLOW:
        return PyLong_Type.tp_as_number->nb_divmod((PyObject *)x,
                                                   (PyObject *)y);
    default:
        return NULL;
    }
}

static PyObject *
int_pow(PyIntObject *v, PyIntObject *w, PyIntObject *z)
{
    register long iv, iw, iz=0, ix, temp, prev;
    CONVERT_TO_LONG(v, iv);
    CONVERT_TO_LONG(w, iw);
    if (iw < 0) {
        if ((PyObject *)z != Py_None) {
            PyErr_SetString(PyExc_TypeError, "pow() 2nd argument "
                 "cannot be negative when 3rd argument specified");
            return NULL;
        }

        return PyFloat_Type.tp_as_number->nb_power(
            (PyObject *)v, (PyObject *)w, (PyObject *)z);
    }
    if ((PyObject *)z != Py_None) {
        CONVERT_TO_LONG(z, iz);
        if (iz == 0) {
            PyErr_SetString(PyExc_ValueError,
                            "pow() 3rd argument cannot be 0");
            return NULL;
        }
    }

    temp = iv;
    ix = 1;
    while (iw > 0) {
        prev = ix;
        if (iw & 1) {

            ix = (unsigned long)ix * temp;
            if (temp == 0)
                break;
            if (ix / temp != prev) {
                return PyLong_Type.tp_as_number->nb_power(
                    (PyObject *)v,
                    (PyObject *)w,
                    (PyObject *)z);
            }
        }
        iw >>= 1;
        if (iw==0) break;
        prev = temp;
        temp = (unsigned long)temp * temp;
        if (prev != 0 && temp / prev != prev) {
            return PyLong_Type.tp_as_number->nb_power(
                (PyObject *)v, (PyObject *)w, (PyObject *)z);
        }
        if (iz) {

            ix = ix % iz;
            temp = temp % iz;
        }
    }
    if (iz) {
        long div, mod;
        switch (i_divmod(ix, iz, &div, &mod)) {
        case DIVMOD_OK:
            ix = mod;
            break;
        case DIVMOD_OVERFLOW:
            return PyLong_Type.tp_as_number->nb_power(
                (PyObject *)v, (PyObject *)w, (PyObject *)z);
        default:
            return NULL;
        }
    }
    return PyInt_FromLong(ix);
}

static PyObject *
int_neg(PyIntObject *v)
{
    register long a;
    a = v->ob_ival;

    if (UNARY_NEG_WOULD_OVERFLOW(a)) {
        PyObject *o = PyLong_FromLong(a);
        if (o != NULL) {
            PyObject *result = PyNumber_Negative(o);
            Py_DECREF(o);
            return result;
        }
        return NULL;
    }
    return PyInt_FromLong(-a);
}

static PyObject *
int_abs(PyIntObject *v)
{
    if (v->ob_ival >= 0)
        return int_int(v);
    else
        return int_neg(v);
}

static int
int_nonzero(PyIntObject *v)
{
    return v->ob_ival != 0;
}

static PyObject *
int_invert(PyIntObject *v)
{
    return PyInt_FromLong(~v->ob_ival);
}

static PyObject *
int_lshift(PyIntObject *v, PyIntObject *w)
{
    long a, b, c;
    PyObject *vv, *ww, *result;

    CONVERT_TO_LONG(v, a);
    CONVERT_TO_LONG(w, b);
    if (b < 0) {
        PyErr_SetString(PyExc_ValueError, "negative shift count");
        return NULL;
    }
    if (a == 0 || b == 0)
        return int_int(v);
    if (b >= LONG_BIT) {
        vv = PyLong_FromLong(PyInt_AS_LONG(v));
        if (vv == NULL)
            return NULL;
        ww = PyLong_FromLong(PyInt_AS_LONG(w));
        if (ww == NULL) {
            Py_DECREF(vv);
            return NULL;
        }
        result = PyNumber_Lshift(vv, ww);
        Py_DECREF(vv);
        Py_DECREF(ww);
        return result;
    }
    c = a << b;
    if (a != Py_ARITHMETIC_RIGHT_SHIFT(long, c, b)) {
        vv = PyLong_FromLong(PyInt_AS_LONG(v));
        if (vv == NULL)
            return NULL;
        ww = PyLong_FromLong(PyInt_AS_LONG(w));
        if (ww == NULL) {
            Py_DECREF(vv);
            return NULL;
        }
        result = PyNumber_Lshift(vv, ww);
        Py_DECREF(vv);
        Py_DECREF(ww);
        return result;
    }
    return PyInt_FromLong(c);
}

static PyObject *
int_rshift(PyIntObject *v, PyIntObject *w)
{
    register long a, b;
    CONVERT_TO_LONG(v, a);
    CONVERT_TO_LONG(w, b);
    if (b < 0) {
        PyErr_SetString(PyExc_ValueError, "negative shift count");
        return NULL;
    }
    if (a == 0 || b == 0)
        return int_int(v);
    if (b >= LONG_BIT) {
        if (a < 0)
            a = -1;
        else
            a = 0;
    }
    else {
        a = Py_ARITHMETIC_RIGHT_SHIFT(long, a, b);
    }
    return PyInt_FromLong(a);
}

static PyObject *
int_and(PyIntObject *v, PyIntObject *w)
{
    register long a, b;
    CONVERT_TO_LONG(v, a);
    CONVERT_TO_LONG(w, b);
    return PyInt_FromLong(a & b);
}

static PyObject *
int_xor(PyIntObject *v, PyIntObject *w)
{
    register long a, b;
    CONVERT_TO_LONG(v, a);
    CONVERT_TO_LONG(w, b);
    return PyInt_FromLong(a ^ b);
}

static PyObject *
int_or(PyIntObject *v, PyIntObject *w)
{
    register long a, b;
    CONVERT_TO_LONG(v, a);
    CONVERT_TO_LONG(w, b);
    return PyInt_FromLong(a | b);
}

static int
int_coerce(PyObject **pv, PyObject **pw)
{
    if (PyInt_Check(*pw)) {
        Py_INCREF(*pv);
        Py_INCREF(*pw);
        return 0;
    }
    return 1;
}

static PyObject *
int_int(PyIntObject *v)
{
    if (PyInt_CheckExact(v))
        Py_INCREF(v);
    else
        v = (PyIntObject *)PyInt_FromLong(v->ob_ival);
    return (PyObject *)v;
}

static PyObject *
int_long(PyIntObject *v)
{
    return PyLong_FromLong((v -> ob_ival));
}

static const unsigned char BitLengthTable[32] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
};

static int
bits_in_ulong(unsigned long d)
{
    int d_bits = 0;
    while (d >= 32) {
        d_bits += 6;
        d >>= 6;
    }
    d_bits += (int)BitLengthTable[d];
    return d_bits;
}

#if 8*SIZEOF_LONG-1 <= DBL_MANT_DIG

static PyObject *
int_float(PyIntObject *v)
{
    return PyFloat_FromDouble((double)(v -> ob_ival));
}

#else

static PyObject *
int_float(PyIntObject *v)
{
    unsigned long abs_ival, lsb;
    int round_up;

    if (v->ob_ival < 0)
        abs_ival = 0U-(unsigned long)v->ob_ival;
    else
        abs_ival = (unsigned long)v->ob_ival;
    if (abs_ival < (1L << DBL_MANT_DIG))

        return PyFloat_FromDouble((double)v->ob_ival);

    lsb = 1L << (bits_in_ulong(abs_ival)-DBL_MANT_DIG-1);
    round_up = (abs_ival & lsb) && (abs_ival & (3*lsb-1));
    abs_ival &= -2*lsb;
    if (round_up)
        abs_ival += 2*lsb;
    return PyFloat_FromDouble(v->ob_ival < 0 ?
                              -(double)abs_ival :
                  (double)abs_ival);
}

#endif

static PyObject *
int_oct(PyIntObject *v)
{
    return _PyInt_Format(v, 8, 0);
}

static PyObject *
int_hex(PyIntObject *v)
{
    return _PyInt_Format(v, 16, 0);
}

static PyObject *
int_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static PyObject *
int_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *x = NULL;
    int base = -909;
    static char *kwlist[] = {"x", "base", 0};

    if (type != &PyInt_Type)
        return int_subtype_new(type, args, kwds);
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Oi:int", kwlist,
                                     &x, &base))
        return NULL;
    if (x == NULL) {
        if (base != -909) {
            PyErr_SetString(PyExc_TypeError,
                            "int() missing string argument");
            return NULL;
        }
        return PyInt_FromLong(0L);
    }
    if (base == -909)
        return PyNumber_Int(x);
    if (PyString_Check(x)) {

        char *string = PyString_AS_STRING(x);
        if (strlen(string) != PyString_Size(x)) {

            PyObject *srepr;
            srepr = PyObject_Repr(x);
            if (srepr == NULL)
                return NULL;
            PyErr_Format(PyExc_ValueError,
                 "invalid literal for int() with base %d: %s",
                 base, PyString_AS_STRING(srepr));
            Py_DECREF(srepr);
            return NULL;
        }
        return PyInt_FromString(string, NULL, base);
    }
#ifdef Py_USING_UNICODE
    if (PyUnicode_Check(x))
        return PyInt_FromUnicode(PyUnicode_AS_UNICODE(x),
                                 PyUnicode_GET_SIZE(x),
                                 base);
#endif
    PyErr_SetString(PyExc_TypeError,
                    "int() can't convert non-string with explicit base");
    return NULL;
}

static PyObject *
int_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *tmp, *newobj;
    long ival;

    assert(PyType_IsSubtype(type, &PyInt_Type));
    tmp = int_new(&PyInt_Type, args, kwds);
    if (tmp == NULL)
        return NULL;
    if (!PyInt_Check(tmp)) {
        ival = PyLong_AsLong(tmp);
        if (ival == -1 && PyErr_Occurred()) {
            Py_DECREF(tmp);
            return NULL;
        }
    } else {
        ival = ((PyIntObject *)tmp)->ob_ival;
    }

    newobj = type->tp_alloc(type, 0);
    if (newobj == NULL) {
        Py_DECREF(tmp);
        return NULL;
    }
    ((PyIntObject *)newobj)->ob_ival = ival;
    Py_DECREF(tmp);
    return newobj;
}

static PyObject *
int_getnewargs(PyIntObject *v)
{
    return Py_BuildValue("(l)", v->ob_ival);
}

static PyObject *
int_get0(PyIntObject *v, void *context) {
    return PyInt_FromLong(0L);
}

static PyObject *
int_get1(PyIntObject *v, void *context) {
    return PyInt_FromLong(1L);
}

static PyObject *
int_to_decimal_string(PyIntObject *v) {
    char buf[sizeof(long)*CHAR_BIT/3+6], *p, *bufend;
    long n = v->ob_ival;
    unsigned long absn;
    p = bufend = buf + sizeof(buf);
    absn = n < 0 ? 0UL - n : n;
    do {
        *--p = '0' + (char)(absn % 10);
        absn /= 10;
    } while (absn);
    if (n < 0)
        *--p = '-';
    return PyString_FromStringAndSize(p, bufend - p);
}

PyAPI_FUNC(PyObject*)
_PyInt_Format(PyIntObject *v, int base, int newstyle)
{

    long n = v->ob_ival;
    int  negative = n < 0;
    int is_zero = n == 0;

    char buf[sizeof(n)*CHAR_BIT+6];

    char* p = &buf[sizeof(buf)];

    assert(base >= 2 && base <= 36);

    if (base == 10)
        return int_to_decimal_string(v);

    do {

        long div = n / base;
        long mod = n - div * base;

        char cdigit = (char)(mod < 0 ? -mod : mod);
        cdigit += (cdigit < 10) ? '0' : 'a'-10;
        *--p = cdigit;

        n = div;
    } while(n);

    if (base == 2) {
        *--p = 'b';
        *--p = '0';
    }
    else if (base == 8) {
        if (newstyle) {
            *--p = 'o';
            *--p = '0';
        }
        else
            if (!is_zero)
                *--p = '0';
    }
    else if (base == 16) {
        *--p = 'x';
        *--p = '0';
    }
    else {
        *--p = '#';
        *--p = '0' + base%10;
        if (base > 10)
            *--p = '0' + base/10;
    }
    if (negative)
        *--p = '-';

    return PyString_FromStringAndSize(p, &buf[sizeof(buf)] - p);
}

static PyObject *
int__format__(PyObject *self, PyObject *args)
{
    PyObject *format_spec;

    if (!PyArg_ParseTuple(args, "O:__format__", &format_spec))
        return NULL;
    if (PyBytes_Check(format_spec))
        return _PyInt_FormatAdvanced(self,
                                     PyBytes_AS_STRING(format_spec),
                                     PyBytes_GET_SIZE(format_spec));
    if (PyUnicode_Check(format_spec)) {

        PyObject *result;
        PyObject *str_spec = PyObject_Str(format_spec);

        if (str_spec == NULL)
            return NULL;

        result = _PyInt_FormatAdvanced(self,
                                       PyBytes_AS_STRING(str_spec),
                                       PyBytes_GET_SIZE(str_spec));

        Py_DECREF(str_spec);
        return result;
    }
    PyErr_SetString(PyExc_TypeError, "__format__ requires str or unicode");
    return NULL;
}

static PyObject *
int_bit_length(PyIntObject *v)
{
    unsigned long n;

    if (v->ob_ival < 0)

        n = 0U-(unsigned long)v->ob_ival;
    else
        n = (unsigned long)v->ob_ival;

    return PyInt_FromLong(bits_in_ulong(n));
}

PyDoc_STRVAR(int_bit_length_doc,
"int.bit_length() -> int\n\
\n\
Number of bits necessary to represent self in binary.\n\
>>> bin(37)\n\
'0b100101'\n\
>>> (37).bit_length()\n\
6");

#if 0
static PyObject *
int_is_finite(PyObject *v)
{
    Py_RETURN_TRUE;
}
#endif

static PyMethodDef int_methods[] = {
    {"conjugate",       (PyCFunction)int_int,   METH_NOARGS,
     "Returns self, the complex conjugate of any int."},
    {"bit_length", (PyCFunction)int_bit_length, METH_NOARGS,
     int_bit_length_doc},
#if 0
    {"is_finite",       (PyCFunction)int_is_finite,     METH_NOARGS,
     "Returns always True."},
#endif
    {"__trunc__",       (PyCFunction)int_int,   METH_NOARGS,
     "Truncating an Integral returns itself."},
    {"__getnewargs__",          (PyCFunction)int_getnewargs,    METH_NOARGS},
    {"__format__", (PyCFunction)int__format__, METH_VARARGS},
    {NULL,              NULL}
};

static PyGetSetDef int_getset[] = {
    {"real",
     (getter)int_int, (setter)NULL,
     "the real part of a complex number",
     NULL},
    {"imag",
     (getter)int_get0, (setter)NULL,
     "the imaginary part of a complex number",
     NULL},
    {"numerator",
     (getter)int_int, (setter)NULL,
     "the numerator of a rational number in lowest terms",
     NULL},
    {"denominator",
     (getter)int_get1, (setter)NULL,
     "the denominator of a rational number in lowest terms",
     NULL},
    {NULL}
};

PyDoc_STRVAR(int_doc,
"int(x=0) -> int or long\n\
int(x, base=10) -> int or long\n\
\n\
Convert a number or string to an integer, or return 0 if no arguments\n\
are given.  If x is floating point, the conversion truncates towards zero.\n\
If x is outside the integer range, the function returns a long instead.\n\
\n\
If x is not a number or if base is given, then x must be a string or\n\
Unicode object representing an integer literal in the given base.  The\n\
literal can be preceded by '+' or '-' and be surrounded by whitespace.\n\
The base defaults to 10.  Valid bases are 0 and 2-36.  Base 0 means to\n\
interpret the base from the string as an integer literal.\n\
>>> int('0b100', base=0)\n\
4");

static PyNumberMethods int_as_number = {
    (binaryfunc)int_add,
    (binaryfunc)int_sub,
    (binaryfunc)int_mul,
    (binaryfunc)int_classic_div,
    (binaryfunc)int_mod,
    (binaryfunc)int_divmod,
    (ternaryfunc)int_pow,
    (unaryfunc)int_neg,
    (unaryfunc)int_int,
    (unaryfunc)int_abs,
    (inquiry)int_nonzero,
    (unaryfunc)int_invert,
    (binaryfunc)int_lshift,
    (binaryfunc)int_rshift,
    (binaryfunc)int_and,
    (binaryfunc)int_xor,
    (binaryfunc)int_or,
    int_coerce,
    (unaryfunc)int_int,
    (unaryfunc)int_long,
    (unaryfunc)int_float,
    (unaryfunc)int_oct,
    (unaryfunc)int_hex,
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
    (binaryfunc)int_div,
    (binaryfunc)int_true_divide,
    0,
    0,
    (unaryfunc)int_int,
};

PyTypeObject PyInt_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "int",
    sizeof(PyIntObject),
    0,
    (destructor)int_dealloc,
    (printfunc)int_print,
    0,
    0,
    (cmpfunc)int_compare,
    (reprfunc)int_to_decimal_string,
    &int_as_number,
    0,
    0,
    (hashfunc)int_hash,
    0,
    (reprfunc)int_to_decimal_string,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_INT_SUBCLASS,
    int_doc,
    0,
    0,
    0,
    0,
    0,
    0,
    int_methods,
    0,
    int_getset,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    int_new,
};

int
_PyInt_Init(void)
{
    PyIntObject *v;
    int ival;
#if NSMALLNEGINTS + NSMALLPOSINTS > 0
    for (ival = -NSMALLNEGINTS; ival < NSMALLPOSINTS; ival++) {
        if (!free_list && (free_list = fill_free_list()) == NULL)
            return 0;

        v = free_list;
        free_list = (PyIntObject *)Py_TYPE(v);
        (void)PyObject_INIT(v, &PyInt_Type);
        v->ob_ival = ival;
        small_ints[ival + NSMALLNEGINTS] = v;
    }
#endif
    return 1;
}

int
PyInt_ClearFreeList(void)
{
    PyIntObject *p;
    PyIntBlock *list, *next;
    int i;
    int u;
    int freelist_size = 0;

    list = block_list;
    block_list = NULL;
    free_list = NULL;
    while (list != NULL) {
        u = 0;
        for (i = 0, p = &list->objects[0];
             i < N_INTOBJECTS;
             i++, p++) {
            if (PyInt_CheckExact(p) && p->ob_refcnt != 0)
                u++;
        }
        next = list->next;
        if (u) {
            list->next = block_list;
            block_list = list;
            for (i = 0, p = &list->objects[0];
                 i < N_INTOBJECTS;
                 i++, p++) {
                if (!PyInt_CheckExact(p) ||
                    p->ob_refcnt == 0) {
                    Py_TYPE(p) = (struct _typeobject *)
                        free_list;
                    free_list = p;
                }
#if NSMALLNEGINTS + NSMALLPOSINTS > 0
                else if (-NSMALLNEGINTS <= p->ob_ival &&
                         p->ob_ival < NSMALLPOSINTS &&
                         small_ints[p->ob_ival +
                                    NSMALLNEGINTS] == NULL) {
                    Py_INCREF(p);
                    small_ints[p->ob_ival +
                               NSMALLNEGINTS] = p;
                }
#endif
            }
        }
        else {
            PyMem_FREE(list);
        }
        freelist_size += u;
        list = next;
    }

    return freelist_size;
}

void
PyInt_Fini(void)
{
    PyIntObject *p;
    PyIntBlock *list;
    int i;
    int u;

#if NSMALLNEGINTS + NSMALLPOSINTS > 0
    PyIntObject **q;

    i = NSMALLNEGINTS + NSMALLPOSINTS;
    q = small_ints;
    while (--i >= 0) {
        Py_XDECREF(*q);
        *q++ = NULL;
    }
#endif
    u = PyInt_ClearFreeList();
    if (!Py_VerboseFlag)
        return;
    fprintf(stderr, "# cleanup ints");
    if (!u) {
        fprintf(stderr, "\n");
    }
    else {
        fprintf(stderr,
            ": %d unfreed int%s\n",
            u, u == 1 ? "" : "s");
    }
    if (Py_VerboseFlag > 1) {
        list = block_list;
        while (list != NULL) {
            for (i = 0, p = &list->objects[0];
                 i < N_INTOBJECTS;
                 i++, p++) {
                if (PyInt_CheckExact(p) && p->ob_refcnt != 0)

                    fprintf(stderr,
                "#   <int at %p, refcnt=%ld, val=%ld>\n",
                                p, (long)p->ob_refcnt,
                                p->ob_ival);
            }
            list = list->next;
        }
    }
}
