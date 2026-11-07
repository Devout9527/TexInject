#include "Python.h"
#include "longintrepr.h"
#include "structseq.h"

#include <float.h>
#include <ctype.h>
#include <stddef.h>

#define KARATSUBA_CUTOFF 70
#define KARATSUBA_SQUARE_CUTOFF (2 * KARATSUBA_CUTOFF)

#define FIVEARY_CUTOFF 8

#define ABS(x) ((x) < 0 ? -(x) : (x))

#undef MIN
#undef MAX
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) > (y) ? (y) : (x))

#define SIGCHECK(PyTryBlock)                            \
    do {                                                \
        if (--_Py_Ticker < 0) {                         \
            _Py_Ticker = _Py_CheckInterval;             \
            if (PyErr_CheckSignals()) PyTryBlock        \
                                          }             \
    } while(0)

static PyLongObject *
long_normalize(register PyLongObject *v)
{
    Py_ssize_t j = ABS(Py_SIZE(v));
    Py_ssize_t i = j;

    while (i > 0 && v->ob_digit[i-1] == 0)
        --i;
    if (i != j)
        Py_SIZE(v) = (Py_SIZE(v) < 0) ? -(i) : i;
    return v;
}

#define MAX_LONG_DIGITS \
    ((PY_SSIZE_T_MAX - offsetof(PyLongObject, ob_digit))/sizeof(digit))

PyLongObject *
_PyLong_New(Py_ssize_t size)
{
    if (size > (Py_ssize_t)MAX_LONG_DIGITS) {
        PyErr_SetString(PyExc_OverflowError,
                        "too many digits in integer");
        return NULL;
    }

    return PyObject_NEW_VAR(PyLongObject, &PyLong_Type, size);
}

PyObject *
_PyLong_Copy(PyLongObject *src)
{
    PyLongObject *result;
    Py_ssize_t i;

    assert(src != NULL);
    i = src->ob_size;
    if (i < 0)
        i = -(i);
    result = _PyLong_New(i);
    if (result != NULL) {
        result->ob_size = src->ob_size;
        while (--i >= 0)
            result->ob_digit[i] = src->ob_digit[i];
    }
    return (PyObject *)result;
}

PyObject *
PyLong_FromLong(long ival)
{
    PyLongObject *v;
    unsigned long abs_ival;
    unsigned long t;
    int ndigits = 0;
    int negative = 0;

    if (ival < 0) {

        abs_ival = (unsigned long)(-1-ival) + 1;
        negative = 1;
    }
    else {
        abs_ival = (unsigned long)ival;
    }

    t = abs_ival;
    while (t) {
        ++ndigits;
        t >>= PyLong_SHIFT;
    }
    v = _PyLong_New(ndigits);
    if (v != NULL) {
        digit *p = v->ob_digit;
        v->ob_size = negative ? -ndigits : ndigits;
        t = abs_ival;
        while (t) {
            *p++ = (digit)(t & PyLong_MASK);
            t >>= PyLong_SHIFT;
        }
    }
    return (PyObject *)v;
}

PyObject *
PyLong_FromUnsignedLong(unsigned long ival)
{
    PyLongObject *v;
    unsigned long t;
    int ndigits = 0;

    t = (unsigned long)ival;
    while (t) {
        ++ndigits;
        t >>= PyLong_SHIFT;
    }
    v = _PyLong_New(ndigits);
    if (v != NULL) {
        digit *p = v->ob_digit;
        Py_SIZE(v) = ndigits;
        while (ival) {
            *p++ = (digit)(ival & PyLong_MASK);
            ival >>= PyLong_SHIFT;
        }
    }
    return (PyObject *)v;
}

PyObject *
PyLong_FromDouble(double dval)
{
    PyLongObject *v;
    double frac;
    int i, ndig, expo, neg;
    neg = 0;
    if (Py_IS_INFINITY(dval)) {
        PyErr_SetString(PyExc_OverflowError,
                        "cannot convert float infinity to integer");
        return NULL;
    }
    if (Py_IS_NAN(dval)) {
        PyErr_SetString(PyExc_ValueError,
                        "cannot convert float NaN to integer");
        return NULL;
    }
    if (dval < 0.0) {
        neg = 1;
        dval = -dval;
    }
    frac = frexp(dval, &expo);
    if (expo <= 0)
        return PyLong_FromLong(0L);
    ndig = (expo-1) / PyLong_SHIFT + 1;
    v = _PyLong_New(ndig);
    if (v == NULL)
        return NULL;
    frac = ldexp(frac, (expo-1) % PyLong_SHIFT + 1);
    for (i = ndig; --i >= 0; ) {
        digit bits = (digit)frac;
        v->ob_digit[i] = bits;
        frac = frac - (double)bits;
        frac = ldexp(frac, PyLong_SHIFT);
    }
    if (neg)
        Py_SIZE(v) = -(Py_SIZE(v));
    return (PyObject *)v;
}

#define PY_ABS_LONG_MIN         (0-(unsigned long)LONG_MIN)
#define PY_ABS_SSIZE_T_MIN      (0-(size_t)PY_SSIZE_T_MIN)

long
PyLong_AsLongAndOverflow(PyObject *vv, int *overflow)
{

    register PyLongObject *v;
    unsigned long x, prev;
    long res;
    Py_ssize_t i;
    int sign;
    int do_decref = 0;

    *overflow = 0;
    if (vv == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }

    if(PyInt_Check(vv))
        return PyInt_AsLong(vv);

    if (!PyLong_Check(vv)) {
        PyNumberMethods *nb;
        nb = vv->ob_type->tp_as_number;
        if (nb == NULL || nb->nb_int == NULL) {
            PyErr_SetString(PyExc_TypeError,
                            "an integer is required");
            return -1;
        }
        vv = (*nb->nb_int) (vv);
        if (vv == NULL)
            return -1;
        do_decref = 1;
        if(PyInt_Check(vv)) {
            res = PyInt_AsLong(vv);
            goto exit;
        }
        if (!PyLong_Check(vv)) {
            Py_DECREF(vv);
            PyErr_SetString(PyExc_TypeError,
                            "nb_int should return int object");
            return -1;
        }
    }

    res = -1;
    v = (PyLongObject *)vv;
    i = Py_SIZE(v);

    switch (i) {
    case -1:
        res = -(sdigit)v->ob_digit[0];
        break;
    case 0:
        res = 0;
        break;
    case 1:
        res = v->ob_digit[0];
        break;
    default:
        sign = 1;
        x = 0;
        if (i < 0) {
            sign = -1;
            i = -(i);
        }
        while (--i >= 0) {
            prev = x;
            x = (x << PyLong_SHIFT) + v->ob_digit[i];
            if ((x >> PyLong_SHIFT) != prev) {
                *overflow = sign;
                goto exit;
            }
        }

        if (x <= (unsigned long)LONG_MAX) {
            res = (long)x * sign;
        }
        else if (sign < 0 && x == PY_ABS_LONG_MIN) {
            res = LONG_MIN;
        }
        else {
            *overflow = sign;

        }
    }
  exit:
    if (do_decref) {
        Py_DECREF(vv);
    }
    return res;
}

long
PyLong_AsLong(PyObject *obj)
{
    int overflow;
    long result = PyLong_AsLongAndOverflow(obj, &overflow);
    if (overflow) {

        PyErr_SetString(PyExc_OverflowError,
                        "Python int too large to convert to C long");
    }
    return result;
}

int
_PyLong_AsInt(PyObject *obj)
{
    int overflow;
    long result = PyLong_AsLongAndOverflow(obj, &overflow);
    if (overflow || result > INT_MAX || result < INT_MIN) {

        PyErr_SetString(PyExc_OverflowError,
                        "Python int too large to convert to C int");
        return -1;
    }
    return (int)result;
}

Py_ssize_t
PyLong_AsSsize_t(PyObject *vv) {
    register PyLongObject *v;
    size_t x, prev;
    Py_ssize_t i;
    int sign;

    if (vv == NULL || !PyLong_Check(vv)) {
        PyErr_BadInternalCall();
        return -1;
    }
    v = (PyLongObject *)vv;
    i = v->ob_size;
    sign = 1;
    x = 0;
    if (i < 0) {
        sign = -1;
        i = -(i);
    }
    while (--i >= 0) {
        prev = x;
        x = (x << PyLong_SHIFT) | v->ob_digit[i];
        if ((x >> PyLong_SHIFT) != prev)
            goto overflow;
    }

    if (x <= (size_t)PY_SSIZE_T_MAX) {
        return (Py_ssize_t)x * sign;
    }
    else if (sign < 0 && x == PY_ABS_SSIZE_T_MIN) {
        return PY_SSIZE_T_MIN;
    }

  overflow:
    PyErr_SetString(PyExc_OverflowError,
                    "long int too large to convert to int");
    return -1;
}

unsigned long
PyLong_AsUnsignedLong(PyObject *vv)
{
    register PyLongObject *v;
    unsigned long x, prev;
    Py_ssize_t i;

    if (vv == NULL || !PyLong_Check(vv)) {
        if (vv != NULL && PyInt_Check(vv)) {
            long val = PyInt_AsLong(vv);
            if (val < 0) {
                PyErr_SetString(PyExc_OverflowError,
                                "can't convert negative value "
                                "to unsigned long");
                return (unsigned long) -1;
            }
            return val;
        }
        PyErr_BadInternalCall();
        return (unsigned long) -1;
    }
    v = (PyLongObject *)vv;
    i = Py_SIZE(v);
    x = 0;
    if (i < 0) {
        PyErr_SetString(PyExc_OverflowError,
                        "can't convert negative value to unsigned long");
        return (unsigned long) -1;
    }
    while (--i >= 0) {
        prev = x;
        x = (x << PyLong_SHIFT) | v->ob_digit[i];
        if ((x >> PyLong_SHIFT) != prev) {
            PyErr_SetString(PyExc_OverflowError,
                            "long int too large to convert");
            return (unsigned long) -1;
        }
    }
    return x;
}

unsigned long
PyLong_AsUnsignedLongMask(PyObject *vv)
{
    register PyLongObject *v;
    unsigned long x;
    Py_ssize_t i;
    int sign;

    if (vv == NULL || !PyLong_Check(vv)) {
        if (vv != NULL && PyInt_Check(vv))
            return PyInt_AsUnsignedLongMask(vv);
        PyErr_BadInternalCall();
        return (unsigned long) -1;
    }
    v = (PyLongObject *)vv;
    i = v->ob_size;
    sign = 1;
    x = 0;
    if (i < 0) {
        sign = -1;
        i = -i;
    }
    while (--i >= 0) {
        x = (x << PyLong_SHIFT) | v->ob_digit[i];
    }
    return x * sign;
}

int
_PyLong_Sign(PyObject *vv)
{
    PyLongObject *v = (PyLongObject *)vv;

    assert(v != NULL);
    assert(PyLong_Check(v));

    return Py_SIZE(v) == 0 ? 0 : (Py_SIZE(v) < 0 ? -1 : 1);
}

size_t
_PyLong_NumBits(PyObject *vv)
{
    PyLongObject *v = (PyLongObject *)vv;
    size_t result = 0;
    Py_ssize_t ndigits;

    assert(v != NULL);
    assert(PyLong_Check(v));
    ndigits = ABS(Py_SIZE(v));
    assert(ndigits == 0 || v->ob_digit[ndigits - 1] != 0);
    if (ndigits > 0) {
        digit msd = v->ob_digit[ndigits - 1];

        result = (ndigits - 1) * PyLong_SHIFT;
        if (result / PyLong_SHIFT != (size_t)(ndigits - 1))
            goto Overflow;
        do {
            ++result;
            if (result == 0)
                goto Overflow;
            msd >>= 1;
        } while (msd);
    }
    return result;

  Overflow:
    PyErr_SetString(PyExc_OverflowError, "long has too many bits "
                    "to express in a platform size_t");
    return (size_t)-1;
}

