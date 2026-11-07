#include "Python.h"
#include "code.h"
#include "eval.h"
#include "structmember.h"

PyObject *
PyFunction_New(PyObject *code, PyObject *globals)
{
    PyFunctionObject *op = PyObject_GC_New(PyFunctionObject,
                                        &PyFunction_Type);
    static PyObject *__name__ = 0;
    if (op != NULL) {
        PyObject *doc;
        PyObject *consts;
        PyObject *module;
        op->func_weakreflist = NULL;
        Py_INCREF(code);
        op->func_code = code;
        Py_INCREF(globals);
        op->func_globals = globals;
        op->func_name = ((PyCodeObject *)code)->co_name;
        Py_INCREF(op->func_name);
        op->func_defaults = NULL;
        op->func_closure = NULL;
        consts = ((PyCodeObject *)code)->co_consts;
        if (PyTuple_Size(consts) >= 1) {
            doc = PyTuple_GetItem(consts, 0);
            if (!PyString_Check(doc) && !PyUnicode_Check(doc))
                doc = Py_None;
        }
        else
            doc = Py_None;
        Py_INCREF(doc);
        op->func_doc = doc;
        op->func_dict = NULL;
        op->func_module = NULL;

        if (!__name__) {
            __name__ = PyString_InternFromString("__name__");
            if (!__name__) {
                Py_DECREF(op);
                return NULL;
            }
        }
        module = PyDict_GetItem(globals, __name__);
        if (module) {
            Py_INCREF(module);
            op->func_module = module;
        }
    }
    else
        return NULL;
    _PyObject_GC_TRACK(op);
    return (PyObject *)op;
}

