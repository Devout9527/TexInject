#include "Python.h"

#ifndef PyTuple_MAXSAVESIZE
#define PyTuple_MAXSAVESIZE     20
#endif
#ifndef PyTuple_MAXFREELIST
#define PyTuple_MAXFREELIST  2000
#endif

#if PyTuple_MAXSAVESIZE > 0

static PyTupleObject *free_list[PyTuple_MAXSAVESIZE];
static int numfree[PyTuple_MAXSAVESIZE];
#endif
#ifdef COUNT_ALLOCS
Py_ssize_t fast_tuple_allocs;
Py_ssize_t tuple_zero_allocs;
#endif

#ifdef SHOW_TRACK_COUNT
static Py_ssize_t count_untracked = 0;
static Py_ssize_t count_tracked = 0;

static void
show_track(void)
{
    fprintf(stderr, "Tuples created: %" PY_FORMAT_SIZE_T "d\n",
        count_tracked + count_untracked);
    fprintf(stderr, "Tuples tracked by the GC: %" PY_FORMAT_SIZE_T
        "d\n", count_tracked);
    fprintf(stderr, "%.2f%% tuple tracking rate\n\n",
        (100.0*count_tracked/(count_untracked+count_tracked)));
}
#endif

PyObject *
PyTuple_New(register Py_ssize_t size)
{
    register PyTupleObject *op;
    Py_ssize_t i;
    if (size < 0) {
        PyErr_BadInternalCall();
        return NULL;
    }
#if PyTuple_MAXSAVESIZE > 0
    if (size == 0 && free_list[0]) {
        op = free_list[0];
        Py_INCREF(op);
#ifdef COUNT_ALLOCS
        tuple_zero_allocs++;
#endif
        return (PyObject *) op;
    }
    if (size < PyTuple_MAXSAVESIZE && (op = free_list[size]) != NULL) {
        free_list[size] = (PyTupleObject *) op->ob_item[0];
        numfree[size]--;
#ifdef COUNT_ALLOCS
        fast_tuple_allocs++;
#endif

#ifdef Py_TRACE_REFS
        Py_SIZE(op) = size;
        Py_TYPE(op) = &PyTuple_Type;
#endif
        _Py_NewReference((PyObject *)op);
    }
    else
#endif
    {
        Py_ssize_t nbytes = size * sizeof(PyObject *);

        if (nbytes / sizeof(PyObject *) != (size_t)size ||
            (nbytes > PY_SSIZE_T_MAX - sizeof(PyTupleObject) - sizeof(PyObject *)))
        {
            return PyErr_NoMemory();
        }

        op = PyObject_GC_NewVar(PyTupleObject, &PyTuple_Type, size);
        if (op == NULL)
            return NULL;
    }
    for (i=0; i < size; i++)
        op->ob_item[i] = NULL;
#if PyTuple_MAXSAVESIZE > 0
    if (size == 0) {
        free_list[0] = op;
        ++numfree[0];
        Py_INCREF(op);
    }
#endif
#ifdef SHOW_TRACK_COUNT
    count_tracked++;
#endif
    _PyObject_GC_TRACK(op);
    return (PyObject *) op;
}

