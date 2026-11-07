#include "Python.h"

#ifdef STDC_HEADERS
#include <stddef.h>
#else
#include <sys/types.h>
#endif

static int
list_resize(PyListObject *self, Py_ssize_t newsize)
{
    PyObject **items;
    size_t new_allocated;
    Py_ssize_t allocated = self->allocated;

    if (allocated >= newsize && newsize >= (allocated >> 1)) {
        assert(self->ob_item != NULL || newsize == 0);
        Py_SIZE(self) = newsize;
        return 0;
    }

    new_allocated = (newsize >> 3) + (newsize < 9 ? 3 : 6);

    if (new_allocated > PY_SIZE_MAX - newsize) {
        PyErr_NoMemory();
        return -1;
    } else {
        new_allocated += newsize;
    }

    if (newsize == 0)
        new_allocated = 0;
    items = self->ob_item;
    if (new_allocated <= (PY_SIZE_MAX / sizeof(PyObject *)))
        PyMem_RESIZE(items, PyObject *, new_allocated);
    else
        items = NULL;
    if (items == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    self->ob_item = items;
    Py_SIZE(self) = newsize;
    self->allocated = new_allocated;
    return 0;
}

#undef SHOW_ALLOC_COUNT
#ifdef SHOW_ALLOC_COUNT
static size_t count_alloc = 0;
static size_t count_reuse = 0;

static void
show_alloc(void)
{
    fprintf(stderr, "List allocations: %" PY_FORMAT_SIZE_T "d\n",
        count_alloc);
    fprintf(stderr, "List reuse through freelist: %" PY_FORMAT_SIZE_T
        "d\n", count_reuse);
    fprintf(stderr, "%.2f%% reuse rate\n\n",
        (100.0*count_reuse/(count_alloc+count_reuse)));
}
#endif

#ifndef PyList_MAXFREELIST
#define PyList_MAXFREELIST 80
#endif
static PyListObject *free_list[PyList_MAXFREELIST];
static int numfree = 0;

void
PyList_Fini(void)
{
    PyListObject *op;

    while (numfree) {
        op = free_list[--numfree];
        assert(PyList_CheckExact(op));
        PyObject_GC_Del(op);
    }
}

PyObject *
PyList_New(Py_ssize_t size)
{
    PyListObject *op;
    size_t nbytes;
#ifdef SHOW_ALLOC_COUNT
    static int initialized = 0;
    if (!initialized) {
        Py_AtExit(show_alloc);
        initialized = 1;
    }
#endif

    if (size < 0) {
        PyErr_BadInternalCall();
        return NULL;
    }

    if ((size_t)size > PY_SIZE_MAX / sizeof(PyObject *))
        return PyErr_NoMemory();
    nbytes = size * sizeof(PyObject *);
    if (numfree) {
        numfree--;
        op = free_list[numfree];
        _Py_NewReference((PyObject *)op);
#ifdef SHOW_ALLOC_COUNT
        count_reuse++;
#endif
    } else {
        op = PyObject_GC_New(PyListObject, &PyList_Type);
        if (op == NULL)
            return NULL;
#ifdef SHOW_ALLOC_COUNT
        count_alloc++;
#endif
    }
    if (size <= 0)
        op->ob_item = NULL;
    else {
        op->ob_item = (PyObject **) PyMem_MALLOC(nbytes);
        if (op->ob_item == NULL) {
            Py_DECREF(op);
            return PyErr_NoMemory();
        }
        memset(op->ob_item, 0, nbytes);
    }
    Py_SIZE(op) = size;
    op->allocated = size;
    _PyObject_GC_TRACK(op);
    return (PyObject *) op;
}

Py_ssize_t
PyList_Size(PyObject *op)
{
    if (!PyList_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    else
        return Py_SIZE(op);
}

static PyObject *indexerr = NULL;

PyObject *
PyList_GetItem(PyObject *op, Py_ssize_t i)
{
    if (!PyList_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (i < 0 || i >= Py_SIZE(op)) {
        if (indexerr == NULL) {
            indexerr = PyString_FromString(
                "list index out of range");
            if (indexerr == NULL)
                return NULL;
        }
        PyErr_SetObject(PyExc_IndexError, indexerr);
        return NULL;
    }
    return ((PyListObject *)op) -> ob_item[i];
}

int
PyList_SetItem(register PyObject *op, register Py_ssize_t i,
               register PyObject *newitem)
{
    register PyObject *olditem;
    register PyObject **p;
    if (!PyList_Check(op)) {
        Py_XDECREF(newitem);
        PyErr_BadInternalCall();
        return -1;
    }
    if (i < 0 || i >= Py_SIZE(op)) {
        Py_XDECREF(newitem);
        PyErr_SetString(PyExc_IndexError,
                        "list assignment index out of range");
        return -1;
    }
    p = ((PyListObject *)op) -> ob_item + i;
    olditem = *p;
    *p = newitem;
    Py_XDECREF(olditem);
    return 0;
}

static int
ins1(PyListObject *self, Py_ssize_t where, PyObject *v)
{
    Py_ssize_t i, n = Py_SIZE(self);
    PyObject **items;
    if (v == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (n == PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError,
            "cannot add more objects to list");
        return -1;
    }

    if (list_resize(self, n+1) == -1)
        return -1;

    if (where < 0) {
        where += n;
        if (where < 0)
            where = 0;
    }
    if (where > n)
        where = n;
    items = self->ob_item;
    for (i = n; --i >= where; )
        items[i+1] = items[i];
    Py_INCREF(v);
    items[where] = v;
    return 0;
}

int
PyList_Insert(PyObject *op, Py_ssize_t where, PyObject *newitem)
{
    if (!PyList_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    return ins1((PyListObject *)op, where, newitem);
}

static int
app1(PyListObject *self, PyObject *v)
{
    Py_ssize_t n = PyList_GET_SIZE(self);

    assert (v != NULL);
    if (n == PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError,
            "cannot add more objects to list");
        return -1;
    }

    if (list_resize(self, n+1) == -1)
        return -1;

    Py_INCREF(v);
    PyList_SET_ITEM(self, n, v);
    return 0;
}

int
PyList_Append(PyObject *op, PyObject *newitem)
{
    if (PyList_Check(op) && (newitem != NULL))
        return app1((PyListObject *)op, newitem);
    PyErr_BadInternalCall();
    return -1;
}

static void
list_dealloc(PyListObject *op)
{
    Py_ssize_t i;
    PyObject_GC_UnTrack(op);
    Py_TRASHCAN_SAFE_BEGIN(op)
    if (op->ob_item != NULL) {

        i = Py_SIZE(op);
        while (--i >= 0) {
            Py_XDECREF(op->ob_item[i]);
        }
        PyMem_FREE(op->ob_item);
    }
    if (numfree < PyList_MAXFREELIST && PyList_CheckExact(op))
        free_list[numfree++] = op;
    else
        Py_TYPE(op)->tp_free((PyObject *)op);
    Py_TRASHCAN_SAFE_END(op)
}

static int
list_print(PyListObject *op, FILE *fp, int flags)
{
    int rc;
    Py_ssize_t i;
    PyObject *item;

    rc = Py_ReprEnter((PyObject*)op);
    if (rc != 0) {
        if (rc < 0)
            return rc;
        Py_BEGIN_ALLOW_THREADS
        fprintf(fp, "[...]");
        Py_END_ALLOW_THREADS
        return 0;
    }
    Py_BEGIN_ALLOW_THREADS
    fprintf(fp, "[");
    Py_END_ALLOW_THREADS
    for (i = 0; i < Py_SIZE(op); i++) {
        item = op->ob_item[i];
        Py_INCREF(item);
        if (i > 0) {
            Py_BEGIN_ALLOW_THREADS
            fprintf(fp, ", ");
            Py_END_ALLOW_THREADS
        }
        if (PyObject_Print(item, fp, 0) != 0) {
            Py_DECREF(item);
            Py_ReprLeave((PyObject *)op);
            return -1;
        }
        Py_DECREF(item);
    }
    Py_BEGIN_ALLOW_THREADS
    fprintf(fp, "]");
    Py_END_ALLOW_THREADS
    Py_ReprLeave((PyObject *)op);
    return 0;
}

static PyObject *
list_repr(PyListObject *v)
{
    Py_ssize_t i;
    PyObject *s, *temp;
    PyObject *pieces = NULL, *result = NULL;

    i = Py_ReprEnter((PyObject*)v);
    if (i != 0) {
        return i > 0 ? PyString_FromString("[...]") : NULL;
    }

    if (Py_SIZE(v) == 0) {
        result = PyString_FromString("[]");
        goto Done;
    }

    pieces = PyList_New(0);
    if (pieces == NULL)
        goto Done;

    for (i = 0; i < Py_SIZE(v); ++i) {
        int status;
        if (Py_EnterRecursiveCall(" while getting the repr of a list"))
            goto Done;
        s = PyObject_Repr(v->ob_item[i]);
        Py_LeaveRecursiveCall();
        if (s == NULL)
            goto Done;
        status = PyList_Append(pieces, s);
        Py_DECREF(s);
        if (status < 0)
            goto Done;
    }

    assert(PyList_GET_SIZE(pieces) > 0);
    s = PyString_FromString("[");
    if (s == NULL)
        goto Done;
    temp = PyList_GET_ITEM(pieces, 0);
    PyString_ConcatAndDel(&s, temp);
    PyList_SET_ITEM(pieces, 0, s);
    if (s == NULL)
        goto Done;

    s = PyString_FromString("]");
    if (s == NULL)
        goto Done;
    temp = PyList_GET_ITEM(pieces, PyList_GET_SIZE(pieces) - 1);
    PyString_ConcatAndDel(&temp, s);
    PyList_SET_ITEM(pieces, PyList_GET_SIZE(pieces) - 1, temp);
    if (temp == NULL)
        goto Done;

    s = PyString_FromString(", ");
    if (s == NULL)
        goto Done;
    result = _PyString_Join(s, pieces);
    Py_DECREF(s);

Done:
    Py_XDECREF(pieces);
    Py_ReprLeave((PyObject *)v);
    return result;
}

static Py_ssize_t
list_length(PyListObject *a)
{
    return Py_SIZE(a);
}

static int
list_contains(PyListObject *a, PyObject *el)
{
    Py_ssize_t i;
    int cmp;

    for (i = 0, cmp = 0 ; cmp == 0 && i < Py_SIZE(a); ++i)
        cmp = PyObject_RichCompareBool(el, PyList_GET_ITEM(a, i),
                                           Py_EQ);
    return cmp;
}

static PyObject *
list_item(PyListObject *a, Py_ssize_t i)
{
    if (i < 0 || i >= Py_SIZE(a)) {
        if (indexerr == NULL) {
            indexerr = PyString_FromString(
                "list index out of range");
            if (indexerr == NULL)
                return NULL;
        }
        PyErr_SetObject(PyExc_IndexError, indexerr);
        return NULL;
    }
    Py_INCREF(a->ob_item[i]);
    return a->ob_item[i];
}

static PyObject *
list_slice(PyListObject *a, Py_ssize_t ilow, Py_ssize_t ihigh)
{
    PyListObject *np;
    PyObject **src, **dest;
    Py_ssize_t i, len;
    if (ilow < 0)
        ilow = 0;
    else if (ilow > Py_SIZE(a))
        ilow = Py_SIZE(a);
    if (ihigh < ilow)
        ihigh = ilow;
    else if (ihigh > Py_SIZE(a))
        ihigh = Py_SIZE(a);
    len = ihigh - ilow;
    np = (PyListObject *) PyList_New(len);
    if (np == NULL)
        return NULL;

    src = a->ob_item + ilow;
    dest = np->ob_item;
    for (i = 0; i < len; i++) {
        PyObject *v = src[i];
        Py_INCREF(v);
        dest[i] = v;
    }
    return (PyObject *)np;
}

PyObject *
PyList_GetSlice(PyObject *a, Py_ssize_t ilow, Py_ssize_t ihigh)
{
    if (!PyList_Check(a)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return list_slice((PyListObject *)a, ilow, ihigh);
}

static PyObject *
list_concat(PyListObject *a, PyObject *bb)
{
    Py_ssize_t size;
    Py_ssize_t i;
    PyObject **src, **dest;
    PyListObject *np;
    if (!PyList_Check(bb)) {
        PyErr_Format(PyExc_TypeError,
                  "can only concatenate list (not \"%.200s\") to list",
                  bb->ob_type->tp_name);
        return NULL;
    }
#define b ((PyListObject *)bb)
    size = Py_SIZE(a) + Py_SIZE(b);
    if (size < 0)
        return PyErr_NoMemory();
    np = (PyListObject *) PyList_New(size);
    if (np == NULL) {
        return NULL;
    }
    src = a->ob_item;
    dest = np->ob_item;
    for (i = 0; i < Py_SIZE(a); i++) {
        PyObject *v = src[i];
        Py_INCREF(v);
        dest[i] = v;
    }
    src = b->ob_item;
    dest = np->ob_item + Py_SIZE(a);
    for (i = 0; i < Py_SIZE(b); i++) {
        PyObject *v = src[i];
        Py_INCREF(v);
        dest[i] = v;
    }
    return (PyObject *)np;
#undef b
}

static PyObject *
list_repeat(PyListObject *a, Py_ssize_t n)
{
    Py_ssize_t i, j;
    Py_ssize_t size;
    PyListObject *np;
    PyObject **p, **items;
    PyObject *elem;
    if (n < 0)
        n = 0;
    if (n > 0 && Py_SIZE(a) > PY_SSIZE_T_MAX / n)
        return PyErr_NoMemory();
    size = Py_SIZE(a) * n;
    if (size == 0)
        return PyList_New(0);
    np = (PyListObject *) PyList_New(size);
    if (np == NULL)
        return NULL;

    items = np->ob_item;
    if (Py_SIZE(a) == 1) {
        elem = a->ob_item[0];
        for (i = 0; i < n; i++) {
            items[i] = elem;
            Py_INCREF(elem);
        }
        return (PyObject *) np;
    }
    p = np->ob_item;
    items = a->ob_item;
    for (i = 0; i < n; i++) {
        for (j = 0; j < Py_SIZE(a); j++) {
            *p = items[j];
            Py_INCREF(*p);
            p++;
        }
    }
    return (PyObject *) np;
}

static int
list_clear(PyListObject *a)
{
    Py_ssize_t i;
    PyObject **item = a->ob_item;
    if (item != NULL) {

        i = Py_SIZE(a);
        Py_SIZE(a) = 0;
        a->ob_item = NULL;
        a->allocated = 0;
        while (--i >= 0) {
            Py_XDECREF(item[i]);
        }
        PyMem_FREE(item);
    }

    return 0;
}

static int
list_ass_slice(PyListObject *a, Py_ssize_t ilow, Py_ssize_t ihigh, PyObject *v)
{

    PyObject *recycle_on_stack[8];
    PyObject **recycle = recycle_on_stack;
    PyObject **item;
    PyObject **vitem = NULL;
    PyObject *v_as_SF = NULL;
    Py_ssize_t n;
    Py_ssize_t norig;
    Py_ssize_t d;
    Py_ssize_t k;
    size_t s;
    int result = -1;
#define b ((PyListObject *)v)
    if (v == NULL)
        n = 0;
    else {
        if (a == b) {

            v = list_slice(b, 0, Py_SIZE(b));
            if (v == NULL)
                return result;
            result = list_ass_slice(a, ilow, ihigh, v);
            Py_DECREF(v);
            return result;
        }
        v_as_SF = PySequence_Fast(v, "can only assign an iterable");
        if(v_as_SF == NULL)
            goto Error;
        n = PySequence_Fast_GET_SIZE(v_as_SF);
        vitem = PySequence_Fast_ITEMS(v_as_SF);
    }
    if (ilow < 0)
        ilow = 0;
    else if (ilow > Py_SIZE(a))
        ilow = Py_SIZE(a);

    if (ihigh < ilow)
        ihigh = ilow;
    else if (ihigh > Py_SIZE(a))
        ihigh = Py_SIZE(a);

    norig = ihigh - ilow;
    assert(norig >= 0);
    d = n - norig;
    if (Py_SIZE(a) + d == 0) {
        Py_XDECREF(v_as_SF);
        return list_clear(a);
    }
    item = a->ob_item;

    s = norig * sizeof(PyObject *);

    if (s) {
        if (s > sizeof(recycle_on_stack)) {
            recycle = (PyObject **)PyMem_MALLOC(s);
            if (recycle == NULL) {
                PyErr_NoMemory();
                goto Error;
            }
        }
        memcpy(recycle, &item[ilow], s);
    }

    if (d < 0) {
        memmove(&item[ihigh+d], &item[ihigh],
            (Py_SIZE(a) - ihigh)*sizeof(PyObject *));
        list_resize(a, Py_SIZE(a) + d);
        item = a->ob_item;
    }
    else if (d > 0) {
        k = Py_SIZE(a);
        if (list_resize(a, k+d) < 0)
            goto Error;
        item = a->ob_item;
        memmove(&item[ihigh+d], &item[ihigh],
            (k - ihigh)*sizeof(PyObject *));
    }
    for (k = 0; k < n; k++, ilow++) {
        PyObject *w = vitem[k];
        Py_XINCREF(w);
        item[ilow] = w;
    }
    for (k = norig - 1; k >= 0; --k)
        Py_XDECREF(recycle[k]);
    result = 0;
 Error:
    if (recycle != recycle_on_stack)
        PyMem_FREE(recycle);
    Py_XDECREF(v_as_SF);
    return result;
#undef b
}

int
PyList_SetSlice(PyObject *a, Py_ssize_t ilow, Py_ssize_t ihigh, PyObject *v)
{
    if (!PyList_Check(a)) {
        PyErr_BadInternalCall();
        return -1;
    }
    return list_ass_slice((PyListObject *)a, ilow, ihigh, v);
}

static PyObject *
list_inplace_repeat(PyListObject *self, Py_ssize_t n)
{
    PyObject **items;
    Py_ssize_t size, i, j, p;

    size = PyList_GET_SIZE(self);
    if (size == 0 || n == 1) {
        Py_INCREF(self);
        return (PyObject *)self;
    }

    if (n < 1) {
        (void)list_clear(self);
        Py_INCREF(self);
        return (PyObject *)self;
    }

    if (size > PY_SSIZE_T_MAX / n) {
        return PyErr_NoMemory();
    }

    if (list_resize(self, size*n) == -1)
        return NULL;

    p = size;
    items = self->ob_item;
    for (i = 1; i < n; i++) {
        for (j = 0; j < size; j++) {
            PyObject *o = items[j];
            Py_INCREF(o);
            items[p++] = o;
        }
    }
    Py_INCREF(self);
    return (PyObject *)self;
}

static int
list_ass_item(PyListObject *a, Py_ssize_t i, PyObject *v)
{
    PyObject *old_value;
    if (i < 0 || i >= Py_SIZE(a)) {
        PyErr_SetString(PyExc_IndexError,
                        "list assignment index out of range");
        return -1;
    }
    if (v == NULL)
        return list_ass_slice(a, i, i+1, v);
    Py_INCREF(v);
    old_value = a->ob_item[i];
    a->ob_item[i] = v;
    Py_DECREF(old_value);
    return 0;
}

static PyObject *
listinsert(PyListObject *self, PyObject *args)
{
    Py_ssize_t i;
    PyObject *v;
    if (!PyArg_ParseTuple(args, "nO:insert", &i, &v))
        return NULL;
    if (ins1(self, i, v) == 0)
        Py_RETURN_NONE;
    return NULL;
}

static PyObject *
listappend(PyListObject *self, PyObject *v)
{
    if (app1(self, v) == 0)
        Py_RETURN_NONE;
    return NULL;
}

static PyObject *
listextend(PyListObject *self, PyObject *b)
{
    PyObject *it;
    Py_ssize_t m;
    Py_ssize_t n;
    Py_ssize_t mn;
    Py_ssize_t i;
    PyObject *(*iternext)(PyObject *);

    if (PyList_CheckExact(b) || PyTuple_CheckExact(b) || (PyObject *)self == b) {
        PyObject **src, **dest;
        b = PySequence_Fast(b, "argument must be iterable");
        if (!b)
            return NULL;
        n = PySequence_Fast_GET_SIZE(b);
        if (n == 0) {

            Py_DECREF(b);
            Py_RETURN_NONE;
        }
        m = Py_SIZE(self);
        if (list_resize(self, m + n) == -1) {
            Py_DECREF(b);
            return NULL;
        }

        src = PySequence_Fast_ITEMS(b);
        dest = self->ob_item + m;
        for (i = 0; i < n; i++) {
            PyObject *o = src[i];
            Py_INCREF(o);
            dest[i] = o;
        }
        Py_DECREF(b);
        Py_RETURN_NONE;
    }

    it = PyObject_GetIter(b);
    if (it == NULL)
        return NULL;
    iternext = *it->ob_type->tp_iternext;

    n = _PyObject_LengthHint(b, 8);
    if (n == -1) {
        Py_DECREF(it);
        return NULL;
    }
    m = Py_SIZE(self);
    mn = m + n;
    if (mn >= m) {

        if (list_resize(self, mn) == -1)
            goto error;

        Py_SIZE(self) = m;
    }

    for (;;) {
        PyObject *item = iternext(it);
        if (item == NULL) {
            if (PyErr_Occurred()) {
                if (PyErr_ExceptionMatches(PyExc_StopIteration))
                    PyErr_Clear();
                else
                    goto error;
            }
            break;
        }
        if (Py_SIZE(self) < self->allocated) {

            PyList_SET_ITEM(self, Py_SIZE(self), item);
            ++Py_SIZE(self);
        }
        else {
            int status = app1(self, item);
            Py_DECREF(item);
            if (status < 0)
                goto error;
        }
    }

    if (Py_SIZE(self) < self->allocated)
        list_resize(self, Py_SIZE(self));

    Py_DECREF(it);
    Py_RETURN_NONE;

  error:
    Py_DECREF(it);
    return NULL;
}

PyObject *
_PyList_Extend(PyListObject *self, PyObject *b)
{
    return listextend(self, b);
}

static PyObject *
list_inplace_concat(PyListObject *self, PyObject *other)
{
    PyObject *result;

    result = listextend(self, other);
    if (result == NULL)
        return result;
    Py_DECREF(result);
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
listpop(PyListObject *self, PyObject *args)
{
    Py_ssize_t i = -1;
    PyObject *v;
    int status;

    if (!PyArg_ParseTuple(args, "|n:pop", &i))
        return NULL;

    if (Py_SIZE(self) == 0) {

        PyErr_SetString(PyExc_IndexError, "pop from empty list");
        return NULL;
    }
    if (i < 0)
        i += Py_SIZE(self);
    if (i < 0 || i >= Py_SIZE(self)) {
        PyErr_SetString(PyExc_IndexError, "pop index out of range");
        return NULL;
    }
    v = self->ob_item[i];
    if (i == Py_SIZE(self) - 1) {
        status = list_resize(self, Py_SIZE(self) - 1);
        assert(status >= 0);
        return v;
    }
    Py_INCREF(v);
    status = list_ass_slice(self, i, i+1, (PyObject *)NULL);
    assert(status >= 0);

    (void) status;

    return v;
}

static void
reverse_slice(PyObject **lo, PyObject **hi)
{
    assert(lo && hi);

    --hi;
    while (lo < hi) {
        PyObject *t = *lo;
        *lo = *hi;
        *hi = t;
        ++lo;
        --hi;
    }
}

static int
islt(PyObject *x, PyObject *y, PyObject *compare)
{
    PyObject *res;
    PyObject *args;
    Py_ssize_t i;

    assert(compare != NULL);

    args = PyTuple_New(2);
    if (args == NULL)
        return -1;
    Py_INCREF(x);
    Py_INCREF(y);
    PyTuple_SET_ITEM(args, 0, x);
    PyTuple_SET_ITEM(args, 1, y);
    res = PyObject_Call(compare, args, NULL);
    Py_DECREF(args);
    if (res == NULL)
        return -1;
    if (!PyInt_Check(res)) {
        PyErr_Format(PyExc_TypeError,
                     "comparison function must return int, not %.200s",
                     res->ob_type->tp_name);
        Py_DECREF(res);
        return -1;
    }
    i = PyInt_AsLong(res);
    Py_DECREF(res);
    return i < 0;
}

#define ISLT(X, Y, COMPARE) ((COMPARE) == NULL ?                        \
                 PyObject_RichCompareBool(X, Y, Py_LT) :                \
                 islt(X, Y, COMPARE))

#define IFLT(X, Y) if ((k = ISLT(X, Y, compare)) < 0) goto fail;  \
           if (k)

static int
binarysort(PyObject **lo, PyObject **hi, PyObject **start, PyObject *compare)

{
    register Py_ssize_t k;
    register PyObject **l, **p, **r;
    register PyObject *pivot;

    assert(lo <= start && start <= hi);

    if (lo == start)
        ++start;
    for (; start < hi; ++start) {

        l = lo;
        r = start;
        pivot = *r;

        assert(l < r);
        do {
            p = l + ((r - l) >> 1);
            IFLT(pivot, *p)
                r = p;
            else
                l = p+1;
        } while (l < r);
        assert(l == r);

        for (p = start; p > l; --p)
            *p = *(p-1);
        *l = pivot;
    }
    return 0;

 fail:
    return -1;
}

static Py_ssize_t
count_run(PyObject **lo, PyObject **hi, PyObject *compare, int *descending)
{
    Py_ssize_t k;
    Py_ssize_t n;

    assert(lo < hi);
    *descending = 0;
    ++lo;
    if (lo == hi)
        return 1;

    n = 2;
    IFLT(*lo, *(lo-1)) {
        *descending = 1;
        for (lo = lo+1; lo < hi; ++lo, ++n) {
            IFLT(*lo, *(lo-1))
                ;
            else
                break;
        }
    }
    else {
        for (lo = lo+1; lo < hi; ++lo, ++n) {
            IFLT(*lo, *(lo-1))
                break;
        }
    }

    return n;
fail:
    return -1;
}

static Py_ssize_t
gallop_left(PyObject *key, PyObject **a, Py_ssize_t n, Py_ssize_t hint, PyObject *compare)
{
    Py_ssize_t ofs;
    Py_ssize_t lastofs;
    Py_ssize_t k;

    assert(key && a && n > 0 && hint >= 0 && hint < n);

    a += hint;
    lastofs = 0;
    ofs = 1;
    IFLT(*a, key) {

        const Py_ssize_t maxofs = n - hint;
        while (ofs < maxofs) {
            IFLT(a[ofs], key) {
                lastofs = ofs;
                ofs = (ofs << 1) + 1;
                if (ofs <= 0)
                    ofs = maxofs;
            }
            else
                break;
        }
        if (ofs > maxofs)
            ofs = maxofs;

        lastofs += hint;
        ofs += hint;
    }
    else {

        const Py_ssize_t maxofs = hint + 1;
        while (ofs < maxofs) {
            IFLT(*(a-ofs), key)
                break;

            lastofs = ofs;
            ofs = (ofs << 1) + 1;
            if (ofs <= 0)
                ofs = maxofs;
        }
        if (ofs > maxofs)
            ofs = maxofs;

        k = lastofs;
        lastofs = hint - ofs;
        ofs = hint - k;
    }
    a -= hint;

    assert(-1 <= lastofs && lastofs < ofs && ofs <= n);

    ++lastofs;
    while (lastofs < ofs) {
        Py_ssize_t m = lastofs + ((ofs - lastofs) >> 1);

        IFLT(a[m], key)
            lastofs = m+1;
        else
            ofs = m;
    }
    assert(lastofs == ofs);
    return ofs;

fail:
    return -1;
}

static Py_ssize_t
gallop_right(PyObject *key, PyObject **a, Py_ssize_t n, Py_ssize_t hint, PyObject *compare)
{
    Py_ssize_t ofs;
    Py_ssize_t lastofs;
    Py_ssize_t k;

    assert(key && a && n > 0 && hint >= 0 && hint < n);

    a += hint;
    lastofs = 0;
    ofs = 1;
    IFLT(key, *a) {

        const Py_ssize_t maxofs = hint + 1;
        while (ofs < maxofs) {
            IFLT(key, *(a-ofs)) {
                lastofs = ofs;
                ofs = (ofs << 1) + 1;
                if (ofs <= 0)
                    ofs = maxofs;
            }
            else
                break;
        }
        if (ofs > maxofs)
            ofs = maxofs;

        k = lastofs;
        lastofs = hint - ofs;
        ofs = hint - k;
    }
    else {

        const Py_ssize_t maxofs = n - hint;
        while (ofs < maxofs) {
            IFLT(key, a[ofs])
                break;

            lastofs = ofs;
            ofs = (ofs << 1) + 1;
            if (ofs <= 0)
                ofs = maxofs;
        }
        if (ofs > maxofs)
            ofs = maxofs;

        lastofs += hint;
        ofs += hint;
    }
    a -= hint;

    assert(-1 <= lastofs && lastofs < ofs && ofs <= n);

    ++lastofs;
    while (lastofs < ofs) {
        Py_ssize_t m = lastofs + ((ofs - lastofs) >> 1);

        IFLT(key, a[m])
            ofs = m;
        else
            lastofs = m+1;
    }
    assert(lastofs == ofs);
    return ofs;

fail:
    return -1;
}

#define MAX_MERGE_PENDING 85

#define MIN_GALLOP 7

#define MERGESTATE_TEMP_SIZE 256

struct s_slice {
    PyObject **base;
    Py_ssize_t len;
};

typedef struct s_MergeState {

    PyObject *compare;

    Py_ssize_t min_gallop;

    PyObject **a;
    Py_ssize_t alloced;

    int n;
    struct s_slice pending[MAX_MERGE_PENDING];

    PyObject *temparray[MERGESTATE_TEMP_SIZE];
} MergeState;

static void
merge_init(MergeState *ms, PyObject *compare)
{
    assert(ms != NULL);
    ms->compare = compare;
    ms->a = ms->temparray;
    ms->alloced = MERGESTATE_TEMP_SIZE;
    ms->n = 0;
    ms->min_gallop = MIN_GALLOP;
}

static void
merge_freemem(MergeState *ms)
{
    assert(ms != NULL);
    if (ms->a != ms->temparray)
        PyMem_Free(ms->a);
    ms->a = ms->temparray;
    ms->alloced = MERGESTATE_TEMP_SIZE;
}

static int
merge_getmem(MergeState *ms, Py_ssize_t need)
{
    assert(ms != NULL);
    if (need <= ms->alloced)
        return 0;

    merge_freemem(ms);
    if ((size_t)need > PY_SSIZE_T_MAX / sizeof(PyObject*)) {
        PyErr_NoMemory();
        return -1;
    }
    ms->a = (PyObject **)PyMem_Malloc(need * sizeof(PyObject*));
    if (ms->a) {
        ms->alloced = need;
        return 0;
    }
    PyErr_NoMemory();
    merge_freemem(ms);
    return -1;
}
#define MERGE_GETMEM(MS, NEED) ((NEED) <= (MS)->alloced ? 0 :   \
                                merge_getmem(MS, NEED))

static Py_ssize_t
merge_lo(MergeState *ms, PyObject **pa, Py_ssize_t na,
                         PyObject **pb, Py_ssize_t nb)
{
    Py_ssize_t k;
    PyObject *compare;
    PyObject **dest;
    int result = -1;
    Py_ssize_t min_gallop;

    assert(ms && pa && pb && na > 0 && nb > 0 && pa + na == pb);
    if (MERGE_GETMEM(ms, na) < 0)
        return -1;
    memcpy(ms->a, pa, na * sizeof(PyObject*));
    dest = pa;
    pa = ms->a;

    *dest++ = *pb++;
    --nb;
    if (nb == 0)
        goto Succeed;
    if (na == 1)
        goto CopyB;

    min_gallop = ms->min_gallop;
    compare = ms->compare;
    for (;;) {
        Py_ssize_t acount = 0;
        Py_ssize_t bcount = 0;

        for (;;) {
            assert(na > 1 && nb > 0);
            k = ISLT(*pb, *pa, compare);
            if (k) {
                if (k < 0)
                    goto Fail;
                *dest++ = *pb++;
                ++bcount;
                acount = 0;
                --nb;
                if (nb == 0)
                    goto Succeed;
                if (bcount >= min_gallop)
                    break;
            }
            else {
                *dest++ = *pa++;
                ++acount;
                bcount = 0;
                --na;
                if (na == 1)
                    goto CopyB;
                if (acount >= min_gallop)
                    break;
            }
        }

        ++min_gallop;
        do {
            assert(na > 1 && nb > 0);
            min_gallop -= min_gallop > 1;
            ms->min_gallop = min_gallop;
            k = gallop_right(*pb, pa, na, 0, compare);
            acount = k;
            if (k) {
                if (k < 0)
                    goto Fail;
                memcpy(dest, pa, k * sizeof(PyObject *));
                dest += k;
                pa += k;
                na -= k;
                if (na == 1)
                    goto CopyB;

                if (na == 0)
                    goto Succeed;
            }
            *dest++ = *pb++;
            --nb;
            if (nb == 0)
                goto Succeed;

            k = gallop_left(*pa, pb, nb, 0, compare);
            bcount = k;
            if (k) {
                if (k < 0)
                    goto Fail;
                memmove(dest, pb, k * sizeof(PyObject *));
                dest += k;
                pb += k;
                nb -= k;
                if (nb == 0)
                    goto Succeed;
            }
            *dest++ = *pa++;
            --na;
            if (na == 1)
                goto CopyB;
        } while (acount >= MIN_GALLOP || bcount >= MIN_GALLOP);
        ++min_gallop;
        ms->min_gallop = min_gallop;
    }
Succeed:
    result = 0;
Fail:
    if (na)
        memcpy(dest, pa, na * sizeof(PyObject*));
    return result;
CopyB:
    assert(na == 1 && nb > 0);

    memmove(dest, pb, nb * sizeof(PyObject *));
    dest[nb] = *pa;
    return 0;
}

static Py_ssize_t
merge_hi(MergeState *ms, PyObject **pa, Py_ssize_t na, PyObject **pb, Py_ssize_t nb)
{
    Py_ssize_t k;
    PyObject *compare;
    PyObject **dest;
    int result = -1;
    PyObject **basea;
    PyObject **baseb;
    Py_ssize_t min_gallop;

    assert(ms && pa && pb && na > 0 && nb > 0 && pa + na == pb);
    if (MERGE_GETMEM(ms, nb) < 0)
        return -1;
    dest = pb + nb - 1;
    memcpy(ms->a, pb, nb * sizeof(PyObject*));
    basea = pa;
    baseb = ms->a;
    pb = ms->a + nb - 1;
    pa += na - 1;

    *dest-- = *pa--;
    --na;
    if (na == 0)
        goto Succeed;
    if (nb == 1)
        goto CopyA;

    min_gallop = ms->min_gallop;
    compare = ms->compare;
    for (;;) {
        Py_ssize_t acount = 0;
        Py_ssize_t bcount = 0;

        for (;;) {
            assert(na > 0 && nb > 1);
            k = ISLT(*pb, *pa, compare);
            if (k) {
                if (k < 0)
                    goto Fail;
                *dest-- = *pa--;
                ++acount;
                bcount = 0;
                --na;
                if (na == 0)
                    goto Succeed;
                if (acount >= min_gallop)
                    break;
            }
            else {
                *dest-- = *pb--;
                ++bcount;
                acount = 0;
                --nb;
                if (nb == 1)
                    goto CopyA;
                if (bcount >= min_gallop)
                    break;
            }
        }

        ++min_gallop;
        do {
            assert(na > 0 && nb > 1);
            min_gallop -= min_gallop > 1;
            ms->min_gallop = min_gallop;
            k = gallop_right(*pb, basea, na, na-1, compare);
            if (k < 0)
                goto Fail;
            k = na - k;
            acount = k;
            if (k) {
                dest -= k;
                pa -= k;
                memmove(dest+1, pa+1, k * sizeof(PyObject *));
                na -= k;
                if (na == 0)
                    goto Succeed;
            }
            *dest-- = *pb--;
            --nb;
            if (nb == 1)
                goto CopyA;

            k = gallop_left(*pa, baseb, nb, nb-1, compare);
            if (k < 0)
                goto Fail;
            k = nb - k;
            bcount = k;
            if (k) {
                dest -= k;
                pb -= k;
                memcpy(dest+1, pb+1, k * sizeof(PyObject *));
                nb -= k;
                if (nb == 1)
                    goto CopyA;

                if (nb == 0)
                    goto Succeed;
            }
            *dest-- = *pa--;
            --na;
            if (na == 0)
                goto Succeed;
        } while (acount >= MIN_GALLOP || bcount >= MIN_GALLOP);
        ++min_gallop;
        ms->min_gallop = min_gallop;
    }
Succeed:
    result = 0;
Fail:
    if (nb)
        memcpy(dest-(nb-1), baseb, nb * sizeof(PyObject*));
    return result;
CopyA:
    assert(nb == 1 && na > 0);

    dest -= na;
    pa -= na;
    memmove(dest+1, pa+1, na * sizeof(PyObject *));
    *dest = *pb;
    return 0;
}

static Py_ssize_t
merge_at(MergeState *ms, Py_ssize_t i)
{
    PyObject **pa, **pb;
    Py_ssize_t na, nb;
    Py_ssize_t k;
    PyObject *compare;

    assert(ms != NULL);
    assert(ms->n >= 2);
    assert(i >= 0);
    assert(i == ms->n - 2 || i == ms->n - 3);

    pa = ms->pending[i].base;
    na = ms->pending[i].len;
    pb = ms->pending[i+1].base;
    nb = ms->pending[i+1].len;
    assert(na > 0 && nb > 0);
    assert(pa + na == pb);

    ms->pending[i].len = na + nb;
    if (i == ms->n - 3)
        ms->pending[i+1] = ms->pending[i+2];
    --ms->n;

    compare = ms->compare;
    k = gallop_right(*pb, pa, na, 0, compare);
    if (k < 0)
        return -1;
    pa += k;
    na -= k;
    if (na == 0)
        return 0;

    nb = gallop_left(pa[na-1], pb, nb, nb-1, compare);
    if (nb <= 0)
        return nb;

    if (na <= nb)
        return merge_lo(ms, pa, na, pb, nb);
    else
        return merge_hi(ms, pa, na, pb, nb);
}

static int
merge_collapse(MergeState *ms)
{
    struct s_slice *p = ms->pending;

    assert(ms);
    while (ms->n > 1) {
        Py_ssize_t n = ms->n - 2;
        if ((n > 0 && p[n-1].len <= p[n].len + p[n+1].len) ||
            (n > 1 && p[n-2].len <= p[n-1].len + p[n].len)) {
            if (p[n-1].len < p[n+1].len)
                --n;
            if (merge_at(ms, n) < 0)
                return -1;
        }
        else if (p[n].len <= p[n+1].len) {
                 if (merge_at(ms, n) < 0)
                        return -1;
        }
        else
            break;
    }
    return 0;
}

static int
merge_force_collapse(MergeState *ms)
{
    struct s_slice *p = ms->pending;

    assert(ms);
    while (ms->n > 1) {
        Py_ssize_t n = ms->n - 2;
        if (n > 0 && p[n-1].len < p[n+1].len)
            --n;
        if (merge_at(ms, n) < 0)
            return -1;
    }
    return 0;
}

static Py_ssize_t
merge_compute_minrun(Py_ssize_t n)
{
    Py_ssize_t r = 0;

    assert(n >= 0);
    while (n >= 64) {
        r |= n & 1;
        n >>= 1;
    }
    return n + r;
}

typedef struct {
    PyObject_HEAD
    PyObject *key;
    PyObject *value;
} sortwrapperobject;

PyDoc_STRVAR(sortwrapper_doc, "Object wrapper with a custom sort key.");
static PyObject *
sortwrapper_richcompare(sortwrapperobject *, sortwrapperobject *, int);
static void
sortwrapper_dealloc(sortwrapperobject *);

static PyTypeObject sortwrapper_type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "sortwrapper",
    sizeof(sortwrapperobject),
    0,

    (destructor)sortwrapper_dealloc,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT |
    Py_TPFLAGS_HAVE_RICHCOMPARE,
    sortwrapper_doc,
    0,
    0,
    (richcmpfunc)sortwrapper_richcompare,
};

static PyObject *
sortwrapper_richcompare(sortwrapperobject *a, sortwrapperobject *b, int op)
{
    if (!PyObject_TypeCheck(b, &sortwrapper_type)) {
        PyErr_SetString(PyExc_TypeError,
            "expected a sortwrapperobject");
        return NULL;
    }
    return PyObject_RichCompare(a->key, b->key, op);
}

static void
sortwrapper_dealloc(sortwrapperobject *so)
{
    Py_XDECREF(so->key);
    Py_XDECREF(so->value);
    PyObject_Del(so);
}

static PyObject *
build_sortwrapper(PyObject *key, PyObject *value)
{
    sortwrapperobject *so;

    so = PyObject_New(sortwrapperobject, &sortwrapper_type);
    if (so == NULL)
        return NULL;
    so->key = key;
    so->value = value;
    return (PyObject *)so;
}

static PyObject *
sortwrapper_getvalue(PyObject *so)
{
    PyObject *value;

    if (!PyObject_TypeCheck(so, &sortwrapper_type)) {
        PyErr_SetString(PyExc_TypeError,
            "expected a sortwrapperobject");
        return NULL;
    }
    value = ((sortwrapperobject *)so)->value;
    Py_INCREF(value);
    return value;
}

typedef struct {
    PyObject_HEAD
    PyObject *func;
} cmpwrapperobject;

static void
cmpwrapper_dealloc(cmpwrapperobject *co)
{
    Py_XDECREF(co->func);
    PyObject_Del(co);
}

static PyObject *
cmpwrapper_call(cmpwrapperobject *co, PyObject *args, PyObject *kwds)
{
    PyObject *x, *y, *xx, *yy;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &x, &y))
        return NULL;
    if (!PyObject_TypeCheck(x, &sortwrapper_type) ||
        !PyObject_TypeCheck(y, &sortwrapper_type)) {
        PyErr_SetString(PyExc_TypeError,
            "expected a sortwrapperobject");
        return NULL;
    }
    xx = ((sortwrapperobject *)x)->key;
    yy = ((sortwrapperobject *)y)->key;
    return PyObject_CallFunctionObjArgs(co->func, xx, yy, NULL);
}

