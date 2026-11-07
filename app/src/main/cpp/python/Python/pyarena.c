#include "Python.h"
#include "pyarena.h"

#define DEFAULT_BLOCK_SIZE 8192
#define ALIGNMENT               8
#define ALIGNMENT_MASK          (ALIGNMENT - 1)
#define ROUNDUP(x)              (((x) + ALIGNMENT_MASK) & ~ALIGNMENT_MASK)

typedef struct _block {

    size_t ab_size;

    size_t ab_offset;

    struct _block *ab_next;

    void *ab_mem;
} block;

struct _arena {

    block *a_head;

    block *a_cur;

    PyObject *a_objects;

#if defined(Py_DEBUG)

    size_t total_allocs;
    size_t total_size;
    size_t total_blocks;
    size_t total_block_size;
    size_t total_big_blocks;
#endif
};

static block *
block_new(size_t size)
{

    block *b = (block *)malloc(sizeof(block) + size);
    if (!b)
        return NULL;
    b->ab_size = size;
    b->ab_mem = (void *)(b + 1);
    b->ab_next = NULL;
    b->ab_offset = ROUNDUP((Py_uintptr_t)(b->ab_mem)) -
      (Py_uintptr_t)(b->ab_mem);
    return b;
}

static void
block_free(block *b) {
    while (b) {
        block *next = b->ab_next;
        free(b);
        b = next;
    }
}

static void *
block_alloc(block *b, size_t size)
{
    void *p;
    assert(b);
    size = ROUNDUP(size);
    if (b->ab_offset + size > b->ab_size) {

        block *newbl = block_new(
                        size < DEFAULT_BLOCK_SIZE ?
                        DEFAULT_BLOCK_SIZE : size);
        if (!newbl)
            return NULL;
        assert(!b->ab_next);
        b->ab_next = newbl;
        b = newbl;
    }

    assert(b->ab_offset + size <= b->ab_size);
    p = (void *)(((char *)b->ab_mem) + b->ab_offset);
    b->ab_offset += size;
    return p;
}

PyArena *
PyArena_New()
{
    PyArena* arena = (PyArena *)malloc(sizeof(PyArena));
    if (!arena)
        return (PyArena*)PyErr_NoMemory();

    arena->a_head = block_new(DEFAULT_BLOCK_SIZE);
    arena->a_cur = arena->a_head;
    if (!arena->a_head) {
        free((void *)arena);
        return (PyArena*)PyErr_NoMemory();
    }
    arena->a_objects = PyList_New(0);
    if (!arena->a_objects) {
        block_free(arena->a_head);
        free((void *)arena);
        return (PyArena*)PyErr_NoMemory();
    }
#if defined(Py_DEBUG)
    arena->total_allocs = 0;
    arena->total_size = 0;
    arena->total_blocks = 1;
    arena->total_block_size = DEFAULT_BLOCK_SIZE;
    arena->total_big_blocks = 0;
#endif
    return arena;
}

void
PyArena_Free(PyArena *arena)
{
    assert(arena);
#if defined(Py_DEBUG)

#endif
    block_free(arena->a_head);

    Py_DECREF(arena->a_objects);
    free(arena);
}

void *
PyArena_Malloc(PyArena *arena, size_t size)
{
    void *p = block_alloc(arena->a_cur, size);
    if (!p)
        return PyErr_NoMemory();
#if defined(Py_DEBUG)
    arena->total_allocs++;
    arena->total_size += size;
#endif

    if (arena->a_cur->ab_next) {
        arena->a_cur = arena->a_cur->ab_next;
#if defined(Py_DEBUG)
        arena->total_blocks++;
        arena->total_block_size += arena->a_cur->ab_size;
        if (arena->a_cur->ab_size > DEFAULT_BLOCK_SIZE)
            ++arena->total_big_blocks;
#endif
    }
    return p;
}

int
PyArena_AddPyObject(PyArena *arena, PyObject *obj)
{
    int r = PyList_Append(arena->a_objects, obj);
    if (r >= 0) {
        Py_DECREF(obj);
    }
    return r;
}
