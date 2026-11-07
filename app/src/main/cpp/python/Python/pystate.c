#include "Python.h"

#ifdef HAVE_DLOPEN
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif
#ifndef RTLD_LAZY
#define RTLD_LAZY 1
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WITH_THREAD
#include "pythread.h"
static PyThread_type_lock head_mutex = NULL;
#define HEAD_INIT() (void)(head_mutex || (head_mutex = PyThread_allocate_lock()))
#define HEAD_LOCK() PyThread_acquire_lock(head_mutex, WAIT_LOCK)
#define HEAD_UNLOCK() PyThread_release_lock(head_mutex)

static PyInterpreterState *autoInterpreterState = NULL;
static int autoTLSkey = 0;
#else
#define HEAD_INIT()
#define HEAD_LOCK()
#define HEAD_UNLOCK()
#endif

static PyInterpreterState *interp_head = NULL;

PyThreadState *_PyThreadState_Current = NULL;
PyThreadFrameGetter _PyThreadState_GetFrame = NULL;

#ifdef WITH_THREAD
static void _PyGILState_NoteThreadState(PyThreadState* tstate);
#endif

PyInterpreterState *
PyInterpreterState_New(void)
{
    PyInterpreterState *interp = (PyInterpreterState *)
                                 malloc(sizeof(PyInterpreterState));

    if (interp != NULL) {
        HEAD_INIT();
#ifdef WITH_THREAD
        if (head_mutex == NULL)
            Py_FatalError("Can't initialize threads for interpreter");
#endif
        interp->modules = NULL;
        interp->modules_reloading = NULL;
        interp->sysdict = NULL;
        interp->builtins = NULL;
        interp->tstate_head = NULL;
        interp->codec_search_path = NULL;
        interp->codec_search_cache = NULL;
        interp->codec_error_registry = NULL;
#ifdef HAVE_DLOPEN
#ifdef RTLD_NOW
        interp->dlopenflags = RTLD_NOW;
#else
        interp->dlopenflags = RTLD_LAZY;
#endif
#endif
#ifdef WITH_TSC
        interp->tscdump = 0;
#endif

        HEAD_LOCK();
        interp->next = interp_head;
        interp_head = interp;
        HEAD_UNLOCK();
    }

    return interp;
}

void
PyInterpreterState_Clear(PyInterpreterState *interp)
{
    PyThreadState *p;
    HEAD_LOCK();
    for (p = interp->tstate_head; p != NULL; p = p->next)
        PyThreadState_Clear(p);
    HEAD_UNLOCK();
    Py_CLEAR(interp->codec_search_path);
    Py_CLEAR(interp->codec_search_cache);
    Py_CLEAR(interp->codec_error_registry);
    Py_CLEAR(interp->modules);
    Py_CLEAR(interp->modules_reloading);
    Py_CLEAR(interp->sysdict);
    Py_CLEAR(interp->builtins);
}

static void
zapthreads(PyInterpreterState *interp)
{
    PyThreadState *p;

    while ((p = interp->tstate_head) != NULL) {
        PyThreadState_Delete(p);
    }
}

void
PyInterpreterState_Delete(PyInterpreterState *interp)
{
    PyInterpreterState **p;
    zapthreads(interp);
    HEAD_LOCK();
    for (p = &interp_head; ; p = &(*p)->next) {
        if (*p == NULL)
            Py_FatalError(
                "PyInterpreterState_Delete: invalid interp");
        if (*p == interp)
            break;
    }
    if (interp->tstate_head != NULL)
        Py_FatalError("PyInterpreterState_Delete: remaining threads");
    *p = interp->next;
    HEAD_UNLOCK();
    free(interp);
}

static struct _frame *
threadstate_getframe(PyThreadState *self)
{
    return self->frame;
}

static PyThreadState *
new_threadstate(PyInterpreterState *interp, int init)
{
    PyThreadState *tstate = (PyThreadState *)malloc(sizeof(PyThreadState));

    if (_PyThreadState_GetFrame == NULL)
        _PyThreadState_GetFrame = threadstate_getframe;

    if (tstate != NULL) {
        tstate->interp = interp;

        tstate->frame = NULL;
        tstate->recursion_depth = 0;
        tstate->tracing = 0;
        tstate->use_tracing = 0;
        tstate->tick_counter = 0;
        tstate->gilstate_counter = 0;
        tstate->async_exc = NULL;
#ifdef WITH_THREAD
        tstate->thread_id = PyThread_get_thread_ident();
#else
        tstate->thread_id = 0;
#endif

        tstate->dict = NULL;

        tstate->curexc_type = NULL;
        tstate->curexc_value = NULL;
        tstate->curexc_traceback = NULL;

        tstate->exc_type = NULL;
        tstate->exc_value = NULL;
        tstate->exc_traceback = NULL;

        tstate->c_profilefunc = NULL;
        tstate->c_tracefunc = NULL;
        tstate->c_profileobj = NULL;
        tstate->c_traceobj = NULL;

        tstate->trash_delete_nesting = 0;
        tstate->trash_delete_later = NULL;

        if (init)
            _PyThreadState_Init(tstate);

        HEAD_LOCK();
        tstate->next = interp->tstate_head;
        interp->tstate_head = tstate;
        HEAD_UNLOCK();
    }

    return tstate;
}

