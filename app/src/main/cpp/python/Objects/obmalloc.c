#include "Python.h"

#if defined(__has_feature)
 #if __has_feature(address_sanitizer)
  #define ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS \
        __attribute__((no_address_safety_analysis)) \
        __attribute__ ((noinline))
 #else
  #define ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS
 #endif
#else
 #if defined(__SANITIZE_ADDRESS__)
  #define ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS \
        __attribute__((no_address_safety_analysis)) \
        __attribute__ ((noinline))
 #else
  #define ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS
 #endif
#endif

#ifdef WITH_PYMALLOC

#ifdef HAVE_MMAP
 #include <sys/mman.h>
 #ifdef MAP_ANONYMOUS
  #define ARENAS_USE_MMAP
 #endif
#endif

#ifdef WITH_VALGRIND
#include <valgrind/valgrind.h>

#if defined(__GNUC__) && (__GNUC__ > 2) && defined(__OPTIMIZE__)
#  define UNLIKELY(value) __builtin_expect((value), 0)
#else
#  define UNLIKELY(value) (value)
#endif

static int running_on_valgrind = -1;
#endif

#define ALIGNMENT               8
#define ALIGNMENT_SHIFT         3
#define ALIGNMENT_MASK          (ALIGNMENT - 1)

#define INDEX2SIZE(I) (((uint)(I) + 1) << ALIGNMENT_SHIFT)

#define SMALL_REQUEST_THRESHOLD 512
#define NB_SMALL_SIZE_CLASSES   (SMALL_REQUEST_THRESHOLD / ALIGNMENT)

#define SYSTEM_PAGE_SIZE        (4 * 1024)
#define SYSTEM_PAGE_SIZE_MASK   (SYSTEM_PAGE_SIZE - 1)

#ifdef WITH_MEMORY_LIMITS
#ifndef SMALL_MEMORY_LIMIT
#define SMALL_MEMORY_LIMIT      (64 * 1024 * 1024)
#endif
#endif

#define ARENA_SIZE              (256 << 10)

#ifdef WITH_MEMORY_LIMITS
#define MAX_ARENAS              (SMALL_MEMORY_LIMIT / ARENA_SIZE)
#endif

#define POOL_SIZE               SYSTEM_PAGE_SIZE
#define POOL_SIZE_MASK          SYSTEM_PAGE_SIZE_MASK

#define SIMPLELOCK_DECL(lock)
#define SIMPLELOCK_INIT(lock)
#define SIMPLELOCK_FINI(lock)
#define SIMPLELOCK_LOCK(lock)
#define SIMPLELOCK_UNLOCK(lock)

#undef  uchar
#define uchar   unsigned char

#undef  uint
#define uint    unsigned int

#undef  ulong
#define ulong   unsigned long

#undef uptr
#define uptr    Py_uintptr_t

typedef uchar block;

struct pool_header {
    union { block *_padding;
            uint count; } ref;
    block *freeblock;
    struct pool_header *nextpool;
    struct pool_header *prevpool;
    uint arenaindex;
    uint szidx;
    uint nextoffset;
    uint maxnextoffset;
};

typedef struct pool_header *poolp;

struct arena_object {

    uptr address;

    block* pool_address;

    uint nfreepools;

    uint ntotalpools;

    struct pool_header* freepools;

    struct arena_object* nextarena;
    struct arena_object* prevarena;
};

#undef  ROUNDUP
#define ROUNDUP(x)              (((x) + ALIGNMENT_MASK) & ~ALIGNMENT_MASK)
#define POOL_OVERHEAD           ROUNDUP(sizeof(struct pool_header))

#define DUMMY_SIZE_IDX          0xffff

#define POOL_ADDR(P) ((poolp)((uptr)(P) & ~(uptr)POOL_SIZE_MASK))

#define NUMBLOCKS(I) ((uint)(POOL_SIZE - POOL_OVERHEAD) / INDEX2SIZE(I))

SIMPLELOCK_DECL(_malloc_lock)
#define LOCK()          SIMPLELOCK_LOCK(_malloc_lock)
#define UNLOCK()        SIMPLELOCK_UNLOCK(_malloc_lock)
#define LOCK_INIT()     SIMPLELOCK_INIT(_malloc_lock)
#define LOCK_FINI()     SIMPLELOCK_FINI(_malloc_lock)

#define PTA(x)  ((poolp )((uchar *)&(usedpools[2*(x)]) - 2*sizeof(block *)))
#define PT(x)   PTA(x), PTA(x)