PyObject *
_PyLong_FromByteArray(const unsigned char* bytes, size_t n,
                      int little_endian, int is_signed)
{
    const unsigned char* pstartbyte;
    int incr;
    const unsigned char* pendbyte;
    size_t numsignificantbytes;
    Py_ssize_t ndigits;
    PyLongObject* v;
    Py_ssize_t idigit = 0;

    if (n == 0)
        return PyLong_FromLong(0L);

    if (little_endian) {
        pstartbyte = bytes;
        pendbyte = bytes + n - 1;
        incr = 1;
    }
    else {
        pstartbyte = bytes + n - 1;
        pendbyte = bytes;
        incr = -1;
    }

    if (is_signed)
        is_signed = *pendbyte >= 0x80;

    {
        size_t i;
        const unsigned char* p = pendbyte;
        const int pincr = -incr;
        const unsigned char insignificant = is_signed ? 0xff : 0x00;

        for (i = 0; i < n; ++i, p += pincr) {
            if (*p != insignificant)
                break;
        }
        numsignificantbytes = n - i;

        if (is_signed && numsignificantbytes < n)
            ++numsignificantbytes;
    }

    if (numsignificantbytes > (PY_SSIZE_T_MAX - PyLong_SHIFT) / 8) {
        PyErr_SetString(PyExc_OverflowError,
                        "byte array too long to convert to int");
        return NULL;
    }
    ndigits = (numsignificantbytes * 8 + PyLong_SHIFT - 1) / PyLong_SHIFT;
    v = _PyLong_New(ndigits);
    if (v == NULL)
        return NULL;

    {
        size_t i;
        twodigits carry = 1;
        twodigits accum = 0;
        unsigned int accumbits = 0;
        const unsigned char* p = pstartbyte;

        for (i = 0; i < numsignificantbytes; ++i, p += incr) {
            twodigits thisbyte = *p;

            if (is_signed) {
                thisbyte = (0xff ^ thisbyte) + carry;
                carry = thisbyte >> 8;
                thisbyte &= 0xff;
            }

            accum |= (twodigits)thisbyte << accumbits;
            accumbits += 8;
            if (accumbits >= PyLong_SHIFT) {

                assert(idigit < ndigits);
                v->ob_digit[idigit] = (digit)(accum & PyLong_MASK);
                ++idigit;
                accum >>= PyLong_SHIFT;
                accumbits -= PyLong_SHIFT;
                assert(accumbits < PyLong_SHIFT);
            }
        }
        assert(accumbits < PyLong_SHIFT);
        if (accumbits) {
            assert(idigit < ndigits);
            v->ob_digit[idigit] = (digit)accum;
            ++idigit;
        }
    }

    Py_SIZE(v) = is_signed ? -idigit : idigit;
    return (PyObject *)long_normalize(v);
}

int
_PyLong_AsByteArray(PyLongObject* v,
                    unsigned char* bytes, size_t n,
                    int little_endian, int is_signed)
{
    Py_ssize_t i;
    Py_ssize_t ndigits;
    twodigits accum;
    unsigned int accumbits;
    int do_twos_comp;
    digit carry;
    size_t j;
    unsigned char* p;
    int pincr;

    assert(v != NULL && PyLong_Check(v));

    if (Py_SIZE(v) < 0) {
        ndigits = -(Py_SIZE(v));
        if (!is_signed) {
            PyErr_SetString(PyExc_OverflowError,
                            "can't convert negative long to unsigned");
            return -1;
        }
        do_twos_comp = 1;
    }
    else {
        ndigits = Py_SIZE(v);
        do_twos_comp = 0;
    }

    if (little_endian) {
        p = bytes;
        pincr = 1;
    }
    else {
        p = bytes + n - 1;
        pincr = -1;
    }

    assert(ndigits == 0 || v->ob_digit[ndigits - 1] != 0);
    j = 0;
    accum = 0;
    accumbits = 0;
    carry = do_twos_comp ? 1 : 0;
    for (i = 0; i < ndigits; ++i) {
        digit thisdigit = v->ob_digit[i];
        if (do_twos_comp) {
            thisdigit = (thisdigit ^ PyLong_MASK) + carry;
            carry = thisdigit >> PyLong_SHIFT;
            thisdigit &= PyLong_MASK;
        }

        accum |= (twodigits)thisdigit << accumbits;

        if (i == ndigits - 1) {

            digit s = do_twos_comp ? thisdigit ^ PyLong_MASK : thisdigit;
            while (s != 0) {
                s >>= 1;
                accumbits++;
            }
        }
        else
            accumbits += PyLong_SHIFT;

        while (accumbits >= 8) {
            if (j >= n)
                goto Overflow;
            ++j;
            *p = (unsigned char)(accum & 0xff);
            p += pincr;
            accumbits -= 8;
            accum >>= 8;
        }
    }

    assert(accumbits < 8);
    assert(carry == 0);
    if (accumbits > 0) {
        if (j >= n)
            goto Overflow;
        ++j;
        if (do_twos_comp) {

            accum |= (~(twodigits)0) << accumbits;
        }
        *p = (unsigned char)(accum & 0xff);
        p += pincr;
    }
    else if (j == n && n > 0 && is_signed) {

        unsigned char msb = *(p - pincr);
        int sign_bit_set = msb >= 0x80;
        assert(accumbits == 0);
        if (sign_bit_set == do_twos_comp)
            return 0;
        else
            goto Overflow;
    }

    {
        unsigned char signbyte = do_twos_comp ? 0xffU : 0U;
        for ( ; j < n; ++j, p += pincr)
            *p = signbyte;
    }

    return 0;

  Overflow:
    PyErr_SetString(PyExc_OverflowError, "long too big to convert");
    return -1;

}

PyObject *
PyLong_FromVoidPtr(void *p)
{
#if SIZEOF_VOID_P <= SIZEOF_LONG
    if ((long)p < 0)
        return PyLong_FromUnsignedLong((unsigned long)p);
    return PyInt_FromLong((long)p);
#else

#ifndef HAVE_LONG_LONG
#   error "PyLong_FromVoidPtr: sizeof(void*) > sizeof(long), but no long long"
#endif
#if SIZEOF_LONG_LONG < SIZEOF_VOID_P
#   error "PyLong_FromVoidPtr: sizeof(PY_LONG_LONG) < sizeof(void*)"
#endif

    if (p == NULL)
        return PyInt_FromLong(0);
    return PyLong_FromUnsignedLongLong((unsigned PY_LONG_LONG)p);

#endif
}

void *
PyLong_AsVoidPtr(PyObject *vv)
{

#if SIZEOF_VOID_P <= SIZEOF_LONG
    long x;

    if (PyInt_Check(vv))
        x = PyInt_AS_LONG(vv);
    else if (PyLong_Check(vv) && _PyLong_Sign(vv) < 0)
        x = PyLong_AsLong(vv);
    else
        x = PyLong_AsUnsignedLong(vv);
#else

#ifndef HAVE_LONG_LONG
#   error "PyLong_AsVoidPtr: sizeof(void*) > sizeof(long), but no long long"
#endif
#if SIZEOF_LONG_LONG < SIZEOF_VOID_P
#   error "PyLong_AsVoidPtr: sizeof(PY_LONG_LONG) < sizeof(void*)"
#endif
    PY_LONG_LONG x;

    if (PyInt_Check(vv))
        x = PyInt_AS_LONG(vv);
    else if (PyLong_Check(vv) && _PyLong_Sign(vv) < 0)
        x = PyLong_AsLongLong(vv);
    else
        x = PyLong_AsUnsignedLongLong(vv);

#endif

    if (x == -1 && PyErr_Occurred())
        return NULL;
    return (void *)x;
}

#ifdef HAVE_LONG_LONG

#define IS_LITTLE_ENDIAN (int)*(unsigned char*)&one
#define PY_ABS_LLONG_MIN (0-(unsigned PY_LONG_LONG)PY_LLONG_MIN)

PyObject *
PyLong_FromLongLong(PY_LONG_LONG ival)
{
    PyLongObject *v;
    unsigned PY_LONG_LONG abs_ival;
    unsigned PY_LONG_LONG t;
    int ndigits = 0;
    int negative = 0;

    if (ival < 0) {

        abs_ival = (unsigned PY_LONG_LONG)(-1-ival) + 1;
        negative = 1;
    }
    else {
        abs_ival = (unsigned PY_LONG_LONG)ival;
    }

    t = abs_ival;
    while (t) {
        ++ndigits;
        t >>= PyLong_SHIFT;
    }
    v = _PyLong_New(ndigits);
    if (v != NULL) {
        digit *p = v->ob_digit;
        Py_SIZE(v) = negative ? -ndigits : ndigits;
        t = abs_ival;
        while (t) {
            *p++ = (digit)(t & PyLong_MASK);
            t >>= PyLong_SHIFT;
        }
    }
    return (PyObject *)v;
}

PyObject *
PyLong_FromUnsignedLongLong(unsigned PY_LONG_LONG ival)
{
    PyLongObject *v;
    unsigned PY_LONG_LONG t;
    int ndigits = 0;

    t = (unsigned PY_LONG_LONG)ival;
    while (t) {
        ++ndigits;
        t >>= PyLong_SHIFT;
    }
    v = _PyLong_New(ndigits);
    if (v != NULL) {
        digit *p = v->ob_digit;
        Py_SIZE(v) = ndigits;
        while (ival) {
            *p++ = (digit)(ival & PyLong_MASK);
            ival >>= PyLong_SHIFT;
        }
    }
    return (PyObject *)v;
}

PyObject *
PyLong_FromSsize_t(Py_ssize_t ival)
{
    Py_ssize_t bytes = ival;
    int one = 1;
    return _PyLong_FromByteArray((unsigned char *)&bytes,
                                 SIZEOF_SIZE_T, IS_LITTLE_ENDIAN, 1);
}

PyObject *
PyLong_FromSize_t(size_t ival)
{
    size_t bytes = ival;
    int one = 1;
    return _PyLong_FromByteArray((unsigned char *)&bytes,
                                 SIZEOF_SIZE_T, IS_LITTLE_ENDIAN, 0);
}

PY_LONG_LONG
PyLong_AsLongLong(PyObject *vv)
{
    PY_LONG_LONG bytes;
    int one = 1;
    int res;

    if (vv == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (!PyLong_Check(vv)) {
        PyNumberMethods *nb;
        PyObject *io;
        if (PyInt_Check(vv))
            return (PY_LONG_LONG)PyInt_AsLong(vv);
        if ((nb = vv->ob_type->tp_as_number) == NULL ||
            nb->nb_int == NULL) {
            PyErr_SetString(PyExc_TypeError, "an integer is required");
            return -1;
        }
        io = (*nb->nb_int) (vv);
        if (io == NULL)
            return -1;
        if (PyInt_Check(io)) {
            bytes = PyInt_AsLong(io);
            Py_DECREF(io);
            return bytes;
        }
        if (PyLong_Check(io)) {
            bytes = PyLong_AsLongLong(io);
            Py_DECREF(io);
            return bytes;
        }
        Py_DECREF(io);
        PyErr_SetString(PyExc_TypeError, "integer conversion failed");
        return -1;
    }

    res = _PyLong_AsByteArray((PyLongObject *)vv, (unsigned char *)&bytes,
                              SIZEOF_LONG_LONG, IS_LITTLE_ENDIAN, 1);

    if (res < 0)
        return (PY_LONG_LONG)-1;
    else
        return bytes;
}

unsigned PY_LONG_LONG
PyLong_AsUnsignedLongLong(PyObject *vv)
{
    unsigned PY_LONG_LONG bytes;
    int one = 1;
    int res;

    if (vv == NULL || !PyLong_Check(vv)) {
        PyErr_BadInternalCall();
        return (unsigned PY_LONG_LONG)-1;
    }

    res = _PyLong_AsByteArray((PyLongObject *)vv, (unsigned char *)&bytes,
                              SIZEOF_LONG_LONG, IS_LITTLE_ENDIAN, 0);

    if (res < 0)
        return (unsigned PY_LONG_LONG)res;
    else
        return bytes;
}

unsigned PY_LONG_LONG
PyLong_AsUnsignedLongLongMask(PyObject *vv)
{
    register PyLongObject *v;
    unsigned PY_LONG_LONG x;
    Py_ssize_t i;
    int sign;

    if (vv == NULL || !PyLong_Check(vv)) {
        PyErr_BadInternalCall();
        return (unsigned long) -1;
    }
    v = (PyLongObject *)vv;
    i = v->ob_size;
    sign = 1;
    x = 0;
    if (i < 0) {
        sign = -1;
        i = -i;
    }
    while (--i >= 0) {
        x = (x << PyLong_SHIFT) | v->ob_digit[i];
    }
    return x * sign;
}

PY_LONG_LONG
PyLong_AsLongLongAndOverflow(PyObject *vv, int *overflow)
{

    register PyLongObject *v;
    unsigned PY_LONG_LONG x, prev;
    PY_LONG_LONG res;
    Py_ssize_t i;
    int sign;
    int do_decref = 0;

    *overflow = 0;
    if (vv == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }

    if (PyInt_Check(vv))
        return PyInt_AsLong(vv);

    if (!PyLong_Check(vv)) {
        PyNumberMethods *nb;
        nb = vv->ob_type->tp_as_number;
        if (nb == NULL || nb->nb_int == NULL) {
            PyErr_SetString(PyExc_TypeError,
                            "an integer is required");
            return -1;
        }
        vv = (*nb->nb_int) (vv);
        if (vv == NULL)
            return -1;
        do_decref = 1;
        if(PyInt_Check(vv)) {
            res = PyInt_AsLong(vv);
            goto exit;
        }
        if (!PyLong_Check(vv)) {
            Py_DECREF(vv);
            PyErr_SetString(PyExc_TypeError,
                            "nb_int should return int object");
            return -1;
        }
    }

    res = -1;
    v = (PyLongObject *)vv;
    i = Py_SIZE(v);

    switch (i) {
    case -1:
        res = -(sdigit)v->ob_digit[0];
        break;
    case 0:
        res = 0;
        break;
    case 1:
        res = v->ob_digit[0];
        break;
    default:
        sign = 1;
        x = 0;
        if (i < 0) {
            sign = -1;
            i = -(i);
        }
        while (--i >= 0) {
            prev = x;
            x = (x << PyLong_SHIFT) + v->ob_digit[i];
            if ((x >> PyLong_SHIFT) != prev) {
                *overflow = sign;
                goto exit;
            }
        }

        if (x <= (unsigned PY_LONG_LONG)PY_LLONG_MAX) {
            res = (PY_LONG_LONG)x * sign;
        }
        else if (sign < 0 && x == PY_ABS_LLONG_MIN) {
            res = PY_LLONG_MIN;
        }
        else {
            *overflow = sign;

        }
    }
  exit:
    if (do_decref) {
        Py_DECREF(vv);
    }
    return res;
}

#undef IS_LITTLE_ENDIAN

#endif

static int
convert_binop(PyObject *v, PyObject *w, PyLongObject **a, PyLongObject **b) {
    if (PyLong_Check(v)) {
        *a = (PyLongObject *) v;
        Py_INCREF(v);
    }
    else if (PyInt_Check(v)) {
        *a = (PyLongObject *) PyLong_FromLong(PyInt_AS_LONG(v));
    }
    else {
        return 0;
    }
    if (PyLong_Check(w)) {
        *b = (PyLongObject *) w;
        Py_INCREF(w);
    }
    else if (PyInt_Check(w)) {
        *b = (PyLongObject *) PyLong_FromLong(PyInt_AS_LONG(w));
    }
    else {
        Py_DECREF(*a);
        return 0;
    }
    return 1;
}

#define CONVERT_BINOP(v, w, a, b)               \
    do {                                        \
        if (!convert_binop(v, w, a, b)) {       \
            Py_INCREF(Py_NotImplemented);       \
            return Py_NotImplemented;           \
        }                                       \
    } while(0)                                  \

static const unsigned char BitLengthTable[32] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
};

