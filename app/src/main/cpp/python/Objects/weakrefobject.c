#include "Python.h"
#include "structmember.h"

#define GET_WEAKREFS_LISTPTR(o) \
        ((PyWeakReference **) PyObject_GET_WEAKREFS_LISTPTR(o))

Py_ssize_t
_PyWeakref_GetWeakrefCount(PyWeakReference *head)
{
    Py_ssize_t count = 0;

    while (head != NULL) {
        ++count;
        head = head->wr_next;
    }
    return count;
}

static void
init_weakref(PyWeakReference *self, PyObject *ob, PyObject *callback)
{
    self->hash = -1;
    self->wr_object = ob;
    Py_XINCREF(callback);
    self->wr_callback = callback;
}

static PyWeakReference *
new_weakref(PyObject *ob, PyObject *callback)
{
    PyWeakReference *result;

    result = PyObject_GC_New(PyWeakReference, &_PyWeakref_RefType);
    if (result) {
        init_weakref(result, ob, callback);
        PyObject_GC_Track(result);
    }
    return result;
}

static void
clear_weakref(PyWeakReference *self)
{
    PyObject *callback = self->wr_callback;

    if (self->wr_object != Py_None) {
        PyWeakReference **list = GET_WEAKREFS_LISTPTR(self->wr_object);

        if (*list == self)

            *list = self->wr_next;
        self->wr_object = Py_None;
        if (self->wr_prev != NULL)
            self->wr_prev->wr_next = self->wr_next;
        if (self->wr_next != NULL)
            self->wr_next->wr_prev = self->wr_prev;
        self->wr_prev = NULL;
        self->wr_next = NULL;
    }
    if (callback != NULL) {
        Py_DECREF(callback);
        self->wr_callback = NULL;
    }
}

void
_PyWeakref_ClearRef(PyWeakReference *self)
{
    PyObject *callback;

    assert(self != NULL);
    assert(PyWeakref_Check(self));

    callback = self->wr_callback;
    self->wr_callback = NULL;
    clear_weakref(self);
    self->wr_callback = callback;
}

static void
weakref_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    clear_weakref((PyWeakReference *) self);
    Py_TYPE(self)->tp_free(self);
}

static int
gc_traverse(PyWeakReference *self, visitproc visit, void *arg)
{
    Py_VISIT(self->wr_callback);
    return 0;
}

static int
gc_clear(PyWeakReference *self)
{
    clear_weakref(self);
    return 0;
}

static PyObject *
weakref_call(PyWeakReference *self, PyObject *args, PyObject *kw)
{
    static char *kwlist[] = {NULL};

    if (PyArg_ParseTupleAndKeywords(args, kw, ":__call__", kwlist)) {
        PyObject *object = PyWeakref_GET_OBJECT(self);
        Py_INCREF(object);
        return (object);
    }
    return NULL;
}

static long
weakref_hash(PyWeakReference *self)
{
    if (self->hash != -1)
        return self->hash;
    if (PyWeakref_GET_OBJECT(self) == Py_None) {
        PyErr_SetString(PyExc_TypeError, "weak object has gone away");
        return -1;
    }
    self->hash = PyObject_Hash(PyWeakref_GET_OBJECT(self));
    return self->hash;
}

static PyObject *
weakref_repr(PyWeakReference *self)
{
    char buffer[256];
    if (PyWeakref_GET_OBJECT(self) == Py_None) {
        PyOS_snprintf(buffer, sizeof(buffer), "<weakref at %p; dead>", self);
    }
    else {
        char *name = NULL;
        PyObject *nameobj = PyObject_GetAttrString(PyWeakref_GET_OBJECT(self),
                                                   "__name__");
        if (nameobj == NULL)
                PyErr_Clear();
        else if (PyString_Check(nameobj))
                name = PyString_AS_STRING(nameobj);
        if (name != NULL) {
            PyOS_snprintf(buffer, sizeof(buffer),
                          "<weakref at %p; to '%.50s' at %p (%s)>",
                          self,
                          Py_TYPE(PyWeakref_GET_OBJECT(self))->tp_name,
                          PyWeakref_GET_OBJECT(self),
                          name);
        }
        else {
            PyOS_snprintf(buffer, sizeof(buffer),
                          "<weakref at %p; to '%.50s' at %p>",
                          self,
                          Py_TYPE(PyWeakref_GET_OBJECT(self))->tp_name,
                          PyWeakref_GET_OBJECT(self));
        }
        Py_XDECREF(nameobj);
    }
    return PyString_FromString(buffer);
}

