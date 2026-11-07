#include "Python.h"
#include "pythread.h"

#ifdef QUICKWIN

#include <io.h>

void
PyOS_InitInterrupts(void)
{
}

void
PyOS_FiniInterrupts(void)
{
}

int
PyOS_InterruptOccurred(void)
{
    _wyield();
}

#define OK

#endif

#if defined(_M_IX86) && !defined(__QNX__)
#include <io.h>
#endif

#if defined(MSDOS) && !defined(QUICKWIN)

#ifdef __GNUC__

#include <go32.h>

void
PyOS_InitInterrupts(void)
{
    _go32_want_ctrl_break(1  );
}

void
PyOS_FiniInterrupts(void)
{
}

int
PyOS_InterruptOccurred(void)
{
    return _go32_was_ctrl_break_hit();
}

#else

void
PyOS_InitInterrupts(void)
{
}

void
PyOS_FiniInterrupts(void)
{
}

int
PyOS_InterruptOccurred(void)
{
    int interrupted = 0;
    while (kbhit()) {
        if (getch() == '\003')
            interrupted = 1;
    }
    return interrupted;
}

#endif

#define OK

#endif

#ifndef OK

#include <stdio.h>
#include <string.h>
#include <signal.h>

static int interrupted;

void
PyErr_SetInterrupt(void)
{
    interrupted = 1;
}

extern int PyErr_CheckSignals(void);

static int
checksignals_witharg(void * arg)
{
    return PyErr_CheckSignals();
}

static void
intcatcher(int sig)
{
    extern void Py_Exit(int);
    static char message[] =
"python: to interrupt a truly hanging Python program, interrupt once more.\n";
    switch (interrupted++) {
    case 0:
        break;
    case 1:
#ifdef RISCOS
        fprintf(stderr, message);
#else
        write(2, message, strlen(message));
#endif
        break;
    case 2:
        interrupted = 0;
        Py_Exit(1);
        break;
    }
    PyOS_setsig(SIGINT, intcatcher);
    Py_AddPendingCall(checksignals_witharg, NULL);
}

static void (*old_siginthandler)(int) = SIG_DFL;

void
PyOS_InitInterrupts(void)
{
    if ((old_siginthandler = PyOS_setsig(SIGINT, SIG_IGN)) != SIG_IGN)
        PyOS_setsig(SIGINT, intcatcher);
}

void
PyOS_FiniInterrupts(void)
{
    PyOS_setsig(SIGINT, old_siginthandler);
}

int
PyOS_InterruptOccurred(void)
{
    if (!interrupted)
        return 0;
    interrupted = 0;
    return 1;
}

#endif

void
PyOS_AfterFork(void)
{
#ifdef WITH_THREAD
    PyEval_ReInitThreads();
    PyThread_ReInitTLS();
#endif
}