PyDoc_STRVAR(cmpwrapper_doc, "cmp() wrapper for sort with custom keys.");

static PyTypeObject cmpwrapper_type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "cmpwrapper",
    sizeof(cmpwrapperobject),
    0,

    (destructor)cmpwrapper_dealloc,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    (ternaryfunc)cmpwrapper_call,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT,
    cmpwrapper_doc,
};

static PyObject *
build_cmpwrapper(PyObject *cmpfunc)
{
    cmpwrapperobject *co;

    co = PyObject_New(cmpwrapperobject, &cmpwrapper_type);
    if (co == NULL)
        return NULL;
    Py_INCREF(cmpfunc);
    co->func = cmpfunc;
    return (PyObject *)co;
}

static PyObject *
listsort(PyListObject *self, PyObject *args, PyObject *kwds)
{
    MergeState ms;
    PyObject **lo, **hi;
    Py_ssize_t nremaining;
    Py_ssize_t minrun;
    Py_ssize_t saved_ob_size, saved_allocated;
    PyObject **saved_ob_item;
    PyObject **final_ob_item;
    PyObject *compare = NULL;
    PyObject *result = NULL;
    int reverse = 0;
    PyObject *keyfunc = NULL;
    Py_ssize_t i;
    PyObject *key, *value, *kvpair;
    static char *kwlist[] = {"cmp", "key", "reverse", 0};

    assert(self != NULL);
    assert (PyList_Check(self));
    if (args != NULL) {
        if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOi:sort",
            kwlist, &compare, &keyfunc, &reverse))
            return NULL;
    }
    if (compare == Py_None)
        compare = NULL;
    if (compare != NULL &&
        PyErr_WarnPy3k("the cmp argument is not supported in 3.x", 1) < 0)
        return NULL;
    if (keyfunc == Py_None)
        keyfunc = NULL;
    if (compare != NULL && keyfunc != NULL) {
        compare = build_cmpwrapper(compare);
        if (compare == NULL)
            return NULL;
    } else
        Py_XINCREF(compare);

    saved_ob_size = Py_SIZE(self);
    saved_ob_item = self->ob_item;
    saved_allocated = self->allocated;
    Py_SIZE(self) = 0;
    self->ob_item = NULL;
    self->allocated = -1;

    if (keyfunc != NULL) {
        for (i=0 ; i < saved_ob_size ; i++) {
            value = saved_ob_item[i];
            key = PyObject_CallFunctionObjArgs(keyfunc, value,
                                               NULL);
            if (key == NULL) {
                for (i=i-1 ; i>=0 ; i--) {
                    kvpair = saved_ob_item[i];
                    value = sortwrapper_getvalue(kvpair);
                    saved_ob_item[i] = value;
                    Py_DECREF(kvpair);
                }
                goto dsu_fail;
            }
            kvpair = build_sortwrapper(key, value);
            if (kvpair == NULL)
                goto dsu_fail;
            saved_ob_item[i] = kvpair;
        }
    }

    if (reverse && saved_ob_size > 1)
        reverse_slice(saved_ob_item, saved_ob_item + saved_ob_size);

    merge_init(&ms, compare);

    nremaining = saved_ob_size;
    if (nremaining < 2)
        goto succeed;

    lo = saved_ob_item;
    hi = lo + nremaining;
    minrun = merge_compute_minrun(nremaining);
    do {
        int descending;
        Py_ssize_t n;

        n = count_run(lo, hi, compare, &descending);
        if (n < 0)
            goto fail;
        if (descending)
            reverse_slice(lo, lo + n);

        if (n < minrun) {
            const Py_ssize_t force = nremaining <= minrun ?
                              nremaining : minrun;
            if (binarysort(lo, lo + force, lo + n, compare) < 0)
                goto fail;
            n = force;
        }

        assert(ms.n < MAX_MERGE_PENDING);
        ms.pending[ms.n].base = lo;
        ms.pending[ms.n].len = n;
        ++ms.n;
        if (merge_collapse(&ms) < 0)
            goto fail;

        lo += n;
        nremaining -= n;
    } while (nremaining);
    assert(lo == hi);

    if (merge_force_collapse(&ms) < 0)
        goto fail;
    assert(ms.n == 1);
    assert(ms.pending[0].base == saved_ob_item);
    assert(ms.pending[0].len == saved_ob_size);