static PyObject *
weakref_richcompare(PyWeakReference* self, PyWeakReference* other, int op)
{
    if ((op != Py_EQ && op != Py_NE) || self->ob_type != other->ob_type) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    if (PyWeakref_GET_OBJECT(self) == Py_None
        || PyWeakref_GET_OBJECT(other) == Py_None) {
        int res = (self == other);
        if (op == Py_NE)
            res = !res;
        if (res)
            Py_RETURN_TRUE;
        else
            Py_RETURN_FALSE;
    }
    return PyObject_RichCompare(PyWeakref_GET_OBJECT(self),
                                PyWeakref_GET_OBJECT(other), op);
}

static void
get_basic_refs(PyWeakReference *head,
               PyWeakReference **refp, PyWeakReference **proxyp)
{
    *refp = NULL;
    *proxyp = NULL;

    if (head != NULL && head->wr_callback == NULL) {

        if (PyWeakref_CheckRefExact(head)) {
            *refp = head;
            head = head->wr_next;
        }
        if (head != NULL
            && head->wr_callback == NULL
            && PyWeakref_CheckProxy(head)) {
            *proxyp = head;

        }
    }
}

static void
insert_after(PyWeakReference *newref, PyWeakReference *prev)
{
    newref->wr_prev = prev;
    newref->wr_next = prev->wr_next;
    if (prev->wr_next != NULL)
        prev->wr_next->wr_prev = newref;
    prev->wr_next = newref;
}

static void
insert_head(PyWeakReference *newref, PyWeakReference **list)
{
    PyWeakReference *next = *list;

    newref->wr_prev = NULL;
    newref->wr_next = next;
    if (next != NULL)
        next->wr_prev = newref;
    *list = newref;
}

static int
parse_weakref_init_args(char *funcname, PyObject *args, PyObject *kwargs,
                        PyObject **obp, PyObject **callbackp)
{
    return PyArg_UnpackTuple(args, funcname, 1, 2, obp, callbackp);
}

static PyObject *
weakref___new__(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyWeakReference *self = NULL;
    PyObject *ob, *callback = NULL;

    if (parse_weakref_init_args("__new__", args, kwargs, &ob, &callback)) {
        PyWeakReference *ref, *proxy;
        PyWeakReference **list;

        if (!PyType_SUPPORTS_WEAKREFS(Py_TYPE(ob))) {
            PyErr_Format(PyExc_TypeError,
                         "cannot create weak reference to '%s' object",
                         Py_TYPE(ob)->tp_name);
            return NULL;
        }
        if (callback == Py_None)
            callback = NULL;
        list = GET_WEAKREFS_LISTPTR(ob);
        get_basic_refs(*list, &ref, &proxy);
        if (callback == NULL && type == &_PyWeakref_RefType) {
            if (ref != NULL) {

                Py_INCREF(ref);
                return (PyObject *)ref;
            }
        }

        self = (PyWeakReference *) (type->tp_alloc(type, 0));
        if (self != NULL) {
            init_weakref(self, ob, callback);
            if (callback == NULL && type == &_PyWeakref_RefType) {
                insert_head(self, list);
            }
            else {
                PyWeakReference *prev;

                get_basic_refs(*list, &ref, &proxy);
                prev = (proxy == NULL) ? ref : proxy;
                if (prev == NULL)
                    insert_head(self, list);
                else
                    insert_after(self, prev);
            }
        }
    }
    return (PyObject *)self;
}

static int
weakref___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *tmp;

    if (!_PyArg_NoKeywords("ref()", kwargs))
        return -1;

    if (parse_weakref_init_args("__init__", args, kwargs, &tmp, &tmp))
        return 0;
    else
        return -1;
}

