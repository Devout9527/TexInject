#include "Python.h"
#include "structmember.h"

typedef struct {
    PyObject_HEAD
    PyObject *md_dict;
} PyModuleObject;

static PyMemberDef module_members[] = {
    {"__dict__", T_OBJECT, offsetof(PyModuleObject, md_dict), READONLY},
    {0}
};

PyObject *
PyModule_New(const char *name)
{
    PyModuleObject *m;
    PyObject *nameobj;
    m = PyObject_GC_New(PyModuleObject, &PyModule_Type);
    if (m == NULL)
        return NULL;
    nameobj = PyString_FromString(name);
    m->md_dict = PyDict_New();
    if (m->md_dict == NULL || nameobj == NULL)
        goto fail;
    if (PyDict_SetItemString(m->md_dict, "__name__", nameobj) != 0)
        goto fail;
    if (PyDict_SetItemString(m->md_dict, "__doc__", Py_None) != 0)
        goto fail;
    if (PyDict_SetItemString(m->md_dict, "__package__", Py_None) != 0)
        goto fail;
    Py_DECREF(nameobj);
    PyObject_GC_Track(m);
    return (PyObject *)m;

 fail:
    Py_XDECREF(nameobj);
    Py_DECREF(m);
    return NULL;
}

PyObject *
PyModule_GetDict(PyObject *m)
{
    PyObject *d;
    if (!PyModule_Check(m)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    d = ((PyModuleObject *)m) -> md_dict;
    if (d == NULL)
        ((PyModuleObject *)m) -> md_dict = d = PyDict_New();
    return d;
}

char *
PyModule_GetName(PyObject *m)
{
    PyObject *d;
    PyObject *nameobj;
    if (!PyModule_Check(m)) {
        PyErr_BadArgument();
        return NULL;
    }
    d = ((PyModuleObject *)m)->md_dict;
    if (d == NULL ||
        (nameobj = PyDict_GetItemString(d, "__name__")) == NULL ||
        !PyString_Check(nameobj))
    {
        PyErr_SetString(PyExc_SystemError, "nameless module");
        return NULL;
    }
    return PyString_AsString(nameobj);
}

char *
PyModule_GetFilename(PyObject *m)
{
    PyObject *d;
    PyObject *fileobj;
    if (!PyModule_Check(m)) {
        PyErr_BadArgument();
        return NULL;
    }
    d = ((PyModuleObject *)m)->md_dict;
    if (d == NULL ||
        (fileobj = PyDict_GetItemString(d, "__file__")) == NULL ||
        !PyString_Check(fileobj))
    {
        PyErr_SetString(PyExc_SystemError, "module filename missing");
        return NULL;
    }
    return PyString_AsString(fileobj);
}

void
_PyModule_Clear(PyObject *m)
{

    Py_ssize_t pos;
    PyObject *key, *value;
    PyObject *d;

    d = ((PyModuleObject *)m)->md_dict;
    if (d == NULL)
        return;

    pos = 0;
    while (PyDict_Next(d, &pos, &key, &value)) {
        if (value != Py_None && PyString_Check(key)) {
            char *s = PyString_AsString(key);
            if (s[0] == '_' && s[1] != '_') {
                if (Py_VerboseFlag > 1)
                    PySys_WriteStderr("#   clear[1] %s\n", s);
                if (PyDict_SetItem(d, key, Py_None) != 0)
                    PyErr_Clear();
            }
        }
    }

    pos = 0;
    while (PyDict_Next(d, &pos, &key, &value)) {
        if (value != Py_None && PyString_Check(key)) {
            char *s = PyString_AsString(key);
            if (s[0] != '_' || strcmp(s, "__builtins__") != 0) {
                if (Py_VerboseFlag > 1)
                    PySys_WriteStderr("#   clear[2] %s\n", s);
                if (PyDict_SetItem(d, key, Py_None) != 0)
                    PyErr_Clear();
            }
        }
    }

}

static int
module_init(PyModuleObject *m, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"name", "doc", NULL};
    PyObject *dict, *name = Py_None, *doc = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "S|O:module.__init__",
                                     kwlist, &name, &doc))
        return -1;
    dict = m->md_dict;
    if (dict == NULL) {
        dict = PyDict_New();
        if (dict == NULL)
            return -1;
        m->md_dict = dict;
    }
    if (PyDict_SetItemString(dict, "__name__", name) < 0)
        return -1;
    if (PyDict_SetItemString(dict, "__doc__", doc) < 0)
        return -1;
    return 0;
}

static void
module_dealloc(PyModuleObject *m)
{
    PyObject_GC_UnTrack(m);
    if (m->md_dict != NULL) {
        _PyModule_Clear((PyObject *)m);
        Py_DECREF(m->md_dict);
    }
    Py_TYPE(m)->tp_free((PyObject *)m);
}

static PyObject *
module_repr(PyModuleObject *m)
{
    char *name;
    char *filename;

    name = PyModule_GetName((PyObject *)m);
    if (name == NULL) {
        PyErr_Clear();
        name = "?";
    }
    filename = PyModule_GetFilename((PyObject *)m);
    if (filename == NULL) {
        PyErr_Clear();
        return PyString_FromFormat("<module '%s' (built-in)>", name);
    }
    return PyString_FromFormat("<module '%s' from '%s'>", name, filename);
}

static int
module_traverse(PyModuleObject *m, visitproc visit, void *arg)
{
    Py_VISIT(m->md_dict);
    return 0;
}

PyDoc_STRVAR(module_doc,
"module(name[, doc])\n\
\n\
Create a module object.\n\
The name must be a string; the optional doc argument can have any type.");

PyTypeObject PyModule_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "module",
    sizeof(PyModuleObject),
    0,
    (destructor)module_dealloc,
    0,
    0,
    0,
    0,
    (reprfunc)module_repr,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    PyObject_GenericSetAttr,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    module_doc,
    (traverseproc)module_traverse,
    0,
    0,
    0,
    0,
    0,
    0,
    module_members,
    0,
    0,
    0,
    0,
    0,
    offsetof(PyModuleObject, md_dict),
    (initproc)module_init,
    PyType_GenericAlloc,
    PyType_GenericNew,
    PyObject_GC_Del,
};
