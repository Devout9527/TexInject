#include "Python.h"

typedef struct {
    PyObject_HEAD
    long        start;
    long        step;
    long        len;
} rangeobject;

static unsigned long
get_len_of_range(long lo, long hi, long step)
{

    assert(step != 0);
    if (step > 0 && lo < hi)
        return 1UL + (hi - 1UL - lo) / step;
    else if (step < 0 && lo > hi)
        return 1UL + (lo - 1UL - hi) / (0UL - step);
    else
        return 0UL;
}

static long
get_stop_for_range(rangeobject *r)
{
    long last;

    if (r->len == 0)
        return r->start;

    last = (long)(r->start + (unsigned long)(r->len - 1) * r->step);
    if (r->step > 0)
        return last > LONG_MAX - r->step ? LONG_MAX : last + r->step;
    else
        return last < LONG_MIN - r->step ? LONG_MIN : last + r->step;
}

static PyObject *
range_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    rangeobject *obj;
    long ilow = 0, ihigh = 0, istep = 1;
    unsigned long n;

    if (!_PyArg_NoKeywords("xrange()", kw))
        return NULL;

    if (PyTuple_Size(args) <= 1) {
        if (!PyArg_ParseTuple(args,
                        "l;xrange() requires 1-3 int arguments",
                        &ihigh))
            return NULL;
    }
    else {
        if (!PyArg_ParseTuple(args,
                        "ll|l;xrange() requires 1-3 int arguments",
                        &ilow, &ihigh, &istep))
            return NULL;
    }
    if (istep == 0) {
        PyErr_SetString(PyExc_ValueError, "xrange() arg 3 must not be zero");
        return NULL;
    }
    n = get_len_of_range(ilow, ihigh, istep);
    if (n > (unsigned long)LONG_MAX || (long)n > PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError,
                        "xrange() result has too many items");
        return NULL;
    }

    obj = PyObject_New(rangeobject, &PyRange_Type);
    if (obj == NULL)
        return NULL;
    obj->start = ilow;
    obj->len   = (long)n;
    obj->step  = istep;
    return (PyObject *) obj;
}

PyDoc_STRVAR(range_doc,
"xrange(stop) -> xrange object\n\
xrange(start, stop[, step]) -> xrange object\n\
\n\
Like range(), but instead of returning a list, returns an object that\n\
generates the numbers in the range on demand.  For looping, this is \n\
slightly faster than range() and more memory efficient.");

static PyObject *
range_item(rangeobject *r, Py_ssize_t i)
{
    if (i < 0 || i >= r->len) {
        PyErr_SetString(PyExc_IndexError,
                        "xrange object index out of range");
        return NULL;
    }

    return PyInt_FromLong((long)(r->start + (unsigned long)i * r->step));
}

static Py_ssize_t
range_length(rangeobject *r)
{
    return (Py_ssize_t)(r->len);
}

static PyObject *
range_repr(rangeobject *r)
{
    PyObject *rtn;

    if (r->start == 0 && r->step == 1)
        rtn = PyString_FromFormat("xrange(%ld)",
                                  get_stop_for_range(r));

    else if (r->step == 1)
        rtn = PyString_FromFormat("xrange(%ld, %ld)",
                                  r->start,
                                  get_stop_for_range(r));

    else
        rtn = PyString_FromFormat("xrange(%ld, %ld, %ld)",
                                  r->start,
                                  get_stop_for_range(r),
                                  r->step);
    return rtn;
}

static PyObject *
range_reduce(rangeobject *r, PyObject *args)
{
    return Py_BuildValue("(O(lll))", Py_TYPE(r),
                         r->start,
                         get_stop_for_range(r),
                         r->step);
}

static PySequenceMethods range_as_sequence = {
    (lenfunc)range_length,
    0,
    0,
    (ssizeargfunc)range_item,
    0,
};

static PyObject * range_iter(PyObject *seq);
static PyObject * range_reverse(PyObject *seq);

PyDoc_STRVAR(reverse_doc,
"Returns a reverse iterator.");

static PyMethodDef range_methods[] = {
    {"__reversed__",            (PyCFunction)range_reverse, METH_NOARGS, reverse_doc},
    {"__reduce__",              (PyCFunction)range_reduce, METH_VARARGS},
    {NULL,              NULL}
};

PyTypeObject PyRange_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "xrange",
    sizeof(rangeobject),
    0,
    (destructor)PyObject_Del,
    0,
    0,
    0,
    0,
    (reprfunc)range_repr,
    0,
    &range_as_sequence,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT,
    range_doc,
    0,
    0,
    0,
    0,
    range_iter,
    0,
    range_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    range_new,
};

typedef struct {
    PyObject_HEAD
    long        index;
    long        start;
    long        step;
    long        len;
} rangeiterobject;

static PyObject *
rangeiter_next(rangeiterobject *r)
{
    if (r->index < r->len)
        return PyInt_FromLong(r->start + (r->index++) * r->step);
    return NULL;
}

static PyObject *
rangeiter_len(rangeiterobject *r)
{
    return PyInt_FromLong(r->len - r->index);
}

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyMethodDef rangeiter_methods[] = {
    {"__length_hint__", (PyCFunction)rangeiter_len, METH_NOARGS, length_hint_doc},
    {NULL,              NULL}
};

static PyTypeObject Pyrangeiter_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "rangeiterator",
    sizeof(rangeiterobject),
    0,

    (destructor)PyObject_Del,
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
    Py_TPFLAGS_DEFAULT,
    0,
    0,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)rangeiter_next,
    rangeiter_methods,
    0,
};

static PyObject *
range_iter(PyObject *seq)
{
    rangeiterobject *it;

    if (!PyRange_Check(seq)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    it = PyObject_New(rangeiterobject, &Pyrangeiter_Type);
    if (it == NULL)
        return NULL;
    it->index = 0;
    it->start = ((rangeobject *)seq)->start;
    it->step = ((rangeobject *)seq)->step;
    it->len = ((rangeobject *)seq)->len;
    return (PyObject *)it;
}

static PyObject *
range_reverse(PyObject *seq)
{
    rangeiterobject *it;
    long start, step, len;

    if (!PyRange_Check(seq)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    it = PyObject_New(rangeiterobject, &Pyrangeiter_Type);
    if (it == NULL)
        return NULL;

    start = ((rangeobject *)seq)->start;
    step = ((rangeobject *)seq)->step;
    len = ((rangeobject *)seq)->len;

    it->index = 0;
    it->len = len;

    it->start = (long)(start + (unsigned long)(len-1) * step);
    it->step = (long)(0UL-step);

    return (PyObject *)it;
}