succeed:
    result = Py_None;
fail:
    if (keyfunc != NULL) {
        for (i=0 ; i < saved_ob_size ; i++) {
            kvpair = saved_ob_item[i];
            value = sortwrapper_getvalue(kvpair);
            saved_ob_item[i] = value;
            Py_DECREF(kvpair);
        }
    }

    if (self->allocated != -1 && result != NULL) {

        PyErr_SetString(PyExc_ValueError, "list modified during sort");
        result = NULL;
    }

    if (reverse && saved_ob_size > 1)
        reverse_slice(saved_ob_item, saved_ob_item + saved_ob_size);

    merge_freemem(&ms);

dsu_fail:
    final_ob_item = self->ob_item;
    i = Py_SIZE(self);
    Py_SIZE(self) = saved_ob_size;
    self->ob_item = saved_ob_item;
    self->allocated = saved_allocated;
    if (final_ob_item != NULL) {

        while (--i >= 0) {
            Py_XDECREF(final_ob_item[i]);
        }
        PyMem_FREE(final_ob_item);
    }
    Py_XDECREF(compare);
    Py_XINCREF(result);
    return result;
}
#undef IFLT
#undef ISLT

int
PyList_Sort(PyObject *v)
{
    if (v == NULL || !PyList_Check(v)) {
        PyErr_BadInternalCall();
        return -1;
    }
    v = listsort((PyListObject *)v, (PyObject *)NULL, (PyObject *)NULL);
    if (v == NULL)
        return -1;
    Py_DECREF(v);
    return 0;
}