static poolp usedpools[2 * ((NB_SMALL_SIZE_CLASSES + 7) / 8) * 8] = {
    PT(0), PT(1), PT(2), PT(3), PT(4), PT(5), PT(6), PT(7)
#if NB_SMALL_SIZE_CLASSES > 8
    , PT(8), PT(9), PT(10), PT(11), PT(12), PT(13), PT(14), PT(15)
#if NB_SMALL_SIZE_CLASSES > 16
    , PT(16), PT(17), PT(18), PT(19), PT(20), PT(21), PT(22), PT(23)
#if NB_SMALL_SIZE_CLASSES > 24
    , PT(24), PT(25), PT(26), PT(27), PT(28), PT(29), PT(30), PT(31)
#if NB_SMALL_SIZE_CLASSES > 32
    , PT(32), PT(33), PT(34), PT(35), PT(36), PT(37), PT(38), PT(39)
#if NB_SMALL_SIZE_CLASSES > 40
    , PT(40), PT(41), PT(42), PT(43), PT(44), PT(45), PT(46), PT(47)
#if NB_SMALL_SIZE_CLASSES > 48
    , PT(48), PT(49), PT(50), PT(51), PT(52), PT(53), PT(54), PT(55)
#if NB_SMALL_SIZE_CLASSES > 56
    , PT(56), PT(57), PT(58), PT(59), PT(60), PT(61), PT(62), PT(63)
#if NB_SMALL_SIZE_CLASSES > 64
#error "NB_SMALL_SIZE_CLASSES should be less than 64"
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
};

static struct arena_object* arenas = NULL;

static uint maxarenas = 0;

static struct arena_object* unused_arena_objects = NULL;

static struct arena_object* usable_arenas = NULL;

#define INITIAL_ARENA_OBJECTS 16

static size_t narenas_currently_allocated = 0;

#ifdef PYMALLOC_DEBUG

static size_t ntimes_arena_allocated = 0;

static size_t narenas_highwater = 0;
#endif

static struct arena_object*
new_arena(void)
{
    struct arena_object* arenaobj;
    uint excess;
    void *address;
    int err;

#ifdef PYMALLOC_DEBUG
    if (Py_GETENV("PYTHONMALLOCSTATS"))
        _PyObject_DebugMallocStats();
#endif
    if (unused_arena_objects == NULL) {
        uint i;
        uint numarenas;
        size_t nbytes;

        numarenas = maxarenas ? maxarenas << 1 : INITIAL_ARENA_OBJECTS;
        if (numarenas <= maxarenas)
            return NULL;
#if SIZEOF_SIZE_T <= SIZEOF_INT
        if (numarenas > PY_SIZE_MAX / sizeof(*arenas))
            return NULL;
#endif
        nbytes = numarenas * sizeof(*arenas);
        arenaobj = (struct arena_object *)realloc(arenas, nbytes);
        if (arenaobj == NULL)
            return NULL;
        arenas = arenaobj;

        assert(usable_arenas == NULL);
        assert(unused_arena_objects == NULL);

        for (i = maxarenas; i < numarenas; ++i) {
            arenas[i].address = 0;
            arenas[i].nextarena = i < numarenas - 1 ?
                                   &arenas[i+1] : NULL;
        }

        unused_arena_objects = &arenas[maxarenas];
        maxarenas = numarenas;
    }

    assert(unused_arena_objects != NULL);
    arenaobj = unused_arena_objects;
    unused_arena_objects = arenaobj->nextarena;
    assert(arenaobj->address == 0);
#ifdef ARENAS_USE_MMAP
    address = mmap(NULL, ARENA_SIZE, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    err = (address == MAP_FAILED);
#else
    address = malloc(ARENA_SIZE);
    err = (address == 0);
#endif
    if (err) {

        arenaobj->nextarena = unused_arena_objects;
        unused_arena_objects = arenaobj;
        return NULL;
    }
    arenaobj->address = (uptr)address;

    ++narenas_currently_allocated;
#ifdef PYMALLOC_DEBUG
    ++ntimes_arena_allocated;
    if (narenas_currently_allocated > narenas_highwater)
        narenas_highwater = narenas_currently_allocated;
#endif
    arenaobj->freepools = NULL;

    arenaobj->pool_address = (block*)arenaobj->address;
    arenaobj->nfreepools = ARENA_SIZE / POOL_SIZE;
    assert(POOL_SIZE * arenaobj->nfreepools == ARENA_SIZE);
    excess = (uint)(arenaobj->address & POOL_SIZE_MASK);
    if (excess != 0) {
        --arenaobj->nfreepools;
        arenaobj->pool_address += POOL_SIZE - excess;
    }
    arenaobj->ntotalpools = arenaobj->nfreepools;

    return arenaobj;
}

#define Py_ADDRESS_IN_RANGE(P, POOL)                    \
    ((arenaindex_temp = (POOL)->arenaindex) < maxarenas &&              \
     (uptr)(P) - arenas[arenaindex_temp].address < (uptr)ARENA_SIZE && \
     arenas[arenaindex_temp].address != 0)

#ifdef Py_USING_MEMORY_DEBUGGER

#undef Py_ADDRESS_IN_RANGE

#if defined(__GNUC__) && ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1) || \
                          (__GNUC__ >= 4))
