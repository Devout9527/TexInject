#include "Python.h"

#ifndef _POSIX_THREADS

# ifdef HAVE_PTHREAD_H
#  include <pthread.h>
# endif
#endif

#ifndef DONT_HAVE_STDIO_H
#include <stdio.h>
#endif

#include <stdlib.h>

#ifdef __sgi
#ifndef HAVE_PTHREAD_H
#undef _POSIX_THREADS
#endif
#endif

#include "pythread.h"

#ifndef _POSIX_THREADS

#ifdef __sgi
#define SGI_THREADS
#endif

#ifdef HAVE_THREAD_H
#define SOLARIS_THREADS
#endif

#if defined(sun) && !defined(SOLARIS_THREADS)
#define SUN_LWP
#endif

#ifdef __hpux
#ifdef _SC_THREADS
#define _POSIX_THREADS
#endif
#endif

#endif

#ifdef Py_DEBUG
static int thread_debug = 0;
#define dprintf(args)   (void)((thread_debug & 1) && printf args)
#define d2printf(args)  ((thread_debug & 8) && printf args)
#else
#define dprintf(args)
#define d2printf(args)
#endif

static int initialized;

static void PyThread__init_thread(void);

void
PyThread_init_thread(void)
{
#ifdef Py_DEBUG
    char *p = Py_GETENV("PYTHONTHREADDEBUG");

    if (p) {
        if (*p)
            thread_debug = atoi(p);
        else
            thread_debug = 1;
    }
#endif
    if (initialized)
        return;
    initialized = 1;
    dprintf(("PyThread_init_thread called\n"));
    PyThread__init_thread();
}

static size_t _pythread_stacksize = 0;

#ifdef SGI_THREADS
#include "thread_sgi.h"
#endif

#ifdef SOLARIS_THREADS
#include "thread_solaris.h"
#endif

#ifdef SUN_LWP
#include "thread_lwp.h"
#endif

#ifdef HAVE_PTH
#include "thread_pth.h"
#undef _POSIX_THREADS
#endif

#ifdef _POSIX_THREADS
#include "thread_pthread.h"
#endif

#ifdef C_THREADS
#include "thread_cthread.h"
#endif

#ifdef NT_THREADS
#include "thread_nt.h"
#endif

#ifdef OS2_THREADS
#include "thread_os2.h"
#endif

#ifdef BEOS_THREADS
#include "thread_beos.h"
#endif

#ifdef PLAN9_THREADS
#include "thread_plan9.h"
#endif

#ifdef ATHEOS_THREADS
#include "thread_atheos.h"
#endif

size_t
PyThread_get_stacksize(void)
{
    return _pythread_stacksize;
}

int
PyThread_set_stacksize(size_t size)
{
#if defined(THREAD_SET_STACKSIZE)
    return THREAD_SET_STACKSIZE(size);
#else
    return -2;
#endif
}

#ifndef Py_HAVE_NATIVE_TLS

struct key {

    struct key *next;

    long id;

    int key;
    void *value;
};

static struct key *keyhead = NULL;
static PyThread_type_lock keymutex = NULL;
static int nkeys = 0;

static struct key *
find_key(int key, void *value)
{
    struct key *p, *prev_p;
    long id = PyThread_get_thread_ident();

    if (!keymutex)
        return NULL;
    PyThread_acquire_lock(keymutex, 1);
    prev_p = NULL;
    for (p = keyhead; p != NULL; p = p->next) {
        if (p->id == id && p->key == key)
            goto Done;

        if (p == prev_p)
            Py_FatalError("tls find_key: small circular list(!)");
        prev_p = p;
        if (p->next == keyhead)
            Py_FatalError("tls find_key: circular list(!)");
    }
    if (value == NULL) {
        assert(p == NULL);
        goto Done;
    }
    p = (struct key *)malloc(sizeof(struct key));
    if (p != NULL) {
        p->id = id;
        p->key = key;
        p->value = value;
        p->next = keyhead;
        keyhead = p;
    }
 Done:
    PyThread_release_lock(keymutex);
    return p;
}

int
PyThread_create_key(void)
{

    if (keymutex == NULL)
        keymutex = PyThread_allocate_lock();
    return ++nkeys;
}

void
PyThread_delete_key(int key)
{
    struct key *p, **q;

    PyThread_acquire_lock(keymutex, 1);
    q = &keyhead;
    while ((p = *q) != NULL) {
        if (p->key == key) {
            *q = p->next;
            free((void *)p);

        }
        else
            q = &p->next;
    }
    PyThread_release_lock(keymutex);
}

int
PyThread_set_key_value(int key, void *value)
{
    struct key *p;

    assert(value != NULL);
    p = find_key(key, value);
    if (p == NULL)
        return -1;
    else
        return 0;
}

void *
PyThread_get_key_value(int key)
{
    struct key *p = find_key(key, NULL);

    if (p == NULL)
        return NULL;
    else
        return p->value;
}

void
PyThread_delete_key_value(int key)
{
    long id = PyThread_get_thread_ident();
    struct key *p, **q;

    PyThread_acquire_lock(keymutex, 1);
    q = &keyhead;
    while ((p = *q) != NULL) {
        if (p->key == key && p->id == id) {
            *q = p->next;
            free((void *)p);

            break;
        }
        else
            q = &p->next;
    }
    PyThread_release_lock(keymutex);
}

void
PyThread_ReInitTLS(void)
{
    long id = PyThread_get_thread_ident();
    struct key *p, **q;

    if (!keymutex)
        return;

    keymutex = PyThread_allocate_lock();

    q = &keyhead;
    while ((p = *q) != NULL) {
        if (p->id != id) {
            *q = p->next;
            free((void *)p);

        }
        else
            q = &p->next;
    }
}

#endif