PyTypeObject
_PyWeakref_RefType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "weakref",
    sizeof(PyWeakReference),
    0,
    weakref_dealloc,
    0,
    0,
    0,
    0,
    (reprfunc)weakref_repr,
    0,
    0,
    0,
    (hashfunc)weakref_hash,
    (ternaryfunc)weakref_call,
    0,
    0,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_RICHCOMPARE
        | Py_TPFLAGS_BASETYPE,
    0,
    (traverseproc)gc_traverse,
    (inquiry)gc_clear,
    (richcmpfunc)weakref_richcompare,
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
    weakref___init__,
    PyType_GenericAlloc,
    weakref___new__,
    PyObject_GC_Del,
};

static int
proxy_checkref(PyWeakReference *proxy)
{
    if (PyWeakref_GET_OBJECT(proxy) == Py_None) {
        PyErr_SetString(PyExc_ReferenceError,
                        "weakly-referenced object no longer exists");
        return 0;
    }
    return 1;
}

#define UNWRAP(o) \
        if (PyWeakref_CheckProxy(o)) { \
            if (!proxy_checkref((PyWeakReference *)o)) \
                return NULL; \
            o = PyWeakref_GET_OBJECT(o); \
        }

#define UNWRAP_I(o) \
        if (PyWeakref_CheckProxy(o)) { \
            if (!proxy_checkref((PyWeakReference *)o)) \
                return -1; \
            o = PyWeakref_GET_OBJECT(o); \
        }

#define WRAP_UNARY(method, generic) \
    static PyObject * \
    method(PyObject *proxy) { \
        UNWRAP(proxy); \
        return generic(proxy); \
    }

#define WRAP_BINARY(method, generic) \
    static PyObject * \
    method(PyObject *x, PyObject *y) { \
        UNWRAP(x); \
        UNWRAP(y); \
        return generic(x, y); \
    }

#define WRAP_TERNARY(method, generic) \
    static PyObject * \
    method(PyObject *proxy, PyObject *v, PyObject *w) { \
        UNWRAP(proxy); \
        UNWRAP(v); \
        if (w != NULL) \
            UNWRAP(w); \
        return generic(proxy, v, w); \
    }

#define WRAP_METHOD(method, special) \
    static PyObject * \
    method(PyObject *proxy) { \
            UNWRAP(proxy); \
                return PyObject_CallMethod(proxy, special, ""); \
        }

WRAP_BINARY(proxy_getattr, PyObject_GetAttr)
WRAP_UNARY(proxy_str, PyObject_Str)
WRAP_TERNARY(proxy_call, PyEval_CallObjectWithKeywords)

static PyObject *
proxy_repr(PyWeakReference *proxy)
{
    char buf[160];
    PyOS_snprintf(buf, sizeof(buf),
                  "<weakproxy at %p to %.100s at %p>", proxy,
                  Py_TYPE(PyWeakref_GET_OBJECT(proxy))->tp_name,
                  PyWeakref_GET_OBJECT(proxy));
    return PyString_FromString(buf);
}

static int
proxy_setattr(PyWeakReference *proxy, PyObject *name, PyObject *value)
{
    if (!proxy_checkref(proxy))
        return -1;
    return PyObject_SetAttr(PyWeakref_GET_OBJECT(proxy), name, value);
}

static int
proxy_compare(PyObject *proxy, PyObject *v)
{
    UNWRAP_I(proxy);
    UNWRAP_I(v);
    return PyObject_Compare(proxy, v);
}