PyObject *
PyFunction_GetCode(PyObject *op)
{
    if (!PyFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return ((PyFunctionObject *) op) -> func_code;
}

PyObject *
PyFunction_GetGlobals(PyObject *op)
{
    if (!PyFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return ((PyFunctionObject *) op) -> func_globals;
}

PyObject *
PyFunction_GetModule(PyObject *op)
{
    if (!PyFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return ((PyFunctionObject *) op) -> func_module;
}

PyObject *
PyFunction_GetDefaults(PyObject *op)
{
    if (!PyFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return ((PyFunctionObject *) op) -> func_defaults;
}

int
PyFunction_SetDefaults(PyObject *op, PyObject *defaults)
{
    if (!PyFunction_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (defaults == Py_None)
        defaults = NULL;
    else if (defaults && PyTuple_Check(defaults)) {
        Py_INCREF(defaults);
    }
    else {
        PyErr_SetString(PyExc_SystemError, "non-tuple default args");
        return -1;
    }
    Py_XSETREF(((PyFunctionObject *)op)->func_defaults, defaults);
    return 0;
}

PyObject *
PyFunction_GetClosure(PyObject *op)
{
    if (!PyFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return ((PyFunctionObject *) op) -> func_closure;
}

int
PyFunction_SetClosure(PyObject *op, PyObject *closure)
{
    if (!PyFunction_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (closure == Py_None)
        closure = NULL;
    else if (PyTuple_Check(closure)) {
        Py_INCREF(closure);
    }
    else {
        PyErr_Format(PyExc_SystemError,
                     "expected tuple for closure, got '%.100s'",
                     closure->ob_type->tp_name);
        return -1;
    }
    Py_XSETREF(((PyFunctionObject *)op)->func_closure, closure);
    return 0;
}

#define OFF(x) offsetof(PyFunctionObject, x)

static PyMemberDef func_memberlist[] = {
    {"func_closure",  T_OBJECT,     OFF(func_closure),
     RESTRICTED|READONLY},
    {"__closure__",  T_OBJECT,      OFF(func_closure),
     RESTRICTED|READONLY},
    {"func_doc",      T_OBJECT,     OFF(func_doc), PY_WRITE_RESTRICTED},
    {"__doc__",       T_OBJECT,     OFF(func_doc), PY_WRITE_RESTRICTED},
    {"func_globals",  T_OBJECT,     OFF(func_globals),
     RESTRICTED|READONLY},
    {"__globals__",  T_OBJECT,      OFF(func_globals),
     RESTRICTED|READONLY},
    {"__module__",    T_OBJECT,     OFF(func_module), PY_WRITE_RESTRICTED},
    {NULL}
};

static int
restricted(void)
{
    if (!PyEval_GetRestricted())
        return 0;
    PyErr_SetString(PyExc_RuntimeError,
        "function attributes not accessible in restricted mode");
    return 1;
}

static PyObject *
func_get_dict(PyFunctionObject *op)
{
    if (restricted())
        return NULL;
    if (op->func_dict == NULL) {
        op->func_dict = PyDict_New();
        if (op->func_dict == NULL)
            return NULL;
    }
    Py_INCREF(op->func_dict);
    return op->func_dict;
}

static int
func_set_dict(PyFunctionObject *op, PyObject *value)
{
    PyObject *tmp;

    if (restricted())
        return -1;

    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "function's dictionary may not be deleted");
        return -1;
    }

    if (!PyDict_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "setting function's dictionary to a non-dict");
        return -1;
    }
    tmp = op->func_dict;
    Py_INCREF(value);
    op->func_dict = value;
    Py_XDECREF(tmp);
    return 0;
}

static PyObject *
func_get_code(PyFunctionObject *op)
{
    if (restricted())
        return NULL;
    Py_INCREF(op->func_code);
    return op->func_code;
}

static int
func_set_code(PyFunctionObject *op, PyObject *value)
{
    PyObject *tmp;
    Py_ssize_t nfree, nclosure;

    if (restricted())
        return -1;

    if (value == NULL || !PyCode_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "__code__ must be set to a code object");
        return -1;
    }
    nfree = PyCode_GetNumFree((PyCodeObject *)value);
    nclosure = (op->func_closure == NULL ? 0 :
            PyTuple_GET_SIZE(op->func_closure));
    if (nclosure != nfree) {
        PyErr_Format(PyExc_ValueError,
                     "%s() requires a code object with %zd free vars,"
                     " not %zd",
                     PyString_AsString(op->func_name),
                     nclosure, nfree);
        return -1;
    }
    tmp = op->func_code;
    Py_INCREF(value);
    op->func_code = value;
    Py_DECREF(tmp);
    return 0;
}

static PyObject *
func_get_name(PyFunctionObject *op)
{
    Py_INCREF(op->func_name);
    return op->func_name;
}

static int
func_set_name(PyFunctionObject *op, PyObject *value)
{
    PyObject *tmp;

    if (restricted())
        return -1;

    if (value == NULL || !PyString_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "__name__ must be set to a string object");
        return -1;
    }
    tmp = op->func_name;
    Py_INCREF(value);
    op->func_name = value;
    Py_DECREF(tmp);
    return 0;
}

static PyObject *
func_get_defaults(PyFunctionObject *op)
{
    if (restricted())
        return NULL;
    if (op->func_defaults == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    Py_INCREF(op->func_defaults);
    return op->func_defaults;
}

static int
func_set_defaults(PyFunctionObject *op, PyObject *value)
{
    PyObject *tmp;

    if (restricted())
        return -1;

    if (value == Py_None)
        value = NULL;
    if (value != NULL && !PyTuple_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "__defaults__ must be set to a tuple object");
        return -1;
    }
    tmp = op->func_defaults;
    Py_XINCREF(value);
    op->func_defaults = value;
    Py_XDECREF(tmp);
    return 0;
}

static PyGetSetDef func_getsetlist[] = {
    {"func_code", (getter)func_get_code, (setter)func_set_code},
    {"__code__", (getter)func_get_code, (setter)func_set_code},
    {"func_defaults", (getter)func_get_defaults,
     (setter)func_set_defaults},
    {"__defaults__", (getter)func_get_defaults,
     (setter)func_set_defaults},
    {"func_dict", (getter)func_get_dict, (setter)func_set_dict},
    {"__dict__", (getter)func_get_dict, (setter)func_set_dict},
    {"func_name", (getter)func_get_name, (setter)func_set_name},
    {"__name__", (getter)func_get_name, (setter)func_set_name},
    {NULL}
};

PyDoc_STRVAR(func_doc,
"function(code, globals[, name[, argdefs[, closure]]])\n\
\n\
Create a function object from a code object and a dictionary.\n\
The optional name string overrides the name from the code object.\n\
The optional argdefs tuple specifies the default argument values.\n\
The optional closure tuple supplies the bindings for free variables.");

static PyObject *
func_new(PyTypeObject* type, PyObject* args, PyObject* kw)
{
    PyCodeObject *code;
    PyObject *globals;
    PyObject *name = Py_None;
    PyObject *defaults = Py_None;
    PyObject *closure = Py_None;
    PyFunctionObject *newfunc;
    Py_ssize_t nfree, nclosure;
    static char *kwlist[] = {"code", "globals", "name",
                             "argdefs", "closure", 0};

    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|OOO:function",
                          kwlist,
                          &PyCode_Type, &code,
                          &PyDict_Type, &globals,
                          &name, &defaults, &closure))
        return NULL;
    if (name != Py_None && !PyString_Check(name)) {
        PyErr_SetString(PyExc_TypeError,
                        "arg 3 (name) must be None or string");
        return NULL;
    }
    if (defaults != Py_None && !PyTuple_Check(defaults)) {
        PyErr_SetString(PyExc_TypeError,
                        "arg 4 (defaults) must be None or tuple");
        return NULL;
    }
    nfree = PyTuple_GET_SIZE(code->co_freevars);
    if (!PyTuple_Check(closure)) {
        if (nfree && closure == Py_None) {
            PyErr_SetString(PyExc_TypeError,
                            "arg 5 (closure) must be tuple");
            return NULL;
        }
        else if (closure != Py_None) {
            PyErr_SetString(PyExc_TypeError,
                "arg 5 (closure) must be None or tuple");
            return NULL;
        }
    }

    nclosure = closure == Py_None ? 0 : PyTuple_GET_SIZE(closure);
    if (nfree != nclosure)
        return PyErr_Format(PyExc_ValueError,
                            "%s requires closure of length %zd, not %zd",
                            PyString_AS_STRING(code->co_name),
                            nfree, nclosure);
    if (nclosure) {
        Py_ssize_t i;
        for (i = 0; i < nclosure; i++) {
            PyObject *o = PyTuple_GET_ITEM(closure, i);
            if (!PyCell_Check(o)) {
                return PyErr_Format(PyExc_TypeError,
                    "arg 5 (closure) expected cell, found %s",
                                    o->ob_type->tp_name);
            }
        }
    }

    newfunc = (PyFunctionObject *)PyFunction_New((PyObject *)code,
                                                 globals);
    if (newfunc == NULL)
        return NULL;

    if (name != Py_None) {
        Py_INCREF(name);
        Py_SETREF(newfunc->func_name, name);
    }
    if (defaults != Py_None) {
        Py_INCREF(defaults);
        newfunc->func_defaults  = defaults;
    }
    if (closure != Py_None) {
        Py_INCREF(closure);
        newfunc->func_closure = closure;
    }

    return (PyObject *)newfunc;
}