Py_ssize_t
PyTuple_Size(register PyObject *op)
{
    if (!PyTuple_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    else
        return Py_SIZE(op);
}

PyObject *
PyTuple_GetItem(register PyObject *op, register Py_ssize_t i)
{
    if (!PyTuple_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (i < 0 || i >= Py_SIZE(op)) {
        PyErr_SetString(PyExc_IndexError, "tuple index out of range");
        return NULL;
    }
    return ((PyTupleObject *)op) -> ob_item[i];
}

int
PyTuple_SetItem(register PyObject *op, register Py_ssize_t i, PyObject *newitem)
{
    register PyObject *olditem;
    register PyObject **p;
    if (!PyTuple_Check(op) || op->ob_refcnt != 1) {
        Py_XDECREF(newitem);
        PyErr_BadInternalCall();
        return -1;
    }
    if (i < 0 || i >= Py_SIZE(op)) {
        Py_XDECREF(newitem);
        PyErr_SetString(PyExc_IndexError,
                        "tuple assignment index out of range");
        return -1;
    }
    p = ((PyTupleObject *)op) -> ob_item + i;
    olditem = *p;
    *p = newitem;
    Py_XDECREF(olditem);
    return 0;
}

void
_PyTuple_MaybeUntrack(PyObject *op)
{
    PyTupleObject *t;
    Py_ssize_t i, n;

    if (!PyTuple_CheckExact(op) || !_PyObject_GC_IS_TRACKED(op))
        return;
    t = (PyTupleObject *) op;
    n = Py_SIZE(t);
    for (i = 0; i < n; i++) {
        PyObject *elt = PyTuple_GET_ITEM(t, i);

        if (!elt ||
            _PyObject_GC_MAY_BE_TRACKED(elt))
            return;
    }
#ifdef SHOW_TRACK_COUNT
    count_tracked--;
    count_untracked++;
#endif
    _PyObject_GC_UNTRACK(op);
}

PyObject *
PyTuple_Pack(Py_ssize_t n, ...)
{
    Py_ssize_t i;
    PyObject *o;
    PyObject *result;
    PyObject **items;
    va_list vargs;

    va_start(vargs, n);
    result = PyTuple_New(n);
    if (result == NULL) {
        va_end(vargs);
        return NULL;
    }
    items = ((PyTupleObject *)result)->ob_item;
    for (i = 0; i < n; i++) {
        o = va_arg(vargs, PyObject *);
        Py_INCREF(o);
        items[i] = o;
    }
    va_end(vargs);
    return result;
}

static void
tupledealloc(register PyTupleObject *op)
{
    register Py_ssize_t i;
    register Py_ssize_t len =  Py_SIZE(op);
    PyObject_GC_UnTrack(op);
    Py_TRASHCAN_SAFE_BEGIN(op)
    if (len > 0) {
        i = len;
        while (--i >= 0)
            Py_XDECREF(op->ob_item[i]);
#if PyTuple_MAXSAVESIZE > 0
        if (len < PyTuple_MAXSAVESIZE &&
            numfree[len] < PyTuple_MAXFREELIST &&
            Py_TYPE(op) == &PyTuple_Type)
        {
            op->ob_item[0] = (PyObject *) free_list[len];
            numfree[len]++;
            free_list[len] = op;
            goto done;
        }
#endif
    }
    Py_TYPE(op)->tp_free((PyObject *)op);
done:
    Py_TRASHCAN_SAFE_END(op)
}

static int
tupleprint(PyTupleObject *op, FILE *fp, int flags)
{
    Py_ssize_t i;
    Py_BEGIN_ALLOW_THREADS
    fprintf(fp, "(");
    Py_END_ALLOW_THREADS
    for (i = 0; i < Py_SIZE(op); i++) {
        if (i > 0) {
            Py_BEGIN_ALLOW_THREADS
            fprintf(fp, ", ");
            Py_END_ALLOW_THREADS
        }
        if (PyObject_Print(op->ob_item[i], fp, 0) != 0)
            return -1;
    }
    i = Py_SIZE(op);
    Py_BEGIN_ALLOW_THREADS
    if (i == 1)
        fprintf(fp, ",");
    fprintf(fp, ")");
    Py_END_ALLOW_THREADS
    return 0;
}

static PyObject *
tuplerepr(PyTupleObject *v)
{
    Py_ssize_t i, n;
    PyObject *s, *temp;
    PyObject *pieces, *result = NULL;

    n = Py_SIZE(v);
    if (n == 0)
        return PyString_FromString("()");

    i = Py_ReprEnter((PyObject *)v);
    if (i != 0) {
        return i > 0 ? PyString_FromString("(...)") : NULL;
    }

    pieces = PyTuple_New(n);
    if (pieces == NULL)
        return NULL;

    for (i = 0; i < n; ++i) {
        if (Py_EnterRecursiveCall(" while getting the repr of a tuple"))
            goto Done;
        s = PyObject_Repr(v->ob_item[i]);
        Py_LeaveRecursiveCall();
        if (s == NULL)
            goto Done;
        PyTuple_SET_ITEM(pieces, i, s);
    }

    assert(n > 0);
    s = PyString_FromString("(");
    if (s == NULL)
        goto Done;
    temp = PyTuple_GET_ITEM(pieces, 0);
    PyString_ConcatAndDel(&s, temp);
    PyTuple_SET_ITEM(pieces, 0, s);
    if (s == NULL)
        goto Done;

    s = PyString_FromString(n == 1 ? ",)" : ")");
    if (s == NULL)
        goto Done;
    temp = PyTuple_GET_ITEM(pieces, n-1);
    PyString_ConcatAndDel(&temp, s);
    PyTuple_SET_ITEM(pieces, n-1, temp);
    if (temp == NULL)
        goto Done;

    s = PyString_FromString(", ");
    if (s == NULL)
        goto Done;
    result = _PyString_Join(s, pieces);
    Py_DECREF(s);

Done:
    Py_DECREF(pieces);
    Py_ReprLeave((PyObject *)v);
    return result;
}

static long
tuplehash(PyTupleObject *v)
{
    register long x, y;
    register Py_ssize_t len = Py_SIZE(v);
    register PyObject **p;
    long mult = 1000003L;
    x = 0x345678L;
    p = v->ob_item;
    while (--len >= 0) {
        y = PyObject_Hash(*p++);
        if (y == -1)
            return -1;
        x = (x ^ y) * mult;

        mult += (long)(82520L + len + len);
    }
    x += 97531L;
    if (x == -1)
        x = -2;
    return x;
}

static Py_ssize_t
tuplelength(PyTupleObject *a)
{
    return Py_SIZE(a);
}

static int
tuplecontains(PyTupleObject *a, PyObject *el)
{
    Py_ssize_t i;
    int cmp;

    for (i = 0, cmp = 0 ; cmp == 0 && i < Py_SIZE(a); ++i)
        cmp = PyObject_RichCompareBool(el, PyTuple_GET_ITEM(a, i),
                                           Py_EQ);
    return cmp;
}

static PyObject *
tupleitem(register PyTupleObject *a, register Py_ssize_t i)
{
    if (i < 0 || i >= Py_SIZE(a)) {
        PyErr_SetString(PyExc_IndexError, "tuple index out of range");
        return NULL;
    }
    Py_INCREF(a->ob_item[i]);
    return a->ob_item[i];
}

static PyObject *
tupleslice(register PyTupleObject *a, register Py_ssize_t ilow,
           register Py_ssize_t ihigh)
{
    register PyTupleObject *np;
    PyObject **src, **dest;
    register Py_ssize_t i;
    Py_ssize_t len;
    if (ilow < 0)
        ilow = 0;
    if (ihigh > Py_SIZE(a))
        ihigh = Py_SIZE(a);
    if (ihigh < ilow)
        ihigh = ilow;
    if (ilow == 0 && ihigh == Py_SIZE(a) && PyTuple_CheckExact(a)) {
        Py_INCREF(a);
        return (PyObject *)a;
    }
    len = ihigh - ilow;
    np = (PyTupleObject *)PyTuple_New(len);
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
PyTuple_GetSlice(PyObject *op, Py_ssize_t i, Py_ssize_t j)
{
    if (op == NULL || !PyTuple_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return tupleslice((PyTupleObject *)op, i, j);
}

static PyObject *
tupleconcat(register PyTupleObject *a, register PyObject *bb)
{
    register Py_ssize_t size;
    register Py_ssize_t i;
    PyObject **src, **dest;
    PyTupleObject *np;
    if (!PyTuple_Check(bb)) {
        PyErr_Format(PyExc_TypeError,
             "can only concatenate tuple (not \"%.200s\") to tuple",
                 Py_TYPE(bb)->tp_name);
        return NULL;
    }
#define b ((PyTupleObject *)bb)
    size = Py_SIZE(a) + Py_SIZE(b);
    if (size < 0)
        return PyErr_NoMemory();
    np = (PyTupleObject *) PyTuple_New(size);
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
tuplerepeat(PyTupleObject *a, Py_ssize_t n)
{
    Py_ssize_t i, j;
    Py_ssize_t size;
    PyTupleObject *np;
    PyObject **p, **items;
    if (n < 0)
        n = 0;
    if (Py_SIZE(a) == 0 || n == 1) {
        if (PyTuple_CheckExact(a)) {

            Py_INCREF(a);
            return (PyObject *)a;
        }
        if (Py_SIZE(a) == 0)
            return PyTuple_New(0);
    }
    size = Py_SIZE(a) * n;
    if (size/Py_SIZE(a) != n)
        return PyErr_NoMemory();
    np = (PyTupleObject *) PyTuple_New(size);
    if (np == NULL)
        return NULL;
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

static PyObject *
tupleindex(PyTupleObject *self, PyObject *args)
{
    Py_ssize_t i, start=0, stop=Py_SIZE(self);
    PyObject *v;

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
    PyErr_SetString(PyExc_ValueError, "tuple.index(x): x not in tuple");
    return NULL;
}

static PyObject *
tuplecount(PyTupleObject *self, PyObject *v)
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

static int
tupletraverse(PyTupleObject *o, visitproc visit, void *arg)
{
    Py_ssize_t i;

    for (i = Py_SIZE(o); --i >= 0; )
        Py_VISIT(o->ob_item[i]);
    return 0;
}

static PyObject *
tuplerichcompare(PyObject *v, PyObject *w, int op)
{
    PyTupleObject *vt, *wt;
    Py_ssize_t i;
    Py_ssize_t vlen, wlen;

    if (!PyTuple_Check(v) || !PyTuple_Check(w)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    vt = (PyTupleObject *)v;
    wt = (PyTupleObject *)w;

    vlen = Py_SIZE(vt);
    wlen = Py_SIZE(wt);

    for (i = 0; i < vlen && i < wlen; i++) {
        int k = PyObject_RichCompareBool(vt->ob_item[i],
                                         wt->ob_item[i], Py_EQ);
        if (k < 0)
            return NULL;
        if (!k)
            break;
    }

    if (i >= vlen || i >= wlen) {

        int cmp;
        PyObject *res;
        switch (op) {
        case Py_LT: cmp = vlen <  wlen; break;
        case Py_LE: cmp = vlen <= wlen; break;
        case Py_EQ: cmp = vlen == wlen; break;
        case Py_NE: cmp = vlen != wlen; break;
        case Py_GT: cmp = vlen >  wlen; break;
        case Py_GE: cmp = vlen >= wlen; break;
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

    return PyObject_RichCompare(vt->ob_item[i], wt->ob_item[i], op);
}

static PyObject *
tuple_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static PyObject *
tuple_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *arg = NULL;
    static char *kwlist[] = {"sequence", 0};

    if (type != &PyTuple_Type)
        return tuple_subtype_new(type, args, kwds);
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:tuple", kwlist, &arg))
        return NULL;

    if (arg == NULL)
        return PyTuple_New(0);
    else
        return PySequence_Tuple(arg);
}

static PyObject *
tuple_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *tmp, *newobj, *item;
    Py_ssize_t i, n;

    assert(PyType_IsSubtype(type, &PyTuple_Type));
    tmp = tuple_new(&PyTuple_Type, args, kwds);
    if (tmp == NULL)
        return NULL;
    assert(PyTuple_Check(tmp));
    newobj = type->tp_alloc(type, n = PyTuple_GET_SIZE(tmp));
    if (newobj == NULL)
        return NULL;
    for (i = 0; i < n; i++) {
        item = PyTuple_GET_ITEM(tmp, i);
        Py_INCREF(item);
        PyTuple_SET_ITEM(newobj, i, item);
    }
    Py_DECREF(tmp);
    return newobj;
}

PyDoc_STRVAR(tuple_doc,
"tuple() -> empty tuple\n\
tuple(iterable) -> tuple initialized from iterable's items\n\
\n\
If the argument is a tuple, the return value is the same object.");

static PySequenceMethods tuple_as_sequence = {
    (lenfunc)tuplelength,
    (binaryfunc)tupleconcat,
    (ssizeargfunc)tuplerepeat,
    (ssizeargfunc)tupleitem,
    (ssizessizeargfunc)tupleslice,
    0,
    0,
    (objobjproc)tuplecontains,
};

static PyObject*
tuplesubscript(PyTupleObject* self, PyObject* item)
{
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        if (i < 0)
            i += PyTuple_GET_SIZE(self);
        return tupleitem(self, i);
    }
    else if (PySlice_Check(item)) {
        Py_ssize_t start, stop, step, slicelength, cur, i;
        PyObject* result;
        PyObject* it;
        PyObject **src, **dest;

        if (PySlice_GetIndicesEx((PySliceObject*)item,
                         PyTuple_GET_SIZE(self),
                         &start, &stop, &step, &slicelength) < 0) {
            return NULL;
        }

        if (slicelength <= 0) {
            return PyTuple_New(0);
        }
        else if (start == 0 && step == 1 &&
                 slicelength == PyTuple_GET_SIZE(self) &&
                 PyTuple_CheckExact(self)) {
            Py_INCREF(self);
            return (PyObject *)self;
        }
        else {
            result = PyTuple_New(slicelength);
            if (!result) return NULL;

            src = self->ob_item;
            dest = ((PyTupleObject *)result)->ob_item;
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
                     "tuple indices must be integers, not %.200s",
                     Py_TYPE(item)->tp_name);
        return NULL;
    }
}

static PyObject *
tuple_getnewargs(PyTupleObject *v)
{
    return Py_BuildValue("(N)", tupleslice(v, 0, Py_SIZE(v)));

}

PyDoc_STRVAR(index_doc,
"T.index(value, [start, [stop]]) -> integer -- return first index of value.\n"
"Raises ValueError if the value is not present."
);
PyDoc_STRVAR(count_doc,
"T.count(value) -> integer -- return number of occurrences of value");

static PyMethodDef tuple_methods[] = {
    {"__getnewargs__",          (PyCFunction)tuple_getnewargs,  METH_NOARGS},
    {"index",           (PyCFunction)tupleindex,  METH_VARARGS, index_doc},
    {"count",           (PyCFunction)tuplecount,  METH_O, count_doc},
    {NULL,              NULL}
};

static PyMappingMethods tuple_as_mapping = {
    (lenfunc)tuplelength,
    (binaryfunc)tuplesubscript,
    0
};

static PyObject *tuple_iter(PyObject *seq);

PyTypeObject PyTuple_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "tuple",
    sizeof(PyTupleObject) - sizeof(PyObject *),
    sizeof(PyObject *),
    (destructor)tupledealloc,
    (printfunc)tupleprint,
    0,
    0,
    0,
    (reprfunc)tuplerepr,
    0,
    &tuple_as_sequence,
    &tuple_as_mapping,
    (hashfunc)tuplehash,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_TUPLE_SUBCLASS,
    tuple_doc,
    (traverseproc)tupletraverse,
    0,
    tuplerichcompare,
    0,
    tuple_iter,
    0,
    tuple_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    tuple_new,
    PyObject_GC_Del,
};

int
_PyTuple_Resize(PyObject **pv, Py_ssize_t newsize)
{
    register PyTupleObject *v;
    register PyTupleObject *sv;
    Py_ssize_t i;
    Py_ssize_t oldsize;

    v = (PyTupleObject *) *pv;
    if (v == NULL || Py_TYPE(v) != &PyTuple_Type ||
        (Py_SIZE(v) != 0 && Py_REFCNT(v) != 1)) {
        *pv = 0;
        Py_XDECREF(v);
        PyErr_BadInternalCall();
        return -1;
    }
    oldsize = Py_SIZE(v);
    if (oldsize == newsize)
        return 0;

    if (oldsize == 0) {

        Py_DECREF(v);
        *pv = PyTuple_New(newsize);
        return *pv == NULL ? -1 : 0;
    }

    _Py_DEC_REFTOTAL;
    if (_PyObject_GC_IS_TRACKED(v))
        _PyObject_GC_UNTRACK(v);
    _Py_ForgetReference((PyObject *) v);

    for (i = newsize; i < oldsize; i++) {
        Py_CLEAR(v->ob_item[i]);
    }
    sv = PyObject_GC_Resize(PyTupleObject, v, newsize);
    if (sv == NULL) {
        *pv = NULL;
        PyObject_GC_Del(v);
        return -1;
    }
    _Py_NewReference((PyObject *) sv);

    if (newsize > oldsize)
        memset(&sv->ob_item[oldsize], 0,
               sizeof(*sv->ob_item) * (newsize - oldsize));
    *pv = (PyObject *) sv;
    _PyObject_GC_TRACK(sv);
    return 0;
}

int
PyTuple_ClearFreeList(void)
{
    int freelist_size = 0;
#if PyTuple_MAXSAVESIZE > 0
    int i;
    for (i = 1; i < PyTuple_MAXSAVESIZE; i++) {
        PyTupleObject *p, *q;
        p = free_list[i];
        freelist_size += numfree[i];
        free_list[i] = NULL;
        numfree[i] = 0;
        while (p) {
            q = p;
            p = (PyTupleObject *)(p->ob_item[0]);
            PyObject_GC_Del(q);
        }
    }
#endif
    return freelist_size;
}

void
PyTuple_Fini(void)
{
#if PyTuple_MAXSAVESIZE > 0

    Py_CLEAR(free_list[0]);

    (void)PyTuple_ClearFreeList();
#endif
#ifdef SHOW_TRACK_COUNT
    show_track();
#endif
}

typedef struct {
    PyObject_HEAD
    long it_index;
    PyTupleObject *it_seq;
} tupleiterobject;

static void
tupleiter_dealloc(tupleiterobject *it)
{
    _PyObject_GC_UNTRACK(it);
    Py_XDECREF(it->it_seq);
    PyObject_GC_Del(it);
}

static int
tupleiter_traverse(tupleiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->it_seq);
    return 0;
}

static PyObject *
tupleiter_next(tupleiterobject *it)
{
    PyTupleObject *seq;
    PyObject *item;

    assert(it != NULL);
    seq = it->it_seq;
    if (seq == NULL)
        return NULL;
    assert(PyTuple_Check(seq));

    if (it->it_index < PyTuple_GET_SIZE(seq)) {
        item = PyTuple_GET_ITEM(seq, it->it_index);
        ++it->it_index;
        Py_INCREF(item);
        return item;
    }

    it->it_seq = NULL;
    Py_DECREF(seq);
    return NULL;
}

static PyObject *
tupleiter_len(tupleiterobject *it)
{
    Py_ssize_t len = 0;
    if (it->it_seq)
        len = PyTuple_GET_SIZE(it->it_seq) - it->it_index;
    return PyInt_FromSsize_t(len);
}

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyMethodDef tupleiter_methods[] = {
    {"__length_hint__", (PyCFunction)tupleiter_len, METH_NOARGS, length_hint_doc},
    {NULL,              NULL}
};

PyTypeObject PyTupleIter_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "tupleiterator",
    sizeof(tupleiterobject),
    0,

    (destructor)tupleiter_dealloc,
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
    (traverseproc)tupleiter_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)tupleiter_next,
    tupleiter_methods,
    0,
};

static PyObject *
tuple_iter(PyObject *seq)
{
    tupleiterobject *it;

    if (!PyTuple_Check(seq)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    it = PyObject_GC_New(tupleiterobject, &PyTupleIter_Type);
    if (it == NULL)
        return NULL;
    it->it_index = 0;
    Py_INCREF(seq);
    it->it_seq = (PyTupleObject *)seq;
    _PyObject_GC_TRACK(it);
    return (PyObject *)it;
}
