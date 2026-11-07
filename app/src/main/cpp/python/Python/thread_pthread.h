#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(HAVE_PTHREAD_DESTRUCTOR)
#define destructor xxdestructor
#endif
#include <pthread.h>
#if defined(__APPLE__) || defined(HAVE_PTHREAD_DESTRUCTOR)
#undef destructor
#endif
#include <signal.h>

#ifdef _POSIX_THREAD_ATTR_STACKSIZE
#ifndef THREAD_STACK_SIZE
#define THREAD_STACK_SIZE       0
#endif

#if (defined(__APPLE__) || defined(__FreeBSD__)) && defined(THREAD_STACK_SIZE) && THREAD_STACK_SIZE == 0

#undef  THREAD_STACK_SIZE
#define THREAD_STACK_SIZE       0x400000
#endif

#define THREAD_STACK_MIN        0x8000
#else
#ifdef THREAD_STACK_SIZE
#error "THREAD_STACK_SIZE defined but _POSIX_THREAD_ATTR_STACKSIZE undefined"
#endif
#endif

#ifdef _POSIX_SEMAPHORES

#if (_POSIX_SEMAPHORES+0) == -1
#define HAVE_BROKEN_POSIX_SEMAPHORES
#else
#include <semaphore.h>
#include <errno.h>
#endif
#endif

#ifdef __FreeBSD__
#include <osreldate.h>
#if __FreeBSD_version >= 500000 && __FreeBSD_version < 504101
#undef PTHREAD_SYSTEM_SCHED_SUPPORTED
#endif
#endif

#if !defined(pthread_attr_default)
#  define pthread_attr_default ((pthread_attr_t *)NULL)
#endif
#if !defined(pthread_mutexattr_default)
#  define pthread_mutexattr_default ((pthread_mutexattr_t *)NULL)
#endif
#if !defined(pthread_condattr_default)
#  define pthread_condattr_default ((pthread_condattr_t *)NULL)
#endif

#if defined(_POSIX_SEMAPHORES) && !defined(HAVE_BROKEN_POSIX_SEMAPHORES)
#  define USE_SEMAPHORES
#else
#  undef USE_SEMAPHORES
#endif

#if defined(HAVE_PTHREAD_SIGMASK) && !defined(HAVE_BROKEN_PTHREAD_SIGMASK)
#  define SET_THREAD_SIGMASK pthread_sigmask
#else
#  define SET_THREAD_SIGMASK sigprocmask
#endif

typedef struct {
    char             locked;

    pthread_cond_t   lock_released;
    pthread_mutex_t  mut;
} pthread_lock;

#define CHECK_STATUS(name)  if (status != 0) { perror(name); error = 1; }

#ifdef _HAVE_BSDI
static
void _noop(void)
{
}

static void
PyThread__init_thread(void)
{

    static int dummy = 0;
    pthread_t thread1;
    pthread_create(&thread1, NULL, (void *) _noop, &dummy);
    pthread_join(thread1, NULL);
}

#else

static void
PyThread__init_thread(void)
{
#if defined(_AIX) && defined(__GNUC__)
    extern void pthread_init(void);
    pthread_init();
#endif
}

#endif

