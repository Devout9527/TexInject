#include "Python.h"
#include "structmember.h"

static PyObject *
functools_reduce(PyObject *self, PyObject *args)
{
    PyObject *seq, *func, *result = NULL, *it;

    if (!PyArg_UnpackTuple(args, "reduce", 2, 3, &func, &seq, &result))
        return NULL;
    if (result != NULL)
        Py_INCREF(result);

    it = PyObject_GetIter(seq);
    if (it == NULL) {
        PyErr_SetString(PyExc_TypeError,
            "reduce() arg 2 must support iteration");
        Py_XDECREF(result);
        return NULL;
    }

    if ((args = PyTuple_New(2)) == NULL)
        goto Fail;

    for (;;) {
        PyObject *op2;

        if (args->ob_refcnt > 1) {
            Py_DECREF(args);
            if ((args = PyTuple_New(2)) == NULL)
                goto Fail;
        }

        op2 = PyIter_Next(it);
        if (op2 == NULL) {
            if (PyErr_Occurred())
                goto Fail;
            break;
        }

        if (result == NULL)
            result = op2;
        else {
            PyTuple_SetItem(args, 0, result);
            PyTuple_SetItem(args, 1, op2);
            if ((result = PyEval_CallObject(func, args)) == NULL)
                goto Fail;
        }
    }

    Py_DECREF(args);

    if (result == NULL)
        PyErr_SetString(PyExc_TypeError,
                   "reduce() of empty sequence with no initial value");

    Py_DECREF(it);
    return result;

Fail:
    Py_XDECREF(args);
    Py_XDECREF(result);
    Py_DECREF(it);
    return NULL;
}

PyDoc_STRVAR(reduce_doc,
"reduce(function, sequence[, initial]) -> value\n\
\n\
Apply a function of two arguments cumulatively to the items of a sequence,\n\
from left to right, so as to reduce the sequence to a single value.\n\
For example, reduce(lambda x, y: x+y, [1, 2, 3, 4, 5]) calculates\n\
((((1+2)+3)+4)+5).  If initial is present, it is placed before the items\n\
of the sequence in the calculation, and serves as a default when the\n\
sequence is empty.");

typedef struct {
    PyObject_HEAD
    PyObject *fn;
    PyObject *args;
    PyObject *kw;
    PyObject *dict;
    PyObject *weakreflist;
} partialobject;

static PyTypeObject partial_type;

static PyObject *
partial_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    PyObject *func;
    partialobject *pto;

    if (PyTuple_GET_SIZE(args) < 1) {
        PyErr_SetString(PyExc_TypeError,
                        "type 'partial' takes at least one argument");
        return NULL;
    }

    func = PyTuple_GET_ITEM(args, 0);
    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError,
                        "the first argument must be callable");
        return NULL;
    }

    pto = (partialobject *)type->tp_alloc(type, 0);
    if (pto == NULL)
        return NULL;

    pto->fn = func;
    Py_INCREF(func);
    pto->args = PyTuple_GetSlice(args, 1, PY_SSIZE_T_MAX);
    if (pto->args == NULL) {
        pto->kw = NULL;
        Py_DECREF(pto);
        return NULL;
    }
    if (kw != NULL) {
        pto->kw = PyDict_Copy(kw);
        if (pto->kw == NULL) {
            Py_DECREF(pto);
            return NULL;
        }
    } else {
        pto->kw = Py_None;
        Py_INCREF(Py_None);
    }

    pto->weakreflist = NULL;
    pto->dict = NULL;

    return (PyObject *)pto;
}

static void
partial_dealloc(partialobject *pto)
{
    PyObject_GC_UnTrack(pto);
    if (pto->weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) pto);
    Py_XDECREF(pto->fn);
    Py_XDECREF(pto->args);
    Py_XDECREF(pto->kw);
    Py_XDECREF(pto->dict);
    Py_TYPE(pto)->tp_free(pto);
}

static PyObject *
partial_call(partialobject *pto, PyObject *args, PyObject *kw)
{
    PyObject *ret;
    PyObject *argappl = NULL, *kwappl = NULL;

    assert (PyCallable_Check(pto->fn));
    assert (PyTuple_Check(pto->args));
    assert (pto->kw == Py_None  ||  PyDict_Check(pto->kw));

    if (PyTuple_GET_SIZE(pto->args) == 0) {
        argappl = args;
        Py_INCREF(args);
    } else if (PyTuple_GET_SIZE(args) == 0) {
        argappl = pto->args;
        Py_INCREF(pto->args);
    } else {
        argappl = PySequence_Concat(pto->args, args);
        if (argappl == NULL)
            return NULL;
    }

    if (pto->kw == Py_None) {
        kwappl = kw;
        Py_XINCREF(kw);
    } else {
        kwappl = PyDict_Copy(pto->kw);
        if (kwappl == NULL) {
            Py_DECREF(argappl);
            return NULL;
        }
        if (kw != NULL) {
            if (PyDict_Merge(kwappl, kw, 1) != 0) {
                Py_DECREF(argappl);
                Py_DECREF(kwappl);
                return NULL;
            }
        }
    }

    ret = PyObject_Call(pto->fn, argappl, kwappl);
    Py_DECREF(argappl);
    Py_XDECREF(kwappl);
    return ret;
}