#define Py_NO_INLINE __attribute__((__noinline__))
#else
#define Py_NO_INLINE
#endif

int Py_ADDRESS_IN_RANGE(void *P, poolp pool) Py_NO_INLINE;
#undef Py_NO_INLINE
#endif

#undef PyObject_Malloc
void *
PyObject_Malloc(size_t nbytes)
{
    block *bp;
    poolp pool;
    poolp next;
    uint size;

#ifdef WITH_VALGRIND
    if (UNLIKELY(running_on_valgrind == -1))
        running_on_valgrind = RUNNING_ON_VALGRIND;
    if (UNLIKELY(running_on_valgrind))
        goto redirect;
#endif

    if (nbytes > PY_SSIZE_T_MAX)
        return NULL;

    if ((nbytes - 1) < SMALL_REQUEST_THRESHOLD) {
        LOCK();

        size = (uint)(nbytes - 1) >> ALIGNMENT_SHIFT;
        pool = usedpools[size + size];
        if (pool != pool->nextpool) {

            ++pool->ref.count;
            bp = pool->freeblock;
            assert(bp != NULL);
            if ((pool->freeblock = *(block **)bp) != NULL) {
                UNLOCK();
                return (void *)bp;
            }

            if (pool->nextoffset <= pool->maxnextoffset) {

                pool->freeblock = (block*)pool +
                                  pool->nextoffset;
                pool->nextoffset += INDEX2SIZE(size);
                *(block **)(pool->freeblock) = NULL;
                UNLOCK();
                return (void *)bp;
            }

            next = pool->nextpool;
            pool = pool->prevpool;
            next->prevpool = pool;
            pool->nextpool = next;
            UNLOCK();
            return (void *)bp;
        }

        if (usable_arenas == NULL) {

#ifdef WITH_MEMORY_LIMITS
            if (narenas_currently_allocated >= MAX_ARENAS) {
                UNLOCK();
                goto redirect;
            }
#endif
            usable_arenas = new_arena();
            if (usable_arenas == NULL) {
                UNLOCK();
                goto redirect;
            }
            usable_arenas->nextarena =
                usable_arenas->prevarena = NULL;
        }
        assert(usable_arenas->address != 0);

        pool = usable_arenas->freepools;
        if (pool != NULL) {

            usable_arenas->freepools = pool->nextpool;

            --usable_arenas->nfreepools;
            if (usable_arenas->nfreepools == 0) {

                assert(usable_arenas->freepools == NULL);
                assert(usable_arenas->nextarena == NULL ||
                       usable_arenas->nextarena->prevarena ==
                       usable_arenas);

                usable_arenas = usable_arenas->nextarena;
                if (usable_arenas != NULL) {
                    usable_arenas->prevarena = NULL;
                    assert(usable_arenas->address != 0);
                }
            }
            else {

                assert(usable_arenas->freepools != NULL ||
                       usable_arenas->pool_address <=
                       (block*)usable_arenas->address +
                           ARENA_SIZE - POOL_SIZE);
            }
        init_pool:

            next = usedpools[size + size];
            pool->nextpool = next;
            pool->prevpool = next;
            next->nextpool = pool;
            next->prevpool = pool;
            pool->ref.count = 1;
            if (pool->szidx == size) {

                bp = pool->freeblock;
                pool->freeblock = *(block **)bp;
                UNLOCK();
                return (void *)bp;
            }

            pool->szidx = size;
            size = INDEX2SIZE(size);
            bp = (block *)pool + POOL_OVERHEAD;
            pool->nextoffset = POOL_OVERHEAD + (size << 1);
            pool->maxnextoffset = POOL_SIZE - size;
            pool->freeblock = bp + size;
            *(block **)(pool->freeblock) = NULL;
            UNLOCK();
            return (void *)bp;
        }

        assert(usable_arenas->nfreepools > 0);
        assert(usable_arenas->freepools == NULL);
        pool = (poolp)usable_arenas->pool_address;
        assert((block*)pool <= (block*)usable_arenas->address +
                               ARENA_SIZE - POOL_SIZE);
        pool->arenaindex = usable_arenas - arenas;
        assert(&arenas[pool->arenaindex] == usable_arenas);
        pool->szidx = DUMMY_SIZE_IDX;
        usable_arenas->pool_address += POOL_SIZE;
        --usable_arenas->nfreepools;

        if (usable_arenas->nfreepools == 0) {
            assert(usable_arenas->nextarena == NULL ||
                   usable_arenas->nextarena->prevarena ==
                   usable_arenas);

            usable_arenas = usable_arenas->nextarena;
            if (usable_arenas != NULL) {
                usable_arenas->prevarena = NULL;
                assert(usable_arenas->address != 0);
            }
        }

        goto init_pool;
    }

redirect:

    if (nbytes == 0)
        nbytes = 1;
    return (void *)malloc(nbytes);
}

