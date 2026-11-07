#ifndef Py_PYMATH_H
#define Py_PYMATH_H

#include "pyconfig.h"

#ifndef HAVE_COPYSIGN
extern double copysign(double, double);
#endif

#ifndef HAVE_ROUND
extern double round(double);
#endif

#ifndef HAVE_HYPOT
extern double hypot(double, double);
#endif

#ifndef _MSC_VER
#ifndef __STDC__
extern double fmod (double, double);
extern double frexp (double, int *);
extern double ldexp (double, int);
extern double modf (double, double *);
extern double pow(double, double);
#endif
#endif

#ifdef _OSF_SOURCE

extern int finite(double);
extern double copysign(double, double);
#endif

#ifndef Py_MATH_PIl
#define Py_MATH_PIl 3.1415926535897932384626433832795029L
#endif
#ifndef Py_MATH_PI
#define Py_MATH_PI 3.14159265358979323846
#endif

#ifndef Py_MATH_El
#define Py_MATH_El 2.7182818284590452353602874713526625L
#endif

#ifndef Py_MATH_E
#define Py_MATH_E 2.7182818284590452354
#endif

#ifndef Py_FORCE_DOUBLE
#  ifdef X87_DOUBLE_ROUNDING
PyAPI_FUNC(double) _Py_force_double(double);
#    define Py_FORCE_DOUBLE(X) (_Py_force_double(X))
#  else
#    define Py_FORCE_DOUBLE(X) (X)
#  endif
#endif

#ifdef HAVE_GCC_ASM_FOR_X87
PyAPI_FUNC(unsigned short) _Py_get_387controlword(void);
PyAPI_FUNC(void) _Py_set_387controlword(unsigned short);
#endif

#ifndef Py_IS_NAN
#if defined HAVE_DECL_ISNAN && HAVE_DECL_ISNAN == 1
#define Py_IS_NAN(X) isnan(X)
#else
#define Py_IS_NAN(X) ((X) != (X))
#endif
#endif

#ifndef Py_IS_INFINITY
#  if defined HAVE_DECL_ISINF && HAVE_DECL_ISINF == 1
#    define Py_IS_INFINITY(X) isinf(X)
#  else
#    define Py_IS_INFINITY(X) ((X) &&                                   \
                               (Py_FORCE_DOUBLE(X)*0.5 == Py_FORCE_DOUBLE(X)))
#  endif
#endif

#ifndef Py_IS_FINITE
#if defined HAVE_DECL_ISFINITE && HAVE_DECL_ISFINITE == 1
#define Py_IS_FINITE(X) isfinite(X)
#elif defined HAVE_FINITE
#define Py_IS_FINITE(X) finite(X)
#else
#define Py_IS_FINITE(X) (!Py_IS_INFINITY(X) && !Py_IS_NAN(X))
#endif
#endif

#ifndef Py_HUGE_VAL
#define Py_HUGE_VAL HUGE_VAL
#endif

#if !defined(Py_NAN) && !defined(Py_NO_NAN)
#if !defined(__INTEL_COMPILER)
    #define Py_NAN (Py_HUGE_VAL * 0.)
#else
    #if defined(ICC_NAN_STRICT)
        #pragma float_control(push)
        #pragma float_control(precise, on)
        #pragma float_control(except,  on)
        #if defined(_MSC_VER)
            __declspec(noinline)
        #else
            __attribute__((noinline))
        #endif
        static double __icc_nan()
        {
            return sqrt(-1.0);
        }
        #pragma float_control (pop)
        #define Py_NAN __icc_nan()
    #else
        static union { unsigned char buf[8]; double __icc_nan; } __nan_store = {0,0,0,0,0,0,0xf8,0x7f};
        #define Py_NAN (__nan_store.__icc_nan)
    #endif
#endif
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#define Py_OVERFLOWED(X) isinf(X)
#else
#define Py_OVERFLOWED(X) ((X) != 0.0 && (errno == ERANGE ||    \
					 (X) == Py_HUGE_VAL || \
					 (X) == -Py_HUGE_VAL))
#endif

#endif