PyThreadState *
PyThreadState_New(PyInterpreterState *interp)
{
    return new_threadstate(interp, 1);
}

PyThreadState *
_PyThreadState_Prealloc(PyInterpreterState *interp)
{
    return new_threadstate(interp, 0);
}

void
_PyThreadState_Init(PyThreadState *tstate)
{
#ifdef WITH_THREAD
    _PyGILState_NoteThreadState(tstate);
#endif
}

void
PyThreadState_Clear(PyThreadState *tstate)
{
    if (Py_VerboseFlag && tstate->frame != NULL)
        fprintf(stderr,
          "PyThreadState_Clear: warning: thread still has a frame\n");

    Py_CLEAR(tstate->frame);

    Py_CLEAR(tstate->dict);
    Py_CLEAR(tstate->async_exc);

    Py_CLEAR(tstate->curexc_type);
    Py_CLEAR(tstate->curexc_value);
    Py_CLEAR(tstate->curexc_traceback);

    Py_CLEAR(tstate->exc_type);
    Py_CLEAR(tstate->exc_value);
    Py_CLEAR(tstate->exc_traceback);

    tstate->c_profilefunc = NULL;
    tstate->c_tracefunc = NULL;
    Py_CLEAR(tstate->c_profileobj);
    Py_CLEAR(tstate->c_traceobj);
}

static void
tstate_delete_common(PyThreadState *tstate)
{
    PyInterpreterState *interp;
    PyThreadState **p;
    PyThreadState *prev_p = NULL;
    if (tstate == NULL)
        Py_FatalError("PyThreadState_Delete: NULL tstate");
    interp = tstate->interp;
    if (interp == NULL)
        Py_FatalError("PyThreadState_Delete: NULL interp");
    HEAD_LOCK();
    for (p = &interp->tstate_head; ; p = &(*p)->next) {
        if (*p == NULL)
            Py_FatalError(
                "PyThreadState_Delete: invalid tstate");
        if (*p == tstate)
            break;

        if (*p == prev_p)
            Py_FatalError(
                "PyThreadState_Delete: small circular list(!)"
                " and tstate not found.");
        prev_p = *p;
        if ((*p)->next == interp->tstate_head)
            Py_FatalError(
                "PyThreadState_Delete: circular list(!) and"
                " tstate not found.");
    }
    *p = tstate->next;
    HEAD_UNLOCK();
    free(tstate);
}

void
PyThreadState_Delete(PyThreadState *tstate)
{
    if (tstate == _PyThreadState_Current)
        Py_FatalError("PyThreadState_Delete: tstate is still current");
    tstate_delete_common(tstate);
#ifdef WITH_THREAD
    if (autoInterpreterState && PyThread_get_key_value(autoTLSkey) == tstate)
        PyThread_delete_key_value(autoTLSkey);
#endif
}

#ifdef WITH_THREAD
void
PyThreadState_DeleteCurrent()
{
    PyThreadState *tstate = _PyThreadState_Current;
    if (tstate == NULL)
        Py_FatalError(
            "PyThreadState_DeleteCurrent: no current tstate");
    _PyThreadState_Current = NULL;
    if (autoInterpreterState && PyThread_get_key_value(autoTLSkey) == tstate)
        PyThread_delete_key_value(autoTLSkey);
    tstate_delete_common(tstate);
    PyEval_ReleaseLock();
}
#endif

PyThreadState *
PyThreadState_Get(void)
{
    if (_PyThreadState_Current == NULL)
        Py_FatalError("PyThreadState_Get: no current thread");

    return _PyThreadState_Current;
}

PyThreadState *
PyThreadState_Swap(PyThreadState *newts)
{
    PyThreadState *oldts = _PyThreadState_Current;

    _PyThreadState_Current = newts;

#if defined(Py_DEBUG) && defined(WITH_THREAD)
    if (newts) {

        int err = errno;
        PyThreadState *check = PyGILState_GetThisThreadState();
        if (check && check->interp == newts->interp && check != newts)
            Py_FatalError("Invalid thread state for this thread");
        errno = err;
    }
#endif
    return oldts;
}

PyObject *
PyThreadState_GetDict(void)
{
    if (_PyThreadState_Current == NULL)
        return NULL;

    if (_PyThreadState_Current->dict == NULL) {
        PyObject *d;
        _PyThreadState_Current->dict = d = PyDict_New();
        if (d == NULL)
            PyErr_Clear();
    }
    return _PyThreadState_Current->dict;
}