WRAP_BINARY(proxy_add, PyNumber_Add)
WRAP_BINARY(proxy_sub, PyNumber_Subtract)
WRAP_BINARY(proxy_mul, PyNumber_Multiply)
WRAP_BINARY(proxy_div, PyNumber_Divide)
WRAP_BINARY(proxy_floor_div, PyNumber_FloorDivide)
WRAP_BINARY(proxy_true_div, PyNumber_TrueDivide)
WRAP_BINARY(proxy_mod, PyNumber_Remainder)
WRAP_BINARY(proxy_divmod, PyNumber_Divmod)
WRAP_TERNARY(proxy_pow, PyNumber_Power)
WRAP_UNARY(proxy_neg, PyNumber_Negative)
WRAP_UNARY(proxy_pos, PyNumber_Positive)
WRAP_UNARY(proxy_abs, PyNumber_Absolute)
WRAP_UNARY(proxy_invert, PyNumber_Invert)
WRAP_BINARY(proxy_lshift, PyNumber_Lshift)
WRAP_BINARY(proxy_rshift, PyNumber_Rshift)
WRAP_BINARY(proxy_and, PyNumber_And)
WRAP_BINARY(proxy_xor, PyNumber_Xor)
WRAP_BINARY(proxy_or, PyNumber_Or)
WRAP_UNARY(proxy_int, PyNumber_Int)
WRAP_UNARY(proxy_long, PyNumber_Long)
WRAP_UNARY(proxy_float, PyNumber_Float)
WRAP_BINARY(proxy_iadd, PyNumber_InPlaceAdd)
WRAP_BINARY(proxy_isub, PyNumber_InPlaceSubtract)
WRAP_BINARY(proxy_imul, PyNumber_InPlaceMultiply)
WRAP_BINARY(proxy_idiv, PyNumber_InPlaceDivide)
WRAP_BINARY(proxy_ifloor_div, PyNumber_InPlaceFloorDivide)
WRAP_BINARY(proxy_itrue_div, PyNumber_InPlaceTrueDivide)
WRAP_BINARY(proxy_imod, PyNumber_InPlaceRemainder)
WRAP_TERNARY(proxy_ipow, PyNumber_InPlacePower)
WRAP_BINARY(proxy_ilshift, PyNumber_InPlaceLshift)
WRAP_BINARY(proxy_irshift, PyNumber_InPlaceRshift)
WRAP_BINARY(proxy_iand, PyNumber_InPlaceAnd)
WRAP_BINARY(proxy_ixor, PyNumber_InPlaceXor)
WRAP_BINARY(proxy_ior, PyNumber_InPlaceOr)
WRAP_UNARY(proxy_index, PyNumber_Index)

static int
proxy_nonzero(PyWeakReference *proxy)
{
    PyObject *o = PyWeakref_GET_OBJECT(proxy);
    if (!proxy_checkref(proxy))
        return -1;
    return PyObject_IsTrue(o);
}

static void
proxy_dealloc(PyWeakReference *self)
{
    if (self->wr_callback != NULL)
        PyObject_GC_UnTrack((PyObject *)self);
    clear_weakref(self);
    PyObject_GC_Del(self);
}

static PyObject *
proxy_slice(PyWeakReference *proxy, Py_ssize_t i, Py_ssize_t j)
{
    if (!proxy_checkref(proxy))
        return NULL;
    return PySequence_GetSlice(PyWeakref_GET_OBJECT(proxy), i, j);
}

static int
proxy_ass_slice(PyWeakReference *proxy, Py_ssize_t i, Py_ssize_t j, PyObject *value)
{
    if (!proxy_checkref(proxy))
        return -1;
    return PySequence_SetSlice(PyWeakref_GET_OBJECT(proxy), i, j, value);
}

static int
proxy_contains(PyWeakReference *proxy, PyObject *value)
{
    if (!proxy_checkref(proxy))
        return -1;
    return PySequence_Contains(PyWeakref_GET_OBJECT(proxy), value);
}

static Py_ssize_t
proxy_length(PyWeakReference *proxy)
{
    if (!proxy_checkref(proxy))
        return -1;
    return PyObject_Length(PyWeakref_GET_OBJECT(proxy));
}

WRAP_BINARY(proxy_getitem, PyObject_GetItem)

static int
proxy_setitem(PyWeakReference *proxy, PyObject *key, PyObject *value)
{
    if (!proxy_checkref(proxy))
        return -1;

    if (value == NULL)
        return PyObject_DelItem(PyWeakref_GET_OBJECT(proxy), key);
    else
        return PyObject_SetItem(PyWeakref_GET_OBJECT(proxy), key, value);
}

static PyObject *
proxy_iter(PyWeakReference *proxy)
{
    if (!proxy_checkref(proxy))
        return NULL;
    return PyObject_GetIter(PyWeakref_GET_OBJECT(proxy));
}

static PyObject *
proxy_iternext(PyWeakReference *proxy)
{
    if (!proxy_checkref(proxy))
        return NULL;
    return PyIter_Next(PyWeakref_GET_OBJECT(proxy));
}

WRAP_METHOD(proxy_unicode, "__unicode__");

static PyMethodDef proxy_methods[] = {
        {"__unicode__", (PyCFunction)proxy_unicode, METH_NOARGS},
        {NULL, NULL}
};

