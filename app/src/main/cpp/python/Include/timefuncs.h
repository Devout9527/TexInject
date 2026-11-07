#ifndef TIMEFUNCS_H
#define TIMEFUNCS_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_FUNC(time_t) _PyTime_DoubleToTimet(double x);

PyAPI_FUNC(double) _PyTime_FloatTime(void);

#ifdef __cplusplus
}
#endif
#endif