int
PyThreadState_SetAsyncExc(long id, PyObject *exc) {
    PyThreadState *tstate = PyThreadState_GET();
    PyInterpreterState *interp = tstate->interp;
    PyThreadState *p;

    HEAD_LOCK();
    for (p = interp->tstate_head; p != NULL; p = p->next) {
        if (p->thread_id == id) {

            PyObject *old_exc = p->async_exc;
            Py_XINCREF(exc);
            p->async_exc = exc;
            HEAD_UNLOCK();
            Py_XDECREF(old_exc);
            return 1;
        }
    }
    HEAD_UNLOCK();
    return 0;
}

PyInterpreterState *
PyInterpreterState_Head(void)
{
    return interp_head;
}

PyInterpreterState *
PyInterpreterState_Next(PyInterpreterState *interp) {
    return interp->next;
}

PyThreadState *
PyInterpreterState_ThreadHead(PyInterpreterState *interp) {
    return interp->tstate_head;
}

PyThreadState *
PyThreadState_Next(PyThreadState *tstate) {
    return tstate->next;
}

PyObject *
_PyThread_CurrentFrames(void)
{
    PyObject *result;
    PyInterpreterState *i;

    result = PyDict_New();
    if (result == NULL)
        return NULL;

    HEAD_LOCK();
    for (i = interp_head; i != NULL; i = i->next) {
        PyThreadState *t;
        for (t = i->tstate_head; t != NULL; t = t->next) {
            PyObject *id;
            int stat;
            struct _frame *frame = t->frame;
            if (frame == NULL)
                continue;
            id = PyInt_FromLong(t->thread_id);
            if (id == NULL)
                goto Fail;
            stat = PyDict_SetItem(result, id, (PyObject *)frame);
            Py_DECREF(id);
            if (stat < 0)
                goto Fail;
        }
    }
    HEAD_UNLOCK();
    return result;

 Fail:
    HEAD_UNLOCK();
    Py_DECREF(result);
    return NULL;
}

#ifdef WITH_THREAD

static int
PyThreadState_IsCurrent(PyThreadState *tstate)
{

    assert(PyGILState_GetThisThreadState()==tstate);

    return tstate == _PyThreadState_Current;
}

void
_PyGILState_Init(PyInterpreterState *i, PyThreadState *t)
{
    assert(i && t);
    autoTLSkey = PyThread_create_key();
    autoInterpreterState = i;
    assert(PyThread_get_key_value(autoTLSkey) == NULL);
    assert(t->gilstate_counter == 0);

    _PyGILState_NoteThreadState(t);
}

void
_PyGILState_Fini(void)
{
    PyThread_delete_key(autoTLSkey);
    autoInterpreterState = NULL;
}

static void
_PyGILState_NoteThreadState(PyThreadState* tstate)
{

    if (!autoInterpreterState)
        return;

    if (PyThread_set_key_value(autoTLSkey, (void *)tstate) < 0)
        Py_FatalError("Couldn't create autoTLSkey mapping");

    tstate->gilstate_counter = 1;
}

PyThreadState *
PyGILState_GetThisThreadState(void)
{
    if (autoInterpreterState == NULL)
        return NULL;
    return (PyThreadState *)PyThread_get_key_value(autoTLSkey);
}

PyGILState_STATE
PyGILState_Ensure(void)
{
    int current;
    PyThreadState *tcur;

    assert(autoInterpreterState);
    tcur = (PyThreadState *)PyThread_get_key_value(autoTLSkey);
    if (tcur == NULL) {

        tcur = PyThreadState_New(autoInterpreterState);
        if (tcur == NULL)
            Py_FatalError("Couldn't create thread-state for new thread");

        tcur->gilstate_counter = 0;
        current = 0;
    }
    else
        current = PyThreadState_IsCurrent(tcur);
    if (current == 0)
        PyEval_RestoreThread(tcur);

    ++tcur->gilstate_counter;
    return current ? PyGILState_LOCKED : PyGILState_UNLOCKED;
}

void
PyGILState_Release(PyGILState_STATE oldstate)
{
    PyThreadState *tcur = (PyThreadState *)PyThread_get_key_value(
                                                            autoTLSkey);
    if (tcur == NULL)
        Py_FatalError("auto-releasing thread-state, "
                      "but no thread-state for this thread");

    if (! PyThreadState_IsCurrent(tcur))
        Py_FatalError("This thread state must be current when releasing");
    assert(PyThreadState_IsCurrent(tcur));
    --tcur->gilstate_counter;
    assert(tcur->gilstate_counter >= 0);

    if (tcur->gilstate_counter == 0) {

        assert(oldstate == PyGILState_UNLOCKED);
        PyThreadState_Clear(tcur);

        PyThreadState_DeleteCurrent();
    }

    else if (oldstate == PyGILState_UNLOCKED)
        PyEval_SaveThread();
}

#endif

#ifdef __cplusplus
}
#endif