static void
func_dealloc(PyFunctionObject *op)
{
    _PyObject_GC_UNTRACK(op);
    if (op->func_weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) op);
    Py_DECREF(op->func_code);
    Py_DECREF(op->func_globals);
    Py_XDECREF(op->func_module);
    Py_DECREF(op->func_name);
    Py_XDECREF(op->func_defaults);
    Py_XDECREF(op->func_doc);
    Py_XDECREF(op->func_dict);
    Py_XDECREF(op->func_closure);
    PyObject_GC_Del(op);
}

static PyObject*
func_repr(PyFunctionObject *op)
{
    return PyString_FromFormat("<function %s at %p>",
                               PyString_AsString(op->func_name),
                               op);
}

static int
func_traverse(PyFunctionObject *f, visitproc visit, void *arg)
{
    Py_VISIT(f->func_code);
    Py_VISIT(f->func_globals);
    Py_VISIT(f->func_module);
    Py_VISIT(f->func_defaults);
    Py_VISIT(f->func_doc);
    Py_VISIT(f->func_name);
    Py_VISIT(f->func_dict);
    Py_VISIT(f->func_closure);
    return 0;
}

static PyObject *
function_call(PyObject *func, PyObject *arg, PyObject *kw)
{
    PyObject *result;
    PyObject *argdefs;
    PyObject *kwtuple = NULL;
    PyObject **d, **k;
    Py_ssize_t nk, nd;

    argdefs = PyFunction_GET_DEFAULTS(func);
    if (argdefs != NULL && PyTuple_Check(argdefs)) {
        d = &PyTuple_GET_ITEM((PyTupleObject *)argdefs, 0);
        nd = PyTuple_GET_SIZE(argdefs);
    }
    else {
        d = NULL;
        nd = 0;
    }

    if (kw != NULL && PyDict_Check(kw)) {
        Py_ssize_t pos, i;
        nk = PyDict_Size(kw);
        kwtuple = PyTuple_New(2*nk);
        if (kwtuple == NULL)
            return NULL;
        k = &PyTuple_GET_ITEM(kwtuple, 0);
        pos = i = 0;
        while (PyDict_Next(kw, &pos, &k[i], &k[i+1])) {
            Py_INCREF(k[i]);
            Py_INCREF(k[i+1]);
            i += 2;
        }
        nk = i/2;
    }
    else {
        k = NULL;
        nk = 0;
    }

    result = PyEval_EvalCodeEx(
        (PyCodeObject *)PyFunction_GET_CODE(func),
        PyFunction_GET_GLOBALS(func), (PyObject *)NULL,
        &PyTuple_GET_ITEM(arg, 0), PyTuple_GET_SIZE(arg),
        k, nk, d, nd,
        PyFunction_GET_CLOSURE(func));

    Py_XDECREF(kwtuple);

    return result;
}