#undef PyObject_Free
ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS
void
PyObject_Free(void *p)
{
    poolp pool;
    block *lastfree;
    poolp next, prev;
    uint size;
#ifndef Py_USING_MEMORY_DEBUGGER
    uint arenaindex_temp;
#endif

    if (p == NULL)
        return;

#ifdef WITH_VALGRIND
    if (UNLIKELY(running_on_valgrind > 0))
        goto redirect;
#endif

    pool = POOL_ADDR(p);
    if (Py_ADDRESS_IN_RANGE(p, pool)) {

        LOCK();

        assert(pool->ref.count > 0);
        *(block **)p = lastfree = pool->freeblock;
        pool->freeblock = (block *)p;
        if (lastfree) {
            struct arena_object* ao;
            uint nf;

            if (--pool->ref.count != 0) {

                UNLOCK();
                return;
            }

            next = pool->nextpool;
            prev = pool->prevpool;
            next->prevpool = prev;
            prev->nextpool = next;

            ao = &arenas[pool->arenaindex];
            pool->nextpool = ao->freepools;
            ao->freepools = pool;
            nf = ++ao->nfreepools;

            if (nf == ao->ntotalpools) {

                assert(ao->prevarena == NULL ||
                       ao->prevarena->address != 0);
                assert(ao ->nextarena == NULL ||
                       ao->nextarena->address != 0);

                if (ao->prevarena == NULL) {
                    usable_arenas = ao->nextarena;
                    assert(usable_arenas == NULL ||
                           usable_arenas->address != 0);
                }
                else {
                    assert(ao->prevarena->nextarena == ao);
                    ao->prevarena->nextarena =
                        ao->nextarena;
                }

                if (ao->nextarena != NULL) {
                    assert(ao->nextarena->prevarena == ao);
                    ao->nextarena->prevarena =
                        ao->prevarena;
                }

                ao->nextarena = unused_arena_objects;
                unused_arena_objects = ao;

#ifdef ARENAS_USE_MMAP
                munmap((void *)ao->address, ARENA_SIZE);
#else
                free((void *)ao->address);
#endif
                ao->address = 0;
                --narenas_currently_allocated;

                UNLOCK();
                return;
            }
            if (nf == 1) {

                ao->nextarena = usable_arenas;
                ao->prevarena = NULL;
                if (usable_arenas)
                    usable_arenas->prevarena = ao;
                usable_arenas = ao;
                assert(usable_arenas->address != 0);

                UNLOCK();
                return;
            }

            if (ao->nextarena == NULL ||
                         nf <= ao->nextarena->nfreepools) {

                UNLOCK();
                return;
            }

            if (ao->prevarena != NULL) {

                assert(ao->prevarena->nextarena == ao);
                ao->prevarena->nextarena = ao->nextarena;
            }
            else {

                assert(usable_arenas == ao);
                usable_arenas = ao->nextarena;
            }
            ao->nextarena->prevarena = ao->prevarena;

            while (ao->nextarena != NULL &&
                            nf > ao->nextarena->nfreepools) {
                ao->prevarena = ao->nextarena;
                ao->nextarena = ao->nextarena->nextarena;
            }

            assert(ao->nextarena == NULL ||
                ao->prevarena == ao->nextarena->prevarena);
            assert(ao->prevarena->nextarena == ao->nextarena);

            ao->prevarena->nextarena = ao;
            if (ao->nextarena != NULL)
                ao->nextarena->prevarena = ao;

            assert(ao->nextarena == NULL ||
                      nf <= ao->nextarena->nfreepools);
            assert(ao->prevarena == NULL ||
                      nf > ao->prevarena->nfreepools);
            assert(ao->nextarena == NULL ||
                ao->nextarena->prevarena == ao);
            assert((usable_arenas == ao &&
                ao->prevarena == NULL) ||
                ao->prevarena->nextarena == ao);

            UNLOCK();
            return;
        }

        --pool->ref.count;
        assert(pool->ref.count > 0);
        size = pool->szidx;
        next = usedpools[size + size];
        prev = next->prevpool;

        pool->nextpool = next;
        pool->prevpool = prev;
        next->prevpool = pool;
        prev->nextpool = pool;
        UNLOCK();
        return;
    }

#ifdef WITH_VALGRIND
redirect:
#endif

    free(p);
}