static PyObject *
listreverse(PyListObject *self)
{
    if (Py_SIZE(self) > 1)
        reverse_slice(self->ob_item, self->ob_item + Py_SIZE(self));
    Py_RETURN_NONE;
}

int
PyList_Reverse(PyObject *v)
{
    PyListObject *self = (PyListObject *)v;

    if (v == NULL || !PyList_Check(v)) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (Py_SIZE(self) > 1)
        reverse_slice(self->ob_item, self->ob_item + Py_SIZE(self));
    return 0;
}

PyObject *
PyList_AsTuple(PyObject *v)
{
    PyObject *w;
    PyObject **p, **q;
    Py_ssize_t n;
    if (v == NULL || !PyList_Check(v)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    n = Py_SIZE(v);
    w = PyTuple_New(n);
    if (w == NULL)
        return NULL;
    p = ((PyTupleObject *)w)->ob_item;
    q = ((PyListObject *)v)->ob_item;
    while (--n >= 0) {
        Py_INCREF(*q);
        *p = *q;
        p++;
        q++;
    }
    return w;
}

static PyObject *
listindex(PyListObject *self, PyObject *args)
{
    Py_ssize_t i, start=0, stop=Py_SIZE(self);
    PyObject *v, *format_tuple, *err_string;
    static PyObject *err_format = NULL;

    if (!PyArg_ParseTuple(args, "O|O&O&:index", &v,
                                _PyEval_SliceIndex, &start,
                                _PyEval_SliceIndex, &stop))
        return NULL;
    if (start < 0) {
        start += Py_SIZE(self);
        if (start < 0)
            start = 0;
    }
    if (stop < 0) {
        stop += Py_SIZE(self);
        if (stop < 0)
            stop = 0;
    }
    for (i = start; i < stop && i < Py_SIZE(self); i++) {
        int cmp = PyObject_RichCompareBool(self->ob_item[i], v, Py_EQ);
        if (cmp > 0)
            return PyInt_FromSsize_t(i);
        else if (cmp < 0)
            return NULL;
    }
    if (err_format == NULL) {
        err_format = PyString_FromString("%r is not in list");
        if (err_format == NULL)
            return NULL;
    }
    format_tuple = PyTuple_Pack(1, v);
    if (format_tuple == NULL)
        return NULL;
    err_string = PyString_Format(err_format, format_tuple);
    Py_DECREF(format_tuple);
    if (err_string == NULL)
        return NULL;
    PyErr_SetObject(PyExc_ValueError, err_string);
    Py_DECREF(err_string);
    return NULL;
}

static PyObject *
listcount(PyListObject *self, PyObject *v)
{
    Py_ssize_t count = 0;
    Py_ssize_t i;

    for (i = 0; i < Py_SIZE(self); i++) {
        int cmp = PyObject_RichCompareBool(self->ob_item[i], v, Py_EQ);
        if (cmp > 0)
            count++;
        else if (cmp < 0)
            return NULL;
    }
    return PyInt_FromSsize_t(count);
}

static PyObject *
listremove(PyListObject *self, PyObject *v)
{
    Py_ssize_t i;

    for (i = 0; i < Py_SIZE(self); i++) {
        int cmp = PyObject_RichCompareBool(self->ob_item[i], v, Py_EQ);
        if (cmp > 0) {
            if (list_ass_slice(self, i, i+1,
                               (PyObject *)NULL) == 0)
                Py_RETURN_NONE;
            return NULL;
        }
        else if (cmp < 0)
            return NULL;
    }
    PyErr_SetString(PyExc_ValueError, "list.remove(x): x not in list");
    return NULL;
}

static int
list_traverse(PyListObject *o, visitproc visit, void *arg)
{
    Py_ssize_t i;

    for (i = Py_SIZE(o); --i >= 0; )
        Py_VISIT(o->ob_item[i]);
    return 0;
}

static PyObject *
list_richcompare(PyObject *v, PyObject *w, int op)
{
    PyListObject *vl, *wl;
    Py_ssize_t i;

    if (!PyList_Check(v) || !PyList_Check(w)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    vl = (PyListObject *)v;
    wl = (PyListObject *)w;

    if (Py_SIZE(vl) != Py_SIZE(wl) && (op == Py_EQ || op == Py_NE)) {

        PyObject *res;
        if (op == Py_EQ)
            res = Py_False;
        else
            res = Py_True;
        Py_INCREF(res);
        return res;
    }

    for (i = 0; i < Py_SIZE(vl) && i < Py_SIZE(wl); i++) {
        int k = PyObject_RichCompareBool(vl->ob_item[i],
                                         wl->ob_item[i], Py_EQ);
        if (k < 0)
            return NULL;
        if (!k)
            break;
    }

    if (i >= Py_SIZE(vl) || i >= Py_SIZE(wl)) {

        Py_ssize_t vs = Py_SIZE(vl);
        Py_ssize_t ws = Py_SIZE(wl);
        int cmp;
        PyObject *res;
        switch (op) {
        case Py_LT: cmp = vs <  ws; break;
        case Py_LE: cmp = vs <= ws; break;
        case Py_EQ: cmp = vs == ws; break;
        case Py_NE: cmp = vs != ws; break;
        case Py_GT: cmp = vs >  ws; break;
        case Py_GE: cmp = vs >= ws; break;
        default: return NULL;
        }
        if (cmp)
            res = Py_True;
        else
            res = Py_False;
        Py_INCREF(res);
        return res;
    }

    if (op == Py_EQ) {
        Py_INCREF(Py_False);
        return Py_False;
    }
    if (op == Py_NE) {
        Py_INCREF(Py_True);
        return Py_True;
    }

    return PyObject_RichCompare(vl->ob_item[i], wl->ob_item[i], op);
}

static int
list_init(PyListObject *self, PyObject *args, PyObject *kw)
{
    PyObject *arg = NULL;
    static char *kwlist[] = {"sequence", 0};

    if (!PyArg_ParseTupleAndKeywords(args, kw, "|O:list", kwlist, &arg))
        return -1;

    assert(0 <= Py_SIZE(self));
    assert(Py_SIZE(self) <= self->allocated || self->allocated == -1);
    assert(self->ob_item != NULL ||
           self->allocated == 0 || self->allocated == -1);

    if (self->ob_item != NULL) {
        (void)list_clear(self);
    }
    if (arg != NULL) {
        PyObject *rv = listextend(self, arg);
        if (rv == NULL)
            return -1;
        Py_DECREF(rv);
    }
    return 0;
}

static PyObject *
list_sizeof(PyListObject *self)
{
    Py_ssize_t res;

    res = _PyObject_SIZE(Py_TYPE(self)) + self->allocated * sizeof(void*);
    return PyInt_FromSsize_t(res);
}

static PyObject *list_iter(PyObject *seq);
static PyObject *list_reversed(PyListObject* seq, PyObject* unused);

PyDoc_STRVAR(getitem_doc,
"x.__getitem__(y) <==> x[y]");
PyDoc_STRVAR(reversed_doc,
"L.__reversed__() -- return a reverse iterator over the list");
PyDoc_STRVAR(sizeof_doc,
"L.__sizeof__() -- size of L in memory, in bytes");
PyDoc_STRVAR(append_doc,
"L.append(object) -- append object to end");
PyDoc_STRVAR(extend_doc,
"L.extend(iterable) -- extend list by appending elements from the iterable");
PyDoc_STRVAR(insert_doc,
"L.insert(index, object) -- insert object before index");
PyDoc_STRVAR(pop_doc,
"L.pop([index]) -> item -- remove and return item at index (default last).\n"
"Raises IndexError if list is empty or index is out of range.");
PyDoc_STRVAR(remove_doc,
"L.remove(value) -- remove first occurrence of value.\n"
"Raises ValueError if the value is not present.");
PyDoc_STRVAR(index_doc,
"L.index(value, [start, [stop]]) -> integer -- return first index of value.\n"
"Raises ValueError if the value is not present.");
PyDoc_STRVAR(count_doc,
"L.count(value) -> integer -- return number of occurrences of value");
PyDoc_STRVAR(reverse_doc,
"L.reverse() -- reverse *IN PLACE*");
PyDoc_STRVAR(sort_doc,
"L.sort(cmp=None, key=None, reverse=False) -- stable sort *IN PLACE*;\n\
cmp(x, y) -> -1, 0, 1");

static PyObject *list_subscript(PyListObject*, PyObject*);

static PyMethodDef list_methods[] = {
    {"__getitem__", (PyCFunction)list_subscript, METH_O|METH_COEXIST, getitem_doc},
    {"__reversed__",(PyCFunction)list_reversed, METH_NOARGS, reversed_doc},
    {"__sizeof__",  (PyCFunction)list_sizeof, METH_NOARGS, sizeof_doc},
    {"append",          (PyCFunction)listappend,  METH_O, append_doc},
    {"insert",          (PyCFunction)listinsert,  METH_VARARGS, insert_doc},
    {"extend",      (PyCFunction)listextend,  METH_O, extend_doc},
    {"pop",             (PyCFunction)listpop,     METH_VARARGS, pop_doc},
    {"remove",          (PyCFunction)listremove,  METH_O, remove_doc},
    {"index",           (PyCFunction)listindex,   METH_VARARGS, index_doc},
    {"count",           (PyCFunction)listcount,   METH_O, count_doc},
    {"reverse",         (PyCFunction)listreverse, METH_NOARGS, reverse_doc},
    {"sort",            (PyCFunction)listsort,    METH_VARARGS | METH_KEYWORDS, sort_doc},
    {NULL,              NULL}
};

static PySequenceMethods list_as_sequence = {
    (lenfunc)list_length,
    (binaryfunc)list_concat,
    (ssizeargfunc)list_repeat,
    (ssizeargfunc)list_item,
    (ssizessizeargfunc)list_slice,
    (ssizeobjargproc)list_ass_item,
    (ssizessizeobjargproc)list_ass_slice,
    (objobjproc)list_contains,
    (binaryfunc)list_inplace_concat,
    (ssizeargfunc)list_inplace_repeat,
};

PyDoc_STRVAR(list_doc,
"list() -> new empty list\n"
"list(iterable) -> new list initialized from iterable's items");

static PyObject *
list_subscript(PyListObject* self, PyObject* item)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i;
        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        if (i < 0)
            i += PyList_GET_SIZE(self);
        return list_item(self, i);
    }
    else if (PySlice_Check(item)) {
        Py_ssize_t start, stop, step, slicelength, cur, i;
        PyObject* result;
        PyObject* it;
        PyObject **src, **dest;

        if (PySlice_GetIndicesEx((PySliceObject*)item, Py_SIZE(self),
                         &start, &stop, &step, &slicelength) < 0) {
            return NULL;
        }

        if (slicelength <= 0) {
            return PyList_New(0);
        }
        else if (step == 1) {
            return list_slice(self, start, stop);
        }
        else {
            result = PyList_New(slicelength);
            if (!result) return NULL;

            src = self->ob_item;
            dest = ((PyListObject *)result)->ob_item;
            for (cur = start, i = 0; i < slicelength;
                 cur += step, i++) {
                it = src[cur];
                Py_INCREF(it);
                dest[i] = it;
            }

            return result;
        }
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "list indices must be integers, not %.200s",
                     item->ob_type->tp_name);
        return NULL;
    }
}