static int
bits_in_digit(digit d)
{
    int d_bits = 0;
    while (d >= 32) {
        d_bits += 6;
        d >>= 6;
    }
    d_bits += (int)BitLengthTable[d];
    return d_bits;
}

static digit
v_iadd(digit *x, Py_ssize_t m, digit *y, Py_ssize_t n)
{
    Py_ssize_t i;
    digit carry = 0;

    assert(m >= n);
    for (i = 0; i < n; ++i) {
        carry += x[i] + y[i];
        x[i] = carry & PyLong_MASK;
        carry >>= PyLong_SHIFT;
        assert((carry & 1) == carry);
    }
    for (; carry && i < m; ++i) {
        carry += x[i];
        x[i] = carry & PyLong_MASK;
        carry >>= PyLong_SHIFT;
        assert((carry & 1) == carry);
    }
    return carry;
}

static digit
v_isub(digit *x, Py_ssize_t m, digit *y, Py_ssize_t n)
{
    Py_ssize_t i;
    digit borrow = 0;

    assert(m >= n);
    for (i = 0; i < n; ++i) {
        borrow = x[i] - y[i] - borrow;
        x[i] = borrow & PyLong_MASK;
        borrow >>= PyLong_SHIFT;
        borrow &= 1;
    }
    for (; borrow && i < m; ++i) {
        borrow = x[i] - borrow;
        x[i] = borrow & PyLong_MASK;
        borrow >>= PyLong_SHIFT;
        borrow &= 1;
    }
    return borrow;
}

static digit
v_lshift(digit *z, digit *a, Py_ssize_t m, int d)
{
    Py_ssize_t i;
    digit carry = 0;

    assert(0 <= d && d < PyLong_SHIFT);
    for (i=0; i < m; i++) {
        twodigits acc = (twodigits)a[i] << d | carry;
        z[i] = (digit)acc & PyLong_MASK;
        carry = (digit)(acc >> PyLong_SHIFT);
    }
    return carry;
}

static digit
v_rshift(digit *z, digit *a, Py_ssize_t m, int d)
{
    Py_ssize_t i;
    digit carry = 0;
    digit mask = ((digit)1 << d) - 1U;

    assert(0 <= d && d < PyLong_SHIFT);
    for (i=m; i-- > 0;) {
        twodigits acc = (twodigits)carry << PyLong_SHIFT | a[i];
        carry = (digit)acc & mask;
        z[i] = (digit)(acc >> d);
    }
    return carry;
}

static digit
inplace_divrem1(digit *pout, digit *pin, Py_ssize_t size, digit n)
{
    twodigits rem = 0;

    assert(n > 0 && n <= PyLong_MASK);
    pin += size;
    pout += size;
    while (--size >= 0) {
        digit hi;
        rem = (rem << PyLong_SHIFT) | *--pin;
        *--pout = hi = (digit)(rem / n);
        rem -= (twodigits)hi * n;
    }
    return (digit)rem;
}

static PyLongObject *
divrem1(PyLongObject *a, digit n, digit *prem)
{
    const Py_ssize_t size = ABS(Py_SIZE(a));
    PyLongObject *z;

    assert(n > 0 && n <= PyLong_MASK);
    z = _PyLong_New(size);
    if (z == NULL)
        return NULL;
    *prem = inplace_divrem1(z->ob_digit, a->ob_digit, size, n);
    return long_normalize(z);
}

