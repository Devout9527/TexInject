#include "Python.h"
#include "_math.h"

#ifdef _OSF_SOURCE

extern double copysign(double, double);
#endif

static const double pi = 3.141592653589793238462643383279502884197;
static const double sqrtpi = 1.772453850905516027298167483341145182798;

static double
sinpi(double x)
{
    double y, r;
    int n;

    assert(Py_IS_FINITE(x));
    y = fmod(fabs(x), 2.0);
    n = (int)round(2.0*y);
    assert(0 <= n && n <= 4);
    switch (n) {
    case 0:
        r = sin(pi*y);
        break;
    case 1:
        r = cos(pi*(y-0.5));
        break;
    case 2:

        r = sin(pi*(1.0-y));
        break;
    case 3:
        r = -cos(pi*(y-1.5));
        break;
    case 4:
        r = sin(pi*(y-2.0));
        break;
    default:
        assert(0);
        r = -1.23e200;
    }
    return copysign(1.0, x)*r;
}

#define LANCZOS_N 13
static const double lanczos_g = 6.024680040776729583740234375;
static const double lanczos_g_minus_half = 5.524680040776729583740234375;
static const double lanczos_num_coeffs[LANCZOS_N] = {
    23531376880.410759688572007674451636754734846804940,
    42919803642.649098768957899047001988850926355848959,
    35711959237.355668049440185451547166705960488635843,
    17921034426.037209699919755754458931112671403265390,
    6039542586.3520280050642916443072979210699388420708,
    1439720407.3117216736632230727949123939715485786772,
    248874557.86205415651146038641322942321632125127801,
    31426415.585400194380614231628318205362874684987640,
    2876370.6289353724412254090516208496135991145378768,
    186056.26539522349504029498971604569928220784236328,
    8071.6720023658162106380029022722506138218516325024,
    210.82427775157934587250973392071336271166969580291,
    2.5066282746310002701649081771338373386264310793408
};

static const double lanczos_den_coeffs[LANCZOS_N] = {
    0.0, 39916800.0, 120543840.0, 150917976.0, 105258076.0, 45995730.0,
    13339535.0, 2637558.0, 357423.0, 32670.0, 1925.0, 66.0, 1.0};

#define NGAMMA_INTEGRAL 23
static const double gamma_integral[NGAMMA_INTEGRAL] = {
    1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0, 5040.0, 40320.0, 362880.0,
    3628800.0, 39916800.0, 479001600.0, 6227020800.0, 87178291200.0,
    1307674368000.0, 20922789888000.0, 355687428096000.0,
    6402373705728000.0, 121645100408832000.0, 2432902008176640000.0,
    51090942171709440000.0, 1124000727777607680000.0,
};

static double
lanczos_sum(double x)
{
    double num = 0.0, den = 0.0;
    int i;
    assert(x > 0.0);

    if (x < 5.0) {
        for (i = LANCZOS_N; --i >= 0; ) {
            num = num * x + lanczos_num_coeffs[i];
            den = den * x + lanczos_den_coeffs[i];
        }
    }
    else {
        for (i = 0; i < LANCZOS_N; i++) {
            num = num / x + lanczos_num_coeffs[i];
            den = den / x + lanczos_den_coeffs[i];
        }
    }
    return num/den;
}

static double
m_tgamma(double x)
{
    double absx, r, y, z, sqrtpow;

    if (!Py_IS_FINITE(x)) {
        if (Py_IS_NAN(x) || x > 0.0)
            return x;
        else {
            errno = EDOM;
            return Py_NAN;
        }
    }
    if (x == 0.0) {
        errno = EDOM;
        return 1.0/x;
    }

    if (x == floor(x)) {
        if (x < 0.0) {
            errno = EDOM;
            return Py_NAN;
        }
        if (x <= NGAMMA_INTEGRAL)
            return gamma_integral[(int)x - 1];
    }
    absx = fabs(x);

    if (absx < 1e-20) {
        r = 1.0/x;
        if (Py_IS_INFINITY(r))
            errno = ERANGE;
        return r;
    }

    if (absx > 200.0) {
        if (x < 0.0) {
            return 0.0/sinpi(x);
        }
        else {
            errno = ERANGE;
            return Py_HUGE_VAL;
        }
    }

    y = absx + lanczos_g_minus_half;

    if (absx > lanczos_g_minus_half) {

        double q = y - absx;
        z = q - lanczos_g_minus_half;
    }
    else {
        double q = y - lanczos_g_minus_half;
        z = q - absx;
    }
    z = z * lanczos_g / y;
    if (x < 0.0) {
        r = -pi / sinpi(absx) / absx * exp(y) / lanczos_sum(absx);
        r -= z * r;
        if (absx < 140.0) {
            r /= pow(y, absx - 0.5);
        }
        else {
            sqrtpow = pow(y, absx / 2.0 - 0.25);
            r /= sqrtpow;
            r /= sqrtpow;
        }
    }
    else {
        r = lanczos_sum(absx) / exp(y);
        r += z * r;
        if (absx < 140.0) {
            r *= pow(y, absx - 0.5);
        }
        else {
            sqrtpow = pow(y, absx / 2.0 - 0.25);
            r *= sqrtpow;
            r *= sqrtpow;
        }
    }
    if (Py_IS_INFINITY(r))
        errno = ERANGE;
    return r;
}

