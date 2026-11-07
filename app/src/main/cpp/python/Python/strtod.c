#include <stdio.h>
#include <string.h>

#include "pyconfig.h"

static  int     MDMINEXPT       = -323;
static  char    MDMINFRAC[]     = "494065645841246544";
static  double  ZERO            = 0.0;

static  int     MDMAXEXPT       = 309;
static  char    MDMAXFRAC[]     = "17976931348623157";
static  double  HUGE            = 1.7976931348623157e308;

extern  double  atof(const char *);

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
extern  int     errno;

double strtod(char *str, char **ptr)
{
    int sign, scale, dotseen;
    int esign, expt;
    char *save;
    register char *sp, *dp;
    register int c;
    char *buforg, *buflim;
    char buffer[64];

    sp = str;
    while (*sp == ' ') sp++;
    sign = 1;
    if (*sp == '-') sign -= 2, sp++;
    dotseen = 0, scale = 0;
    dp = buffer;
    *dp++ = '0'; *dp++ = '.';
    buforg = dp, buflim = buffer+48;
    for (save = sp; (c = *sp); sp++)
        if (c == '.') {
        if (dotseen) break;
        dotseen++;
        } else
        if ((unsigned)(c-'0') > (unsigned)('9'-'0')) {
        break;
        } else
        if (c == '0') {
        if (dp != buforg) {

            if (dp < buflim) *dp++ = c;
            if (!dotseen) scale++;
        } else {

            if (dotseen) scale--;
        }
        } else {

        if (dp < buflim) *dp++ = c;

        if (!dotseen) scale++;
        }
    if (sp == save) {
        if (ptr) *ptr = str;
        errno = EDOM;
        return ZERO;
    }

    while (dp > buforg && dp[-1] == '0') --dp;
    if (dp == buforg) *dp++ = '0';
    *dp = '\0';

    save = sp, expt = 0, esign = 1;
    do {
        c = *sp++;
        if (c != 'e' && c != 'E') break;
        c = *sp++;
        if (c == '-') esign -= 2, c = *sp++; else
        if (c == '+'   ) c = *sp++;
        if ((unsigned)(c-'0') > (unsigned)('9'-'0')) break;
        while (c == '0') c = *sp++;
        for (; (unsigned)(c-'0') <= (unsigned)('9'-'0'); c = *sp++)
        expt = expt*10 + c-'0';
        if (esign < 0) expt = -expt;
        save = sp-1;
    } while (0);
    if (ptr) *ptr = save;
    expt += scale;

    errno = ERANGE;
    if (expt > MDMAXEXPT) {
        return HUGE*sign;
    } else
    if (expt == MDMAXEXPT) {
        if (strcmp(buforg, MDMAXFRAC) > 0) return HUGE*sign;
    } else
    if (expt < MDMINEXPT) {
        return ZERO*sign;
    } else
    if (expt == MDMINEXPT) {
        if (strcmp(buforg, MDMINFRAC) < 0) return ZERO*sign;
    }

    (void) sprintf(dp, "E%d", expt);
    errno = 0;
    return atof(buffer)*sign;
}