static PyObject *
long_to_decimal_string(PyObject *aa, int addL)
{
    PyLongObject *scratch, *a;
    PyObject *str;
    Py_ssize_t size, strlen, size_a, i, j;
    digit *pout, *pin, rem, tenpow;
    char *p;
    int negative;

    a = (PyLongObject *)aa;
    if (a == NULL || !PyLong_Check(a)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    size_a = ABS(Py_SIZE(a));
    negative = Py_SIZE(a) < 0;

    if (size_a > PY_SSIZE_T_MAX / PyLong_SHIFT) {
        PyErr_SetString(PyExc_OverflowError,
                        "long is too large to format");
        return NULL;
    }

    size = 1 + size_a * PyLong_SHIFT / (3 * _PyLong_DECIMAL_SHIFT);
    scratch = _PyLong_New(size);
    if (scratch == NULL)
        return NULL;

    pin = a->ob_digit;
    pout = scratch->ob_digit;
    size = 0;
    for (i = size_a; --i >= 0; ) {
        digit hi = pin[i];
        for (j = 0; j < size; j++) {
            twodigits z = (twodigits)pout[j] << PyLong_SHIFT | hi;
            hi = (digit)(z / _PyLong_DECIMAL_BASE);
            pout[j] = (digit)(z - (twodigits)hi *
                              _PyLong_DECIMAL_BASE);
        }
        while (hi) {
            pout[size++] = hi % _PyLong_DECIMAL_BASE;
            hi /= _PyLong_DECIMAL_BASE;
        }

        SIGCHECK({
                Py_DECREF(scratch);
                return NULL;
            });
    }

    if (size == 0)
        pout[size++] = 0;

    strlen = (addL != 0) + negative +
        1 + (size - 1) * _PyLong_DECIMAL_SHIFT;
    tenpow = 10;
    rem = pout[size-1];
    while (rem >= tenpow) {
        tenpow *= 10;
        strlen++;
    }
    str = PyString_FromStringAndSize(NULL, strlen);
    if (str == NULL) {
        Py_DECREF(scratch);
        return NULL;
    }

    p = PyString_AS_STRING(str) + strlen;
    *p = '\0';
    if (addL)
        *--p = 'L';

    for (i=0; i < size - 1; i++) {
        rem = pout[i];
        for (j = 0; j < _PyLong_DECIMAL_SHIFT; j++) {
            *--p = '0' + rem % 10;
            rem /= 10;
        }
    }

    rem = pout[i];
    do {
        *--p = '0' + rem % 10;
        rem /= 10;
    } while (rem != 0);

    if (negative)
        *--p = '-';

    assert(p == PyString_AS_STRING(str));
    Py_DECREF(scratch);
    return (PyObject *)str;
}

PyAPI_FUNC(PyObject *)
_PyLong_Format(PyObject *aa, int base, int addL, int newstyle)
{
    register PyLongObject *a = (PyLongObject *)aa;
    PyStringObject *str;
    Py_ssize_t i, sz;
    Py_ssize_t size_a;
    char *p;
    int bits;
    char sign = '\0';

    if (base == 10)
        return long_to_decimal_string((PyObject *)a, addL);

    if (a == NULL || !PyLong_Check(a)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    assert(base >= 2 && base <= 36);
    size_a = ABS(Py_SIZE(a));

    i = base;
    bits = 0;
    while (i > 1) {
        ++bits;
        i >>= 1;
    }
    i = 5 + (addL ? 1 : 0);

    if (size_a > (PY_SSIZE_T_MAX - i) / PyLong_SHIFT) {
        PyErr_SetString(PyExc_OverflowError,
                        "long is too large to format");
        return NULL;
    }
    sz = i + 1 + (size_a * PyLong_SHIFT - 1) / bits;
    assert(sz >= 0);
    str = (PyStringObject *) PyString_FromStringAndSize((char *)0, sz);
    if (str == NULL)
        return NULL;
    p = PyString_AS_STRING(str) + sz;
    *p = '\0';
    if (addL)
        *--p = 'L';
    if (a->ob_size < 0)
        sign = '-';

    if (a->ob_size == 0) {
        *--p = '0';
    }
    else if ((base & (base - 1)) == 0) {

        twodigits accum = 0;
        int accumbits = 0;
        int basebits = 1;
        i = base;
        while ((i >>= 1) > 1)
            ++basebits;

        for (i = 0; i < size_a; ++i) {
            accum |= (twodigits)a->ob_digit[i] << accumbits;
            accumbits += PyLong_SHIFT;
            assert(accumbits >= basebits);
            do {
                char cdigit = (char)(accum & (base - 1));
                cdigit += (cdigit < 10) ? '0' : 'a'-10;
                assert(p > PyString_AS_STRING(str));
                *--p = cdigit;
                accumbits -= basebits;
                accum >>= basebits;
            } while (i < size_a-1 ? accumbits >= basebits : accum > 0);
        }
    }
    else {

        Py_ssize_t size = size_a;
        digit *pin = a->ob_digit;
        PyLongObject *scratch;

        digit powbase = base;
        int power = 1;
        for (;;) {
            twodigits newpow = powbase * (twodigits)base;
            if (newpow >> PyLong_SHIFT)

                break;
            powbase = (digit)newpow;
            ++power;
        }

        scratch = _PyLong_New(size);
        if (scratch == NULL) {
            Py_DECREF(str);
            return NULL;
        }

        do {
            int ntostore = power;
            digit rem = inplace_divrem1(scratch->ob_digit,
                                        pin, size, powbase);
            pin = scratch->ob_digit;
            if (pin[size - 1] == 0)
                --size;
            SIGCHECK({
                    Py_DECREF(scratch);
                    Py_DECREF(str);
                    return NULL;
                });

            assert(ntostore > 0);
            do {
                digit nextrem = (digit)(rem / base);
                char c = (char)(rem - nextrem * base);
                assert(p > PyString_AS_STRING(str));
                c += (c < 10) ? '0' : 'a'-10;
                *--p = c;
                rem = nextrem;
                --ntostore;

            } while (ntostore && (size || rem));
        } while (size != 0);
        Py_DECREF(scratch);
    }

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
            if (size_a != 0)
                *--p = '0';
    }
    else if (base == 16) {
        *--p = 'x';
        *--p = '0';
    }
    else if (base != 10) {
        *--p = '#';
        *--p = '0' + base%10;
        if (base > 10)
            *--p = '0' + base/10;
    }
    if (sign)
        *--p = sign;
    if (p != PyString_AS_STRING(str)) {
        char *q = PyString_AS_STRING(str);
        assert(p > q);
        do {
        } while ((*q++ = *p++) != '\0');
        q--;
        _PyString_Resize((PyObject **)&str,
                         (Py_ssize_t) (q - PyString_AS_STRING(str)));
    }
    return (PyObject *)str;
}

int _PyLong_DigitValue[256] = {
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  37, 37, 37, 37, 37, 37,
    37, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 37, 37, 37, 37,
    37, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
};

static PyLongObject *
long_from_binary_base(char **str, int base)
{
    char *p = *str;
    char *start = p;
    int bits_per_char;
    Py_ssize_t n;
    PyLongObject *z;
    twodigits accum;
    int bits_in_accum;
    digit *pdigit;

    assert(base >= 2 && base <= 32 && (base & (base - 1)) == 0);
    n = base;
    for (bits_per_char = -1; n; ++bits_per_char)
        n >>= 1;

    while (_PyLong_DigitValue[Py_CHARMASK(*p)] < base)
        ++p;
    *str = p;

    n = (p - start) * bits_per_char + PyLong_SHIFT - 1;
    if (n / bits_per_char < p - start) {
        PyErr_SetString(PyExc_ValueError,
                        "long string too large to convert");
        return NULL;
    }
    n = n / PyLong_SHIFT;
    z = _PyLong_New(n);
    if (z == NULL)
        return NULL;

    accum = 0;
    bits_in_accum = 0;
    pdigit = z->ob_digit;
    while (--p >= start) {
        int k = _PyLong_DigitValue[Py_CHARMASK(*p)];
        assert(k >= 0 && k < base);
        accum |= (twodigits)k << bits_in_accum;
        bits_in_accum += bits_per_char;
        if (bits_in_accum >= PyLong_SHIFT) {
            *pdigit++ = (digit)(accum & PyLong_MASK);
            assert(pdigit - z->ob_digit <= n);
            accum >>= PyLong_SHIFT;
            bits_in_accum -= PyLong_SHIFT;
            assert(bits_in_accum < PyLong_SHIFT);
        }
    }
    if (bits_in_accum) {
        assert(bits_in_accum <= PyLong_SHIFT);
        *pdigit++ = (digit)accum;
        assert(pdigit - z->ob_digit <= n);
    }
    while (pdigit - z->ob_digit < n)
        *pdigit++ = 0;
    return long_normalize(z);
}

PyObject *
PyLong_FromString(char *str, char **pend, int base)
{
    int sign = 1;
    char *start, *orig_str = str;
    PyLongObject *z;
    PyObject *strobj, *strrepr;
    Py_ssize_t slen;

    if ((base != 0 && base < 2) || base > 36) {
        PyErr_SetString(PyExc_ValueError,
                        "long() arg 2 must be >= 2 and <= 36");
        return NULL;
    }
    while (*str != '\0' && isspace(Py_CHARMASK(*str)))
        str++;
    if (*str == '+')
        ++str;
    else if (*str == '-') {
        ++str;
        sign = -1;
    }
    while (*str != '\0' && isspace(Py_CHARMASK(*str)))
        str++;
    if (base == 0) {

        if (str[0] != '0')
            base = 10;
        else if (str[1] == 'x' || str[1] == 'X')
            base = 16;
        else if (str[1] == 'o' || str[1] == 'O')
            base = 8;
        else if (str[1] == 'b' || str[1] == 'B')
            base = 2;
        else

            base = 8;
    }

    if (str[0] == '0' &&
        ((base == 16 && (str[1] == 'x' || str[1] == 'X')) ||
         (base == 8  && (str[1] == 'o' || str[1] == 'O')) ||
         (base == 2  && (str[1] == 'b' || str[1] == 'B'))))
        str += 2;

    start = str;
    if ((base & (base - 1)) == 0)
        z = long_from_binary_base(&str, base);
    else {

        register twodigits c;
        Py_ssize_t size_z;
        int i;
        int convwidth;
        twodigits convmultmax, convmult;
        digit *pz, *pzstop;
        char* scan;

        static double log_base_PyLong_BASE[37] = {0.0e0,};
        static int convwidth_base[37] = {0,};
        static twodigits convmultmax_base[37] = {0,};

        if (log_base_PyLong_BASE[base] == 0.0) {
            twodigits convmax = base;
            int i = 1;

            log_base_PyLong_BASE[base] = (log((double)base) /
                                          log((double)PyLong_BASE));
            for (;;) {
                twodigits next = convmax * base;
                if (next > PyLong_BASE)
                    break;
                convmax = next;
                ++i;
            }
            convmultmax_base[base] = convmax;
            assert(i > 0);
            convwidth_base[base] = i;
        }

        scan = str;
        while (_PyLong_DigitValue[Py_CHARMASK(*scan)] < base)
            ++scan;

        size_z = (Py_ssize_t)((scan - str) * log_base_PyLong_BASE[base]) + 1;

        assert(size_z > 0);
        z = _PyLong_New(size_z);
        if (z == NULL)
            return NULL;
        Py_SIZE(z) = 0;

        convwidth = convwidth_base[base];
        convmultmax = convmultmax_base[base];

        while (str < scan) {

            c = (digit)_PyLong_DigitValue[Py_CHARMASK(*str++)];
            for (i = 1; i < convwidth && str != scan; ++i, ++str) {
                c = (twodigits)(c *  base +
                                _PyLong_DigitValue[Py_CHARMASK(*str)]);
                assert(c < PyLong_BASE);
            }

            convmult = convmultmax;

            if (i != convwidth) {
                convmult = base;
                for ( ; i > 1; --i)
                    convmult *= base;
            }

            pz = z->ob_digit;
            pzstop = pz + Py_SIZE(z);
            for (; pz < pzstop; ++pz) {
                c += (twodigits)*pz * convmult;
                *pz = (digit)(c & PyLong_MASK);
                c >>= PyLong_SHIFT;
            }

            if (c) {
                assert(c < PyLong_BASE);
                if (Py_SIZE(z) < size_z) {
                    *pz = (digit)c;
                    ++Py_SIZE(z);
                }
                else {
                    PyLongObject *tmp;

                    assert(Py_SIZE(z) == size_z);
                    tmp = _PyLong_New(size_z + 1);
                    if (tmp == NULL) {
                        Py_DECREF(z);
                        return NULL;
                    }
                    memcpy(tmp->ob_digit,
                           z->ob_digit,
                           sizeof(digit) * size_z);
                    Py_DECREF(z);
                    z = tmp;
                    z->ob_digit[size_z] = (digit)c;
                    ++size_z;
                }
            }
        }
    }
    if (z == NULL)
        return NULL;
    if (str == start)
        goto onError;
    if (sign < 0)
        Py_SIZE(z) = -(Py_SIZE(z));
    if (*str == 'L' || *str == 'l')
        str++;
    while (*str && isspace(Py_CHARMASK(*str)))
        str++;
    if (*str != '\0')
        goto onError;
    if (pend)
        *pend = str;
    return (PyObject *) z;

  onError:
    Py_XDECREF(z);
    slen = strlen(orig_str) < 200 ? strlen(orig_str) : 200;
    strobj = PyString_FromStringAndSize(orig_str, slen);
    if (strobj == NULL)
        return NULL;
    strrepr = PyObject_Repr(strobj);
    Py_DECREF(strobj);
    if (strrepr == NULL)
        return NULL;
    PyErr_Format(PyExc_ValueError,
                 "invalid literal for long() with base %d: %s",
                 base, PyString_AS_STRING(strrepr));
    Py_DECREF(strrepr);
    return NULL;
}

#ifdef Py_USING_UNICODE
PyObject *
PyLong_FromUnicode(Py_UNICODE *u, Py_ssize_t length, int base)
{
    PyObject *result;
    char *buffer = (char *)PyMem_MALLOC(length+1);

    if (buffer == NULL)
        return NULL;

    if (PyUnicode_EncodeDecimal(u, length, buffer, NULL)) {
        PyMem_FREE(buffer);
        return NULL;
    }
    result = PyLong_FromString(buffer, NULL, base);
    PyMem_FREE(buffer);
    return result;
}
#endif

static PyLongObject *x_divrem
    (PyLongObject *, PyLongObject *, PyLongObject **);
static PyObject *long_long(PyObject *v);

static int
long_divrem(PyLongObject *a, PyLongObject *b,
            PyLongObject **pdiv, PyLongObject **prem)
{
    Py_ssize_t size_a = ABS(Py_SIZE(a)), size_b = ABS(Py_SIZE(b));
    PyLongObject *z;

    if (size_b == 0) {
        PyErr_SetString(PyExc_ZeroDivisionError,
                        "long division or modulo by zero");
        return -1;
    }
    if (size_a < size_b ||
        (size_a == size_b &&
         a->ob_digit[size_a-1] < b->ob_digit[size_b-1])) {

        *pdiv = _PyLong_New(0);
        if (*pdiv == NULL)
            return -1;
        Py_INCREF(a);
        *prem = (PyLongObject *) a;
        return 0;
    }
    if (size_b == 1) {
        digit rem = 0;
        z = divrem1(a, b->ob_digit[0], &rem);
        if (z == NULL)
            return -1;
        *prem = (PyLongObject *) PyLong_FromLong((long)rem);
        if (*prem == NULL) {
            Py_DECREF(z);
            return -1;
        }
    }
    else {
        z = x_divrem(a, b, prem);
        if (z == NULL)
            return -1;
    }

    if ((a->ob_size < 0) != (b->ob_size < 0))
        z->ob_size = -(z->ob_size);
    if (a->ob_size < 0 && (*prem)->ob_size != 0)
        (*prem)->ob_size = -((*prem)->ob_size);
    *pdiv = z;
    return 0;
}

static PyLongObject *
x_divrem(PyLongObject *v1, PyLongObject *w1, PyLongObject **prem)
{
    PyLongObject *v, *w, *a;
    Py_ssize_t i, k, size_v, size_w;
    int d;
    digit wm1, wm2, carry, q, r, vtop, *v0, *vk, *w0, *ak;
    twodigits vv;
    sdigit zhi;
    stwodigits z;

    size_v = ABS(Py_SIZE(v1));
    size_w = ABS(Py_SIZE(w1));
    assert(size_v >= size_w && size_w >= 2);
    v = _PyLong_New(size_v+1);
    if (v == NULL) {
        *prem = NULL;
        return NULL;
    }
    w = _PyLong_New(size_w);
    if (w == NULL) {
        Py_DECREF(v);
        *prem = NULL;
        return NULL;
    }

    d = PyLong_SHIFT - bits_in_digit(w1->ob_digit[size_w-1]);
    carry = v_lshift(w->ob_digit, w1->ob_digit, size_w, d);
    assert(carry == 0);
    carry = v_lshift(v->ob_digit, v1->ob_digit, size_v, d);
    if (carry != 0 || v->ob_digit[size_v-1] >= w->ob_digit[size_w-1]) {
        v->ob_digit[size_v] = carry;
        size_v++;
    }

    k = size_v - size_w;
    assert(k >= 0);
    a = _PyLong_New(k);
    if (a == NULL) {
        Py_DECREF(w);
        Py_DECREF(v);
        *prem = NULL;
        return NULL;
    }
    v0 = v->ob_digit;
    w0 = w->ob_digit;
    wm1 = w0[size_w-1];
    wm2 = w0[size_w-2];
    for (vk = v0+k, ak = a->ob_digit + k; vk-- > v0;) {

        SIGCHECK({
                Py_DECREF(a);
                Py_DECREF(w);
                Py_DECREF(v);
                *prem = NULL;
                return NULL;
            });

        vtop = vk[size_w];
        assert(vtop <= wm1);
        vv = ((twodigits)vtop << PyLong_SHIFT) | vk[size_w-1];
        q = (digit)(vv / wm1);
        r = (digit)(vv - (twodigits)wm1 * q);
        while ((twodigits)wm2 * q > (((twodigits)r << PyLong_SHIFT)
                                     | vk[size_w-2])) {
            --q;
            r += wm1;
            if (r >= PyLong_BASE)
                break;
        }
        assert(q <= PyLong_BASE);

        zhi = 0;
        for (i = 0; i < size_w; ++i) {

            z = (sdigit)vk[i] + zhi -
                (stwodigits)q * (stwodigits)w0[i];
            vk[i] = (digit)z & PyLong_MASK;
            zhi = (sdigit)Py_ARITHMETIC_RIGHT_SHIFT(stwodigits,
                                                    z, PyLong_SHIFT);
        }

        assert((sdigit)vtop + zhi == -1 || (sdigit)vtop + zhi == 0);
        if ((sdigit)vtop + zhi < 0) {
            carry = 0;
            for (i = 0; i < size_w; ++i) {
                carry += vk[i] + w0[i];
                vk[i] = carry & PyLong_MASK;
                carry >>= PyLong_SHIFT;
            }
            --q;
        }

        assert(q < PyLong_BASE);
        *--ak = q;
    }

    carry = v_rshift(w0, v0, size_w, d);
    assert(carry==0);
    Py_DECREF(v);

    *prem = long_normalize(w);
    return long_normalize(a);
}

#if DBL_MANT_DIG == 53
#define EXP2_DBL_MANT_DIG 9007199254740992.0
#else
#define EXP2_DBL_MANT_DIG (ldexp(1.0, DBL_MANT_DIG))
#endif

double
_PyLong_Frexp(PyLongObject *a, Py_ssize_t *e)
{
    Py_ssize_t a_size, a_bits, shift_digits, shift_bits, x_size;

    digit rem, x_digits[2 + (DBL_MANT_DIG + 1) / PyLong_SHIFT];
    double dx;

    static const int half_even_correction[8] = {0, -1, -2, 1, 0, -1, 2, 1};

    a_size = ABS(Py_SIZE(a));
    if (a_size == 0) {

        *e = 0;
        return 0.0;
    }
    a_bits = bits_in_digit(a->ob_digit[a_size-1]);

    if (a_size >= (PY_SSIZE_T_MAX - 1) / PyLong_SHIFT + 1 &&
        (a_size > (PY_SSIZE_T_MAX - 1) / PyLong_SHIFT + 1 ||
         a_bits > (PY_SSIZE_T_MAX - 1) % PyLong_SHIFT + 1))
        goto overflow;
    a_bits = (a_size - 1) * PyLong_SHIFT + a_bits;

    if (a_bits <= DBL_MANT_DIG + 2) {
        shift_digits = (DBL_MANT_DIG + 2 - a_bits) / PyLong_SHIFT;
        shift_bits = (DBL_MANT_DIG + 2 - a_bits) % PyLong_SHIFT;
        x_size = 0;
        while (x_size < shift_digits)
            x_digits[x_size++] = 0;
        rem = v_lshift(x_digits + x_size, a->ob_digit, a_size,
                       (int)shift_bits);
        x_size += a_size;
        x_digits[x_size++] = rem;
    }
    else {
        shift_digits = (a_bits - DBL_MANT_DIG - 2) / PyLong_SHIFT;
        shift_bits = (a_bits - DBL_MANT_DIG - 2) % PyLong_SHIFT;
        rem = v_rshift(x_digits, a->ob_digit + shift_digits,
                       a_size - shift_digits, (int)shift_bits);
        x_size = a_size - shift_digits;

        if (rem)
            x_digits[0] |= 1;
        else
            while (shift_digits > 0)
                if (a->ob_digit[--shift_digits]) {
                    x_digits[0] |= 1;
                    break;
                }
    }
    assert(1 <= x_size &&
           x_size <= (Py_ssize_t)(sizeof(x_digits)/sizeof(digit)));

    x_digits[0] += half_even_correction[x_digits[0] & 7];
    dx = x_digits[--x_size];
    while (x_size > 0)
        dx = dx * PyLong_BASE + x_digits[--x_size];

    dx /= 4.0 * EXP2_DBL_MANT_DIG;
    if (dx == 1.0) {
        if (a_bits == PY_SSIZE_T_MAX)
            goto overflow;
        dx = 0.5;
        a_bits += 1;
    }

    *e = a_bits;
    return Py_SIZE(a) < 0 ? -dx : dx;

  overflow:

    PyErr_SetString(PyExc_OverflowError,
                    "huge integer: number of bits overflows a Py_ssize_t");
    *e = 0;
    return -1.0;
}

double
PyLong_AsDouble(PyObject *v)
{
    Py_ssize_t exponent;
    double x;

    if (v == NULL || !PyLong_Check(v)) {
        PyErr_BadInternalCall();
        return -1.0;
    }
    x = _PyLong_Frexp((PyLongObject *)v, &exponent);
    if ((x == -1.0 && PyErr_Occurred()) || exponent > DBL_MAX_EXP) {
        PyErr_SetString(PyExc_OverflowError,
                        "long int too large to convert to float");
        return -1.0;
    }
    return ldexp(x, (int)exponent);
}

static void
long_dealloc(PyObject *v)
{
    Py_TYPE(v)->tp_free(v);
}

static PyObject *
long_repr(PyObject *v)
{
    return _PyLong_Format(v, 10, 1, 0);
}

static PyObject *
long_str(PyObject *v)
{
    return _PyLong_Format(v, 10, 0, 0);
}

static int
long_compare(PyLongObject *a, PyLongObject *b)
{
    Py_ssize_t sign;

    if (Py_SIZE(a) != Py_SIZE(b)) {
        sign = Py_SIZE(a) - Py_SIZE(b);
    }
    else {
        Py_ssize_t i = ABS(Py_SIZE(a));
        while (--i >= 0 && a->ob_digit[i] == b->ob_digit[i])
            ;
        if (i < 0)
            sign = 0;
        else {
            sign = (sdigit)a->ob_digit[i] - (sdigit)b->ob_digit[i];
            if (Py_SIZE(a) < 0)
                sign = -sign;
        }
    }
    return sign < 0 ? -1 : sign > 0 ? 1 : 0;
}

static long
long_hash(PyLongObject *v)
{
    unsigned long x;
    Py_ssize_t i;
    int sign;

    i = v->ob_size;
    sign = 1;
    x = 0;
    if (i < 0) {
        sign = -1;
        i = -(i);
    }

    while (--i >= 0) {

        x = (x >> (8*SIZEOF_LONG-PyLong_SHIFT)) | (x << PyLong_SHIFT);
        x += v->ob_digit[i];

        if (x < v->ob_digit[i])
            x++;
    }
    x = x * sign;
    if (x == (unsigned long)-1)
        x = (unsigned long)-2;
    return (long)x;
}

static PyLongObject *
x_add(PyLongObject *a, PyLongObject *b)
{
    Py_ssize_t size_a = ABS(Py_SIZE(a)), size_b = ABS(Py_SIZE(b));
    PyLongObject *z;
    Py_ssize_t i;
    digit carry = 0;

    if (size_a < size_b) {
        { PyLongObject *temp = a; a = b; b = temp; }
        { Py_ssize_t size_temp = size_a;
            size_a = size_b;
            size_b = size_temp; }
    }
    z = _PyLong_New(size_a+1);
    if (z == NULL)
        return NULL;
    for (i = 0; i < size_b; ++i) {
        carry += a->ob_digit[i] + b->ob_digit[i];
        z->ob_digit[i] = carry & PyLong_MASK;
        carry >>= PyLong_SHIFT;
    }
    for (; i < size_a; ++i) {
        carry += a->ob_digit[i];
        z->ob_digit[i] = carry & PyLong_MASK;
        carry >>= PyLong_SHIFT;
    }
    z->ob_digit[i] = carry;
    return long_normalize(z);
}

static PyLongObject *
x_sub(PyLongObject *a, PyLongObject *b)
{
    Py_ssize_t size_a = ABS(Py_SIZE(a)), size_b = ABS(Py_SIZE(b));
    PyLongObject *z;
    Py_ssize_t i;
    int sign = 1;
    digit borrow = 0;

    if (size_a < size_b) {
        sign = -1;
        { PyLongObject *temp = a; a = b; b = temp; }
        { Py_ssize_t size_temp = size_a;
            size_a = size_b;
            size_b = size_temp; }
    }
    else if (size_a == size_b) {

        i = size_a;
        while (--i >= 0 && a->ob_digit[i] == b->ob_digit[i])
            ;
        if (i < 0)
            return _PyLong_New(0);
        if (a->ob_digit[i] < b->ob_digit[i]) {
            sign = -1;
            { PyLongObject *temp = a; a = b; b = temp; }
        }
        size_a = size_b = i+1;
    }
    z = _PyLong_New(size_a);
    if (z == NULL)
        return NULL;
    for (i = 0; i < size_b; ++i) {

        borrow = a->ob_digit[i] - b->ob_digit[i] - borrow;
        z->ob_digit[i] = borrow & PyLong_MASK;
        borrow >>= PyLong_SHIFT;
        borrow &= 1;
    }
    for (; i < size_a; ++i) {
        borrow = a->ob_digit[i] - borrow;
        z->ob_digit[i] = borrow & PyLong_MASK;
        borrow >>= PyLong_SHIFT;
        borrow &= 1;
    }
    assert(borrow == 0);
    if (sign < 0)
        z->ob_size = -(z->ob_size);
    return long_normalize(z);
}

static PyObject *
long_add(PyLongObject *v, PyLongObject *w)
{
    PyLongObject *a, *b, *z;

    CONVERT_BINOP((PyObject *)v, (PyObject *)w, &a, &b);

    if (a->ob_size < 0) {
        if (b->ob_size < 0) {
            z = x_add(a, b);
            if (z != NULL && z->ob_size != 0)
                z->ob_size = -(z->ob_size);
        }
        else
            z = x_sub(b, a);
    }
    else {
        if (b->ob_size < 0)
            z = x_sub(a, b);
        else
            z = x_add(a, b);
    }
    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *)z;
}