#undef PyObject_Realloc
ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS
void *
PyObject_Realloc(void *p, size_t nbytes)
{
    void *bp;
    poolp pool;
    size_t size;
#ifndef Py_USING_MEMORY_DEBUGGER
    uint arenaindex_temp;
#endif

    if (p == NULL)
        return PyObject_Malloc(nbytes);

    if (nbytes > PY_SSIZE_T_MAX)
        return NULL;

#ifdef WITH_VALGRIND

    if (UNLIKELY(running_on_valgrind > 0))
        goto redirect;
#endif

    pool = POOL_ADDR(p);
    if (Py_ADDRESS_IN_RANGE(p, pool)) {

        size = INDEX2SIZE(pool->szidx);
        if (nbytes <= size) {

            if (4 * nbytes > 3 * size) {

                return p;
            }
            size = nbytes;
        }
        bp = PyObject_Malloc(nbytes);
        if (bp != NULL) {
            memcpy(bp, p, size);
            PyObject_Free(p);
        }
        return bp;
    }
#ifdef WITH_VALGRIND
 redirect:
#endif

    if (nbytes)
        return realloc(p, nbytes);

    bp = realloc(p, 1);
    return bp ? bp : p;
}

#else

void *
PyObject_Malloc(size_t n)
{
    return PyMem_MALLOC(n);
}

void *
PyObject_Realloc(void *p, size_t n)
{
    return PyMem_REALLOC(p, n);
}

void
PyObject_Free(void *p)
{
    PyMem_FREE(p);
}
#endif

#ifdef PYMALLOC_DEBUG

#undef CLEANBYTE
#undef DEADBYTE
#undef FORBIDDENBYTE
#define CLEANBYTE      0xCB
#define DEADBYTE       0xDB
#define FORBIDDENBYTE  0xFB

#define _PYMALLOC_MEM_ID 'm'
#define _PYMALLOC_OBJ_ID 'o'

static size_t serialno = 0;

static void
bumpserialno(void)
{
    ++serialno;
}

#define SST SIZEOF_SIZE_T

static size_t
read_size_t(const void *p)
{
    const uchar *q = (const uchar *)p;
    size_t result = *q++;
    int i;

    for (i = SST; --i > 0; ++q)
        result = (result << 8) | *q;
    return result;
}

static void
write_size_t(void *p, size_t n)
{
    uchar *q = (uchar *)p + SST - 1;
    int i;

    for (i = SST; --i >= 0; --q) {
        *q = (uchar)(n & 0xff);
        n >>= 8;
    }
}

#ifdef Py_DEBUG

static int
pool_is_in_list(const poolp target, poolp list)
{
    poolp origlist = list;
    assert(target != NULL);
    if (list == NULL)
        return 0;
    do {
        if (target == list)
            return 1;
        list = list->nextpool;
    } while (list != NULL && list != origlist);
    return 0;
}

#else
#define pool_is_in_list(X, Y) 1

#endif

void *
_PyMem_DebugMalloc(size_t nbytes)
{
    return _PyObject_DebugMallocApi(_PYMALLOC_MEM_ID, nbytes);
}
void *
_PyMem_DebugRealloc(void *p, size_t nbytes)
{
    return _PyObject_DebugReallocApi(_PYMALLOC_MEM_ID, p, nbytes);
}
void
_PyMem_DebugFree(void *p)
{
    _PyObject_DebugFreeApi(_PYMALLOC_MEM_ID, p);
}