static PyNumberMethods proxy_as_number = {
    proxy_add,
    proxy_sub,
    proxy_mul,
    proxy_div,
    proxy_mod,
    proxy_divmod,
    proxy_pow,
    proxy_neg,
    proxy_pos,
    proxy_abs,
    (inquiry)proxy_nonzero,
    proxy_invert,
    proxy_lshift,
    proxy_rshift,
    proxy_and,
    proxy_xor,
    proxy_or,
    0,
    proxy_int,
    proxy_long,
    proxy_float,
    0,
    0,
    proxy_iadd,
    proxy_isub,
    proxy_imul,
    proxy_idiv,
    proxy_imod,
    proxy_ipow,
    proxy_ilshift,
    proxy_irshift,
    proxy_iand,
    proxy_ixor,
    proxy_ior,
    proxy_floor_div,
    proxy_true_div,
    proxy_ifloor_div,
    proxy_itrue_div,
    proxy_index,
};

static PySequenceMethods proxy_as_sequence = {
    (lenfunc)proxy_length,
    0,
    0,
    0,
    (ssizessizeargfunc)proxy_slice,
    0,
    (ssizessizeobjargproc)proxy_ass_slice,
    (objobjproc)proxy_contains,
};

static PyMappingMethods proxy_as_mapping = {
    (lenfunc)proxy_length,
    proxy_getitem,
    (objobjargproc)proxy_setitem,
};

PyTypeObject
_PyWeakref_ProxyType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "weakproxy",
    sizeof(PyWeakReference),
    0,

    (destructor)proxy_dealloc,
    0,
    0,
    0,
    proxy_compare,
    (reprfunc)proxy_repr,
    &proxy_as_number,
    &proxy_as_sequence,
    &proxy_as_mapping,
    0,
    0,
    proxy_str,
    proxy_getattr,
    (setattrofunc)proxy_setattr,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC
    | Py_TPFLAGS_CHECKTYPES,
    0,
    (traverseproc)gc_traverse,
    (inquiry)gc_clear,
    0,
    0,
    (getiterfunc)proxy_iter,
    (iternextfunc)proxy_iternext,
        proxy_methods,
};

PyTypeObject
_PyWeakref_CallableProxyType = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "weakcallableproxy",
    sizeof(PyWeakReference),
    0,

    (destructor)proxy_dealloc,
    0,
    0,
    0,
    proxy_compare,
    (unaryfunc)proxy_repr,
    &proxy_as_number,
    &proxy_as_sequence,
    &proxy_as_mapping,
    0,
    proxy_call,
    proxy_str,
    proxy_getattr,
    (setattrofunc)proxy_setattr,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC
    | Py_TPFLAGS_CHECKTYPES,
    0,
    (traverseproc)gc_traverse,
    (inquiry)gc_clear,
    0,
    0,
    (getiterfunc)proxy_iter,
    (iternextfunc)proxy_iternext,
};

PyObject *
PyWeakref_NewRef(PyObject *ob, PyObject *callback)
{
    PyWeakReference *result = NULL;
    PyWeakReference **list;
    PyWeakReference *ref, *proxy;

    if (!PyType_SUPPORTS_WEAKREFS(Py_TYPE(ob))) {
        PyErr_Format(PyExc_TypeError,
                     "cannot create weak reference to '%s' object",
                     Py_TYPE(ob)->tp_name);
        return NULL;
    }
    list = GET_WEAKREFS_LISTPTR(ob);
    get_basic_refs(*list, &ref, &proxy);
    if (callback == Py_None)
        callback = NULL;
    if (callback == NULL)

        result = ref;
    if (result != NULL)
        Py_INCREF(result);
    else {

        result = new_weakref(ob, callback);
        if (result != NULL) {
            get_basic_refs(*list, &ref, &proxy);
            if (callback == NULL) {
                if (ref == NULL)
                    insert_head(result, list);
                else {

                    Py_DECREF(result);
                    Py_INCREF(ref);
                    result = ref;
                }
            }
            else {
                PyWeakReference *prev;

                prev = (proxy == NULL) ? ref : proxy;
                if (prev == NULL)
                    insert_head(result, list);
                else
                    insert_after(result, prev);
            }
        }
    }
    return (PyObject *) result;
}

