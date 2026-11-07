#include <windows.h>
#include <limits.h>
#ifdef HAVE_PROCESS_H
#include <process.h>
#endif

typedef struct NRMUTEX {
    LONG   owned ;
    DWORD  thread_id ;
    HANDLE hevent ;
} NRMUTEX, *PNRMUTEX ;

BOOL
InitializeNonRecursiveMutex(PNRMUTEX mutex)
{
    mutex->owned = -1 ;
    mutex->thread_id = 0 ;
    mutex->hevent = CreateEvent(NULL, FALSE, FALSE, NULL) ;
    return mutex->hevent != NULL ;
}

VOID
DeleteNonRecursiveMutex(PNRMUTEX mutex)
{

    CloseHandle(mutex->hevent) ;
    mutex->hevent = NULL ;
}

DWORD
EnterNonRecursiveMutex(PNRMUTEX mutex, BOOL wait)
{

    DWORD ret ;

    if (!wait)
    {
        if (InterlockedCompareExchange(&mutex->owned, 0, -1) != -1)
            return WAIT_TIMEOUT ;
        ret = WAIT_OBJECT_0 ;
    }
    else
        ret = InterlockedIncrement(&mutex->owned) ?

            WaitForSingleObject(mutex->hevent, INFINITE) : WAIT_OBJECT_0 ;

    mutex->thread_id = GetCurrentThreadId() ;
    return ret ;
}

BOOL
LeaveNonRecursiveMutex(PNRMUTEX mutex)
{

    mutex->thread_id = 0 ;
    return
        InterlockedDecrement(&mutex->owned) < 0 ||
        SetEvent(mutex->hevent) ;
}

PNRMUTEX
AllocNonRecursiveMutex(void)
{
    PNRMUTEX mutex = (PNRMUTEX)malloc(sizeof(NRMUTEX)) ;
    if (mutex && !InitializeNonRecursiveMutex(mutex))
    {
        free(mutex) ;
        mutex = NULL ;
    }
    return mutex ;
}

void
FreeNonRecursiveMutex(PNRMUTEX mutex)
{
    if (mutex)
    {
        DeleteNonRecursiveMutex(mutex) ;
        free(mutex) ;
    }
}

long PyThread_get_thread_ident(void);

static void
PyThread__init_thread(void)
{
}

typedef struct {
    void (*func)(void*);
    void *arg;
} callobj;

#if defined(MS_WINCE)
static DWORD WINAPI
#else
static unsigned __stdcall
#endif
bootstrap(void *call)
{
    callobj *obj = (callobj*)call;
    void (*func)(void*) = obj->func;
    void *arg = obj->arg;
    HeapFree(GetProcessHeap(), 0, obj);
    func(arg);
    return 0;
}

long
PyThread_start_new_thread(void (*func)(void *), void *arg)
{
    HANDLE hThread;
    unsigned threadID;
    callobj *obj;

    dprintf(("%ld: PyThread_start_new_thread called\n",
             PyThread_get_thread_ident()));
    if (!initialized)
        PyThread_init_thread();

    obj = (callobj*)HeapAlloc(GetProcessHeap(), 0, sizeof(*obj));
    if (!obj)
        return -1;
    obj->func = func;
    obj->arg = arg;
#if defined(MS_WINCE)
    hThread = CreateThread(NULL,
                           Py_SAFE_DOWNCAST(_pythread_stacksize, Py_ssize_t, SIZE_T),
                           bootstrap, obj, 0, &threadID);
#else
    hThread = (HANDLE)_beginthreadex(0,
                      Py_SAFE_DOWNCAST(_pythread_stacksize,
                                       Py_ssize_t, unsigned int),
                      bootstrap, obj,
                      0, &threadID);
#endif
    if (hThread == 0) {
#if defined(MS_WINCE)

        unsigned e = GetLastError();
        dprintf(("%ld: PyThread_start_new_thread failed, win32 error code %u\n",
                 PyThread_get_thread_ident(), e));
#else

        int e = errno;
        dprintf(("%ld: PyThread_start_new_thread failed, errno %d\n",
                 PyThread_get_thread_ident(), e));
#endif
        threadID = (unsigned)-1;
        HeapFree(GetProcessHeap(), 0, obj);
    }
    else {
        dprintf(("%ld: PyThread_start_new_thread succeeded: %p\n",
                 PyThread_get_thread_ident(), (void*)hThread));
        CloseHandle(hThread);
    }
    return (long) threadID;
}