static double
m_lgamma(double x)
{
    double r, absx;

    if (!Py_IS_FINITE(x)) {
        if (Py_IS_NAN(x))
            return x;
        else
            return Py_HUGE_VAL;
    }

    if (x == floor(x) && x <= 2.0) {
        if (x <= 0.0) {
            errno = EDOM;
            return Py_HUGE_VAL;
        }
        else {
            return 0.0;
        }
    }

    absx = fabs(x);

    if (absx < 1e-20)
        return -log(absx);

    if (x > 0.0) {

        r = log(lanczos_sum(x)) - lanczos_g +
            (x-0.5)*(log(x+lanczos_g-0.5)-1);
    }
    else {
        r = log(pi) - log(fabs(sinpi(absx))) - log(absx) -
            (log(lanczos_sum(absx)) - lanczos_g +
             (absx-0.5)*(log(absx+lanczos_g-0.5)-1));
    }
    if (Py_IS_INFINITY(r))
        errno = ERANGE;
    return r;
}

#define ERF_SERIES_CUTOFF 1.5
#define ERF_SERIES_TERMS 25
#define ERFC_CONTFRAC_CUTOFF 30.0
#define ERFC_CONTFRAC_TERMS 50

static double
m_erf_series(double x)
{
    double x2, acc, fk, result;
    int i, saved_errno;

    x2 = x * x;
    acc = 0.0;
    fk = (double)ERF_SERIES_TERMS + 0.5;
    for (i = 0; i < ERF_SERIES_TERMS; i++) {
        acc = 2.0 + x2 * acc / fk;
        fk -= 1.0;
    }

    saved_errno = errno;
    result = acc * x * exp(-x2) / sqrtpi;
    errno = saved_errno;
    return result;
}

static double
m_erfc_contfrac(double x)
{
    double x2, a, da, p, p_last, q, q_last, b, result;
    int i, saved_errno;

    if (x >= ERFC_CONTFRAC_CUTOFF)
        return 0.0;

    x2 = x*x;
    a = 0.0;
    da = 0.5;
    p = 1.0; p_last = 0.0;
    q = da + x2; q_last = 1.0;
    for (i = 0; i < ERFC_CONTFRAC_TERMS; i++) {
        double temp;
        a += da;
        da += 2.0;
        b = da + x2;
        temp = p; p = b*p - a*p_last; p_last = temp;
        temp = q; q = b*q - a*q_last; q_last = temp;
    }

    saved_errno = errno;
    result = p / q * x * exp(-x2) / sqrtpi;
    errno = saved_errno;
    return result;
}

static double
m_erf(double x)
{
    double absx, cf;

    if (Py_IS_NAN(x))
        return x;
    absx = fabs(x);
    if (absx < ERF_SERIES_CUTOFF)
        return m_erf_series(x);
    else {
        cf = m_erfc_contfrac(absx);
        return x > 0.0 ? 1.0 - cf : cf - 1.0;
    }
}

static double
m_erfc(double x)
{
    double absx, cf;

    if (Py_IS_NAN(x))
        return x;
    absx = fabs(x);
    if (absx < ERF_SERIES_CUTOFF)
        return 1.0 - m_erf_series(x);
    else {
        cf = m_erfc_contfrac(absx);
        return x > 0.0 ? cf : 2.0 - cf;
    }
}

static double
m_atan2(double y, double x)
{
    if (Py_IS_NAN(x) || Py_IS_NAN(y))
        return Py_NAN;
    if (Py_IS_INFINITY(y)) {
        if (Py_IS_INFINITY(x)) {
            if (copysign(1., x) == 1.)

                return copysign(0.25*Py_MATH_PI, y);
            else

                return copysign(0.75*Py_MATH_PI, y);
        }

        return copysign(0.5*Py_MATH_PI, y);
    }
    if (Py_IS_INFINITY(x) || y == 0.) {
        if (copysign(1., x) == 1.)

            return copysign(0., y);
        else

            return copysign(Py_MATH_PI, y);
    }
    return atan2(y, x);
}

static double
m_log(double x)
{
    if (Py_IS_FINITE(x)) {
        if (x > 0.0)
            return log(x);
        errno = EDOM;
        if (x == 0.0)
            return -Py_HUGE_VAL;
        else
            return Py_NAN;
    }
    else if (Py_IS_NAN(x))
        return x;
    else if (x > 0.0)
        return x;
    else {
        errno = EDOM;
        return Py_NAN;
    }
}