void *
_PyObject_DebugMalloc(size_t nbytes)
{
    return _PyObject_DebugMallocApi(_PYMALLOC_OBJ_ID, nbytes);
}
void *
_PyObject_DebugRealloc(void *p, size_t nbytes)
{
    return _PyObject_DebugReallocApi(_PYMALLOC_OBJ_ID, p, nbytes);
}
void
_PyObject_DebugFree(void *p)
{
    _PyObject_DebugFreeApi(_PYMALLOC_OBJ_ID, p);
}
void
_PyObject_DebugCheckAddress(const void *p)
{
    _PyObject_DebugCheckAddressApi(_PYMALLOC_OBJ_ID, p);
}

void *
_PyObject_DebugMallocApi(char id, size_t nbytes)
{
    uchar *p;
    uchar *tail;
    size_t total;

    bumpserialno();
    total = nbytes + 4*SST;
    if (total < nbytes)

        return NULL;

    p = (uchar *)PyObject_Malloc(total);
    if (p == NULL)
        return NULL;

    write_size_t(p, nbytes);
    p[SST] = (uchar)id;
    memset(p + SST + 1 , FORBIDDENBYTE, SST-1);

    if (nbytes > 0)
        memset(p + 2*SST, CLEANBYTE, nbytes);

    tail = p + 2*SST + nbytes;
    memset(tail, FORBIDDENBYTE, SST);
    write_size_t(tail + SST, serialno);

    return p + 2*SST;
}

void
_PyObject_DebugFreeApi(char api, void *p)
{
    uchar *q = (uchar *)p - 2*SST;
    size_t nbytes;

    if (p == NULL)
        return;
    _PyObject_DebugCheckAddressApi(api, p);
    nbytes = read_size_t(q);
    nbytes += 4*SST;
    if (nbytes > 0)
        memset(q, DEADBYTE, nbytes);
    PyObject_Free(q);
}

void *
_PyObject_DebugReallocApi(char api, void *p, size_t nbytes)
{
    uchar *q = (uchar *)p;
    uchar *tail;
    size_t total;
    size_t original_nbytes;
    int i;

    if (p == NULL)
        return _PyObject_DebugMallocApi(api, nbytes);

    _PyObject_DebugCheckAddressApi(api, p);
    bumpserialno();
    original_nbytes = read_size_t(q - 2*SST);
    total = nbytes + 4*SST;
    if (total < nbytes)

        return NULL;

    if (nbytes < original_nbytes) {

        memset(q + nbytes, DEADBYTE, original_nbytes - nbytes + 2*SST);
    }

    q = (uchar *)PyObject_Realloc(q - 2*SST, total);
    if (q == NULL)
        return NULL;

    write_size_t(q, nbytes);
    assert(q[SST] == (uchar)api);
    for (i = 1; i < SST; ++i)
        assert(q[SST + i] == FORBIDDENBYTE);
    q += 2*SST;
    tail = q + nbytes;
    memset(tail, FORBIDDENBYTE, SST);
    write_size_t(tail + SST, serialno);

    if (nbytes > original_nbytes) {

        memset(q + original_nbytes, CLEANBYTE,
               nbytes - original_nbytes);
    }

    return q;
}

 void
_PyObject_DebugCheckAddressApi(char api, const void *p)
{
    const uchar *q = (const uchar *)p;
    char msgbuf[64];
    char *msg;
    size_t nbytes;
    const uchar *tail;
    int i;
    char id;

    if (p == NULL) {
        msg = "didn't expect a NULL pointer";
        goto error;
    }

    id = (char)q[-SST];
    if (id != api) {
        msg = msgbuf;
        snprintf(msg, sizeof(msgbuf), "bad ID: Allocated using API '%c', verified using API '%c'", id, api);
        msgbuf[sizeof(msgbuf)-1] = 0;
        goto error;
    }

    for (i = SST-1; i >= 1; --i) {
        if (*(q-i) != FORBIDDENBYTE) {
            msg = "bad leading pad byte";
            goto error;
        }
    }

    nbytes = read_size_t(q - 2*SST);
    tail = q + nbytes;
    for (i = 0; i < SST; ++i) {
        if (tail[i] != FORBIDDENBYTE) {
            msg = "bad trailing pad byte";
            goto error;
        }
    }

    return;

error:
    _PyObject_DebugDumpAddress(p);
    Py_FatalError(msg);
}