static int
list_ass_subscript(PyListObject* self, PyObject* item, PyObject* value)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return -1;
        if (i < 0)
            i += PyList_GET_SIZE(self);
        return list_ass_item(self, i, value);
    }
    else if (PySlice_Check(item)) {
        Py_ssize_t start, stop, step, slicelength;

        if (PySlice_GetIndicesEx((PySliceObject*)item, Py_SIZE(self),
                         &start, &stop, &step, &slicelength) < 0) {
            return -1;
        }

        if (step == 1)
            return list_ass_slice(self, start, stop, value);

        if ((step < 0 && start < stop) ||
            (step > 0 && start > stop))
            stop = start;

        if (value == NULL) {

            PyObject **garbage;
            size_t cur;
            Py_ssize_t i;

            if (slicelength <= 0)
                return 0;

            if (step < 0) {
                stop = start + 1;
                start = stop + step*(slicelength - 1) - 1;
                step = -step;
            }

            assert((size_t)slicelength <=
                   PY_SIZE_MAX / sizeof(PyObject*));

            garbage = (PyObject**)
                PyMem_MALLOC(slicelength*sizeof(PyObject*));
            if (!garbage) {
                PyErr_NoMemory();
                return -1;
            }

            for (cur = start, i = 0;
                 cur < (size_t)stop;
                 cur += step, i++) {
                Py_ssize_t lim = step - 1;

                garbage[i] = PyList_GET_ITEM(self, cur);

                if (cur + step >= (size_t)Py_SIZE(self)) {
                    lim = Py_SIZE(self) - cur - 1;
                }

                memmove(self->ob_item + cur - i,
                    self->ob_item + cur + 1,
                    lim * sizeof(PyObject *));
            }
            cur = start + slicelength*step;
            if (cur < (size_t)Py_SIZE(self)) {
                memmove(self->ob_item + cur - slicelength,
                    self->ob_item + cur,
                    (Py_SIZE(self) - cur) *
                     sizeof(PyObject *));
            }

            Py_SIZE(self) -= slicelength;
            list_resize(self, Py_SIZE(self));

            for (i = 0; i < slicelength; i++) {
                Py_DECREF(garbage[i]);
            }
            PyMem_FREE(garbage);

            return 0;
        }
        else {

            PyObject *ins, *seq;
            PyObject **garbage, **seqitems, **selfitems;
            Py_ssize_t cur, i;

            if (self == (PyListObject*)value) {
                seq = list_slice((PyListObject*)value, 0,
                                   PyList_GET_SIZE(value));
            }
            else {
                seq = PySequence_Fast(value,
                                      "must assign iterable "
                                      "to extended slice");
            }
            if (!seq)
                return -1;

            if (PySequence_Fast_GET_SIZE(seq) != slicelength) {
                PyErr_Format(PyExc_ValueError,
                    "attempt to assign sequence of "
                    "size %zd to extended slice of "
                    "size %zd",
                         PySequence_Fast_GET_SIZE(seq),
                         slicelength);
                Py_DECREF(seq);
                return -1;
            }

            if (!slicelength) {
                Py_DECREF(seq);
                return 0;
            }

            garbage = (PyObject**)
                PyMem_MALLOC(slicelength*sizeof(PyObject*));
            if (!garbage) {
                Py_DECREF(seq);
                PyErr_NoMemory();
                return -1;
            }

            selfitems = self->ob_item;
            seqitems = PySequence_Fast_ITEMS(seq);
            for (cur = start, i = 0; i < slicelength;
                 cur += step, i++) {
                garbage[i] = selfitems[cur];
                ins = seqitems[i];
                Py_INCREF(ins);
                selfitems[cur] = ins;
            }

            for (i = 0; i < slicelength; i++) {
                Py_DECREF(garbage[i]);
            }

            PyMem_FREE(garbage);
            Py_DECREF(seq);

            return 0;
        }
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "list indices must be integers, not %.200s",
                     item->ob_type->tp_name);
        return -1;
    }
}