static double
m_log10(double x)
{
    if (Py_IS_FINITE(x)) {
        if (x > 0.0)
            return log10(x);
        errno = EDOM;
        if (x == 0.0)
            return -Py_HUGE_VAL;
        else
            return Py_NAN;
    }
    else if (Py_IS_NAN(x))
        return x;
    else if (x > 0.0)
        return x;
    else {
        errno = EDOM;
        return Py_NAN;
    }
}

static int
is_error(double x)
{
    int result = 1;
    assert(errno);
    if (errno == EDOM)
        PyErr_SetString(PyExc_ValueError, "math domain error");

    else if (errno == ERANGE) {

        if (fabs(x) < 1.0)
            result = 0;
        else
            PyErr_SetString(PyExc_OverflowError,
                            "math range error");
    }
    else

        PyErr_SetFromErrno(PyExc_ValueError);
    return result;
}

static PyObject *
math_1(PyObject *arg, double (*func) (double), int can_overflow)
{
    double x, r;
    x = PyFloat_AsDouble(arg);
    if (x == -1.0 && PyErr_Occurred())
        return NULL;
    errno = 0;
    PyFPE_START_PROTECT("in math_1", return 0);
    r = (*func)(x);
    PyFPE_END_PROTECT(r);
    if (Py_IS_NAN(r)) {
        if (!Py_IS_NAN(x))
            errno = EDOM;
        else
            errno = 0;
    }
    else if (Py_IS_INFINITY(r)) {
        if (Py_IS_FINITE(x))
            errno = can_overflow ? ERANGE : EDOM;
        else
            errno = 0;
    }
    if (errno && is_error(r))
        return NULL;
    else
        return PyFloat_FromDouble(r);
}

static PyObject *
math_1a(PyObject *arg, double (*func) (double))
{
    double x, r;
    x = PyFloat_AsDouble(arg);
    if (x == -1.0 && PyErr_Occurred())
        return NULL;
    errno = 0;
    PyFPE_START_PROTECT("in math_1a", return 0);
    r = (*func)(x);
    PyFPE_END_PROTECT(r);
    if (errno && is_error(r))
        return NULL;
    return PyFloat_FromDouble(r);
}

static PyObject *
math_2(PyObject *args, double (*func) (double, double), char *funcname)
{
    PyObject *ox, *oy;
    double x, y, r;
    if (! PyArg_UnpackTuple(args, funcname, 2, 2, &ox, &oy))
        return NULL;
    x = PyFloat_AsDouble(ox);
    y = PyFloat_AsDouble(oy);
    if ((x == -1.0 || y == -1.0) && PyErr_Occurred())
        return NULL;
    errno = 0;
    PyFPE_START_PROTECT("in math_2", return 0);
    r = (*func)(x, y);
    PyFPE_END_PROTECT(r);
    if (Py_IS_NAN(r)) {
        if (!Py_IS_NAN(x) && !Py_IS_NAN(y))
            errno = EDOM;
        else
            errno = 0;
    }
    else if (Py_IS_INFINITY(r)) {
        if (Py_IS_FINITE(x) && Py_IS_FINITE(y))
            errno = ERANGE;
        else
            errno = 0;
    }
    if (errno && is_error(r))
        return NULL;
    else
        return PyFloat_FromDouble(r);
}

