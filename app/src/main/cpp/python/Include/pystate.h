#ifndef Py_PYSTATE_H
#define Py_PYSTATE_H

#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _ts;
struct _is;

typedef struct _is {

    struct _is *next;
    struct _ts *tstate_head;

    PyObject *modules;
    PyObject *sysdict;
    PyObject *builtins;
    PyObject *modules_reloading;

    PyObject *codec_search_path;
    PyObject *codec_search_cache;
    PyObject *codec_error_registry;

#ifdef HAVE_DLOPEN
    int dlopenflags;
#endif
#ifdef WITH_TSC
    int tscdump;
#endif

} PyInterpreterState;

struct _frame;

typedef int (*Py_tracefunc)(PyObject *, struct _frame *, int, PyObject *);

#define PyTrace_CALL 0
#define PyTrace_EXCEPTION 1
#define PyTrace_LINE 2
#define PyTrace_RETURN 3
#define PyTrace_C_CALL 4
#define PyTrace_C_EXCEPTION 5
#define PyTrace_C_RETURN 6

typedef struct _ts {

    struct _ts *next;
    PyInterpreterState *interp;

    struct _frame *frame;
    int recursion_depth;

    int tracing;
    int use_tracing;

    Py_tracefunc c_profilefunc;
    Py_tracefunc c_tracefunc;
    PyObject *c_profileobj;
    PyObject *c_traceobj;

    PyObject *curexc_type;
    PyObject *curexc_value;
    PyObject *curexc_traceback;

    PyObject *exc_type;
    PyObject *exc_value;
    PyObject *exc_traceback;

    PyObject *dict;

    int tick_counter;

    int gilstate_counter;

    PyObject *async_exc;
    long thread_id;

    int trash_delete_nesting;
    PyObject *trash_delete_later;

} PyThreadState;

PyAPI_FUNC(PyInterpreterState *) PyInterpreterState_New(void);
PyAPI_FUNC(void) PyInterpreterState_Clear(PyInterpreterState *);
PyAPI_FUNC(void) PyInterpreterState_Delete(PyInterpreterState *);

PyAPI_FUNC(PyThreadState *) PyThreadState_New(PyInterpreterState *);
PyAPI_FUNC(PyThreadState *) _PyThreadState_Prealloc(PyInterpreterState *);
PyAPI_FUNC(void) _PyThreadState_Init(PyThreadState *);
PyAPI_FUNC(void) PyThreadState_Clear(PyThreadState *);
PyAPI_FUNC(void) PyThreadState_Delete(PyThreadState *);
#ifdef WITH_THREAD
PyAPI_FUNC(void) PyThreadState_DeleteCurrent(void);
#endif

PyAPI_FUNC(PyThreadState *) PyThreadState_Get(void);
PyAPI_FUNC(PyThreadState *) PyThreadState_Swap(PyThreadState *);
PyAPI_FUNC(PyObject *) PyThreadState_GetDict(void);
PyAPI_FUNC(int) PyThreadState_SetAsyncExc(long, PyObject *);

PyAPI_DATA(PyThreadState *) _PyThreadState_Current;

#ifdef Py_DEBUG
#define PyThreadState_GET() PyThreadState_Get()
#else
#define PyThreadState_GET() (_PyThreadState_Current)
#endif

typedef
    enum {PyGILState_LOCKED, PyGILState_UNLOCKED}
        PyGILState_STATE;

PyAPI_FUNC(PyGILState_STATE) PyGILState_Ensure(void);

PyAPI_FUNC(void) PyGILState_Release(PyGILState_STATE);

PyAPI_FUNC(PyThreadState *) PyGILState_GetThisThreadState(void);

PyAPI_FUNC(PyObject *) _PyThread_CurrentFrames(void);

PyAPI_FUNC(PyInterpreterState *) PyInterpreterState_Head(void);
PyAPI_FUNC(PyInterpreterState *) PyInterpreterState_Next(PyInterpreterState *);
PyAPI_FUNC(PyThreadState *) PyInterpreterState_ThreadHead(PyInterpreterState *);
PyAPI_FUNC(PyThreadState *) PyThreadState_Next(PyThreadState *);

typedef struct _frame *(*PyThreadFrameGetter)(PyThreadState *self_);

PyAPI_DATA(PyThreadFrameGetter) _PyThreadState_GetFrame;

#ifdef __cplusplus
}
#endif
#endif