static PyObject *
func_descr_get(PyObject *func, PyObject *obj, PyObject *type)
{
    if (obj == Py_None)
        obj = NULL;
    return PyMethod_New(func, obj, type);
}

PyTypeObject PyFunction_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "function",
    sizeof(PyFunctionObject),
    0,
    (destructor)func_dealloc,
    0,
    0,
    0,
    0,
    (reprfunc)func_repr,
    0,
    0,
    0,
    0,
    function_call,
    0,
    PyObject_GenericGetAttr,
    PyObject_GenericSetAttr,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    func_doc,
    (traverseproc)func_traverse,
    0,
    0,
    offsetof(PyFunctionObject, func_weakreflist),
    0,
    0,
    0,
    func_memberlist,
    func_getsetlist,
    0,
    0,
    func_descr_get,
    0,
    offsetof(PyFunctionObject, func_dict),
    0,
    0,
    func_new,
};

typedef struct {
    PyObject_HEAD
    PyObject *cm_callable;
} classmethod;

static void
cm_dealloc(classmethod *cm)
{
    _PyObject_GC_UNTRACK((PyObject *)cm);
    Py_XDECREF(cm->cm_callable);
    Py_TYPE(cm)->tp_free((PyObject *)cm);
}

static int
cm_traverse(classmethod *cm, visitproc visit, void *arg)
{
    Py_VISIT(cm->cm_callable);
    return 0;
}

static int
cm_clear(classmethod *cm)
{
    Py_CLEAR(cm->cm_callable);
    return 0;
}

static PyObject *
cm_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    classmethod *cm = (classmethod *)self;

    if (cm->cm_callable == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                        "uninitialized classmethod object");
        return NULL;
    }
    if (type == NULL)
        type = (PyObject *)(Py_TYPE(obj));
    return PyMethod_New(cm->cm_callable,
                        type, (PyObject *)(Py_TYPE(type)));
}

static int
cm_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    classmethod *cm = (classmethod *)self;
    PyObject *callable;

    if (!PyArg_UnpackTuple(args, "classmethod", 1, 1, &callable))
        return -1;
    if (!_PyArg_NoKeywords("classmethod", kwds))
        return -1;
    Py_INCREF(callable);
    cm->cm_callable = callable;
    return 0;
}

static PyMemberDef cm_memberlist[] = {
    {"__func__", T_OBJECT, offsetof(classmethod, cm_callable), READONLY},
    {NULL}
};