#define FUNC1(funcname, func, can_overflow, docstring)                  \
    static PyObject * math_##funcname(PyObject *self, PyObject *args) { \
        return math_1(args, func, can_overflow);                            \
    }\
    PyDoc_STRVAR(math_##funcname##_doc, docstring);

#define FUNC1A(funcname, func, docstring)                               \
    static PyObject * math_##funcname(PyObject *self, PyObject *args) { \
        return math_1a(args, func);                                     \
    }\
    PyDoc_STRVAR(math_##funcname##_doc, docstring);

#define FUNC2(funcname, func, docstring) \
    static PyObject * math_##funcname(PyObject *self, PyObject *args) { \
        return math_2(args, func, #funcname); \
    }\
    PyDoc_STRVAR(math_##funcname##_doc, docstring);

FUNC1(acos, acos, 0,
      "acos(x)\n\nReturn the arc cosine (measured in radians) of x.")
FUNC1(acosh, m_acosh, 0,
      "acosh(x)\n\nReturn the hyperbolic arc cosine (measured in radians) of x.")
FUNC1(asin, asin, 0,
      "asin(x)\n\nReturn the arc sine (measured in radians) of x.")
FUNC1(asinh, m_asinh, 0,
      "asinh(x)\n\nReturn the hyperbolic arc sine (measured in radians) of x.")
FUNC1(atan, atan, 0,
      "atan(x)\n\nReturn the arc tangent (measured in radians) of x.")
FUNC2(atan2, m_atan2,
      "atan2(y, x)\n\nReturn the arc tangent (measured in radians) of y/x.\n"
      "Unlike atan(y/x), the signs of both x and y are considered.")
FUNC1(atanh, m_atanh, 0,
      "atanh(x)\n\nReturn the hyperbolic arc tangent (measured in radians) of x.")
FUNC1(ceil, ceil, 0,
      "ceil(x)\n\nReturn the ceiling of x as a float.\n"
      "This is the smallest integral value >= x.")
FUNC2(copysign, copysign,
      "copysign(x, y)\n\nReturn x with the sign of y.")
FUNC1(cos, cos, 0,
      "cos(x)\n\nReturn the cosine of x (measured in radians).")
FUNC1(cosh, cosh, 1,
      "cosh(x)\n\nReturn the hyperbolic cosine of x.")
FUNC1A(erf, m_erf,
       "erf(x)\n\nError function at x.")
FUNC1A(erfc, m_erfc,
       "erfc(x)\n\nComplementary error function at x.")
FUNC1(exp, exp, 1,
      "exp(x)\n\nReturn e raised to the power of x.")
FUNC1(expm1, m_expm1, 1,
      "expm1(x)\n\nReturn exp(x)-1.\n"
      "This function avoids the loss of precision involved in the direct "
      "evaluation of exp(x)-1 for small x.")
FUNC1(fabs, fabs, 0,
      "fabs(x)\n\nReturn the absolute value of the float x.")
FUNC1(floor, floor, 0,
      "floor(x)\n\nReturn the floor of x as a float.\n"
      "This is the largest integral value <= x.")
FUNC1A(gamma, m_tgamma,
      "gamma(x)\n\nGamma function at x.")
FUNC1A(lgamma, m_lgamma,
      "lgamma(x)\n\nNatural logarithm of absolute value of Gamma function at x.")
FUNC1(log1p, m_log1p, 1,
      "log1p(x)\n\nReturn the natural logarithm of 1+x (base e).\n"
      "The result is computed in a way which is accurate for x near zero.")
FUNC1(sin, sin, 0,
      "sin(x)\n\nReturn the sine of x (measured in radians).")
FUNC1(sinh, sinh, 1,
      "sinh(x)\n\nReturn the hyperbolic sine of x.")
FUNC1(sqrt, sqrt, 0,
      "sqrt(x)\n\nReturn the square root of x.")
FUNC1(tan, tan, 0,
      "tan(x)\n\nReturn the tangent of x (measured in radians).")
FUNC1(tanh, tanh, 0,
      "tanh(x)\n\nReturn the hyperbolic tangent of x.")

#define NUM_PARTIALS  32

static int
_fsum_realloc(double **p_ptr, Py_ssize_t  n,
             double  *ps,    Py_ssize_t *m_ptr)
{
    void *v = NULL;
    Py_ssize_t m = *m_ptr;

    m += m;
    if (n < m && m < (PY_SSIZE_T_MAX / sizeof(double))) {
        double *p = *p_ptr;
        if (p == ps) {
            v = PyMem_Malloc(sizeof(double) * m);
            if (v != NULL)
                memcpy(v, ps, sizeof(double) * n);
        }
        else
            v = PyMem_Realloc(p, sizeof(double) * m);
    }
    if (v == NULL) {
        PyErr_SetString(PyExc_MemoryError, "math.fsum partials");
        return 1;
    }
    *p_ptr = (double*) v;
    *m_ptr = m;
    return 0;
}

static PyObject*
math_fsum(PyObject *self, PyObject *seq)
{
    PyObject *item, *iter, *sum = NULL;
    Py_ssize_t i, j, n = 0, m = NUM_PARTIALS;
    double x, y, t, ps[NUM_PARTIALS], *p = ps;
    double xsave, special_sum = 0.0, inf_sum = 0.0;
    volatile double hi, yr, lo;

    iter = PyObject_GetIter(seq);
    if (iter == NULL)
        return NULL;

    PyFPE_START_PROTECT("fsum", Py_DECREF(iter); return NULL)

    for(;;) {
        assert(0 <= n && n <= m);
        assert((m == NUM_PARTIALS && p == ps) ||
               (m >  NUM_PARTIALS && p != NULL));

        item = PyIter_Next(iter);
        if (item == NULL) {
            if (PyErr_Occurred())
                goto _fsum_error;
            break;
        }
        x = PyFloat_AsDouble(item);
        Py_DECREF(item);
        if (PyErr_Occurred())
            goto _fsum_error;

        xsave = x;
        for (i = j = 0; j < n; j++) {
            y = p[j];
            if (fabs(x) < fabs(y)) {
                t = x; x = y; y = t;
            }
            hi = x + y;
            yr = hi - x;
            lo = y - yr;
            if (lo != 0.0)
                p[i++] = lo;
            x = hi;
        }

        n = i;
        if (x != 0.0) {
            if (! Py_IS_FINITE(x)) {

                if (Py_IS_FINITE(xsave)) {
                    PyErr_SetString(PyExc_OverflowError,
                          "intermediate overflow in fsum");
                    goto _fsum_error;
                }
                if (Py_IS_INFINITY(xsave))
                    inf_sum += xsave;
                special_sum += xsave;

                n = 0;
            }
            else if (n >= m && _fsum_realloc(&p, n, ps, &m))
                goto _fsum_error;
            else
                p[n++] = x;
        }
    }

    if (special_sum != 0.0) {
        if (Py_IS_NAN(inf_sum))
            PyErr_SetString(PyExc_ValueError,
                            "-inf + inf in fsum");
        else
            sum = PyFloat_FromDouble(special_sum);
        goto _fsum_error;
    }

    hi = 0.0;
    if (n > 0) {
        hi = p[--n];

        while (n > 0) {
            x = hi;
            y = p[--n];
            assert(fabs(y) < fabs(x));
            hi = x + y;
            yr = hi - x;
            lo = y - yr;
            if (lo != 0.0)
                break;
        }

        if (n > 0 && ((lo < 0.0 && p[n-1] < 0.0) ||
                      (lo > 0.0 && p[n-1] > 0.0))) {
            y = lo * 2.0;
            x = hi + y;
            yr = x - hi;
            if (y == yr)
                hi = x;
        }
    }
    sum = PyFloat_FromDouble(hi);

_fsum_error:
    PyFPE_END_PROTECT(hi)
    Py_DECREF(iter);
    if (p != ps)
        PyMem_Free(p);
    return sum;
}

#undef NUM_PARTIALS

PyDoc_STRVAR(math_fsum_doc,
"fsum(iterable)\n\n\
Return an accurate floating point sum of values in the iterable.\n\
Assumes IEEE-754 floating point arithmetic.");

static PyObject *
math_factorial(PyObject *self, PyObject *arg)
{
    long i, x;
    PyObject *result, *iobj, *newresult;

    if (PyFloat_Check(arg)) {
        PyObject *lx;
        double dx = PyFloat_AS_DOUBLE((PyFloatObject *)arg);
        if (!(Py_IS_FINITE(dx) && dx == floor(dx))) {
            PyErr_SetString(PyExc_ValueError,
                "factorial() only accepts integral values");
            return NULL;
        }
        lx = PyLong_FromDouble(dx);
        if (lx == NULL)
            return NULL;
        x = PyLong_AsLong(lx);
        Py_DECREF(lx);
    }
    else
        x = PyInt_AsLong(arg);

    if (x == -1 && PyErr_Occurred())
        return NULL;
    if (x < 0) {
        PyErr_SetString(PyExc_ValueError,
            "factorial() not defined for negative values");
        return NULL;
    }

    result = (PyObject *)PyInt_FromLong(1);
    if (result == NULL)
        return NULL;
    for (i=1 ; i<=x ; i++) {
        iobj = (PyObject *)PyInt_FromLong(i);
        if (iobj == NULL)
            goto error;
        newresult = PyNumber_Multiply(result, iobj);
        Py_DECREF(iobj);
        if (newresult == NULL)
            goto error;
        Py_DECREF(result);
        result = newresult;
    }
    return result;

error:
    Py_DECREF(result);
    return NULL;
}

PyDoc_STRVAR(math_factorial_doc,
"factorial(x) -> Integral\n"
"\n"
"Find x!. Raise a ValueError if x is negative or non-integral.");

static PyObject *
math_trunc(PyObject *self, PyObject *number)
{
    return PyObject_CallMethod(number, "__trunc__", NULL);
}

PyDoc_STRVAR(math_trunc_doc,
"trunc(x:Real) -> Integral\n"
"\n"
"Truncates x to the nearest Integral toward 0. Uses the __trunc__ magic method.");

static PyObject *
math_frexp(PyObject *self, PyObject *arg)
{
    int i;
    double x = PyFloat_AsDouble(arg);
    if (x == -1.0 && PyErr_Occurred())
        return NULL;

    if (Py_IS_NAN(x) || Py_IS_INFINITY(x) || !x) {
        i = 0;
    }
    else {
        PyFPE_START_PROTECT("in math_frexp", return 0);
        x = frexp(x, &i);
        PyFPE_END_PROTECT(x);
    }
    return Py_BuildValue("(di)", x, i);
}

PyDoc_STRVAR(math_frexp_doc,
"frexp(x)\n"
"\n"
"Return the mantissa and exponent of x, as pair (m, e).\n"
"m is a float and e is an int, such that x = m * 2.**e.\n"
"If x is 0, m and e are both 0.  Else 0.5 <= abs(m) < 1.0.");

static PyObject *
math_ldexp(PyObject *self, PyObject *args)
{
    double x, r;
    PyObject *oexp;
    long exp;
    int overflow;
    if (! PyArg_ParseTuple(args, "dO:ldexp", &x, &oexp))
        return NULL;

    if (PyLong_Check(oexp) || PyInt_Check(oexp)) {

        exp = PyLong_AsLongAndOverflow(oexp, &overflow);
        if (exp == -1 && PyErr_Occurred())
            return NULL;
        if (overflow)
            exp = overflow < 0 ? LONG_MIN : LONG_MAX;
    }
    else {
        PyErr_SetString(PyExc_TypeError,
                        "Expected an int or long as second argument "
                        "to ldexp.");
        return NULL;
    }

    if (x == 0. || !Py_IS_FINITE(x)) {

        r = x;
        errno = 0;
    } else if (exp > INT_MAX) {

        r = copysign(Py_HUGE_VAL, x);
        errno = ERANGE;
    } else if (exp < INT_MIN) {

        r = copysign(0., x);
        errno = 0;
    } else {
        errno = 0;
        PyFPE_START_PROTECT("in math_ldexp", return 0);
        r = ldexp(x, (int)exp);
        PyFPE_END_PROTECT(r);
        if (Py_IS_INFINITY(r))
            errno = ERANGE;
    }

    if (errno && is_error(r))
        return NULL;
    return PyFloat_FromDouble(r);
}

PyDoc_STRVAR(math_ldexp_doc,
"ldexp(x, i)\n\n\
Return x * (2**i).");

static PyObject *
math_modf(PyObject *self, PyObject *arg)
{
    double y, x = PyFloat_AsDouble(arg);
    if (x == -1.0 && PyErr_Occurred())
        return NULL;

    if (!Py_IS_FINITE(x)) {
        if (Py_IS_INFINITY(x))
            return Py_BuildValue("(dd)", copysign(0., x), x);
        else if (Py_IS_NAN(x))
            return Py_BuildValue("(dd)", x, x);
    }

    errno = 0;
    PyFPE_START_PROTECT("in math_modf", return 0);
    x = modf(x, &y);
    PyFPE_END_PROTECT(x);
    return Py_BuildValue("(dd)", x, y);
}

PyDoc_STRVAR(math_modf_doc,
"modf(x)\n"
"\n"
"Return the fractional and integer parts of x.  Both results carry the sign\n"
"of x and are floats.");

static PyObject*
loghelper(PyObject* arg, double (*func)(double), char *funcname)
{

    if (PyLong_Check(arg)) {
        double x;
        Py_ssize_t e;
        x = _PyLong_Frexp((PyLongObject *)arg, &e);
        if (x == -1.0 && PyErr_Occurred())
            return NULL;
        if (x <= 0.0) {
            PyErr_SetString(PyExc_ValueError,
                            "math domain error");
            return NULL;
        }

        if (e == 1 && x == 0.5)
            return PyFloat_FromDouble(0.0);

        x = func(x) + func(2.0) * e;
        return PyFloat_FromDouble(x);
    }

    return math_1(arg, func, 0);
}

static PyObject *
math_log(PyObject *self, PyObject *args)
{
    PyObject *arg;
    PyObject *base = NULL;
    PyObject *num, *den;
    PyObject *ans;

    if (!PyArg_UnpackTuple(args, "log", 1, 2, &arg, &base))
        return NULL;

    num = loghelper(arg, m_log, "log");
    if (num == NULL || base == NULL)
        return num;

    den = loghelper(base, m_log, "log");
    if (den == NULL) {
        Py_DECREF(num);
        return NULL;
    }

    ans = PyNumber_Divide(num, den);
    Py_DECREF(num);
    Py_DECREF(den);
    return ans;
}

PyDoc_STRVAR(math_log_doc,
"log(x[, base])\n\n\
Return the logarithm of x to the given base.\n\
If the base not specified, returns the natural logarithm (base e) of x.");

static PyObject *
math_log10(PyObject *self, PyObject *arg)
{
    return loghelper(arg, m_log10, "log10");
}

PyDoc_STRVAR(math_log10_doc,
"log10(x)\n\nReturn the base 10 logarithm of x.");

static PyObject *
math_fmod(PyObject *self, PyObject *args)
{
    PyObject *ox, *oy;
    double r, x, y;
    if (! PyArg_UnpackTuple(args, "fmod", 2, 2, &ox, &oy))
        return NULL;
    x = PyFloat_AsDouble(ox);
    y = PyFloat_AsDouble(oy);
    if ((x == -1.0 || y == -1.0) && PyErr_Occurred())
        return NULL;

    if (Py_IS_INFINITY(y) && Py_IS_FINITE(x))
        return PyFloat_FromDouble(x);
    errno = 0;
    PyFPE_START_PROTECT("in math_fmod", return 0);
    r = fmod(x, y);
    PyFPE_END_PROTECT(r);
    if (Py_IS_NAN(r)) {
        if (!Py_IS_NAN(x) && !Py_IS_NAN(y))
            errno = EDOM;
        else
            errno = 0;
    }
    if (errno && is_error(r))
        return NULL;
    else
        return PyFloat_FromDouble(r);
}

PyDoc_STRVAR(math_fmod_doc,
"fmod(x, y)\n\nReturn fmod(x, y), according to platform C."
"  x % y may differ.");

static PyObject *
math_hypot(PyObject *self, PyObject *args)
{
    PyObject *ox, *oy;
    double r, x, y;
    if (! PyArg_UnpackTuple(args, "hypot", 2, 2, &ox, &oy))
        return NULL;
    x = PyFloat_AsDouble(ox);
    y = PyFloat_AsDouble(oy);
    if ((x == -1.0 || y == -1.0) && PyErr_Occurred())
        return NULL;

    if (Py_IS_INFINITY(x))
        return PyFloat_FromDouble(fabs(x));
    if (Py_IS_INFINITY(y))
        return PyFloat_FromDouble(fabs(y));
    errno = 0;
    PyFPE_START_PROTECT("in math_hypot", return 0);
    r = hypot(x, y);
    PyFPE_END_PROTECT(r);
    if (Py_IS_NAN(r)) {
        if (!Py_IS_NAN(x) && !Py_IS_NAN(y))
            errno = EDOM;
        else
            errno = 0;
    }
    else if (Py_IS_INFINITY(r)) {
        if (Py_IS_FINITE(x) && Py_IS_FINITE(y))
            errno = ERANGE;
        else
            errno = 0;
    }
    if (errno && is_error(r))
        return NULL;
    else
        return PyFloat_FromDouble(r);
}

PyDoc_STRVAR(math_hypot_doc,
"hypot(x, y)\n\nReturn the Euclidean distance, sqrt(x*x + y*y).");

static PyObject *
math_pow(PyObject *self, PyObject *args)
{
    PyObject *ox, *oy;
    double r, x, y;
    int odd_y;

    if (! PyArg_UnpackTuple(args, "pow", 2, 2, &ox, &oy))
        return NULL;
    x = PyFloat_AsDouble(ox);
    y = PyFloat_AsDouble(oy);
    if ((x == -1.0 || y == -1.0) && PyErr_Occurred())
        return NULL;

    r = 0.;
    if (!Py_IS_FINITE(x) || !Py_IS_FINITE(y)) {
        errno = 0;
        if (Py_IS_NAN(x))
            r = y == 0. ? 1. : x;
        else if (Py_IS_NAN(y))
            r = x == 1. ? 1. : y;
        else if (Py_IS_INFINITY(x)) {
            odd_y = Py_IS_FINITE(y) && fmod(fabs(y), 2.0) == 1.0;
            if (y > 0.)
                r = odd_y ? x : fabs(x);
            else if (y == 0.)
                r = 1.;
            else
                r = odd_y ? copysign(0., x) : 0.;
        }
        else if (Py_IS_INFINITY(y)) {
            if (fabs(x) == 1.0)
                r = 1.;
            else if (y > 0. && fabs(x) > 1.0)
                r = y;
            else if (y < 0. && fabs(x) < 1.0) {
                r = -y;
                if (x == 0.)
                    errno = EDOM;
            }
            else
                r = 0.;
        }
    }
    else {

        errno = 0;
        PyFPE_START_PROTECT("in math_pow", return 0);
        r = pow(x, y);
        PyFPE_END_PROTECT(r);

        if (!Py_IS_FINITE(r)) {
            if (Py_IS_NAN(r)) {
                errno = EDOM;
            }

            else if (Py_IS_INFINITY(r)) {
                if (x == 0.)
                    errno = EDOM;
                else
                    errno = ERANGE;
            }
        }
    }

    if (errno && is_error(r))
        return NULL;
    else
        return PyFloat_FromDouble(r);
}

PyDoc_STRVAR(math_pow_doc,
"pow(x, y)\n\nReturn x**y (x to the power of y).");

static const double degToRad = Py_MATH_PI / 180.0;
static const double radToDeg = 180.0 / Py_MATH_PI;

static PyObject *
math_degrees(PyObject *self, PyObject *arg)
{
    double x = PyFloat_AsDouble(arg);
    if (x == -1.0 && PyErr_Occurred())
        return NULL;
    return PyFloat_FromDouble(x * radToDeg);
}

PyDoc_STRVAR(math_degrees_doc,
"degrees(x)\n\n\
Convert angle x from radians to degrees.");

static PyObject *
math_radians(PyObject *self, PyObject *arg)
{
    double x = PyFloat_AsDouble(arg);
    if (x == -1.0 && PyErr_Occurred())
        return NULL;
    return PyFloat_FromDouble(x * degToRad);
}

PyDoc_STRVAR(math_radians_doc,
"radians(x)\n\n\
Convert angle x from degrees to radians.");

static PyObject *
math_isnan(PyObject *self, PyObject *arg)
{
    double x = PyFloat_AsDouble(arg);
    if (x == -1.0 && PyErr_Occurred())
        return NULL;
    return PyBool_FromLong((long)Py_IS_NAN(x));
}

PyDoc_STRVAR(math_isnan_doc,
"isnan(x) -> bool\n\n\
Check if float x is not a number (NaN).");

static PyObject *
math_isinf(PyObject *self, PyObject *arg)
{
    double x = PyFloat_AsDouble(arg);
    if (x == -1.0 && PyErr_Occurred())
        return NULL;
    return PyBool_FromLong((long)Py_IS_INFINITY(x));
}

PyDoc_STRVAR(math_isinf_doc,
"isinf(x) -> bool\n\n\
Check if float x is infinite (positive or negative).");

static PyMethodDef math_methods[] = {
    {"acos",            math_acos,      METH_O,         math_acos_doc},
    {"acosh",           math_acosh,     METH_O,         math_acosh_doc},
    {"asin",            math_asin,      METH_O,         math_asin_doc},
    {"asinh",           math_asinh,     METH_O,         math_asinh_doc},
    {"atan",            math_atan,      METH_O,         math_atan_doc},
    {"atan2",           math_atan2,     METH_VARARGS,   math_atan2_doc},
    {"atanh",           math_atanh,     METH_O,         math_atanh_doc},
    {"ceil",            math_ceil,      METH_O,         math_ceil_doc},
    {"copysign",        math_copysign,  METH_VARARGS,   math_copysign_doc},
    {"cos",             math_cos,       METH_O,         math_cos_doc},
    {"cosh",            math_cosh,      METH_O,         math_cosh_doc},
    {"degrees",         math_degrees,   METH_O,         math_degrees_doc},
    {"erf",             math_erf,       METH_O,         math_erf_doc},
    {"erfc",            math_erfc,      METH_O,         math_erfc_doc},
    {"exp",             math_exp,       METH_O,         math_exp_doc},
    {"expm1",           math_expm1,     METH_O,         math_expm1_doc},
    {"fabs",            math_fabs,      METH_O,         math_fabs_doc},
    {"factorial",       math_factorial, METH_O,         math_factorial_doc},
    {"floor",           math_floor,     METH_O,         math_floor_doc},
    {"fmod",            math_fmod,      METH_VARARGS,   math_fmod_doc},
    {"frexp",           math_frexp,     METH_O,         math_frexp_doc},
    {"fsum",            math_fsum,      METH_O,         math_fsum_doc},
    {"gamma",           math_gamma,     METH_O,         math_gamma_doc},
    {"hypot",           math_hypot,     METH_VARARGS,   math_hypot_doc},
    {"isinf",           math_isinf,     METH_O,         math_isinf_doc},
    {"isnan",           math_isnan,     METH_O,         math_isnan_doc},
    {"ldexp",           math_ldexp,     METH_VARARGS,   math_ldexp_doc},
    {"lgamma",          math_lgamma,    METH_O,         math_lgamma_doc},
    {"log",             math_log,       METH_VARARGS,   math_log_doc},
    {"log1p",           math_log1p,     METH_O,         math_log1p_doc},
    {"log10",           math_log10,     METH_O,         math_log10_doc},
    {"modf",            math_modf,      METH_O,         math_modf_doc},
    {"pow",             math_pow,       METH_VARARGS,   math_pow_doc},
    {"radians",         math_radians,   METH_O,         math_radians_doc},
    {"sin",             math_sin,       METH_O,         math_sin_doc},
    {"sinh",            math_sinh,      METH_O,         math_sinh_doc},
    {"sqrt",            math_sqrt,      METH_O,         math_sqrt_doc},
    {"tan",             math_tan,       METH_O,         math_tan_doc},
    {"tanh",            math_tanh,      METH_O,         math_tanh_doc},
    {"trunc",           math_trunc,     METH_O,         math_trunc_doc},
    {NULL,              NULL}
};

PyDoc_STRVAR(module_doc,
"This module is always available.  It provides access to the\n"
"mathematical functions defined by the C standard.");

PyMODINIT_FUNC
initmath(void)
{
    PyObject *m;

    m = Py_InitModule3("math", math_methods, module_doc);
    if (m == NULL)
        goto finally;

    PyModule_AddObject(m, "pi", PyFloat_FromDouble(Py_MATH_PI));
    PyModule_AddObject(m, "e", PyFloat_FromDouble(Py_MATH_E));

    finally:
    return;
}