static PyMappingMethods list_as_mapping = {
    (lenfunc)list_length,
    (binaryfunc)list_subscript,
    (objobjargproc)list_ass_subscript
};

PyTypeObject PyList_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "list",
    sizeof(PyListObject),
    0,
    (destructor)list_dealloc,
    (printfunc)list_print,
    0,
    0,
    0,
    (reprfunc)list_repr,
    0,
    &list_as_sequence,
    &list_as_mapping,
    (hashfunc)PyObject_HashNotImplemented,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_LIST_SUBCLASS,
    list_doc,
    (traverseproc)list_traverse,
    (inquiry)list_clear,
    list_richcompare,
    0,
    list_iter,
    0,
    list_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    (initproc)list_init,
    PyType_GenericAlloc,
    PyType_GenericNew,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    long it_index;
    PyListObject *it_seq;
} listiterobject;

static PyObject *list_iter(PyObject *);
static void listiter_dealloc(listiterobject *);
static int listiter_traverse(listiterobject *, visitproc, void *);
static PyObject *listiter_next(listiterobject *);
static PyObject *listiter_len(listiterobject *);

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyMethodDef listiter_methods[] = {
    {"__length_hint__", (PyCFunction)listiter_len, METH_NOARGS, length_hint_doc},
    {NULL,              NULL}
};