static PyObject *
long_sub(PyLongObject *v, PyLongObject *w)
{
    PyLongObject *a, *b, *z;

    CONVERT_BINOP((PyObject *)v, (PyObject *)w, &a, &b);

    if (a->ob_size < 0) {
        if (b->ob_size < 0)
            z = x_sub(a, b);
        else
            z = x_add(a, b);
        if (z != NULL && z->ob_size != 0)
            z->ob_size = -(z->ob_size);
    }
    else {
        if (b->ob_size < 0)
            z = x_add(a, b);
        else
            z = x_sub(a, b);
    }
    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *)z;
}

static PyLongObject *
x_mul(PyLongObject *a, PyLongObject *b)
{
    PyLongObject *z;
    Py_ssize_t size_a = ABS(Py_SIZE(a));
    Py_ssize_t size_b = ABS(Py_SIZE(b));
    Py_ssize_t i;

    z = _PyLong_New(size_a + size_b);
    if (z == NULL)
        return NULL;

    memset(z->ob_digit, 0, Py_SIZE(z) * sizeof(digit));
    if (a == b) {

        for (i = 0; i < size_a; ++i) {
            twodigits carry;
            twodigits f = a->ob_digit[i];
            digit *pz = z->ob_digit + (i << 1);
            digit *pa = a->ob_digit + i + 1;
            digit *paend = a->ob_digit + size_a;

            SIGCHECK({
                    Py_DECREF(z);
                    return NULL;
                });

            carry = *pz + f * f;
            *pz++ = (digit)(carry & PyLong_MASK);
            carry >>= PyLong_SHIFT;
            assert(carry <= PyLong_MASK);

            f <<= 1;
            while (pa < paend) {
                carry += *pz + *pa++ * f;
                *pz++ = (digit)(carry & PyLong_MASK);
                carry >>= PyLong_SHIFT;
                assert(carry <= (PyLong_MASK << 1));
            }
            if (carry) {
                carry += *pz;
                *pz++ = (digit)(carry & PyLong_MASK);
                carry >>= PyLong_SHIFT;
            }
            if (carry)
                *pz += (digit)(carry & PyLong_MASK);
            assert((carry >> PyLong_SHIFT) == 0);
        }
    }
    else {
        for (i = 0; i < size_a; ++i) {
            twodigits carry = 0;
            twodigits f = a->ob_digit[i];
            digit *pz = z->ob_digit + i;
            digit *pb = b->ob_digit;
            digit *pbend = b->ob_digit + size_b;

            SIGCHECK({
                    Py_DECREF(z);
                    return NULL;
                });

            while (pb < pbend) {
                carry += *pz + *pb++ * f;
                *pz++ = (digit)(carry & PyLong_MASK);
                carry >>= PyLong_SHIFT;
                assert(carry <= PyLong_MASK);
            }
            if (carry)
                *pz += (digit)(carry & PyLong_MASK);
            assert((carry >> PyLong_SHIFT) == 0);
        }
    }
    return long_normalize(z);
}

static int
kmul_split(PyLongObject *n,
           Py_ssize_t size,
           PyLongObject **high,
           PyLongObject **low)
{
    PyLongObject *hi, *lo;
    Py_ssize_t size_lo, size_hi;
    const Py_ssize_t size_n = ABS(Py_SIZE(n));

    size_lo = MIN(size_n, size);
    size_hi = size_n - size_lo;

    if ((hi = _PyLong_New(size_hi)) == NULL)
        return -1;
    if ((lo = _PyLong_New(size_lo)) == NULL) {
        Py_DECREF(hi);
        return -1;
    }

    memcpy(lo->ob_digit, n->ob_digit, size_lo * sizeof(digit));
    memcpy(hi->ob_digit, n->ob_digit + size_lo, size_hi * sizeof(digit));

    *high = long_normalize(hi);
    *low = long_normalize(lo);
    return 0;
}

static PyLongObject *k_lopsided_mul(PyLongObject *a, PyLongObject *b);