long
PyThread_get_thread_ident(void)
{
    if (!initialized)
        PyThread_init_thread();

    return GetCurrentThreadId();
}

void
PyThread_exit_thread(void)
{
    dprintf(("%ld: PyThread_exit_thread called\n", PyThread_get_thread_ident()));
    if (!initialized)
        exit(0);
#if defined(MS_WINCE)
    ExitThread(0);
#else
    _endthreadex(0);
#endif
}

PyThread_type_lock
PyThread_allocate_lock(void)
{
    PNRMUTEX aLock;

    dprintf(("PyThread_allocate_lock called\n"));
    if (!initialized)
        PyThread_init_thread();

    aLock = AllocNonRecursiveMutex() ;

    dprintf(("%ld: PyThread_allocate_lock() -> %p\n", PyThread_get_thread_ident(), aLock));

    return (PyThread_type_lock) aLock;
}

void
PyThread_free_lock(PyThread_type_lock aLock)
{
    dprintf(("%ld: PyThread_free_lock(%p) called\n", PyThread_get_thread_ident(),aLock));

    FreeNonRecursiveMutex(aLock) ;
}

int
PyThread_acquire_lock(PyThread_type_lock aLock, int waitflag)
{
    int success ;

    dprintf(("%ld: PyThread_acquire_lock(%p, %d) called\n", PyThread_get_thread_ident(),aLock, waitflag));

    success = aLock && EnterNonRecursiveMutex((PNRMUTEX) aLock, (waitflag ? INFINITE : 0)) == WAIT_OBJECT_0 ;

    dprintf(("%ld: PyThread_acquire_lock(%p, %d) -> %d\n", PyThread_get_thread_ident(),aLock, waitflag, success));

    return success;
}

void
PyThread_release_lock(PyThread_type_lock aLock)
{
    dprintf(("%ld: PyThread_release_lock(%p) called\n", PyThread_get_thread_ident(),aLock));

    if (!(aLock && LeaveNonRecursiveMutex((PNRMUTEX) aLock)))
        dprintf(("%ld: Could not PyThread_release_lock(%p) error: %ld\n", PyThread_get_thread_ident(), aLock, GetLastError()));
}

#define THREAD_MIN_STACKSIZE    0x8000
#define THREAD_MAX_STACKSIZE    0x10000000

static int
_pythread_nt_set_stacksize(size_t size)
{

    if (size == 0) {
        _pythread_stacksize = 0;
        return 0;
    }

    if (size >= THREAD_MIN_STACKSIZE && size < THREAD_MAX_STACKSIZE) {
        _pythread_stacksize = size;
        return 0;
    }

    return -1;
}

#define THREAD_SET_STACKSIZE(x) _pythread_nt_set_stacksize(x)

#define Py_HAVE_NATIVE_TLS

#ifdef Py_HAVE_NATIVE_TLS
int
PyThread_create_key(void)
{
    return (int) TlsAlloc();
}

void
PyThread_delete_key(int key)
{
    TlsFree(key);
}

int
PyThread_set_key_value(int key, void *value)
{
    BOOL ok;
    void *oldvalue;

    assert(value != NULL);
    oldvalue = TlsGetValue(key);
    if (oldvalue != NULL)

        return 0;
    ok = TlsSetValue(key, value);
    if (!ok)
        return -1;
    return 0;
}

void *
PyThread_get_key_value(int key)
{

    DWORD error = GetLastError();
    void *result = TlsGetValue(key);
    SetLastError(error);
    return result;
}

void
PyThread_delete_key_value(int key)
{

    TlsSetValue(key, NULL);
}

void
PyThread_ReInitTLS(void)
{}

#endif