PyTypeObject PyListIter_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "listiterator",
    sizeof(listiterobject),
    0,

    (destructor)listiter_dealloc,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    0,
    (traverseproc)listiter_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)listiter_next,
    listiter_methods,
    0,
};

static PyObject *
list_iter(PyObject *seq)
{
    listiterobject *it;

    if (!PyList_Check(seq)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    it = PyObject_GC_New(listiterobject, &PyListIter_Type);
    if (it == NULL)
        return NULL;
    it->it_index = 0;
    Py_INCREF(seq);
    it->it_seq = (PyListObject *)seq;
    _PyObject_GC_TRACK(it);
    return (PyObject *)it;
}

static void
listiter_dealloc(listiterobject *it)
{
    _PyObject_GC_UNTRACK(it);
    Py_XDECREF(it->it_seq);
    PyObject_GC_Del(it);
}

static int
listiter_traverse(listiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->it_seq);
    return 0;
}

static PyObject *
listiter_next(listiterobject *it)
{
    PyListObject *seq;
    PyObject *item;

    assert(it != NULL);
    seq = it->it_seq;
    if (seq == NULL)
        return NULL;
    assert(PyList_Check(seq));

    if (it->it_index < PyList_GET_SIZE(seq)) {
        item = PyList_GET_ITEM(seq, it->it_index);
        ++it->it_index;
        Py_INCREF(item);
        return item;
    }

    it->it_seq = NULL;
    Py_DECREF(seq);
    return NULL;
}