static PyLongObject *
k_mul(PyLongObject *a, PyLongObject *b)
{
    Py_ssize_t asize = ABS(Py_SIZE(a));
    Py_ssize_t bsize = ABS(Py_SIZE(b));
    PyLongObject *ah = NULL;
    PyLongObject *al = NULL;
    PyLongObject *bh = NULL;
    PyLongObject *bl = NULL;
    PyLongObject *ret = NULL;
    PyLongObject *t1, *t2, *t3;
    Py_ssize_t shift;
    Py_ssize_t i;

    if (asize > bsize) {
        t1 = a;
        a = b;
        b = t1;

        i = asize;
        asize = bsize;
        bsize = i;
    }

    i = a == b ? KARATSUBA_SQUARE_CUTOFF : KARATSUBA_CUTOFF;
    if (asize <= i) {
        if (asize == 0)
            return _PyLong_New(0);
        else
            return x_mul(a, b);
    }

    if (2 * asize <= bsize)
        return k_lopsided_mul(a, b);

    shift = bsize >> 1;
    if (kmul_split(a, shift, &ah, &al) < 0) goto fail;
    assert(Py_SIZE(ah) > 0);

    if (a == b) {
        bh = ah;
        bl = al;
        Py_INCREF(bh);
        Py_INCREF(bl);
    }
    else if (kmul_split(b, shift, &bh, &bl) < 0) goto fail;

    ret = _PyLong_New(asize + bsize);
    if (ret == NULL) goto fail;
#ifdef Py_DEBUG

    memset(ret->ob_digit, 0xDF, Py_SIZE(ret) * sizeof(digit));
#endif

    if ((t1 = k_mul(ah, bh)) == NULL) goto fail;
    assert(Py_SIZE(t1) >= 0);
    assert(2*shift + Py_SIZE(t1) <= Py_SIZE(ret));
    memcpy(ret->ob_digit + 2*shift, t1->ob_digit,
           Py_SIZE(t1) * sizeof(digit));

    i = Py_SIZE(ret) - 2*shift - Py_SIZE(t1);
    if (i)
        memset(ret->ob_digit + 2*shift + Py_SIZE(t1), 0,
               i * sizeof(digit));

    if ((t2 = k_mul(al, bl)) == NULL) {
        Py_DECREF(t1);
        goto fail;
    }
    assert(Py_SIZE(t2) >= 0);
    assert(Py_SIZE(t2) <= 2*shift);
    memcpy(ret->ob_digit, t2->ob_digit, Py_SIZE(t2) * sizeof(digit));

    i = 2*shift - Py_SIZE(t2);
    if (i)
        memset(ret->ob_digit + Py_SIZE(t2), 0, i * sizeof(digit));

    i = Py_SIZE(ret) - shift;
    (void)v_isub(ret->ob_digit + shift, i, t2->ob_digit, Py_SIZE(t2));
    Py_DECREF(t2);

    (void)v_isub(ret->ob_digit + shift, i, t1->ob_digit, Py_SIZE(t1));
    Py_DECREF(t1);

    if ((t1 = x_add(ah, al)) == NULL) goto fail;
    Py_DECREF(ah);
    Py_DECREF(al);
    ah = al = NULL;

    if (a == b) {
        t2 = t1;
        Py_INCREF(t2);
    }
    else if ((t2 = x_add(bh, bl)) == NULL) {
        Py_DECREF(t1);
        goto fail;
    }
    Py_DECREF(bh);
    Py_DECREF(bl);
    bh = bl = NULL;

    t3 = k_mul(t1, t2);
    Py_DECREF(t1);
    Py_DECREF(t2);
    if (t3 == NULL) goto fail;
    assert(Py_SIZE(t3) >= 0);

    (void)v_iadd(ret->ob_digit + shift, i, t3->ob_digit, Py_SIZE(t3));
    Py_DECREF(t3);

    return long_normalize(ret);

  fail:
    Py_XDECREF(ret);
    Py_XDECREF(ah);
    Py_XDECREF(al);
    Py_XDECREF(bh);
    Py_XDECREF(bl);
    return NULL;
}

static PyLongObject *
k_lopsided_mul(PyLongObject *a, PyLongObject *b)
{
    const Py_ssize_t asize = ABS(Py_SIZE(a));
    Py_ssize_t bsize = ABS(Py_SIZE(b));
    Py_ssize_t nbdone;
    PyLongObject *ret;
    PyLongObject *bslice = NULL;

    assert(asize > KARATSUBA_CUTOFF);
    assert(2 * asize <= bsize);

    ret = _PyLong_New(asize + bsize);
    if (ret == NULL)
        return NULL;
    memset(ret->ob_digit, 0, Py_SIZE(ret) * sizeof(digit));

    bslice = _PyLong_New(asize);
    if (bslice == NULL)
        goto fail;

    nbdone = 0;
    while (bsize > 0) {
        PyLongObject *product;
        const Py_ssize_t nbtouse = MIN(bsize, asize);

        memcpy(bslice->ob_digit, b->ob_digit + nbdone,
               nbtouse * sizeof(digit));
        Py_SIZE(bslice) = nbtouse;
        product = k_mul(a, bslice);
        if (product == NULL)
            goto fail;

        (void)v_iadd(ret->ob_digit + nbdone, Py_SIZE(ret) - nbdone,
                     product->ob_digit, Py_SIZE(product));
        Py_DECREF(product);

        bsize -= nbtouse;
        nbdone += nbtouse;
    }

    Py_DECREF(bslice);
    return long_normalize(ret);

  fail:
    Py_DECREF(ret);
    Py_XDECREF(bslice);
    return NULL;
}

static PyObject *
long_mul(PyLongObject *v, PyLongObject *w)
{
    PyLongObject *a, *b, *z;

    if (!convert_binop((PyObject *)v, (PyObject *)w, &a, &b)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    z = k_mul(a, b);

    if (((a->ob_size ^ b->ob_size) < 0) && z)
        z->ob_size = -(z->ob_size);
    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *)z;
}

static int
l_divmod(PyLongObject *v, PyLongObject *w,
         PyLongObject **pdiv, PyLongObject **pmod)
{
    PyLongObject *div, *mod;

    if (long_divrem(v, w, &div, &mod) < 0)
        return -1;
    if ((Py_SIZE(mod) < 0 && Py_SIZE(w) > 0) ||
        (Py_SIZE(mod) > 0 && Py_SIZE(w) < 0)) {
        PyLongObject *temp;
        PyLongObject *one;
        temp = (PyLongObject *) long_add(mod, w);
        Py_DECREF(mod);
        mod = temp;
        if (mod == NULL) {
            Py_DECREF(div);
            return -1;
        }
        one = (PyLongObject *) PyLong_FromLong(1L);
        if (one == NULL ||
            (temp = (PyLongObject *) long_sub(div, one)) == NULL) {
            Py_DECREF(mod);
            Py_DECREF(div);
            Py_XDECREF(one);
            return -1;
        }
        Py_DECREF(one);
        Py_DECREF(div);
        div = temp;
    }
    if (pdiv != NULL)
        *pdiv = div;
    else
        Py_DECREF(div);

    if (pmod != NULL)
        *pmod = mod;
    else
        Py_DECREF(mod);

    return 0;
}

static PyObject *
long_div(PyObject *v, PyObject *w)
{
    PyLongObject *a, *b, *div;

    CONVERT_BINOP(v, w, &a, &b);
    if (l_divmod(a, b, &div, NULL) < 0)
        div = NULL;
    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *)div;
}

static PyObject *
long_classic_div(PyObject *v, PyObject *w)
{
    PyLongObject *a, *b, *div;

    CONVERT_BINOP(v, w, &a, &b);
    if (Py_DivisionWarningFlag &&
        PyErr_Warn(PyExc_DeprecationWarning, "classic long division") < 0)
        div = NULL;
    else if (l_divmod(a, b, &div, NULL) < 0)
        div = NULL;
    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *)div;
}

#define MANT_DIG_DIGITS (DBL_MANT_DIG / PyLong_SHIFT)
#define MANT_DIG_BITS (DBL_MANT_DIG % PyLong_SHIFT)

static PyObject *
long_true_divide(PyObject *v, PyObject *w)
{
    PyLongObject *a, *b, *x;
    Py_ssize_t a_size, b_size, shift, extra_bits, diff, x_size, x_bits;
    digit mask, low;
    int inexact, negate, a_is_small, b_is_small;
    double dx, result;

    CONVERT_BINOP(v, w, &a, &b);

    a_size = ABS(Py_SIZE(a));
    b_size = ABS(Py_SIZE(b));
    negate = (Py_SIZE(a) < 0) ^ (Py_SIZE(b) < 0);
    if (b_size == 0) {
        PyErr_SetString(PyExc_ZeroDivisionError,
                        "division by zero");
        goto error;
    }
    if (a_size == 0)
        goto underflow_or_zero;

    a_is_small = a_size <= MANT_DIG_DIGITS ||
        (a_size == MANT_DIG_DIGITS+1 &&
         a->ob_digit[MANT_DIG_DIGITS] >> MANT_DIG_BITS == 0);
    b_is_small = b_size <= MANT_DIG_DIGITS ||
        (b_size == MANT_DIG_DIGITS+1 &&
         b->ob_digit[MANT_DIG_DIGITS] >> MANT_DIG_BITS == 0);
    if (a_is_small && b_is_small) {
        double da, db;
        da = a->ob_digit[--a_size];
        while (a_size > 0)
            da = da * PyLong_BASE + a->ob_digit[--a_size];
        db = b->ob_digit[--b_size];
        while (b_size > 0)
            db = db * PyLong_BASE + b->ob_digit[--b_size];
        result = da / db;
        goto success;
    }

    diff = a_size - b_size;
    if (diff > PY_SSIZE_T_MAX/PyLong_SHIFT - 1)

        goto overflow;
    else if (diff < 1 - PY_SSIZE_T_MAX/PyLong_SHIFT)

        goto underflow_or_zero;

    diff = diff * PyLong_SHIFT + bits_in_digit(a->ob_digit[a_size - 1]) -
        bits_in_digit(b->ob_digit[b_size - 1]);

    if (diff > DBL_MAX_EXP)
        goto overflow;
    else if (diff < DBL_MIN_EXP - DBL_MANT_DIG - 1)
        goto underflow_or_zero;

    shift = MAX(diff, DBL_MIN_EXP) - DBL_MANT_DIG - 2;

    inexact = 0;

    if (shift <= 0) {
        Py_ssize_t i, shift_digits = -shift / PyLong_SHIFT;
        digit rem;

        if (a_size >= PY_SSIZE_T_MAX - 1 - shift_digits) {

            PyErr_SetString(PyExc_OverflowError,
                            "intermediate overflow during division");
            goto error;
        }
        x = _PyLong_New(a_size + shift_digits + 1);
        if (x == NULL)
            goto error;
        for (i = 0; i < shift_digits; i++)
            x->ob_digit[i] = 0;
        rem = v_lshift(x->ob_digit + shift_digits, a->ob_digit,
                       a_size, -shift % PyLong_SHIFT);
        x->ob_digit[a_size + shift_digits] = rem;
    }
    else {
        Py_ssize_t shift_digits = shift / PyLong_SHIFT;
        digit rem;

        assert(a_size >= shift_digits);
        x = _PyLong_New(a_size - shift_digits);
        if (x == NULL)
            goto error;
        rem = v_rshift(x->ob_digit, a->ob_digit + shift_digits,
                       a_size - shift_digits, shift % PyLong_SHIFT);

        if (rem)
            inexact = 1;
        while (!inexact && shift_digits > 0)
            if (a->ob_digit[--shift_digits])
                inexact = 1;
    }
    long_normalize(x);
    x_size = Py_SIZE(x);

    if (b_size == 1) {
        digit rem = inplace_divrem1(x->ob_digit, x->ob_digit, x_size,
                              b->ob_digit[0]);
        long_normalize(x);
        if (rem)
            inexact = 1;
    }
    else {
        PyLongObject *div, *rem;
        div = x_divrem(x, b, &rem);
        Py_DECREF(x);
        x = div;
        if (x == NULL)
            goto error;
        if (Py_SIZE(rem))
            inexact = 1;
        Py_DECREF(rem);
    }
    x_size = ABS(Py_SIZE(x));
    assert(x_size > 0);
    x_bits = (x_size-1)*PyLong_SHIFT+bits_in_digit(x->ob_digit[x_size-1]);

    extra_bits = MAX(x_bits, DBL_MIN_EXP - shift) - DBL_MANT_DIG;
    assert(extra_bits == 2 || extra_bits == 3);

    mask = (digit)1 << (extra_bits - 1);
    low = x->ob_digit[0] | inexact;
    if ((low & mask) && (low & (3U*mask-1U)))
        low += mask;
    x->ob_digit[0] = low & ~(2U*mask-1U);

    dx = x->ob_digit[--x_size];
    while (x_size > 0)
        dx = dx * PyLong_BASE + x->ob_digit[--x_size];
    Py_DECREF(x);

    if (shift + x_bits >= DBL_MAX_EXP &&
        (shift + x_bits > DBL_MAX_EXP || dx == ldexp(1.0, (int)x_bits)))
        goto overflow;
    result = ldexp(dx, (int)shift);

  success:
    Py_DECREF(a);
    Py_DECREF(b);
    return PyFloat_FromDouble(negate ? -result : result);

  underflow_or_zero:
    Py_DECREF(a);
    Py_DECREF(b);
    return PyFloat_FromDouble(negate ? -0.0 : 0.0);

  overflow:
    PyErr_SetString(PyExc_OverflowError,
                    "integer division result too large for a float");
  error:
    Py_DECREF(a);
    Py_DECREF(b);
    return NULL;
}

static PyObject *
long_mod(PyObject *v, PyObject *w)
{
    PyLongObject *a, *b, *mod;

    CONVERT_BINOP(v, w, &a, &b);

    if (l_divmod(a, b, NULL, &mod) < 0)
        mod = NULL;
    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *)mod;
}