static int
partial_traverse(partialobject *pto, visitproc visit, void *arg)
{
    Py_VISIT(pto->fn);
    Py_VISIT(pto->args);
    Py_VISIT(pto->kw);
    Py_VISIT(pto->dict);
    return 0;
}

PyDoc_STRVAR(partial_doc,
"partial(func, *args, **keywords) - new function with partial application\n\
    of the given arguments and keywords.\n");

#define OFF(x) offsetof(partialobject, x)
static PyMemberDef partial_memberlist[] = {
    {"func",            T_OBJECT,       OFF(fn),        READONLY,
     "function object to use in future partial calls"},
    {"args",            T_OBJECT,       OFF(args),      READONLY,
     "tuple of arguments to future partial calls"},
    {"keywords",        T_OBJECT,       OFF(kw),        READONLY,
     "dictionary of keyword arguments to future partial calls"},
    {NULL}
};

static PyObject *
partial_get_dict(partialobject *pto)
{
    if (pto->dict == NULL) {
        pto->dict = PyDict_New();
        if (pto->dict == NULL)
            return NULL;
    }
    Py_INCREF(pto->dict);
    return pto->dict;
}

static int
partial_set_dict(partialobject *pto, PyObject *value)
{
    PyObject *tmp;

    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "a partial object's dictionary may not be deleted");
        return -1;
    }

    if (!PyDict_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "setting partial object's dictionary to a non-dict");
        return -1;
    }
    tmp = pto->dict;
    Py_INCREF(value);
    pto->dict = value;
    Py_XDECREF(tmp);
    return 0;
}

static PyGetSetDef partial_getsetlist[] = {
    {"__dict__", (getter)partial_get_dict, (setter)partial_set_dict},
    {NULL}
};

PyObject *
partial_reduce(partialobject *pto, PyObject *unused)
{
    return Py_BuildValue("O(O)(OOOO)", Py_TYPE(pto), pto->fn, pto->fn,
                         pto->args, pto->kw,
                         pto->dict ? pto->dict : Py_None);
}

PyObject *
partial_setstate(partialobject *pto, PyObject *state)
{
    PyObject *fn, *fnargs, *kw, *dict;
    if (!PyArg_ParseTuple(state, "OOOO",
                          &fn, &fnargs, &kw, &dict))
        return NULL;
    Py_XDECREF(pto->fn);
    Py_XDECREF(pto->args);
    Py_XDECREF(pto->kw);
    Py_XDECREF(pto->dict);
    pto->fn = fn;
    pto->args = fnargs;
    pto->kw = kw;
    if (dict != Py_None) {
      pto->dict = dict;
      Py_INCREF(dict);
    } else {
      pto->dict = NULL;
    }
    Py_INCREF(fn);
    Py_INCREF(fnargs);
    Py_INCREF(kw);
    Py_RETURN_NONE;
}

static PyMethodDef partial_methods[] = {
    {"__reduce__", (PyCFunction)partial_reduce, METH_NOARGS},
    {"__setstate__", (PyCFunction)partial_setstate, METH_O},
    {NULL,              NULL}
};

static PyTypeObject partial_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "functools.partial",
    sizeof(partialobject),
    0,

    (destructor)partial_dealloc,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    (ternaryfunc)partial_call,
    0,
    PyObject_GenericGetAttr,
    PyObject_GenericSetAttr,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_WEAKREFS,
    partial_doc,
    (traverseproc)partial_traverse,
    0,
    0,
    offsetof(partialobject, weakreflist),
    0,
    0,
    partial_methods,
    partial_memberlist,
    partial_getsetlist,
    0,
    0,
    0,
    0,
    offsetof(partialobject, dict),
    0,
    0,
    partial_new,
    PyObject_GC_Del,
};

PyDoc_STRVAR(module_doc,
"Tools that operate on functions.");

static PyMethodDef module_methods[] = {
    {"reduce",          functools_reduce,     METH_VARARGS, reduce_doc},
    {NULL,              NULL}
};

PyMODINIT_FUNC
init_functools(void)
{
    int i;
    PyObject *m;
    char *name;
    PyTypeObject *typelist[] = {
        &partial_type,
        NULL
    };

    m = Py_InitModule3("_functools", module_methods, module_doc);
    if (m == NULL)
        return;

    for (i=0 ; typelist[i] != NULL ; i++) {
        if (PyType_Ready(typelist[i]) < 0)
            return;
        name = strchr(typelist[i]->tp_name, '.');
        assert (name != NULL);
        Py_INCREF(typelist[i]);
        PyModule_AddObject(m, name+1, (PyObject *)typelist[i]);
    }
}