static PyObject *
listiter_len(listiterobject *it)
{
    Py_ssize_t len;
    if (it->it_seq) {
        len = PyList_GET_SIZE(it->it_seq) - it->it_index;
        if (len >= 0)
            return PyInt_FromSsize_t(len);
    }
    return PyInt_FromLong(0);
}

typedef struct {
    PyObject_HEAD
    Py_ssize_t it_index;
    PyListObject *it_seq;
} listreviterobject;

static PyObject *list_reversed(PyListObject *, PyObject *);
static void listreviter_dealloc(listreviterobject *);
static int listreviter_traverse(listreviterobject *, visitproc, void *);
static PyObject *listreviter_next(listreviterobject *);
static PyObject *listreviter_len(listreviterobject *);

static PyMethodDef listreviter_methods[] = {
    {"__length_hint__", (PyCFunction)listreviter_len, METH_NOARGS, length_hint_doc},
    {NULL,              NULL}
};

PyTypeObject PyListRevIter_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "listreverseiterator",
    sizeof(listreviterobject),
    0,

    (destructor)listreviter_dealloc,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    0,
    (traverseproc)listreviter_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)listreviter_next,
    listreviter_methods,
    0,
};

static PyObject *
list_reversed(PyListObject *seq, PyObject *unused)
{
    listreviterobject *it;

    it = PyObject_GC_New(listreviterobject, &PyListRevIter_Type);
    if (it == NULL)
        return NULL;
    assert(PyList_Check(seq));
    it->it_index = PyList_GET_SIZE(seq) - 1;
    Py_INCREF(seq);
    it->it_seq = seq;
    PyObject_GC_Track(it);
    return (PyObject *)it;
}

static void
listreviter_dealloc(listreviterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_XDECREF(it->it_seq);
    PyObject_GC_Del(it);
}

static int
listreviter_traverse(listreviterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->it_seq);
    return 0;
}

static PyObject *
listreviter_next(listreviterobject *it)
{
    PyObject *item;
    Py_ssize_t index;
    PyListObject *seq;

    assert(it != NULL);
    seq = it->it_seq;
    if (seq == NULL) {
        return NULL;
    }
    assert(PyList_Check(seq));

    index = it->it_index;
    if (index>=0 && index < PyList_GET_SIZE(seq)) {
        item = PyList_GET_ITEM(seq, index);
        it->it_index--;
        Py_INCREF(item);
        return item;
    }
    it->it_index = -1;
    it->it_seq = NULL;
    Py_DECREF(seq);
    return NULL;
}

static PyObject *
listreviter_len(listreviterobject *it)
{
    Py_ssize_t len = it->it_index + 1;
    if (it->it_seq == NULL || PyList_GET_SIZE(it->it_seq) < len)
        len = 0;
    return PyLong_FromSsize_t(len);
}