long
PyThread_start_new_thread(void (*func)(void *), void *arg)
{
    pthread_t th;
    int status;
#if defined(THREAD_STACK_SIZE) || defined(PTHREAD_SYSTEM_SCHED_SUPPORTED)
    pthread_attr_t attrs;
#endif
#if defined(THREAD_STACK_SIZE)
    size_t      tss;
#endif

    dprintf(("PyThread_start_new_thread called\n"));
    if (!initialized)
        PyThread_init_thread();

#if defined(THREAD_STACK_SIZE) || defined(PTHREAD_SYSTEM_SCHED_SUPPORTED)
    if (pthread_attr_init(&attrs) != 0)
        return -1;
#endif
#if defined(THREAD_STACK_SIZE)
    tss = (_pythread_stacksize != 0) ? _pythread_stacksize
                                     : THREAD_STACK_SIZE;
    if (tss != 0) {
        if (pthread_attr_setstacksize(&attrs, tss) != 0) {
            pthread_attr_destroy(&attrs);
            return -1;
        }
    }
#endif
#if defined(PTHREAD_SYSTEM_SCHED_SUPPORTED)
    pthread_attr_setscope(&attrs, PTHREAD_SCOPE_SYSTEM);
#endif

    status = pthread_create(&th,
#if defined(THREAD_STACK_SIZE) || defined(PTHREAD_SYSTEM_SCHED_SUPPORTED)
                             &attrs,
#else
                             (pthread_attr_t*)NULL,
#endif
                             (void* (*)(void *))func,
                             (void *)arg
                             );

#if defined(THREAD_STACK_SIZE) || defined(PTHREAD_SYSTEM_SCHED_SUPPORTED)
    pthread_attr_destroy(&attrs);
#endif
    if (status != 0)
        return -1;

    pthread_detach(th);

#if SIZEOF_PTHREAD_T <= SIZEOF_LONG
    return (long) th;
#else
    return (long) *(long *) &th;
#endif
}

long
PyThread_get_thread_ident(void)
{
    volatile pthread_t threadid;
    if (!initialized)
        PyThread_init_thread();

    threadid = pthread_self();
#if SIZEOF_PTHREAD_T <= SIZEOF_LONG
    return (long) threadid;
#else
    return (long) *(long *) &threadid;
#endif
}

void
PyThread_exit_thread(void)
{
    dprintf(("PyThread_exit_thread called\n"));
    if (!initialized) {
        exit(0);
    }
}

#ifdef USE_SEMAPHORES

PyThread_type_lock
PyThread_allocate_lock(void)
{
    sem_t *lock;
    int status, error = 0;

    dprintf(("PyThread_allocate_lock called\n"));
    if (!initialized)
        PyThread_init_thread();

    lock = (sem_t *)malloc(sizeof(sem_t));

    if (lock) {
        status = sem_init(lock,0,1);
        CHECK_STATUS("sem_init");

        if (error) {
            free((void *)lock);
            lock = NULL;
        }
    }

    dprintf(("PyThread_allocate_lock() -> %p\n", lock));
    return (PyThread_type_lock)lock;
}

void
PyThread_free_lock(PyThread_type_lock lock)
{
    sem_t *thelock = (sem_t *)lock;
    int status, error = 0;

    (void) error;
    dprintf(("PyThread_free_lock(%p) called\n", lock));

    if (!thelock)
        return;

    status = sem_destroy(thelock);
    CHECK_STATUS("sem_destroy");

    free((void *)thelock);
}

static int
fix_status(int status)
{
    return (status == -1) ? errno : status;
}

int
PyThread_acquire_lock(PyThread_type_lock lock, int waitflag)
{
    int success;
    sem_t *thelock = (sem_t *)lock;
    int status, error = 0;

    (void) error;
    dprintf(("PyThread_acquire_lock(%p, %d) called\n", lock, waitflag));

    do {
        if (waitflag)
            status = fix_status(sem_wait(thelock));
        else
            status = fix_status(sem_trywait(thelock));
    } while (status == EINTR);

    if (waitflag) {
        CHECK_STATUS("sem_wait");
    } else if (status != EAGAIN) {
        CHECK_STATUS("sem_trywait");
    }

    success = (status == 0) ? 1 : 0;

    dprintf(("PyThread_acquire_lock(%p, %d) -> %d\n", lock, waitflag, success));
    return success;
}

void
PyThread_release_lock(PyThread_type_lock lock)
{
    sem_t *thelock = (sem_t *)lock;
    int status, error = 0;

    (void) error;
    dprintf(("PyThread_release_lock(%p) called\n", lock));

    status = sem_post(thelock);
    CHECK_STATUS("sem_post");
}

#else

