#include "Python.h"

static int
bool_print(PyBoolObject *self, FILE *fp, int flags)
{
    Py_BEGIN_ALLOW_THREADS
    fputs(self->ob_ival == 0 ? "False" : "True", fp);
    Py_END_ALLOW_THREADS
    return 0;
}

static PyObject *false_str = NULL;
static PyObject *true_str = NULL;

static PyObject *
bool_repr(PyBoolObject *self)
{
    PyObject *s;

    if (self->ob_ival)
        s = true_str ? true_str :
            (true_str = PyString_InternFromString("True"));
    else
        s = false_str ? false_str :
            (false_str = PyString_InternFromString("False"));
    Py_XINCREF(s);
    return s;
}

PyObject *PyBool_FromLong(long ok)
{
    PyObject *result;

    if (ok)
        result = Py_True;
    else
        result = Py_False;
    Py_INCREF(result);
    return result;
}

static PyObject *
bool_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"x", 0};
    PyObject *x = Py_False;
    long ok;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:bool", kwlist, &x))
        return NULL;
    ok = PyObject_IsTrue(x);
    if (ok < 0)
        return NULL;
    return PyBool_FromLong(ok);
}

static PyObject *
bool_and(PyObject *a, PyObject *b)
{
    if (!PyBool_Check(a) || !PyBool_Check(b))
        return PyInt_Type.tp_as_number->nb_and(a, b);
    return PyBool_FromLong(
        ((PyBoolObject *)a)->ob_ival & ((PyBoolObject *)b)->ob_ival);
}

static PyObject *
bool_or(PyObject *a, PyObject *b)
{
    if (!PyBool_Check(a) || !PyBool_Check(b))
        return PyInt_Type.tp_as_number->nb_or(a, b);
    return PyBool_FromLong(
        ((PyBoolObject *)a)->ob_ival | ((PyBoolObject *)b)->ob_ival);
}

static PyObject *
bool_xor(PyObject *a, PyObject *b)
{
    if (!PyBool_Check(a) || !PyBool_Check(b))
        return PyInt_Type.tp_as_number->nb_xor(a, b);
    return PyBool_FromLong(
        ((PyBoolObject *)a)->ob_ival ^ ((PyBoolObject *)b)->ob_ival);
}

PyDoc_STRVAR(bool_doc,
"bool(x) -> bool\n\
\n\
Returns True when the argument x is true, False otherwise.\n\
The builtins True and False are the only two instances of the class bool.\n\
The class bool is a subclass of the class int, and cannot be subclassed.");

static PyNumberMethods bool_as_number = {
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
    0,
    0,
    0,
    bool_and,
    bool_xor,
    bool_or,
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
};

PyTypeObject PyBool_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "bool",
    sizeof(PyIntObject),
    0,
    0,
    (printfunc)bool_print,
    0,
    0,
    0,
    (reprfunc)bool_repr,
    &bool_as_number,
    0,
    0,
    0,
    0,
    (reprfunc)bool_repr,
    0,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES,
    bool_doc,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    &PyInt_Type,
    0,
    0,
    0,
    0,
    0,
    0,
    bool_new,
};

PyIntObject _Py_ZeroStruct = {
    PyObject_HEAD_INIT(&PyBool_Type)
    0
};

PyIntObject _Py_TrueStruct = {
    PyObject_HEAD_INIT(&PyBool_Type)
    1
};
