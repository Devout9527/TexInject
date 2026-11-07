#include "Python.h"
#include "structmember.h"

static PyObject *
ellipsis_repr(PyObject *op)
{
    return PyString_FromString("Ellipsis");
}

PyTypeObject PyEllipsis_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "ellipsis",
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    ellipsis_repr,
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
};

PyObject _Py_EllipsisObject = {
    _PyObject_EXTRA_INIT
    1, &PyEllipsis_Type
};

PyObject *
PySlice_New(PyObject *start, PyObject *stop, PyObject *step)
{
    PySliceObject *obj = PyObject_GC_New(PySliceObject, &PySlice_Type);

    if (obj == NULL)
        return NULL;

    if (step == NULL) step = Py_None;
    Py_INCREF(step);
    if (start == NULL) start = Py_None;
    Py_INCREF(start);
    if (stop == NULL) stop = Py_None;
    Py_INCREF(stop);

    obj->step = step;
    obj->start = start;
    obj->stop = stop;

    _PyObject_GC_TRACK(obj);
    return (PyObject *) obj;
}

PyObject *
_PySlice_FromIndices(Py_ssize_t istart, Py_ssize_t istop)
{
    PyObject *start, *end, *slice;
    start = PyInt_FromSsize_t(istart);
    if (!start)
        return NULL;
    end = PyInt_FromSsize_t(istop);
    if (!end) {
        Py_DECREF(start);
        return NULL;
    }

    slice = PySlice_New(start, end, NULL);
    Py_DECREF(start);
    Py_DECREF(end);
    return slice;
}

int
PySlice_GetIndices(PySliceObject *r, Py_ssize_t length,
                   Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step)
{

    if (r->step == Py_None) {
        *step = 1;
    } else {
        if (!PyInt_Check(r->step) && !PyLong_Check(r->step)) return -1;
        *step = PyInt_AsSsize_t(r->step);
    }
    if (r->start == Py_None) {
        *start = *step < 0 ? length-1 : 0;
    } else {
        if (!PyInt_Check(r->start) && !PyLong_Check(r->step)) return -1;
        *start = PyInt_AsSsize_t(r->start);
        if (*start < 0) *start += length;
    }
    if (r->stop == Py_None) {
        *stop = *step < 0 ? -1 : length;
    } else {
        if (!PyInt_Check(r->stop) && !PyLong_Check(r->step)) return -1;
        *stop = PyInt_AsSsize_t(r->stop);
        if (*stop < 0) *stop += length;
    }
    if (*stop > length) return -1;
    if (*start >= length) return -1;
    if (*step == 0) return -1;
    return 0;
}

int
PySlice_GetIndicesEx(PySliceObject *r, Py_ssize_t length,
                     Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step, Py_ssize_t *slicelength)
{

    Py_ssize_t defstart, defstop;

    if (r->step == Py_None) {
        *step = 1;
    }
    else {
        if (!_PyEval_SliceIndex(r->step, step)) return -1;
        if (*step == 0) {
            PyErr_SetString(PyExc_ValueError,
                            "slice step cannot be zero");
            return -1;
        }
    }

    defstart = *step < 0 ? length-1 : 0;
    defstop = *step < 0 ? -1 : length;

    if (r->start == Py_None) {
        *start = defstart;
    }
    else {
        if (!_PyEval_SliceIndex(r->start, start)) return -1;
        if (*start < 0) *start += length;
        if (*start < 0) *start = (*step < 0) ? -1 : 0;
        if (*start >= length)
            *start = (*step < 0) ? length - 1 : length;
    }

    if (r->stop == Py_None) {
        *stop = defstop;
    }
    else {
        if (!_PyEval_SliceIndex(r->stop, stop)) return -1;
        if (*stop < 0) *stop += length;
        if (*stop < 0) *stop = (*step < 0) ? -1 : 0;
        if (*stop >= length)
            *stop = (*step < 0) ? length - 1 : length;
    }

    if ((*step < 0 && *stop >= *start)
        || (*step > 0 && *start >= *stop)) {
        *slicelength = 0;
    }
    else if (*step < 0) {
        *slicelength = (*stop-*start+1)/(*step)+1;
    }
    else {
        *slicelength = (*stop-*start-1)/(*step)+1;
    }

    return 0;
}

