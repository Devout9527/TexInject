#include "Python.h"

typedef struct {
    PyObject_HEAD
    long      it_index;
    PyObject *it_seq;
} seqiterobject;

PyObject *
PySeqIter_New(PyObject *seq)
{
    seqiterobject *it;

    if (!PySequence_Check(seq)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    it = PyObject_GC_New(seqiterobject, &PySeqIter_Type);
    if (it == NULL)
        return NULL;
    it->it_index = 0;
    Py_INCREF(seq);
    it->it_seq = seq;
    _PyObject_GC_TRACK(it);
    return (PyObject *)it;
}

static void
iter_dealloc(seqiterobject *it)
{
    _PyObject_GC_UNTRACK(it);
    Py_XDECREF(it->it_seq);
    PyObject_GC_Del(it);
}

static int
iter_traverse(seqiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->it_seq);
    return 0;
}

static PyObject *
iter_iternext(PyObject *iterator)
{
    seqiterobject *it;
    PyObject *seq;
    PyObject *result;

    assert(PySeqIter_Check(iterator));
    it = (seqiterobject *)iterator;
    seq = it->it_seq;
    if (seq == NULL)
        return NULL;
    if (it->it_index == LONG_MAX) {
        PyErr_SetString(PyExc_OverflowError,
                        "iter index too large");
        return NULL;
    }

    result = PySequence_GetItem(seq, it->it_index);
    if (result != NULL) {
        it->it_index++;
        return result;
    }
    if (PyErr_ExceptionMatches(PyExc_IndexError) ||
        PyErr_ExceptionMatches(PyExc_StopIteration))
    {
        PyErr_Clear();
        it->it_seq = NULL;
        Py_DECREF(seq);
    }
    return NULL;
}

static PyObject *
iter_len(seqiterobject *it)
{
    Py_ssize_t seqsize, len;

    if (it->it_seq) {
        seqsize = PySequence_Size(it->it_seq);
        if (seqsize == -1)
            return NULL;
        len = seqsize - it->it_index;
        if (len >= 0)
            return PyInt_FromSsize_t(len);
    }
    return PyInt_FromLong(0);
}

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyMethodDef seqiter_methods[] = {
    {"__length_hint__", (PyCFunction)iter_len, METH_NOARGS, length_hint_doc},
    {NULL,              NULL}
};

PyTypeObject PySeqIter_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "iterator",
    sizeof(seqiterobject),
    0,

    (destructor)iter_dealloc,
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
    (traverseproc)iter_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    iter_iternext,
    seqiter_methods,
    0,
};

typedef struct {
    PyObject_HEAD
    PyObject *it_callable;
    PyObject *it_sentinel;
} calliterobject;

PyObject *
PyCallIter_New(PyObject *callable, PyObject *sentinel)
{
    calliterobject *it;
    it = PyObject_GC_New(calliterobject, &PyCallIter_Type);
    if (it == NULL)
        return NULL;
    Py_INCREF(callable);
    it->it_callable = callable;
    Py_INCREF(sentinel);
    it->it_sentinel = sentinel;
    _PyObject_GC_TRACK(it);
    return (PyObject *)it;
}
static void
calliter_dealloc(calliterobject *it)
{
    _PyObject_GC_UNTRACK(it);
    Py_XDECREF(it->it_callable);
    Py_XDECREF(it->it_sentinel);
    PyObject_GC_Del(it);
}

static int
calliter_traverse(calliterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->it_callable);
    Py_VISIT(it->it_sentinel);
    return 0;
}

static PyObject *
calliter_iternext(calliterobject *it)
{
    if (it->it_callable != NULL) {
        PyObject *args = PyTuple_New(0);
        PyObject *result;
        if (args == NULL)
            return NULL;
        result = PyObject_Call(it->it_callable, args, NULL);
        Py_DECREF(args);
        if (result != NULL) {
            int ok;
            ok = PyObject_RichCompareBool(result,
                                          it->it_sentinel,
                                          Py_EQ);
            if (ok == 0)
                return result;
            Py_DECREF(result);
            if (ok > 0) {
                Py_CLEAR(it->it_callable);
                Py_CLEAR(it->it_sentinel);
            }
        }
        else if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
            PyErr_Clear();
            Py_CLEAR(it->it_callable);
            Py_CLEAR(it->it_sentinel);
        }
    }
    return NULL;
}

PyTypeObject PyCallIter_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "callable-iterator",
    sizeof(calliterobject),
    0,

    (destructor)calliter_dealloc,
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
    (traverseproc)calliter_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)calliter_iternext,
    0,
};
