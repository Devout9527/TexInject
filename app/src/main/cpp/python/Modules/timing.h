#ifndef _TIMING_H_
#define _TIMING_H_

#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

static struct timeval aftertp, beforetp;

#define BEGINTIMING gettimeofday(&beforetp, NULL)

#define ENDTIMING gettimeofday(&aftertp, NULL); \
    if(beforetp.tv_usec > aftertp.tv_usec) \
    {  \
         aftertp.tv_usec += 1000000;  \
         aftertp.tv_sec--; \
    }

#define TIMINGUS (((aftertp.tv_sec - beforetp.tv_sec) * 1000000) + \
		  (aftertp.tv_usec - beforetp.tv_usec))

#define TIMINGMS (((aftertp.tv_sec - beforetp.tv_sec) * 1000) + \
		  ((aftertp.tv_usec - beforetp.tv_usec) / 1000))

#define TIMINGS  ((aftertp.tv_sec - beforetp.tv_sec) + \
		  (aftertp.tv_usec - beforetp.tv_usec) / 1000000)

#endif
