#include "Python.h"

#ifdef X87_DOUBLE_ROUNDING

double _Py_force_double(double x)
{
    volatile double y;
    y = x;
    return y;
}
#endif

#ifdef HAVE_GCC_ASM_FOR_X87

unsigned short _Py_get_387controlword(void) {
    unsigned short cw;
    __asm__ __volatile__ ("fnstcw %0" : "=m" (cw));
    return cw;
}

void _Py_set_387controlword(unsigned short cw) {
    __asm__ __volatile__ ("fldcw %0" : : "m" (cw));
}

#endif

#ifndef HAVE_HYPOT
double hypot(double x, double y)
{
    double yx;

    x = fabs(x);
    y = fabs(y);
    if (x < y) {
        double temp = x;
        x = y;
        y = temp;
    }
    if (x == 0.)
        return 0.;
    else {
        yx = y/x;
        return x*sqrt(1.+yx*yx);
    }
}
#endif

#ifndef HAVE_COPYSIGN
double
copysign(double x, double y)
{

    if (y > 0. || (y == 0. && atan2(y, -1.) > 0.)) {
        return fabs(x);
    } else {
        return -fabs(x);
    }
}
#endif

#ifndef HAVE_ROUND
double
round(double x)
{
    double absx, y;
    absx = fabs(x);
    y = floor(absx);
    if (absx - y >= 0.5)
        y += 1.0;
    return copysign(y, x);
}
#endif