static PyObject *
long_divmod(PyObject *v, PyObject *w)
{
    PyLongObject *a, *b, *div, *mod;
    PyObject *z;

    CONVERT_BINOP(v, w, &a, &b);

    if (l_divmod(a, b, &div, &mod) < 0) {
        Py_DECREF(a);
        Py_DECREF(b);
        return NULL;
    }
    z = PyTuple_New(2);
    if (z != NULL) {
        PyTuple_SetItem(z, 0, (PyObject *) div);
        PyTuple_SetItem(z, 1, (PyObject *) mod);
    }
    else {
        Py_DECREF(div);
        Py_DECREF(mod);
    }
    Py_DECREF(a);
    Py_DECREF(b);
    return z;
}

static PyObject *
long_pow(PyObject *v, PyObject *w, PyObject *x)
{
    PyLongObject *a, *b, *c;
    int negativeOutput = 0;

    PyLongObject *z = NULL;
    Py_ssize_t i, j, k;
    PyLongObject *temp = NULL;

    PyLongObject *table[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    CONVERT_BINOP(v, w, &a, &b);
    if (PyLong_Check(x)) {
        c = (PyLongObject *)x;
        Py_INCREF(x);
    }
    else if (PyInt_Check(x)) {
        c = (PyLongObject *)PyLong_FromLong(PyInt_AS_LONG(x));
        if (c == NULL)
            goto Error;
    }
    else if (x == Py_None)
        c = NULL;
    else {
        Py_DECREF(a);
        Py_DECREF(b);
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    if (Py_SIZE(b) < 0) {
        if (c) {
            PyErr_SetString(PyExc_TypeError, "pow() 2nd argument "
                            "cannot be negative when 3rd argument specified");
            goto Error;
        }
        else {

            Py_DECREF(a);
            Py_DECREF(b);
            return PyFloat_Type.tp_as_number->nb_power(v, w, x);
        }
    }

    if (c) {

        if (Py_SIZE(c) == 0) {
            PyErr_SetString(PyExc_ValueError,
                            "pow() 3rd argument cannot be 0");
            goto Error;
        }

        if (Py_SIZE(c) < 0) {
            negativeOutput = 1;
            temp = (PyLongObject *)_PyLong_Copy(c);
            if (temp == NULL)
                goto Error;
            Py_DECREF(c);
            c = temp;
            temp = NULL;
            c->ob_size = - c->ob_size;
        }

        if ((Py_SIZE(c) == 1) && (c->ob_digit[0] == 1)) {
            z = (PyLongObject *)PyLong_FromLong(0L);
            goto Done;
        }

        if (Py_SIZE(a) < 0 || Py_SIZE(a) > Py_SIZE(c)) {
            if (l_divmod(a, c, NULL, &temp) < 0)
                goto Error;
            Py_DECREF(a);
            a = temp;
            temp = NULL;
        }
    }

    z = (PyLongObject *)PyLong_FromLong(1L);
    if (z == NULL)
        goto Error;

#define REDUCE(X)                                       \
    do {                                                \
        if (c != NULL) {                                \
            if (l_divmod(X, c, NULL, &temp) < 0)        \
                goto Error;                             \
            Py_XDECREF(X);                              \
            X = temp;                                   \
            temp = NULL;                                \
        }                                               \
    } while(0)

#define MULT(X, Y, result)                      \
    do {                                        \
        temp = (PyLongObject *)long_mul(X, Y);  \
        if (temp == NULL)                       \
            goto Error;                         \
        Py_XDECREF(result);                     \
        result = temp;                          \
        temp = NULL;                            \
        REDUCE(result);                         \
    } while(0)

    if (Py_SIZE(b) <= FIVEARY_CUTOFF) {

        for (i = Py_SIZE(b) - 1; i >= 0; --i) {
            digit bi = b->ob_digit[i];

            for (j = (digit)1 << (PyLong_SHIFT-1); j != 0; j >>= 1) {
                MULT(z, z, z);
                if (bi & j)
                    MULT(z, a, z);
            }
        }
    }
    else {

        Py_INCREF(z);
        table[0] = z;
        for (i = 1; i < 32; ++i)
            MULT(table[i-1], a, table[i]);

        for (i = Py_SIZE(b) - 1; i >= 0; --i) {
            const digit bi = b->ob_digit[i];

            for (j = PyLong_SHIFT - 5; j >= 0; j -= 5) {
                const int index = (bi >> j) & 0x1f;
                for (k = 0; k < 5; ++k)
                    MULT(z, z, z);
                if (index)
                    MULT(z, table[index], z);
            }
        }
    }

    if (negativeOutput && (Py_SIZE(z) != 0)) {
        temp = (PyLongObject *)long_sub(z, c);
        if (temp == NULL)
            goto Error;
        Py_DECREF(z);
        z = temp;
        temp = NULL;
    }
    goto Done;

  Error:
    if (z != NULL) {
        Py_DECREF(z);
        z = NULL;
    }

  Done:
    if (Py_SIZE(b) > FIVEARY_CUTOFF) {
        for (i = 0; i < 32; ++i)
            Py_XDECREF(table[i]);
    }
    Py_DECREF(a);
    Py_DECREF(b);
    Py_XDECREF(c);
    Py_XDECREF(temp);
    return (PyObject *)z;
}

static PyObject *
long_invert(PyLongObject *v)
{

    PyLongObject *x;
    PyLongObject *w;
    w = (PyLongObject *)PyLong_FromLong(1L);
    if (w == NULL)
        return NULL;
    x = (PyLongObject *) long_add(v, w);
    Py_DECREF(w);
    if (x == NULL)
        return NULL;
    Py_SIZE(x) = -(Py_SIZE(x));
    return (PyObject *)x;
}

static PyObject *
long_neg(PyLongObject *v)
{
    PyLongObject *z;
    if (v->ob_size == 0 && PyLong_CheckExact(v)) {

        Py_INCREF(v);
        return (PyObject *) v;
    }
    z = (PyLongObject *)_PyLong_Copy(v);
    if (z != NULL)
        z->ob_size = -(v->ob_size);
    return (PyObject *)z;
}

static PyObject *
long_abs(PyLongObject *v)
{
    if (v->ob_size < 0)
        return long_neg(v);
    else
        return long_long((PyObject *)v);
}

static int
long_nonzero(PyLongObject *v)
{
    return Py_SIZE(v) != 0;
}

static PyObject *
long_rshift(PyLongObject *v, PyLongObject *w)
{
    PyLongObject *a, *b;
    PyLongObject *z = NULL;
    Py_ssize_t shiftby, newsize, wordshift, loshift, hishift, i, j;
    digit lomask, himask;

    CONVERT_BINOP((PyObject *)v, (PyObject *)w, &a, &b);

    if (Py_SIZE(a) < 0) {

        PyLongObject *a1, *a2;
        a1 = (PyLongObject *) long_invert(a);
        if (a1 == NULL)
            goto rshift_error;
        a2 = (PyLongObject *) long_rshift(a1, b);
        Py_DECREF(a1);
        if (a2 == NULL)
            goto rshift_error;
        z = (PyLongObject *) long_invert(a2);
        Py_DECREF(a2);
    }
    else {
        shiftby = PyLong_AsSsize_t((PyObject *)b);
        if (shiftby == -1L && PyErr_Occurred())
            goto rshift_error;
        if (shiftby < 0) {
            PyErr_SetString(PyExc_ValueError,
                            "negative shift count");
            goto rshift_error;
        }
        wordshift = shiftby / PyLong_SHIFT;
        newsize = ABS(Py_SIZE(a)) - wordshift;
        if (newsize <= 0) {
            z = _PyLong_New(0);
            Py_DECREF(a);
            Py_DECREF(b);
            return (PyObject *)z;
        }
        loshift = shiftby % PyLong_SHIFT;
        hishift = PyLong_SHIFT - loshift;
        lomask = ((digit)1 << hishift) - 1;
        himask = PyLong_MASK ^ lomask;
        z = _PyLong_New(newsize);
        if (z == NULL)
            goto rshift_error;
        if (Py_SIZE(a) < 0)
            Py_SIZE(z) = -(Py_SIZE(z));
        for (i = 0, j = wordshift; i < newsize; i++, j++) {
            z->ob_digit[i] = (a->ob_digit[j] >> loshift) & lomask;
            if (i+1 < newsize)
                z->ob_digit[i] |= (a->ob_digit[j+1] << hishift) & himask;
        }
        z = long_normalize(z);
    }
  rshift_error:
    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *) z;

}

static PyObject *
long_lshift(PyObject *v, PyObject *w)
{

    PyLongObject *a, *b;
    PyLongObject *z = NULL;
    Py_ssize_t shiftby, oldsize, newsize, wordshift, remshift, i, j;
    twodigits accum;

    CONVERT_BINOP(v, w, &a, &b);

    shiftby = PyLong_AsSsize_t((PyObject *)b);
    if (shiftby == -1L && PyErr_Occurred())
        goto out;
    if (shiftby < 0) {
        PyErr_SetString(PyExc_ValueError, "negative shift count");
        goto out;
    }

    if (Py_SIZE(a) == 0) {
        z = (PyLongObject *)PyLong_FromLong(0);
        goto out;
    }

    wordshift = shiftby / PyLong_SHIFT;
    remshift  = shiftby - wordshift * PyLong_SHIFT;

    oldsize = ABS(a->ob_size);
    newsize = oldsize + wordshift;
    if (remshift)
        ++newsize;
    z = _PyLong_New(newsize);
    if (z == NULL)
        goto out;
    if (a->ob_size < 0)
        z->ob_size = -(z->ob_size);
    for (i = 0; i < wordshift; i++)
        z->ob_digit[i] = 0;
    accum = 0;
    for (i = wordshift, j = 0; j < oldsize; i++, j++) {
        accum |= (twodigits)a->ob_digit[j] << remshift;
        z->ob_digit[i] = (digit)(accum & PyLong_MASK);
        accum >>= PyLong_SHIFT;
    }
    if (remshift)
        z->ob_digit[newsize-1] = (digit)accum;
    else
        assert(!accum);
    z = long_normalize(z);
  out:
    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *) z;
}

static void
v_complement(digit *z, digit *a, Py_ssize_t m)
{
    Py_ssize_t i;
    digit carry = 1;
    for (i = 0; i < m; ++i) {
        carry += a[i] ^ PyLong_MASK;
        z[i] = carry & PyLong_MASK;
        carry >>= PyLong_SHIFT;
    }
    assert(carry == 0);
}

static PyObject *
long_bitwise(PyLongObject *a,
             int op,
             PyLongObject *b)
{
    int nega, negb, negz;
    Py_ssize_t size_a, size_b, size_z, i;
    PyLongObject *z;

    size_a = ABS(Py_SIZE(a));
    nega = Py_SIZE(a) < 0;
    if (nega) {
        z = _PyLong_New(size_a);
        if (z == NULL)
            return NULL;
        v_complement(z->ob_digit, a->ob_digit, size_a);
        a = z;
    }
    else

        Py_INCREF(a);

    size_b = ABS(Py_SIZE(b));
    negb = Py_SIZE(b) < 0;
    if (negb) {
        z = _PyLong_New(size_b);
        if (z == NULL) {
            Py_DECREF(a);
            return NULL;
        }
        v_complement(z->ob_digit, b->ob_digit, size_b);
        b = z;
    }
    else
        Py_INCREF(b);

    if (size_a < size_b) {
        z = a; a = b; b = z;
        size_z = size_a; size_a = size_b; size_b = size_z;
        negz = nega; nega = negb; negb = negz;
    }

    switch (op) {
    case '^':
        negz = nega ^ negb;
        size_z = size_a;
        break;
    case '&':
        negz = nega & negb;
        size_z = negb ? size_a : size_b;
        break;
    case '|':
        negz = nega | negb;
        size_z = negb ? size_b : size_a;
        break;
    default:
        PyErr_BadArgument();
        return NULL;
    }

    z = _PyLong_New(size_z + negz);
    if (z == NULL) {
        Py_DECREF(a);
        Py_DECREF(b);
        return NULL;
    }

    switch(op) {
    case '&':
        for (i = 0; i < size_b; ++i)
            z->ob_digit[i] = a->ob_digit[i] & b->ob_digit[i];
        break;
    case '|':
        for (i = 0; i < size_b; ++i)
            z->ob_digit[i] = a->ob_digit[i] | b->ob_digit[i];
        break;
    case '^':
        for (i = 0; i < size_b; ++i)
            z->ob_digit[i] = a->ob_digit[i] ^ b->ob_digit[i];
        break;
    default:
        PyErr_BadArgument();
        return NULL;
    }

    if (op == '^' && negb)
        for (; i < size_z; ++i)
            z->ob_digit[i] = a->ob_digit[i] ^ PyLong_MASK;
    else if (i < size_z)
        memcpy(&z->ob_digit[i], &a->ob_digit[i],
               (size_z-i)*sizeof(digit));

    if (negz) {
        Py_SIZE(z) = -(Py_SIZE(z));
        z->ob_digit[size_z] = PyLong_MASK;
        v_complement(z->ob_digit, z->ob_digit, size_z+1);
    }

    Py_DECREF(a);
    Py_DECREF(b);
    return (PyObject *)long_normalize(z);
}