PyObject *
PyWeakref_NewProxy(PyObject *ob, PyObject *callback)
{
    PyWeakReference *result = NULL;
    PyWeakReference **list;
    PyWeakReference *ref, *proxy;

    if (!PyType_SUPPORTS_WEAKREFS(Py_TYPE(ob))) {
        PyErr_Format(PyExc_TypeError,
                     "cannot create weak reference to '%s' object",
                     Py_TYPE(ob)->tp_name);
        return NULL;
    }
    list = GET_WEAKREFS_LISTPTR(ob);
    get_basic_refs(*list, &ref, &proxy);
    if (callback == Py_None)
        callback = NULL;
    if (callback == NULL)

        result = proxy;
    if (result != NULL)
        Py_INCREF(result);
    else {

        result = new_weakref(ob, callback);
        if (result != NULL) {
            PyWeakReference *prev;

            if (PyCallable_Check(ob))
                Py_TYPE(result) = &_PyWeakref_CallableProxyType;
            else
                Py_TYPE(result) = &_PyWeakref_ProxyType;
            get_basic_refs(*list, &ref, &proxy);
            if (callback == NULL) {
                if (proxy != NULL) {

                    Py_DECREF(result);
                    Py_INCREF(result = proxy);
                    goto skip_insert;
                }
                prev = ref;
            }
            else
                prev = (proxy == NULL) ? ref : proxy;

            if (prev == NULL)
                insert_head(result, list);
            else
                insert_after(result, prev);
        skip_insert:
            ;
        }
    }
    return (PyObject *) result;
}

PyObject *
PyWeakref_GetObject(PyObject *ref)
{
    if (ref == NULL || !PyWeakref_Check(ref)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return PyWeakref_GET_OBJECT(ref);
}

static void
handle_callback(PyWeakReference *ref, PyObject *callback)
{
    PyObject *cbresult = PyObject_CallFunctionObjArgs(callback, ref, NULL);

    if (cbresult == NULL)
        PyErr_WriteUnraisable(callback);
    else
        Py_DECREF(cbresult);
}

void
PyObject_ClearWeakRefs(PyObject *object)
{
    PyWeakReference **list;

    if (object == NULL
        || !PyType_SUPPORTS_WEAKREFS(Py_TYPE(object))
        || object->ob_refcnt != 0) {
        PyErr_BadInternalCall();
        return;
    }
    list = GET_WEAKREFS_LISTPTR(object);

    if (*list != NULL && (*list)->wr_callback == NULL) {
        clear_weakref(*list);
        if (*list != NULL && (*list)->wr_callback == NULL)
            clear_weakref(*list);
    }
    if (*list != NULL) {
        PyWeakReference *current = *list;
        Py_ssize_t count = _PyWeakref_GetWeakrefCount(current);
        PyObject *err_type, *err_value, *err_tb;

        PyErr_Fetch(&err_type, &err_value, &err_tb);
        if (count == 1) {
            PyObject *callback = current->wr_callback;

            current->wr_callback = NULL;
            clear_weakref(current);
            if (callback != NULL) {
                if (current->ob_refcnt > 0)
                    handle_callback(current, callback);
                Py_DECREF(callback);
            }
        }
        else {
            PyObject *tuple;
            Py_ssize_t i = 0;

            tuple = PyTuple_New(count * 2);
            if (tuple == NULL) {
                _PyErr_ReplaceException(err_type, err_value, err_tb);
                return;
            }

            for (i = 0; i < count; ++i) {
                PyWeakReference *next = current->wr_next;

                if (current->ob_refcnt > 0)
                {
                    Py_INCREF(current);
                    PyTuple_SET_ITEM(tuple, i * 2, (PyObject *) current);
                    PyTuple_SET_ITEM(tuple, i * 2 + 1, current->wr_callback);
                }
                else {
                    Py_DECREF(current->wr_callback);
                }
                current->wr_callback = NULL;
                clear_weakref(current);
                current = next;
            }
            for (i = 0; i < count; ++i) {
                PyObject *callback = PyTuple_GET_ITEM(tuple, i * 2 + 1);

                if (callback != NULL) {
                    PyObject *item = PyTuple_GET_ITEM(tuple, i * 2);
                    handle_callback((PyWeakReference *)item, callback);
                }
            }
            Py_DECREF(tuple);
        }
        assert(!PyErr_Occurred());
        PyErr_Restore(err_type, err_value, err_tb);
    }
}