PyDoc_STRVAR(classmethod_doc,
"classmethod(function) -> method\n\
\n\
Convert a function to be a class method.\n\
\n\
A class method receives the class as implicit first argument,\n\
just like an instance method receives the instance.\n\
To declare a class method, use this idiom:\n\
\n\
  class C:\n\
      @classmethod\n\
      def f(cls, arg1, arg2, ...):\n\
          ...\n\
\n\
It can be called either on the class (e.g. C.f()) or on an instance\n\
(e.g. C().f()).  The instance is ignored except for its class.\n\
If a class method is called for a derived class, the derived class\n\
object is passed as the implied first argument.\n\
\n\
Class methods are different than C++ or Java static methods.\n\
If you want those, see the staticmethod builtin.");

PyTypeObject PyClassMethod_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "classmethod",
    sizeof(classmethod),
    0,
    (destructor)cm_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    classmethod_doc,
    (traverseproc)cm_traverse,
    (inquiry)cm_clear,
    0,
    0,
    0,
    0,
    0,
    cm_memberlist,
    0,
    0,
    0,
    cm_descr_get,
    0,
    0,
    cm_init,
    PyType_GenericAlloc,
    PyType_GenericNew,
    PyObject_GC_Del,
};

PyObject *
PyClassMethod_New(PyObject *callable)
{
    classmethod *cm = (classmethod *)
        PyType_GenericAlloc(&PyClassMethod_Type, 0);
    if (cm != NULL) {
        Py_INCREF(callable);
        cm->cm_callable = callable;
    }
    return (PyObject *)cm;
}

typedef struct {
    PyObject_HEAD
    PyObject *sm_callable;
} staticmethod;

static void
sm_dealloc(staticmethod *sm)
{
    _PyObject_GC_UNTRACK((PyObject *)sm);
    Py_XDECREF(sm->sm_callable);
    Py_TYPE(sm)->tp_free((PyObject *)sm);
}

static int
sm_traverse(staticmethod *sm, visitproc visit, void *arg)
{
    Py_VISIT(sm->sm_callable);
    return 0;
}

static int
sm_clear(staticmethod *sm)
{
    Py_CLEAR(sm->sm_callable);
    return 0;
}

static PyObject *
sm_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    staticmethod *sm = (staticmethod *)self;

    if (sm->sm_callable == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                        "uninitialized staticmethod object");
        return NULL;
    }
    Py_INCREF(sm->sm_callable);
    return sm->sm_callable;
}

static int
sm_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    staticmethod *sm = (staticmethod *)self;
    PyObject *callable;

    if (!PyArg_UnpackTuple(args, "staticmethod", 1, 1, &callable))
        return -1;
    if (!_PyArg_NoKeywords("staticmethod", kwds))
        return -1;
    Py_INCREF(callable);
    sm->sm_callable = callable;
    return 0;
}

static PyMemberDef sm_memberlist[] = {
    {"__func__", T_OBJECT, offsetof(staticmethod, sm_callable), READONLY},
    {NULL}
};

PyDoc_STRVAR(staticmethod_doc,
"staticmethod(function) -> method\n\
\n\
Convert a function to be a static method.\n\
\n\
A static method does not receive an implicit first argument.\n\
To declare a static method, use this idiom:\n\
\n\
     class C:\n\
         @staticmethod\n\
         def f(arg1, arg2, ...):\n\
             ...\n\
\n\
It can be called either on the class (e.g. C.f()) or on an instance\n\
(e.g. C().f()).  The instance is ignored except for its class.\n\
\n\
Static methods in Python are similar to those found in Java or C++.\n\
For a more advanced concept, see the classmethod builtin.");

PyTypeObject PyStaticMethod_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "staticmethod",
    sizeof(staticmethod),
    0,
    (destructor)sm_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    staticmethod_doc,
    (traverseproc)sm_traverse,
    (inquiry)sm_clear,
    0,
    0,
    0,
    0,
    0,
    sm_memberlist,
    0,
    0,
    0,
    sm_descr_get,
    0,
    0,
    sm_init,
    PyType_GenericAlloc,
    PyType_GenericNew,
    PyObject_GC_Del,
};

PyObject *
PyStaticMethod_New(PyObject *callable)
{
    staticmethod *sm = (staticmethod *)
        PyType_GenericAlloc(&PyStaticMethod_Type, 0);
    if (sm != NULL) {
        Py_INCREF(callable);
        sm->sm_callable = callable;
    }
    return (PyObject *)sm;
}