static PyObject *
long_and(PyObject *v, PyObject *w)
{
    PyLongObject *a, *b;
    PyObject *c;
    CONVERT_BINOP(v, w, &a, &b);
    c = long_bitwise(a, '&', b);
    Py_DECREF(a);
    Py_DECREF(b);
    return c;
}

static PyObject *
long_xor(PyObject *v, PyObject *w)
{
    PyLongObject *a, *b;
    PyObject *c;
    CONVERT_BINOP(v, w, &a, &b);
    c = long_bitwise(a, '^', b);
    Py_DECREF(a);
    Py_DECREF(b);
    return c;
}

static PyObject *
long_or(PyObject *v, PyObject *w)
{
    PyLongObject *a, *b;
    PyObject *c;
    CONVERT_BINOP(v, w, &a, &b);
    c = long_bitwise(a, '|', b);
    Py_DECREF(a);
    Py_DECREF(b);
    return c;
}

static int
long_coerce(PyObject **pv, PyObject **pw)
{
    if (PyInt_Check(*pw)) {
        *pw = PyLong_FromLong(PyInt_AS_LONG(*pw));
        if (*pw == NULL)
            return -1;
        Py_INCREF(*pv);
        return 0;
    }
    else if (PyLong_Check(*pw)) {
        Py_INCREF(*pv);
        Py_INCREF(*pw);
        return 0;
    }
    return 1;
}

static PyObject *
long_long(PyObject *v)
{
    if (PyLong_CheckExact(v))
        Py_INCREF(v);
    else
        v = _PyLong_Copy((PyLongObject *)v);
    return v;
}

static PyObject *
long_int(PyObject *v)
{
    long x;
    x = PyLong_AsLong(v);
    if (PyErr_Occurred()) {
        if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
            PyErr_Clear();
            if (PyLong_CheckExact(v)) {
                Py_INCREF(v);
                return v;
            }
            else
                return _PyLong_Copy((PyLongObject *)v);
        }
        else
            return NULL;
    }
    return PyInt_FromLong(x);
}

static PyObject *
long_float(PyObject *v)
{
    double result;
    result = PyLong_AsDouble(v);
    if (result == -1.0 && PyErr_Occurred())
        return NULL;
    return PyFloat_FromDouble(result);
}

static PyObject *
long_oct(PyObject *v)
{
    return _PyLong_Format(v, 8, 1, 0);
}

static PyObject *
long_hex(PyObject *v)
{
    return _PyLong_Format(v, 16, 1, 0);
}

static PyObject *
long_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static PyObject *
long_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *x = NULL;
    int base = -909;
    static char *kwlist[] = {"x", "base", 0};

    if (type != &PyLong_Type)
        return long_subtype_new(type, args, kwds);
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Oi:long", kwlist,
                                     &x, &base))
        return NULL;
    if (x == NULL) {
        if (base != -909) {
            PyErr_SetString(PyExc_TypeError,
                            "long() missing string argument");
            return NULL;
        }
        return PyLong_FromLong(0L);
    }
    if (base == -909)
        return PyNumber_Long(x);
    else if (PyString_Check(x)) {

        char *string = PyString_AS_STRING(x);
        if (strlen(string) != (size_t)PyString_Size(x)) {

            PyObject *srepr;
            srepr = PyObject_Repr(x);
            if (srepr == NULL)
                return NULL;
            PyErr_Format(PyExc_ValueError,
                         "invalid literal for long() with base %d: %s",
                         base, PyString_AS_STRING(srepr));
            Py_DECREF(srepr);
            return NULL;
        }
        return PyLong_FromString(PyString_AS_STRING(x), NULL, base);
    }
#ifdef Py_USING_UNICODE
    else if (PyUnicode_Check(x))
        return PyLong_FromUnicode(PyUnicode_AS_UNICODE(x),
                                  PyUnicode_GET_SIZE(x),
                                  base);
#endif
    else {
        PyErr_SetString(PyExc_TypeError,
                        "long() can't convert non-string with explicit base");
        return NULL;
    }
}

static PyObject *
long_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyLongObject *tmp, *newobj;
    Py_ssize_t i, n;

    assert(PyType_IsSubtype(type, &PyLong_Type));
    tmp = (PyLongObject *)long_new(&PyLong_Type, args, kwds);
    if (tmp == NULL)
        return NULL;
    assert(PyLong_Check(tmp));
    n = Py_SIZE(tmp);
    if (n < 0)
        n = -n;
    newobj = (PyLongObject *)type->tp_alloc(type, n);
    if (newobj == NULL) {
        Py_DECREF(tmp);
        return NULL;
    }
    assert(PyLong_Check(newobj));
    Py_SIZE(newobj) = Py_SIZE(tmp);
    for (i = 0; i < n; i++)
        newobj->ob_digit[i] = tmp->ob_digit[i];
    Py_DECREF(tmp);
    return (PyObject *)newobj;
}

static PyObject *
long_getnewargs(PyLongObject *v)
{
    return Py_BuildValue("(N)", _PyLong_Copy(v));
}

static PyObject *
long_get0(PyLongObject *v, void *context) {
    return PyLong_FromLong(0L);
}

static PyObject *
long_get1(PyLongObject *v, void *context) {
    return PyLong_FromLong(1L);
}

static PyObject *
long__format__(PyObject *self, PyObject *args)
{
    PyObject *format_spec;

    if (!PyArg_ParseTuple(args, "O:__format__", &format_spec))
        return NULL;
    if (PyBytes_Check(format_spec))
        return _PyLong_FormatAdvanced(self,
                                      PyBytes_AS_STRING(format_spec),
                                      PyBytes_GET_SIZE(format_spec));
    if (PyUnicode_Check(format_spec)) {

        PyObject *result;
        PyObject *str_spec = PyObject_Str(format_spec);

        if (str_spec == NULL)
            return NULL;

        result = _PyLong_FormatAdvanced(self,
                                        PyBytes_AS_STRING(str_spec),
                                        PyBytes_GET_SIZE(str_spec));

        Py_DECREF(str_spec);
        return result;
    }
    PyErr_SetString(PyExc_TypeError, "__format__ requires str or unicode");
    return NULL;
}

static PyObject *
long_sizeof(PyLongObject *v)
{
    Py_ssize_t res;

    res = v->ob_type->tp_basicsize + ABS(Py_SIZE(v))*sizeof(digit);
    return PyInt_FromSsize_t(res);
}

static PyObject *
long_bit_length(PyLongObject *v)
{
    PyLongObject *result, *x, *y;
    Py_ssize_t ndigits, msd_bits = 0;
    digit msd;

    assert(v != NULL);
    assert(PyLong_Check(v));

    ndigits = ABS(Py_SIZE(v));
    if (ndigits == 0)
        return PyInt_FromLong(0);

    msd = v->ob_digit[ndigits-1];
    while (msd >= 32) {
        msd_bits += 6;
        msd >>= 6;
    }
    msd_bits += (long)(BitLengthTable[msd]);

    if (ndigits <= PY_SSIZE_T_MAX/PyLong_SHIFT)
        return PyInt_FromSsize_t((ndigits-1)*PyLong_SHIFT + msd_bits);

    result = (PyLongObject *)PyLong_FromSsize_t(ndigits - 1);
    if (result == NULL)
        return NULL;
    x = (PyLongObject *)PyLong_FromLong(PyLong_SHIFT);
    if (x == NULL)
        goto error;
    y = (PyLongObject *)long_mul(result, x);
    Py_DECREF(x);
    if (y == NULL)
        goto error;
    Py_DECREF(result);
    result = y;

    x = (PyLongObject *)PyLong_FromLong((long)msd_bits);
    if (x == NULL)
        goto error;
    y = (PyLongObject *)long_add(result, x);
    Py_DECREF(x);
    if (y == NULL)
        goto error;
    Py_DECREF(result);
    result = y;

    return (PyObject *)result;

  error:
    Py_DECREF(result);
    return NULL;
}

PyDoc_STRVAR(long_bit_length_doc,
"long.bit_length() -> int or long\n\
\n\
Number of bits necessary to represent self in binary.\n\
>>> bin(37L)\n\
'0b100101'\n\
>>> (37L).bit_length()\n\
6");

#if 0
static PyObject *
long_is_finite(PyObject *v)
{
    Py_RETURN_TRUE;
}
#endif

static PyMethodDef long_methods[] = {
    {"conjugate",       (PyCFunction)long_long, METH_NOARGS,
     "Returns self, the complex conjugate of any long."},
    {"bit_length",      (PyCFunction)long_bit_length, METH_NOARGS,
     long_bit_length_doc},
#if 0
    {"is_finite",       (PyCFunction)long_is_finite,    METH_NOARGS,
     "Returns always True."},
#endif
    {"__trunc__",       (PyCFunction)long_long, METH_NOARGS,
     "Truncating an Integral returns itself."},
    {"__getnewargs__",          (PyCFunction)long_getnewargs,   METH_NOARGS},
    {"__format__", (PyCFunction)long__format__, METH_VARARGS},
    {"__sizeof__",      (PyCFunction)long_sizeof, METH_NOARGS,
     "Returns size in memory, in bytes"},
    {NULL,              NULL}
};

static PyGetSetDef long_getset[] = {
    {"real",
     (getter)long_long, (setter)NULL,
     "the real part of a complex number",
     NULL},
    {"imag",
     (getter)long_get0, (setter)NULL,
     "the imaginary part of a complex number",
     NULL},
    {"numerator",
     (getter)long_long, (setter)NULL,
     "the numerator of a rational number in lowest terms",
     NULL},
    {"denominator",
     (getter)long_get1, (setter)NULL,
     "the denominator of a rational number in lowest terms",
     NULL},
    {NULL}
};

PyDoc_STRVAR(long_doc,
"long(x=0) -> long\n\
long(x, base=10) -> long\n\
\n\
Convert a number or string to a long integer, or return 0L if no arguments\n\
are given.  If x is floating point, the conversion truncates towards zero.\n\
\n\
If x is not a number or if base is given, then x must be a string or\n\
Unicode object representing an integer literal in the given base.  The\n\
literal can be preceded by '+' or '-' and be surrounded by whitespace.\n\
The base defaults to 10.  Valid bases are 0 and 2-36.  Base 0 means to\n\
interpret the base from the string as an integer literal.\n\
>>> int('0b100', base=0)\n\
4L");

static PyNumberMethods long_as_number = {
    (binaryfunc)long_add,
    (binaryfunc)long_sub,
    (binaryfunc)long_mul,
    long_classic_div,
    long_mod,
    long_divmod,
    long_pow,
    (unaryfunc)long_neg,
    (unaryfunc)long_long,
    (unaryfunc)long_abs,
    (inquiry)long_nonzero,
    (unaryfunc)long_invert,
    long_lshift,
    (binaryfunc)long_rshift,
    long_and,
    long_xor,
    long_or,
    long_coerce,
    long_int,
    long_long,
    long_float,
    long_oct,
    long_hex,
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
    long_div,
    long_true_divide,
    0,
    0,
    long_long,
};

PyTypeObject PyLong_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "long",
    offsetof(PyLongObject, ob_digit),
    sizeof(digit),
    long_dealloc,
    0,
    0,
    0,
    (cmpfunc)long_compare,
    long_repr,
    &long_as_number,
    0,
    0,
    (hashfunc)long_hash,
    0,
    long_str,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_LONG_SUBCLASS,
    long_doc,
    0,
    0,
    0,
    0,
    0,
    0,
    long_methods,
    0,
    long_getset,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    long_new,
    PyObject_Del,
};

static PyTypeObject Long_InfoType;

PyDoc_STRVAR(long_info__doc__,
"sys.long_info\n\
\n\
A struct sequence that holds information about Python's\n\
internal representation of integers.  The attributes are read only.");

static PyStructSequence_Field long_info_fields[] = {
    {"bits_per_digit", "size of a digit in bits"},
    {"sizeof_digit", "size in bytes of the C type used to represent a digit"},
    {NULL, NULL}
};

static PyStructSequence_Desc long_info_desc = {
    "sys.long_info",
    long_info__doc__,
    long_info_fields,
    2
};

PyObject *
PyLong_GetInfo(void)
{
    PyObject* long_info;
    int field = 0;
    long_info = PyStructSequence_New(&Long_InfoType);
    if (long_info == NULL)
        return NULL;
    PyStructSequence_SET_ITEM(long_info, field++,
                              PyInt_FromLong(PyLong_SHIFT));
    PyStructSequence_SET_ITEM(long_info, field++,
                              PyInt_FromLong(sizeof(digit)));
    if (PyErr_Occurred()) {
        Py_CLEAR(long_info);
        return NULL;
    }
    return long_info;
}

int
_PyLong_Init(void)
{

    if (Long_InfoType.tp_name == 0)
        PyStructSequence_InitType(&Long_InfoType, &long_info_desc);
    return 1;
}