PyThread_type_lock
PyThread_allocate_lock(void)
{
    pthread_lock *lock;
    int status, error = 0;

    dprintf(("PyThread_allocate_lock called\n"));
    if (!initialized)
        PyThread_init_thread();

    lock = (pthread_lock *) malloc(sizeof(pthread_lock));
    if (lock) {
        memset((void *)lock, '\0', sizeof(pthread_lock));
        lock->locked = 0;

        status = pthread_mutex_init(&lock->mut,
                                    pthread_mutexattr_default);
        CHECK_STATUS("pthread_mutex_init");

        status = pthread_cond_init(&lock->lock_released,
                                   pthread_condattr_default);
        CHECK_STATUS("pthread_cond_init");

        if (error) {
            free((void *)lock);
            lock = 0;
        }
    }

    dprintf(("PyThread_allocate_lock() -> %p\n", lock));
    return (PyThread_type_lock) lock;
}

void
PyThread_free_lock(PyThread_type_lock lock)
{
    pthread_lock *thelock = (pthread_lock *)lock;
    int status, error = 0;

    (void) error;
    dprintf(("PyThread_free_lock(%p) called\n", lock));

    status = pthread_mutex_destroy( &thelock->mut );
    CHECK_STATUS("pthread_mutex_destroy");

    status = pthread_cond_destroy( &thelock->lock_released );
    CHECK_STATUS("pthread_cond_destroy");

    free((void *)thelock);
}

int
PyThread_acquire_lock(PyThread_type_lock lock, int waitflag)
{
    int success;
    pthread_lock *thelock = (pthread_lock *)lock;
    int status, error = 0;

    dprintf(("PyThread_acquire_lock(%p, %d) called\n", lock, waitflag));

    status = pthread_mutex_lock( &thelock->mut );
    CHECK_STATUS("pthread_mutex_lock[1]");
    success = thelock->locked == 0;

    if ( !success && waitflag ) {

        while ( thelock->locked ) {
            status = pthread_cond_wait(&thelock->lock_released,
                                       &thelock->mut);
            CHECK_STATUS("pthread_cond_wait");
        }
        success = 1;
    }
    if (success) thelock->locked = 1;
    status = pthread_mutex_unlock( &thelock->mut );
    CHECK_STATUS("pthread_mutex_unlock[1]");

    if (error) success = 0;
    dprintf(("PyThread_acquire_lock(%p, %d) -> %d\n", lock, waitflag, success));
    return success;
}

void
PyThread_release_lock(PyThread_type_lock lock)
{
    pthread_lock *thelock = (pthread_lock *)lock;
    int status, error = 0;

    (void) error;
    dprintf(("PyThread_release_lock(%p) called\n", lock));

    status = pthread_mutex_lock( &thelock->mut );
    CHECK_STATUS("pthread_mutex_lock[3]");

    thelock->locked = 0;

    status = pthread_mutex_unlock( &thelock->mut );
    CHECK_STATUS("pthread_mutex_unlock[3]");

    status = pthread_cond_signal( &thelock->lock_released );
    CHECK_STATUS("pthread_cond_signal");
}

#endif

static int
_pythread_pthread_set_stacksize(size_t size)
{
#if defined(THREAD_STACK_SIZE)
    pthread_attr_t attrs;
    size_t tss_min;
    int rc = 0;
#endif

    if (size == 0) {
        _pythread_stacksize = 0;
        return 0;
    }

#if defined(THREAD_STACK_SIZE)
#if defined(PTHREAD_STACK_MIN)
    tss_min = PTHREAD_STACK_MIN > THREAD_STACK_MIN ? PTHREAD_STACK_MIN
                                                   : THREAD_STACK_MIN;
#else
    tss_min = THREAD_STACK_MIN;
#endif
    if (size >= tss_min) {

        if (pthread_attr_init(&attrs) == 0) {
            rc = pthread_attr_setstacksize(&attrs, size);
            pthread_attr_destroy(&attrs);
            if (rc == 0) {
                _pythread_stacksize = size;
                return 0;
            }
        }
    }
    return -1;
#else
    return -2;
#endif
}

#define THREAD_SET_STACKSIZE(x) _pythread_pthread_set_stacksize(x)