void
_PyObject_DebugDumpAddress(const void *p)
{
    const uchar *q = (const uchar *)p;
    const uchar *tail;
    size_t nbytes, serial;
    int i;
    int ok;
    char id;

    fprintf(stderr, "Debug memory block at address p=%p:", p);
    if (p == NULL) {
        fprintf(stderr, "\n");
        return;
    }
    id = (char)q[-SST];
    fprintf(stderr, " API '%c'\n", id);

    nbytes = read_size_t(q - 2*SST);
    fprintf(stderr, "    %" PY_FORMAT_SIZE_T "u bytes originally "
                    "requested\n", nbytes);

    fprintf(stderr, "    The %d pad bytes at p-%d are ", SST-1, SST-1);
    ok = 1;
    for (i = 1; i <= SST-1; ++i) {
        if (*(q-i) != FORBIDDENBYTE) {
            ok = 0;
            break;
        }
    }
    if (ok)
        fputs("FORBIDDENBYTE, as expected.\n", stderr);
    else {
        fprintf(stderr, "not all FORBIDDENBYTE (0x%02x):\n",
            FORBIDDENBYTE);
        for (i = SST-1; i >= 1; --i) {
            const uchar byte = *(q-i);
            fprintf(stderr, "        at p-%d: 0x%02x", i, byte);
            if (byte != FORBIDDENBYTE)
                fputs(" *** OUCH", stderr);
            fputc('\n', stderr);
        }

        fputs("    Because memory is corrupted at the start, the "
              "count of bytes requested\n"
              "       may be bogus, and checking the trailing pad "
              "bytes may segfault.\n", stderr);
    }

    tail = q + nbytes;
    fprintf(stderr, "    The %d pad bytes at tail=%p are ", SST, tail);
    ok = 1;
    for (i = 0; i < SST; ++i) {
        if (tail[i] != FORBIDDENBYTE) {
            ok = 0;
            break;
        }
    }
    if (ok)
        fputs("FORBIDDENBYTE, as expected.\n", stderr);
    else {
        fprintf(stderr, "not all FORBIDDENBYTE (0x%02x):\n",
                FORBIDDENBYTE);
        for (i = 0; i < SST; ++i) {
            const uchar byte = tail[i];
            fprintf(stderr, "        at tail+%d: 0x%02x",
                    i, byte);
            if (byte != FORBIDDENBYTE)
                fputs(" *** OUCH", stderr);
            fputc('\n', stderr);
        }
    }

    serial = read_size_t(tail + SST);
    fprintf(stderr, "    The block was made by call #%" PY_FORMAT_SIZE_T
                    "u to debug malloc/realloc.\n", serial);

    if (nbytes > 0) {
        i = 0;
        fputs("    Data at p:", stderr);

        while (q < tail && i < 8) {
            fprintf(stderr, " %02x", *q);
            ++i;
            ++q;
        }

        if (q < tail) {
            if (tail - q > 8) {
                fputs(" ...", stderr);
                q = tail - 8;
            }
            while (q < tail) {
                fprintf(stderr, " %02x", *q);
                ++q;
            }
        }
        fputc('\n', stderr);
    }
}

static size_t
printone(const char* msg, size_t value)
{
    int i, k;
    char buf[100];
    size_t origvalue = value;

    fputs(msg, stderr);
    for (i = (int)strlen(msg); i < 35; ++i)
        fputc(' ', stderr);
    fputc('=', stderr);

    i = 22;
    buf[i--] = '\0';
    buf[i--] = '\n';
    k = 3;
    do {
        size_t nextvalue = value / 10;
        unsigned int digit = (unsigned int)(value - nextvalue * 10);
        value = nextvalue;
        buf[i--] = (char)(digit + '0');
        --k;
        if (k == 0 && value && i >= 0) {
            k = 3;
            buf[i--] = ',';
        }
    } while (value && i >= 0);

    while (i >= 0)
        buf[i--] = ' ';
    fputs(buf, stderr);

    return origvalue;
}

