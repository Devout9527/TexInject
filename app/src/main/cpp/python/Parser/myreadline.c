#include "Python.h"
#ifdef MS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#endif

#ifdef __VMS
extern char* vms__StdioReadline(FILE *sys_stdin, FILE *sys_stdout, char *prompt);
#endif

PyThreadState* _PyOS_ReadlineTState;

#ifdef WITH_THREAD
#include "pythread.h"
static PyThread_type_lock _PyOS_ReadlineLock = NULL;
#endif

int (*PyOS_InputHook)(void) = NULL;

#ifdef RISCOS
int Py_RISCOSWimpFlag;
#endif

static int
my_fgets(char *buf, int len, FILE *fp)
{
    char *p;
#ifdef MS_WINDOWS
    int i;
#endif

    while (1) {
        if (PyOS_InputHook != NULL)
            (void)(PyOS_InputHook)();
        errno = 0;
        clearerr(fp);
        p = fgets(buf, len, fp);
        if (p != NULL)
            return 0;
#ifdef MS_WINDOWS

        if (GetLastError()==ERROR_OPERATION_ABORTED) {
            for (i = 0; i < 10; i++) {
                if (PyOS_InterruptOccurred())
                    return 1;
                Sleep(1);
            }
        }
#endif
        if (feof(fp)) {
            clearerr(fp);
            return -1;
        }
#ifdef EINTR
        if (errno == EINTR) {
            int s;
#ifdef WITH_THREAD
            PyEval_RestoreThread(_PyOS_ReadlineTState);
#endif
            s = PyErr_CheckSignals();
#ifdef WITH_THREAD
            PyEval_SaveThread();
#endif
            if (s < 0)
                    return 1;

            continue;
        }
#endif
        if (PyOS_InterruptOccurred()) {
            return 1;
        }
        return -2;
    }

}

char *
PyOS_StdioReadline(FILE *sys_stdin, FILE *sys_stdout, char *prompt)
{
    size_t n;
    char *p;
    n = 100;
    if ((p = (char *)PyMem_MALLOC(n)) == NULL)
        return NULL;
    fflush(sys_stdout);
#ifndef RISCOS
    if (prompt)
        fprintf(stderr, "%s", prompt);
#else
    if (prompt) {
        if(Py_RISCOSWimpFlag)
            fprintf(stderr, "\x0cr%s\x0c", prompt);
        else
            fprintf(stderr, "%s", prompt);
    }
#endif
    fflush(stderr);
    switch (my_fgets(p, (int)n, sys_stdin)) {
    case 0:
        break;
    case 1:
        PyMem_FREE(p);
        return NULL;
    case -1:
    case -2:
    default:
        *p = '\0';
        break;
    }
    n = strlen(p);
    while (n > 0 && p[n-1] != '\n') {
        size_t incr = n+2;
        p = (char *)PyMem_REALLOC(p, n + incr);
        if (p == NULL)
            return NULL;
        if (incr > INT_MAX) {
            PyErr_SetString(PyExc_OverflowError, "input line too long");
        }
        if (my_fgets(p+n, (int)incr, sys_stdin) != 0)
            break;
        n += strlen(p+n);
    }
    return (char *)PyMem_REALLOC(p, n+1);
}

char *(*PyOS_ReadlineFunctionPointer)(FILE *, FILE *, char *);

char *
PyOS_Readline(FILE *sys_stdin, FILE *sys_stdout, char *prompt)
{
    char *rv;

    if (_PyOS_ReadlineTState == PyThreadState_GET()) {
        PyErr_SetString(PyExc_RuntimeError,
                        "can't re-enter readline");
        return NULL;
    }

    if (PyOS_ReadlineFunctionPointer == NULL) {
#ifdef __VMS
        PyOS_ReadlineFunctionPointer = vms__StdioReadline;
#else
        PyOS_ReadlineFunctionPointer = PyOS_StdioReadline;
#endif
    }

#ifdef WITH_THREAD
    if (_PyOS_ReadlineLock == NULL) {
        _PyOS_ReadlineLock = PyThread_allocate_lock();
    }
#endif

    _PyOS_ReadlineTState = PyThreadState_GET();
    Py_BEGIN_ALLOW_THREADS
#ifdef WITH_THREAD
    PyThread_acquire_lock(_PyOS_ReadlineLock, 1);
#endif

    if (!isatty (fileno (sys_stdin)) || !isatty (fileno (sys_stdout)))
        rv = PyOS_StdioReadline (sys_stdin, sys_stdout, prompt);
    else
        rv = (*PyOS_ReadlineFunctionPointer)(sys_stdin, sys_stdout,
                                             prompt);
    Py_END_ALLOW_THREADS

#ifdef WITH_THREAD
    PyThread_release_lock(_PyOS_ReadlineLock);
#endif

    _PyOS_ReadlineTState = NULL;

    return rv;
}