static PyObject *
slice_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    PyObject *start, *stop, *step;

    start = stop = step = NULL;

    if (!_PyArg_NoKeywords("slice()", kw))
        return NULL;

    if (!PyArg_UnpackTuple(args, "slice", 1, 3, &start, &stop, &step))
        return NULL;

    if (stop == NULL) {
        stop = start;
        start = NULL;
    }
    return PySlice_New(start, stop, step);
}

PyDoc_STRVAR(slice_doc,
"slice(stop)\n\
slice(start, stop[, step])\n\
\n\
Create a slice object.  This is used for extended slicing (e.g. a[0:10:2]).");

static void
slice_dealloc(PySliceObject *r)
{
    _PyObject_GC_UNTRACK(r);
    Py_DECREF(r->step);
    Py_DECREF(r->start);
    Py_DECREF(r->stop);
    PyObject_GC_Del(r);
}

static PyObject *
slice_repr(PySliceObject *r)
{
    PyObject *s, *comma;

    s = PyString_FromString("slice(");
    comma = PyString_FromString(", ");
    PyString_ConcatAndDel(&s, PyObject_Repr(r->start));
    PyString_Concat(&s, comma);
    PyString_ConcatAndDel(&s, PyObject_Repr(r->stop));
    PyString_Concat(&s, comma);
    PyString_ConcatAndDel(&s, PyObject_Repr(r->step));
    PyString_ConcatAndDel(&s, PyString_FromString(")"));
    Py_DECREF(comma);
    return s;
}

static PyMemberDef slice_members[] = {
    {"start", T_OBJECT, offsetof(PySliceObject, start), READONLY},
    {"stop", T_OBJECT, offsetof(PySliceObject, stop), READONLY},
    {"step", T_OBJECT, offsetof(PySliceObject, step), READONLY},
    {0}
};

static PyObject*
slice_indices(PySliceObject* self, PyObject* len)
{
    Py_ssize_t ilen, start, stop, step, slicelength;

    ilen = PyNumber_AsSsize_t(len, PyExc_OverflowError);

    if (ilen == -1 && PyErr_Occurred()) {
        return NULL;
    }

    if (PySlice_GetIndicesEx(self, ilen, &start, &stop,
                             &step, &slicelength) < 0) {
        return NULL;
    }

    return Py_BuildValue("(nnn)", start, stop, step);
}

PyDoc_STRVAR(slice_indices_doc,
"S.indices(len) -> (start, stop, stride)\n\
\n\
Assuming a sequence of length len, calculate the start and stop\n\
indices, and the stride length of the extended slice described by\n\
S. Out of bounds indices are clipped in a manner consistent with the\n\
handling of normal slices.");

static PyObject *
slice_reduce(PySliceObject* self)
{
    return Py_BuildValue("O(OOO)", Py_TYPE(self), self->start, self->stop, self->step);
}

PyDoc_STRVAR(reduce_doc, "Return state information for pickling.");

static PyMethodDef slice_methods[] = {
    {"indices",         (PyCFunction)slice_indices,
     METH_O,            slice_indices_doc},
    {"__reduce__",      (PyCFunction)slice_reduce,
     METH_NOARGS,       reduce_doc},
    {NULL, NULL}
};

static int
slice_compare(PySliceObject *v, PySliceObject *w)
{
    int result = 0;

    if (v == w)
        return 0;

    if (PyObject_Cmp(v->start, w->start, &result) < 0)
        return -2;
    if (result != 0)
        return result;
    if (PyObject_Cmp(v->stop, w->stop, &result) < 0)
        return -2;
    if (result != 0)
        return result;
    if (PyObject_Cmp(v->step, w->step, &result) < 0)
        return -2;
    return result;
}

static long
slice_hash(PySliceObject *v)
{
    PyErr_SetString(PyExc_TypeError, "unhashable type");
    return -1L;
}

static int
slice_traverse(PySliceObject *v, visitproc visit, void *arg)
{
    Py_VISIT(v->start);
    Py_VISIT(v->stop);
    Py_VISIT(v->step);
    return 0;
}

PyTypeObject PySlice_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "slice",
    sizeof(PySliceObject),
    0,
    (destructor)slice_dealloc,
    0,
    0,
    0,
    (cmpfunc)slice_compare,
    (reprfunc)slice_repr,
    0,
    0,
    0,
    (hashfunc)slice_hash,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    slice_doc,
    (traverseproc)slice_traverse,
    0,
    0,
    0,
    0,
    0,
    slice_methods,
    slice_members,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    slice_new,
};