void
_PyObject_DebugMallocStats(void)
{
    uint i;
    const uint numclasses = SMALL_REQUEST_THRESHOLD >> ALIGNMENT_SHIFT;

    size_t numpools[SMALL_REQUEST_THRESHOLD >> ALIGNMENT_SHIFT];
    size_t numblocks[SMALL_REQUEST_THRESHOLD >> ALIGNMENT_SHIFT];
    size_t numfreeblocks[SMALL_REQUEST_THRESHOLD >> ALIGNMENT_SHIFT];

    size_t allocated_bytes = 0;

    size_t available_bytes = 0;

    uint numfreepools = 0;

    size_t arena_alignment = 0;

    size_t pool_header_bytes = 0;

    size_t quantization = 0;

    size_t narenas = 0;

    size_t total;
    char buf[128];

    fprintf(stderr, "Small block threshold = %d, in %u size classes.\n",
            SMALL_REQUEST_THRESHOLD, numclasses);

    for (i = 0; i < numclasses; ++i)
        numpools[i] = numblocks[i] = numfreeblocks[i] = 0;

    for (i = 0; i < maxarenas; ++i) {
        uint j;
        uptr base = arenas[i].address;

        if (arenas[i].address == (uptr)NULL)
            continue;
        narenas += 1;

        numfreepools += arenas[i].nfreepools;

        if (base & (uptr)POOL_SIZE_MASK) {
            arena_alignment += POOL_SIZE;
            base &= ~(uptr)POOL_SIZE_MASK;
            base += POOL_SIZE;
        }

        assert(base <= (uptr) arenas[i].pool_address);
        for (j = 0;
                    base < (uptr) arenas[i].pool_address;
                    ++j, base += POOL_SIZE) {
            poolp p = (poolp)base;
            const uint sz = p->szidx;
            uint freeblocks;

            if (p->ref.count == 0) {

                assert(pool_is_in_list(p, arenas[i].freepools));
                continue;
            }
            ++numpools[sz];
            numblocks[sz] += p->ref.count;
            freeblocks = NUMBLOCKS(sz) - p->ref.count;
            numfreeblocks[sz] += freeblocks;
#ifdef Py_DEBUG
            if (freeblocks > 0)
                assert(pool_is_in_list(p, usedpools[sz + sz]));
#endif
        }
    }
    assert(narenas == narenas_currently_allocated);

    fputc('\n', stderr);
    fputs("class   size   num pools   blocks in use  avail blocks\n"
          "-----   ----   ---------   -------------  ------------\n",
          stderr);

    for (i = 0; i < numclasses; ++i) {
        size_t p = numpools[i];
        size_t b = numblocks[i];
        size_t f = numfreeblocks[i];
        uint size = INDEX2SIZE(i);
        if (p == 0) {
            assert(b == 0 && f == 0);
            continue;
        }
        fprintf(stderr, "%5u %6u "
                        "%11" PY_FORMAT_SIZE_T "u "
                        "%15" PY_FORMAT_SIZE_T "u "
                        "%13" PY_FORMAT_SIZE_T "u\n",
                i, size, p, b, f);
        allocated_bytes += b * size;
        available_bytes += f * size;
        pool_header_bytes += p * POOL_OVERHEAD;
        quantization += p * ((POOL_SIZE - POOL_OVERHEAD) % size);
    }
    fputc('\n', stderr);
    (void)printone("# times object malloc called", serialno);

    (void)printone("# arenas allocated total", ntimes_arena_allocated);
    (void)printone("# arenas reclaimed", ntimes_arena_allocated - narenas);
    (void)printone("# arenas highwater mark", narenas_highwater);
    (void)printone("# arenas allocated current", narenas);

    PyOS_snprintf(buf, sizeof(buf),
        "%" PY_FORMAT_SIZE_T "u arenas * %d bytes/arena",
        narenas, ARENA_SIZE);
    (void)printone(buf, narenas * ARENA_SIZE);

    fputc('\n', stderr);

    total = printone("# bytes in allocated blocks", allocated_bytes);
    total += printone("# bytes in available blocks", available_bytes);

    PyOS_snprintf(buf, sizeof(buf),
        "%u unused pools * %d bytes", numfreepools, POOL_SIZE);
    total += printone(buf, (size_t)numfreepools * POOL_SIZE);

    total += printone("# bytes lost to pool headers", pool_header_bytes);
    total += printone("# bytes lost to quantization", quantization);
    total += printone("# bytes lost to arena alignment", arena_alignment);
    (void)printone("Total", total);
}

#endif

#ifdef Py_USING_MEMORY_DEBUGGER

int
Py_ADDRESS_IN_RANGE(void *P, poolp pool)
{
    uint arenaindex_temp = pool->arenaindex;

    return arenaindex_temp < maxarenas &&
           (uptr)P - arenas[arenaindex_temp].address < (uptr)ARENA_SIZE &&
           arenas[arenaindex_temp].address != 0;
}
#endif
