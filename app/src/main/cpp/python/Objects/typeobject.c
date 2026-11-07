#include "Python.h"
#include "structmember.h"

#include <ctype.h>

#define MCACHE_MAX_ATTR_SIZE    100
#define MCACHE_SIZE_EXP         12
#define MCACHE_HASH(version, name_hash)                                 \
        (((unsigned int)(version) ^ (unsigned int)(name_hash))          \
         & ((1 << MCACHE_SIZE_EXP) - 1))

#define MCACHE_HASH_METHOD(type, name)                                  \
        MCACHE_HASH((type)->tp_version_tag,                     \
                    ((PyStringObject *)(name))->ob_shash)
#define MCACHE_CACHEABLE_NAME(name)                                     \
        PyString_CheckExact(name) &&                            \
        PyString_GET_SIZE(name) <= MCACHE_MAX_ATTR_SIZE

struct method_cache_entry {
    unsigned int version;
    PyObject *name;
    PyObject *value;
};

static struct method_cache_entry method_cache[1 << MCACHE_SIZE_EXP];
static unsigned int next_version_tag = 0;

#define MCACHE_STATS 0

#if MCACHE_STATS
static size_t method_cache_hits = 0;
static size_t method_cache_misses = 0;
static size_t method_cache_collisions = 0;
#endif

unsigned int
PyType_ClearCache(void)
{
    Py_ssize_t i;
    unsigned int cur_version_tag = next_version_tag - 1;

#if MCACHE_STATS
    size_t total = method_cache_hits + method_cache_collisions + method_cache_misses;
    fprintf(stderr, "-- Method cache hits        = %zd (%d%%)\n",
            method_cache_hits, (int) (100.0 * method_cache_hits / total));
    fprintf(stderr, "-- Method cache true misses = %zd (%d%%)\n",
            method_cache_misses, (int) (100.0 * method_cache_misses / total));
    fprintf(stderr, "-- Method cache collisions  = %zd (%d%%)\n",
            method_cache_collisions, (int) (100.0 * method_cache_collisions / total));
    fprintf(stderr, "-- Method cache size        = %zd KB\n",
            sizeof(method_cache) / 1024);
#endif

    for (i = 0; i < (1 << MCACHE_SIZE_EXP); i++) {
        method_cache[i].version = 0;
        Py_CLEAR(method_cache[i].name);
        method_cache[i].value = NULL;
    }
    next_version_tag = 0;

    PyType_Modified(&PyBaseObject_Type);
    return cur_version_tag;
}

void
PyType_Modified(PyTypeObject *type)
{

    PyObject *raw, *ref;
    Py_ssize_t i, n;

    if (!PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG))
        return;

    raw = type->tp_subclasses;
    if (raw != NULL) {
        n = PyList_GET_SIZE(raw);
        for (i = 0; i < n; i++) {
            ref = PyList_GET_ITEM(raw, i);
            ref = PyWeakref_GET_OBJECT(ref);
            if (ref != Py_None) {
                PyType_Modified((PyTypeObject *)ref);
            }
        }
    }
    type->tp_flags &= ~Py_TPFLAGS_VALID_VERSION_TAG;
}

static void
type_mro_modified(PyTypeObject *type, PyObject *bases) {

    Py_ssize_t i, n;
    int clear = 0;

    if (!PyType_HasFeature(type, Py_TPFLAGS_HAVE_VERSION_TAG))
        return;

    n = PyTuple_GET_SIZE(bases);
    for (i = 0; i < n; i++) {
        PyObject *b = PyTuple_GET_ITEM(bases, i);
        PyTypeObject *cls;

        if (!PyType_Check(b) ) {
            clear = 1;
            break;
        }

        cls = (PyTypeObject *)b;

        if (!PyType_HasFeature(cls, Py_TPFLAGS_HAVE_VERSION_TAG) ||
            !PyType_IsSubtype(type, cls)) {
            clear = 1;
            break;
        }
    }

    if (clear)
        type->tp_flags &= ~(Py_TPFLAGS_HAVE_VERSION_TAG|
                            Py_TPFLAGS_VALID_VERSION_TAG);
}

static int
assign_version_tag(PyTypeObject *type)
{

    Py_ssize_t i, n;
    PyObject *bases;

    if (PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG))
        return 1;
    if (!PyType_HasFeature(type, Py_TPFLAGS_HAVE_VERSION_TAG))
        return 0;
    if (!PyType_HasFeature(type, Py_TPFLAGS_READY))
        return 0;

    type->tp_version_tag = next_version_tag++;

    if (type->tp_version_tag == 0) {

        for (i = 0; i < (1 << MCACHE_SIZE_EXP); i++) {
            method_cache[i].value = NULL;
            Py_INCREF(Py_None);
            Py_XSETREF(method_cache[i].name, Py_None);
        }

        PyType_Modified(&PyBaseObject_Type);
        return 1;
    }
    bases = type->tp_bases;
    n = PyTuple_GET_SIZE(bases);
    for (i = 0; i < n; i++) {
        PyObject *b = PyTuple_GET_ITEM(bases, i);
        assert(PyType_Check(b));
        if (!assign_version_tag((PyTypeObject *)b))
            return 0;
    }
    type->tp_flags |= Py_TPFLAGS_VALID_VERSION_TAG;
    return 1;
}

static PyMemberDef type_members[] = {
    {"__basicsize__", T_PYSSIZET, offsetof(PyTypeObject,tp_basicsize),READONLY},
    {"__itemsize__", T_PYSSIZET, offsetof(PyTypeObject, tp_itemsize), READONLY},
    {"__flags__", T_LONG, offsetof(PyTypeObject, tp_flags), READONLY},
    {"__weakrefoffset__", T_LONG,
     offsetof(PyTypeObject, tp_weaklistoffset), READONLY},
    {"__base__", T_OBJECT, offsetof(PyTypeObject, tp_base), READONLY},
    {"__dictoffset__", T_LONG,
     offsetof(PyTypeObject, tp_dictoffset), READONLY},
    {"__mro__", T_OBJECT, offsetof(PyTypeObject, tp_mro), READONLY},
    {0}
};

static PyObject *
type_name(PyTypeObject *type, void *context)
{
    const char *s;

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        PyHeapTypeObject* et = (PyHeapTypeObject*)type;

        Py_INCREF(et->ht_name);
        return et->ht_name;
    }
    else {
        s = strrchr(type->tp_name, '.');
        if (s == NULL)
            s = type->tp_name;
        else
            s++;
        return PyString_FromString(s);
    }
}

static int
type_set_name(PyTypeObject *type, PyObject *value, void *context)
{
    PyHeapTypeObject* et;
    PyObject *tmp;

    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(PyExc_TypeError,
                     "can't set %s.__name__", type->tp_name);
        return -1;
    }
    if (!value) {
        PyErr_Format(PyExc_TypeError,
                     "can't delete %s.__name__", type->tp_name);
        return -1;
    }
    if (!PyString_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "can only assign string to %s.__name__, not '%s'",
                     type->tp_name, Py_TYPE(value)->tp_name);
        return -1;
    }
    if (strlen(PyString_AS_STRING(value))
        != (size_t)PyString_GET_SIZE(value)) {
        PyErr_SetString(PyExc_ValueError,
                        "type name must not contain null characters");
        return -1;
    }

    et = (PyHeapTypeObject*)type;

    Py_INCREF(value);

    tmp = et->ht_name;
    et->ht_name = value;

    type->tp_name = PyString_AS_STRING(value);
    Py_DECREF(tmp);

    return 0;
}

static PyObject *
type_module(PyTypeObject *type, void *context)
{
    PyObject *mod;
    char *s;

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        mod = PyDict_GetItemString(type->tp_dict, "__module__");
        if (!mod) {
            PyErr_Format(PyExc_AttributeError, "__module__");
            return 0;
        }
        Py_XINCREF(mod);
        return mod;
    }
    else {
        s = strrchr(type->tp_name, '.');
        if (s != NULL)
            return PyString_FromStringAndSize(
                type->tp_name, (Py_ssize_t)(s - type->tp_name));
        return PyString_FromString("__builtin__");
    }
}

static int
type_set_module(PyTypeObject *type, PyObject *value, void *context)
{
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(PyExc_TypeError,
                     "can't set %s.__module__", type->tp_name);
        return -1;
    }
    if (!value) {
        PyErr_Format(PyExc_TypeError,
                     "can't delete %s.__module__", type->tp_name);
        return -1;
    }

    PyType_Modified(type);

    return PyDict_SetItemString(type->tp_dict, "__module__", value);
}

static PyObject *
type_abstractmethods(PyTypeObject *type, void *context)
{
    PyObject *mod = NULL;

    if (type != &PyType_Type)
        mod = PyDict_GetItemString(type->tp_dict, "__abstractmethods__");
    if (!mod) {
        PyErr_SetString(PyExc_AttributeError, "__abstractmethods__");
        return NULL;
    }
    Py_XINCREF(mod);
    return mod;
}

static int
type_set_abstractmethods(PyTypeObject *type, PyObject *value, void *context)
{

    int abstract, res;
    if (value != NULL) {
        abstract = PyObject_IsTrue(value);
        if (abstract < 0)
            return -1;
        res = PyDict_SetItemString(type->tp_dict, "__abstractmethods__", value);
    }
    else {
        abstract = 0;
        res = PyDict_DelItemString(type->tp_dict, "__abstractmethods__");
        if (res && PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyErr_SetString(PyExc_AttributeError, "__abstractmethods__");
            return -1;
        }
    }
    if (res == 0) {
        PyType_Modified(type);
        if (abstract)
            type->tp_flags |= Py_TPFLAGS_IS_ABSTRACT;
        else
            type->tp_flags &= ~Py_TPFLAGS_IS_ABSTRACT;
    }
    return res;
}

static PyObject *
type_get_bases(PyTypeObject *type, void *context)
{
    Py_INCREF(type->tp_bases);
    return type->tp_bases;
}

static PyTypeObject *best_base(PyObject *);
static int mro_internal(PyTypeObject *);
static int compatible_for_assignment(PyTypeObject *, PyTypeObject *, char *);
static int add_subclass(PyTypeObject*, PyTypeObject*);
static void remove_subclass(PyTypeObject *, PyTypeObject *);
static void update_all_slots(PyTypeObject *);

typedef int (*update_callback)(PyTypeObject *, void *);
static int update_subclasses(PyTypeObject *type, PyObject *name,
                             update_callback callback, void *data);
static int recurse_down_subclasses(PyTypeObject *type, PyObject *name,
                                   update_callback callback, void *data);

static int
mro_subclasses(PyTypeObject *type, PyObject* temp)
{
    PyTypeObject *subclass;
    PyObject *ref, *subclasses, *old_mro;
    Py_ssize_t i, n;

    subclasses = type->tp_subclasses;
    if (subclasses == NULL)
        return 0;
    assert(PyList_Check(subclasses));
    n = PyList_GET_SIZE(subclasses);
    for (i = 0; i < n; i++) {
        ref = PyList_GET_ITEM(subclasses, i);
        assert(PyWeakref_CheckRef(ref));
        subclass = (PyTypeObject *)PyWeakref_GET_OBJECT(ref);
        assert(subclass != NULL);
        if ((PyObject *)subclass == Py_None)
            continue;
        assert(PyType_Check(subclass));
        old_mro = subclass->tp_mro;
        if (mro_internal(subclass) < 0) {
            subclass->tp_mro = old_mro;
            return -1;
        }
        else {
            PyObject* tuple;
            tuple = PyTuple_Pack(2, subclass, old_mro);
            Py_DECREF(old_mro);
            if (!tuple)
                return -1;
            if (PyList_Append(temp, tuple) < 0)
                return -1;
            Py_DECREF(tuple);
        }
        if (mro_subclasses(subclass, temp) < 0)
            return -1;
    }
    return 0;
}

static int
type_set_bases(PyTypeObject *type, PyObject *value, void *context)
{
    Py_ssize_t i;
    int r = 0;
    PyObject *ob, *temp;
    PyTypeObject *new_base, *old_base;
    PyObject *old_bases, *old_mro;

    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(PyExc_TypeError,
                     "can't set %s.__bases__", type->tp_name);
        return -1;
    }
    if (!value) {
        PyErr_Format(PyExc_TypeError,
                     "can't delete %s.__bases__", type->tp_name);
        return -1;
    }
    if (!PyTuple_Check(value)) {
        PyErr_Format(PyExc_TypeError,
             "can only assign tuple to %s.__bases__, not %s",
                 type->tp_name, Py_TYPE(value)->tp_name);
        return -1;
    }
    if (PyTuple_GET_SIZE(value) == 0) {
        PyErr_Format(PyExc_TypeError,
             "can only assign non-empty tuple to %s.__bases__, not ()",
                 type->tp_name);
        return -1;
    }
    for (i = 0; i < PyTuple_GET_SIZE(value); i++) {
        ob = PyTuple_GET_ITEM(value, i);
        if (!PyClass_Check(ob) && !PyType_Check(ob)) {
            PyErr_Format(
                PyExc_TypeError,
    "%s.__bases__ must be tuple of old- or new-style classes, not '%s'",
                            type->tp_name, Py_TYPE(ob)->tp_name);
                    return -1;
        }
        if (PyType_Check(ob)) {
            if (PyType_IsSubtype((PyTypeObject*)ob, type)) {
                PyErr_SetString(PyExc_TypeError,
            "a __bases__ item causes an inheritance cycle");
                return -1;
            }
        }
    }

    new_base = best_base(value);

    if (!new_base) {
        return -1;
    }

    if (!compatible_for_assignment(type->tp_base, new_base, "__bases__"))
        return -1;

    Py_INCREF(new_base);
    Py_INCREF(value);

    old_bases = type->tp_bases;
    old_base = type->tp_base;
    old_mro = type->tp_mro;

    type->tp_bases = value;
    type->tp_base = new_base;

    if (mro_internal(type) < 0) {
        goto bail;
    }

    temp = PyList_New(0);
    if (!temp)
        goto bail;

    r = mro_subclasses(type, temp);

    if (r < 0) {
        for (i = 0; i < PyList_Size(temp); i++) {
            PyTypeObject* cls;
            PyObject* mro;
            PyArg_UnpackTuple(PyList_GET_ITEM(temp, i),
                             "", 2, 2, &cls, &mro);
            Py_INCREF(mro);
            ob = cls->tp_mro;
            cls->tp_mro = mro;
            Py_DECREF(ob);
        }
        Py_DECREF(temp);
        goto bail;
    }

    Py_DECREF(temp);

    for (i = PyTuple_GET_SIZE(old_bases) - 1; i >= 0; i--) {
        ob = PyTuple_GET_ITEM(old_bases, i);
        if (PyType_Check(ob)) {
            remove_subclass(
                (PyTypeObject*)ob, type);
        }
    }

    for (i = PyTuple_GET_SIZE(value) - 1; i >= 0; i--) {
        ob = PyTuple_GET_ITEM(value, i);
        if (PyType_Check(ob)) {
            if (add_subclass((PyTypeObject*)ob, type) < 0)
                r = -1;
        }
    }

    update_all_slots(type);

    Py_DECREF(old_bases);
    Py_DECREF(old_base);
    Py_DECREF(old_mro);

    return r;

  bail:
    Py_DECREF(type->tp_bases);
    Py_DECREF(type->tp_base);
    if (type->tp_mro != old_mro) {
        Py_DECREF(type->tp_mro);
    }

    type->tp_bases = old_bases;
    type->tp_base = old_base;
    type->tp_mro = old_mro;

    return -1;
}

static PyObject *
type_dict(PyTypeObject *type, void *context)
{
    if (type->tp_dict == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    return PyDictProxy_New(type->tp_dict);
}

static PyObject *
type_get_doc(PyTypeObject *type, void *context)
{
    PyObject *result;
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE) && type->tp_doc != NULL)
        return PyString_FromString(type->tp_doc);
    result = PyDict_GetItemString(type->tp_dict, "__doc__");
    if (result == NULL) {
        result = Py_None;
        Py_INCREF(result);
    }
    else if (Py_TYPE(result)->tp_descr_get) {
        result = Py_TYPE(result)->tp_descr_get(result, NULL,
                                               (PyObject *)type);
    }
    else {
        Py_INCREF(result);
    }
    return result;
}

static PyObject *
type___instancecheck__(PyObject *type, PyObject *inst)
{
    switch (_PyObject_RealIsInstance(inst, type)) {
    case -1:
        return NULL;
    case 0:
        Py_RETURN_FALSE;
    default:
        Py_RETURN_TRUE;
    }
}

static PyObject *
type___subclasscheck__(PyObject *type, PyObject *inst)
{
    switch (_PyObject_RealIsSubclass(inst, type)) {
    case -1:
        return NULL;
    case 0:
        Py_RETURN_FALSE;
    default:
        Py_RETURN_TRUE;
    }
}

static PyGetSetDef type_getsets[] = {
    {"__name__", (getter)type_name, (setter)type_set_name, NULL},
    {"__bases__", (getter)type_get_bases, (setter)type_set_bases, NULL},
    {"__module__", (getter)type_module, (setter)type_set_module, NULL},
    {"__abstractmethods__", (getter)type_abstractmethods,
     (setter)type_set_abstractmethods, NULL},
    {"__dict__",  (getter)type_dict,  NULL, NULL},
    {"__doc__", (getter)type_get_doc, NULL, NULL},
    {0}
};

static PyObject*
type_richcompare(PyObject *v, PyObject *w, int op)
{
    PyObject *result;
    Py_uintptr_t vv, ww;
    int c;

    if (!PyType_Check(v) || !PyType_Check(w) ||

        Py_TYPE(v)->tp_compare || Py_TYPE(w)->tp_compare) {
        result = Py_NotImplemented;
        goto out;
    }

    if (Py_Py3kWarningFlag && op != Py_EQ && op != Py_NE &&
        PyErr_WarnEx(PyExc_DeprecationWarning,
                   "type inequality comparisons not supported "
                   "in 3.x", 1) < 0) {
        return NULL;
    }

    vv = (Py_uintptr_t)v;
    ww = (Py_uintptr_t)w;
    switch (op) {
    case Py_LT: c = vv <  ww; break;
    case Py_LE: c = vv <= ww; break;
    case Py_EQ: c = vv == ww; break;
    case Py_NE: c = vv != ww; break;
    case Py_GT: c = vv >  ww; break;
    case Py_GE: c = vv >= ww; break;
    default:
        result = Py_NotImplemented;
        goto out;
    }
    result = c ? Py_True : Py_False;

  out:
    Py_INCREF(result);
    return result;
}

static PyObject *
type_repr(PyTypeObject *type)
{
    PyObject *mod, *name, *rtn;
    char *kind;

    mod = type_module(type, NULL);
    if (mod == NULL)
        PyErr_Clear();
    else if (!PyString_Check(mod)) {
        Py_DECREF(mod);
        mod = NULL;
    }
    name = type_name(type, NULL);
    if (name == NULL) {
        Py_XDECREF(mod);
        return NULL;
    }

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        kind = "class";
    else
        kind = "type";

    if (mod != NULL && strcmp(PyString_AS_STRING(mod), "__builtin__")) {
        rtn = PyString_FromFormat("<%s '%s.%s'>",
                                  kind,
                                  PyString_AS_STRING(mod),
                                  PyString_AS_STRING(name));
    }
    else
        rtn = PyString_FromFormat("<%s '%s'>", kind, type->tp_name);

    Py_XDECREF(mod);
    Py_DECREF(name);
    return rtn;
}

static PyObject *
type_call(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *obj;

    if (type->tp_new == NULL) {
        PyErr_Format(PyExc_TypeError,
                     "cannot create '%.100s' instances",
                     type->tp_name);
        return NULL;
    }

    obj = type->tp_new(type, args, kwds);
    if (obj != NULL) {

        if (type == &PyType_Type &&
            PyTuple_Check(args) && PyTuple_GET_SIZE(args) == 1 &&
            (kwds == NULL ||
             (PyDict_Check(kwds) && PyDict_Size(kwds) == 0)))
            return obj;

        if (!PyType_IsSubtype(obj->ob_type, type))
            return obj;
        type = obj->ob_type;
        if (PyType_HasFeature(type, Py_TPFLAGS_HAVE_CLASS) &&
            type->tp_init != NULL &&
            type->tp_init(obj, args, kwds) < 0) {
            Py_DECREF(obj);
            obj = NULL;
        }
    }
    return obj;
}

PyObject *
PyType_GenericAlloc(PyTypeObject *type, Py_ssize_t nitems)
{
    PyObject *obj;
    const size_t size = _PyObject_VAR_SIZE(type, nitems+1);

    if (PyType_IS_GC(type))
        obj = _PyObject_GC_Malloc(size);
    else
        obj = (PyObject *)PyObject_MALLOC(size);

    if (obj == NULL)
        return PyErr_NoMemory();

    memset(obj, '\0', size);

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        Py_INCREF(type);

    if (type->tp_itemsize == 0)
        (void)PyObject_INIT(obj, type);
    else
        (void) PyObject_INIT_VAR((PyVarObject *)obj, type, nitems);

    if (PyType_IS_GC(type))
        _PyObject_GC_TRACK(obj);
    return obj;
}

PyObject *
PyType_GenericNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    return type->tp_alloc(type, 0);
}

static int
traverse_slots(PyTypeObject *type, PyObject *self, visitproc visit, void *arg)
{
    Py_ssize_t i, n;
    PyMemberDef *mp;

    n = Py_SIZE(type);
    mp = PyHeapType_GET_MEMBERS((PyHeapTypeObject *)type);
    for (i = 0; i < n; i++, mp++) {
        if (mp->type == T_OBJECT_EX) {
            char *addr = (char *)self + mp->offset;
            PyObject *obj = *(PyObject **)addr;
            if (obj != NULL) {
                int err = visit(obj, arg);
                if (err)
                    return err;
            }
        }
    }
    return 0;
}

static int
subtype_traverse(PyObject *self, visitproc visit, void *arg)
{
    PyTypeObject *type, *base;
    traverseproc basetraverse;

    type = Py_TYPE(self);
    base = type;
    while ((basetraverse = base->tp_traverse) == subtype_traverse) {
        if (Py_SIZE(base)) {
            int err = traverse_slots(base, self, visit, arg);
            if (err)
                return err;
        }
        base = base->tp_base;
        assert(base);
    }

    if (type->tp_dictoffset != base->tp_dictoffset) {
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr && *dictptr)
            Py_VISIT(*dictptr);
    }

    if (type->tp_flags & Py_TPFLAGS_HEAPTYPE)

        Py_VISIT(type);

    if (basetraverse)
        return basetraverse(self, visit, arg);
    return 0;
}

static void
clear_slots(PyTypeObject *type, PyObject *self)
{
    Py_ssize_t i, n;
    PyMemberDef *mp;

    n = Py_SIZE(type);
    mp = PyHeapType_GET_MEMBERS((PyHeapTypeObject *)type);
    for (i = 0; i < n; i++, mp++) {
        if (mp->type == T_OBJECT_EX && !(mp->flags & READONLY)) {
            char *addr = (char *)self + mp->offset;
            PyObject *obj = *(PyObject **)addr;
            if (obj != NULL) {
                *(PyObject **)addr = NULL;
                Py_DECREF(obj);
            }
        }
    }
}

static int
subtype_clear(PyObject *self)
{
    PyTypeObject *type, *base;
    inquiry baseclear;

    type = Py_TYPE(self);
    base = type;
    while ((baseclear = base->tp_clear) == subtype_clear) {
        if (Py_SIZE(base))
            clear_slots(base, self);
        base = base->tp_base;
        assert(base);
    }

    if (type->tp_dictoffset != base->tp_dictoffset) {
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr && *dictptr)
            Py_CLEAR(*dictptr);
    }

    if (baseclear)
        return baseclear(self);
    return 0;
}

static void
subtype_dealloc(PyObject *self)
{
    PyTypeObject *type, *base;
    destructor basedealloc;
    PyThreadState *tstate = PyThreadState_GET();

    type = Py_TYPE(self);
    assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);

    if (!PyType_IS_GC(type)) {

        if (type->tp_del) {
            type->tp_del(self);
            if (self->ob_refcnt > 0)
                return;
        }

        base = type;
        while ((basedealloc = base->tp_dealloc) == subtype_dealloc) {
            assert(Py_SIZE(base) == 0);
            base = base->tp_base;
            assert(base);
        }

        type = Py_TYPE(self);

        assert(basedealloc);
        basedealloc(self);

        Py_DECREF(type);

        return;
    }

    PyObject_GC_UnTrack(self);
    ++_PyTrash_delete_nesting;
    ++ tstate->trash_delete_nesting;
    Py_TRASHCAN_SAFE_BEGIN(self);
    --_PyTrash_delete_nesting;
    -- tstate->trash_delete_nesting;

    base = type;
    while ((basedealloc = base->tp_dealloc) == subtype_dealloc) {
        base = base->tp_base;
        assert(base);
    }

    if (type->tp_weaklistoffset && !base->tp_weaklistoffset)
        PyObject_ClearWeakRefs(self);

    if (type->tp_del) {
        _PyObject_GC_TRACK(self);
        type->tp_del(self);
        if (self->ob_refcnt > 0)
            goto endlabel;
        else
            _PyObject_GC_UNTRACK(self);

        if (type->tp_weaklistoffset && !base->tp_weaklistoffset) {

            PyWeakReference **list = (PyWeakReference **) \
                PyObject_GET_WEAKREFS_LISTPTR(self);
            while (*list)
                _PyWeakref_ClearRef(*list);
        }
    }

    base = type;
    while (base->tp_dealloc == subtype_dealloc) {
        if (Py_SIZE(base))
            clear_slots(base, self);
        base = base->tp_base;
        assert(base);
    }

    if (type->tp_dictoffset && !base->tp_dictoffset) {
        PyObject **dictptr = _PyObject_GetDictPtr(self);
        if (dictptr != NULL) {
            PyObject *dict = *dictptr;
            if (dict != NULL) {
                Py_DECREF(dict);
                *dictptr = NULL;
            }
        }
    }

    type = Py_TYPE(self);

    if (PyType_IS_GC(base))
        _PyObject_GC_TRACK(self);
    assert(basedealloc);
    basedealloc(self);

    Py_DECREF(type);

  endlabel:
    ++_PyTrash_delete_nesting;
    ++ tstate->trash_delete_nesting;
    Py_TRASHCAN_SAFE_END(self);
    --_PyTrash_delete_nesting;
    -- tstate->trash_delete_nesting;

}

static PyTypeObject *solid_base(PyTypeObject *type);

int
PyType_IsSubtype(PyTypeObject *a, PyTypeObject *b)
{
    PyObject *mro;

    if (!(a->tp_flags & Py_TPFLAGS_HAVE_CLASS))
        return b == a || b == &PyBaseObject_Type;

    mro = a->tp_mro;
    if (mro != NULL) {

        Py_ssize_t i, n;
        assert(PyTuple_Check(mro));
        n = PyTuple_GET_SIZE(mro);
        for (i = 0; i < n; i++) {
            if (PyTuple_GET_ITEM(mro, i) == (PyObject *)b)
                return 1;
        }
        return 0;
    }
    else {

        do {
            if (a == b)
                return 1;
            a = a->tp_base;
        } while (a != NULL);
        return b == &PyBaseObject_Type;
    }
}

static PyObject *
lookup_maybe(PyObject *self, char *attrstr, PyObject **attrobj)
{
    PyObject *res;

    if (*attrobj == NULL) {
        *attrobj = PyString_InternFromString(attrstr);
        if (*attrobj == NULL)
            return NULL;
    }
    res = _PyType_Lookup(Py_TYPE(self), *attrobj);
    if (res != NULL) {
        descrgetfunc f;
        if ((f = Py_TYPE(res)->tp_descr_get) == NULL)
            Py_INCREF(res);
        else
            res = f(res, self, (PyObject *)(Py_TYPE(self)));
    }
    return res;
}

static PyObject *
lookup_method(PyObject *self, char *attrstr, PyObject **attrobj)
{
    PyObject *res = lookup_maybe(self, attrstr, attrobj);
    if (res == NULL && !PyErr_Occurred())
        PyErr_SetObject(PyExc_AttributeError, *attrobj);
    return res;
}

PyObject *
_PyObject_LookupSpecial(PyObject *self, char *attrstr, PyObject **attrobj)
{
    assert(!PyInstance_Check(self));
    return lookup_maybe(self, attrstr, attrobj);
}

static PyObject *
call_method(PyObject *o, char *name, PyObject **nameobj, char *format, ...)
{
    va_list va;
    PyObject *args, *func = 0, *retval;
    va_start(va, format);

    func = lookup_maybe(o, name, nameobj);
    if (func == NULL) {
        va_end(va);
        if (!PyErr_Occurred())
            PyErr_SetObject(PyExc_AttributeError, *nameobj);
        return NULL;
    }

    if (format && *format)
        args = Py_VaBuildValue(format, va);
    else
        args = PyTuple_New(0);

    va_end(va);

    if (args == NULL) {
        Py_DECREF(func);
        return NULL;
    }

    assert(PyTuple_Check(args));
    retval = PyObject_Call(func, args, NULL);

    Py_DECREF(args);
    Py_DECREF(func);

    return retval;
}

static PyObject *
call_maybe(PyObject *o, char *name, PyObject **nameobj, char *format, ...)
{
    va_list va;
    PyObject *args, *func = 0, *retval;
    va_start(va, format);

    func = lookup_maybe(o, name, nameobj);
    if (func == NULL) {
        va_end(va);
        if (!PyErr_Occurred()) {
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
        return NULL;
    }

    if (format && *format)
        args = Py_VaBuildValue(format, va);
    else
        args = PyTuple_New(0);

    va_end(va);

    if (args == NULL) {
        Py_DECREF(func);
        return NULL;
    }

    assert(PyTuple_Check(args));
    retval = PyObject_Call(func, args, NULL);

    Py_DECREF(args);
    Py_DECREF(func);

    return retval;
}

static int
fill_classic_mro(PyObject *mro, PyObject *cls)
{
    PyObject *bases, *base;
    Py_ssize_t i, n;

    assert(PyList_Check(mro));
    assert(PyClass_Check(cls));
    i = PySequence_Contains(mro, cls);
    if (i < 0)
        return -1;
    if (!i) {
        if (PyList_Append(mro, cls) < 0)
            return -1;
    }
    bases = ((PyClassObject *)cls)->cl_bases;
    assert(bases && PyTuple_Check(bases));
    n = PyTuple_GET_SIZE(bases);
    for (i = 0; i < n; i++) {
        base = PyTuple_GET_ITEM(bases, i);
        if (fill_classic_mro(mro, base) < 0)
            return -1;
    }
    return 0;
}

static PyObject *
classic_mro(PyObject *cls)
{
    PyObject *mro;

    assert(PyClass_Check(cls));
    mro = PyList_New(0);
    if (mro != NULL) {
        if (fill_classic_mro(mro, cls) == 0)
            return mro;
        Py_DECREF(mro);
    }
    return NULL;
}

static int
tail_contains(PyObject *list, int whence, PyObject *o) {
    Py_ssize_t j, size;
    size = PyList_GET_SIZE(list);

    for (j = whence+1; j < size; j++) {
        if (PyList_GET_ITEM(list, j) == o)
            return 1;
    }
    return 0;
}

static PyObject *
class_name(PyObject *cls)
{
    PyObject *name = PyObject_GetAttrString(cls, "__name__");
    if (name == NULL) {
        PyErr_Clear();
        Py_XDECREF(name);
        name = PyObject_Repr(cls);
    }
    if (name == NULL)
        return NULL;
    if (!PyString_Check(name)) {
        Py_DECREF(name);
        return NULL;
    }
    return name;
}

static int
check_duplicates(PyObject *list)
{
    Py_ssize_t i, j, n;

    n = PyList_GET_SIZE(list);
    for (i = 0; i < n; i++) {
        PyObject *o = PyList_GET_ITEM(list, i);
        for (j = i + 1; j < n; j++) {
            if (PyList_GET_ITEM(list, j) == o) {
                o = class_name(o);
                PyErr_Format(PyExc_TypeError,
                             "duplicate base class %s",
                             o ? PyString_AS_STRING(o) : "?");
                Py_XDECREF(o);
                return -1;
            }
        }
    }
    return 0;
}

static void
set_mro_error(PyObject *to_merge, int *remain)
{
    Py_ssize_t i, n, off, to_merge_size;
    char buf[1000];
    PyObject *k, *v;
    PyObject *set = PyDict_New();
    if (!set) return;

    to_merge_size = PyList_GET_SIZE(to_merge);
    for (i = 0; i < to_merge_size; i++) {
        PyObject *L = PyList_GET_ITEM(to_merge, i);
        if (remain[i] < PyList_GET_SIZE(L)) {
            PyObject *c = PyList_GET_ITEM(L, remain[i]);
            if (PyDict_SetItem(set, c, Py_None) < 0) {
                Py_DECREF(set);
                return;
            }
        }
    }
    n = PyDict_Size(set);

    off = PyOS_snprintf(buf, sizeof(buf), "Cannot create a \
consistent method resolution\norder (MRO) for bases");
    i = 0;
    while (PyDict_Next(set, &i, &k, &v) && (size_t)off < sizeof(buf)) {
        PyObject *name = class_name(k);
        off += PyOS_snprintf(buf + off, sizeof(buf) - off, " %s",
                             name ? PyString_AS_STRING(name) : "?");
        Py_XDECREF(name);
        if (--n && (size_t)(off+1) < sizeof(buf)) {
            buf[off++] = ',';
            buf[off] = '\0';
        }
    }
    PyErr_SetString(PyExc_TypeError, buf);
    Py_DECREF(set);
}

static int
pmerge(PyObject *acc, PyObject* to_merge) {
    Py_ssize_t i, j, to_merge_size, empty_cnt;
    int *remain;
    int ok;

    to_merge_size = PyList_GET_SIZE(to_merge);

    remain = (int *)PyMem_MALLOC(SIZEOF_INT*to_merge_size);
    if (remain == NULL)
        return -1;
    for (i = 0; i < to_merge_size; i++)
        remain[i] = 0;

  again:
    empty_cnt = 0;
    for (i = 0; i < to_merge_size; i++) {
        PyObject *candidate;

        PyObject *cur_list = PyList_GET_ITEM(to_merge, i);

        if (remain[i] >= PyList_GET_SIZE(cur_list)) {
            empty_cnt++;
            continue;
        }

        candidate = PyList_GET_ITEM(cur_list, remain[i]);
        for (j = 0; j < to_merge_size; j++) {
            PyObject *j_lst = PyList_GET_ITEM(to_merge, j);
            if (tail_contains(j_lst, remain[j], candidate)) {
                goto skip;
            }
        }
        ok = PyList_Append(acc, candidate);
        if (ok < 0) {
            PyMem_Free(remain);
            return -1;
        }
        for (j = 0; j < to_merge_size; j++) {
            PyObject *j_lst = PyList_GET_ITEM(to_merge, j);
            if (remain[j] < PyList_GET_SIZE(j_lst) &&
                PyList_GET_ITEM(j_lst, remain[j]) == candidate) {
                remain[j]++;
            }
        }
        goto again;
      skip: ;
    }

    if (empty_cnt == to_merge_size) {
        PyMem_FREE(remain);
        return 0;
    }
    set_mro_error(to_merge, remain);
    PyMem_FREE(remain);
    return -1;
}

static PyObject *
mro_implementation(PyTypeObject *type)
{
    Py_ssize_t i, n;
    int ok;
    PyObject *bases, *result;
    PyObject *to_merge, *bases_aslist;

    if (type->tp_dict == NULL) {
        if (PyType_Ready(type) < 0)
            return NULL;
    }

    bases = type->tp_bases;
    n = PyTuple_GET_SIZE(bases);

    to_merge = PyList_New(n+1);
    if (to_merge == NULL)
        return NULL;

    for (i = 0; i < n; i++) {
        PyObject *base = PyTuple_GET_ITEM(bases, i);
        PyObject *parentMRO;
        if (PyType_Check(base))
            parentMRO = PySequence_List(
                ((PyTypeObject*)base)->tp_mro);
        else
            parentMRO = classic_mro(base);
        if (parentMRO == NULL) {
            Py_DECREF(to_merge);
            return NULL;
        }

        PyList_SET_ITEM(to_merge, i, parentMRO);
    }

    bases_aslist = PySequence_List(bases);
    if (bases_aslist == NULL) {
        Py_DECREF(to_merge);
        return NULL;
    }

    if (check_duplicates(bases_aslist) < 0) {
        Py_DECREF(to_merge);
        Py_DECREF(bases_aslist);
        return NULL;
    }
    PyList_SET_ITEM(to_merge, n, bases_aslist);

    result = Py_BuildValue("[O]", (PyObject *)type);
    if (result == NULL) {
        Py_DECREF(to_merge);
        return NULL;
    }

    ok = pmerge(result, to_merge);
    Py_DECREF(to_merge);
    if (ok < 0) {
        Py_DECREF(result);
        return NULL;
    }

    return result;
}

static PyObject *
mro_external(PyObject *self)
{
    PyTypeObject *type = (PyTypeObject *)self;

    return mro_implementation(type);
}

static int
mro_internal(PyTypeObject *type)
{
    PyObject *mro, *result, *tuple;
    int checkit = 0;

    if (Py_TYPE(type) == &PyType_Type) {
        result = mro_implementation(type);
    }
    else {
        static PyObject *mro_str;
        checkit = 1;
        mro = lookup_method((PyObject *)type, "mro", &mro_str);
        if (mro == NULL)
            return -1;
        result = PyObject_CallObject(mro, NULL);
        Py_DECREF(mro);
    }
    if (result == NULL)
        return -1;
    tuple = PySequence_Tuple(result);
    Py_DECREF(result);
    if (tuple == NULL)
        return -1;
    if (checkit) {
        Py_ssize_t i, len;
        PyObject *cls;
        PyTypeObject *solid;

        solid = solid_base(type);

        len = PyTuple_GET_SIZE(tuple);

        for (i = 0; i < len; i++) {
            PyTypeObject *t;
            cls = PyTuple_GET_ITEM(tuple, i);
            if (PyClass_Check(cls))
                continue;
            else if (!PyType_Check(cls)) {
                PyErr_Format(PyExc_TypeError,
                 "mro() returned a non-class ('%.500s')",
                                 Py_TYPE(cls)->tp_name);
                Py_DECREF(tuple);
                return -1;
            }
            t = (PyTypeObject*)cls;
            if (!PyType_IsSubtype(solid, solid_base(t))) {
                PyErr_Format(PyExc_TypeError,
             "mro() returned base with unsuitable layout ('%.500s')",
                                     t->tp_name);
                        Py_DECREF(tuple);
                        return -1;
            }
        }
    }
    type->tp_mro = tuple;

    type_mro_modified(type, type->tp_mro);

    type_mro_modified(type, type->tp_bases);

    PyType_Modified(type);

    return 0;
}

static PyTypeObject *
best_base(PyObject *bases)
{
    Py_ssize_t i, n;
    PyTypeObject *base, *winner, *candidate, *base_i;
    PyObject *base_proto;

    assert(PyTuple_Check(bases));
    n = PyTuple_GET_SIZE(bases);
    assert(n > 0);
    base = NULL;
    winner = NULL;
    for (i = 0; i < n; i++) {
        base_proto = PyTuple_GET_ITEM(bases, i);
        if (PyClass_Check(base_proto))
            continue;
        if (!PyType_Check(base_proto)) {
            PyErr_SetString(
                PyExc_TypeError,
                "bases must be types");
            return NULL;
        }
        base_i = (PyTypeObject *)base_proto;
        if (base_i->tp_dict == NULL) {
            if (PyType_Ready(base_i) < 0)
                return NULL;
        }
        if (!PyType_HasFeature(base_i, Py_TPFLAGS_BASETYPE)) {
            PyErr_Format(PyExc_TypeError,
                         "type '%.100s' is not an acceptable base type",
                         base_i->tp_name);
            return NULL;
        }
        candidate = solid_base(base_i);
        if (winner == NULL) {
            winner = candidate;
            base = base_i;
        }
        else if (PyType_IsSubtype(winner, candidate))
            ;
        else if (PyType_IsSubtype(candidate, winner)) {
            winner = candidate;
            base = base_i;
        }
        else {
            PyErr_SetString(
                PyExc_TypeError,
                "multiple bases have "
                "instance lay-out conflict");
            return NULL;
        }
    }
    if (base == NULL)
        PyErr_SetString(PyExc_TypeError,
            "a new-style class can't have only classic bases");
    return base;
}

static int
extra_ivars(PyTypeObject *type, PyTypeObject *base)
{
    size_t t_size = type->tp_basicsize;
    size_t b_size = base->tp_basicsize;

    assert(t_size >= b_size);
    if (type->tp_itemsize || base->tp_itemsize) {

        return t_size != b_size ||
            type->tp_itemsize != base->tp_itemsize;
    }
    if (type->tp_weaklistoffset && base->tp_weaklistoffset == 0 &&
        type->tp_weaklistoffset + sizeof(PyObject *) == t_size &&
        type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        t_size -= sizeof(PyObject *);
    if (type->tp_dictoffset && base->tp_dictoffset == 0 &&
        type->tp_dictoffset + sizeof(PyObject *) == t_size &&
        type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        t_size -= sizeof(PyObject *);

    return t_size != b_size;
}

static PyTypeObject *
solid_base(PyTypeObject *type)
{
    PyTypeObject *base;

    if (type->tp_base)
        base = solid_base(type->tp_base);
    else
        base = &PyBaseObject_Type;
    if (extra_ivars(type, base))
        return type;
    else
        return base;
}

static void object_dealloc(PyObject *);
static int object_init(PyObject *, PyObject *, PyObject *);
static int update_slot(PyTypeObject *, PyObject *);
static void fixup_slot_dispatchers(PyTypeObject *);

static PyTypeObject *
get_builtin_base_with_dict(PyTypeObject *type)
{
    while (type->tp_base != NULL) {
        if (type->tp_dictoffset != 0 &&
            !(type->tp_flags & Py_TPFLAGS_HEAPTYPE))
            return type;
        type = type->tp_base;
    }
    return NULL;
}

static PyObject *
get_dict_descriptor(PyTypeObject *type)
{
    static PyObject *dict_str;
    PyObject *descr;

    if (dict_str == NULL) {
        dict_str = PyString_InternFromString("__dict__");
        if (dict_str == NULL)
            return NULL;
    }
    descr = _PyType_Lookup(type, dict_str);
    if (descr == NULL || !PyDescr_IsData(descr))
        return NULL;

    return descr;
}

static void
raise_dict_descr_error(PyObject *obj)
{
    PyErr_Format(PyExc_TypeError,
                 "this __dict__ descriptor does not support "
                 "'%.200s' objects", obj->ob_type->tp_name);
}

static PyObject *
subtype_dict(PyObject *obj, void *context)
{
    PyObject **dictptr;
    PyObject *dict;
    PyTypeObject *base;

    base = get_builtin_base_with_dict(obj->ob_type);
    if (base != NULL) {
        descrgetfunc func;
        PyObject *descr = get_dict_descriptor(base);
        if (descr == NULL) {
            raise_dict_descr_error(obj);
            return NULL;
        }
        func = descr->ob_type->tp_descr_get;
        if (func == NULL) {
            raise_dict_descr_error(obj);
            return NULL;
        }
        return func(descr, obj, (PyObject *)(obj->ob_type));
    }

    dictptr = _PyObject_GetDictPtr(obj);
    if (dictptr == NULL) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __dict__");
        return NULL;
    }
    dict = *dictptr;
    if (dict == NULL)
        *dictptr = dict = PyDict_New();
    Py_XINCREF(dict);
    return dict;
}

static int
subtype_setdict(PyObject *obj, PyObject *value, void *context)
{
    PyObject **dictptr;
    PyObject *dict;
    PyTypeObject *base;

    base = get_builtin_base_with_dict(obj->ob_type);
    if (base != NULL) {
        descrsetfunc func;
        PyObject *descr = get_dict_descriptor(base);
        if (descr == NULL) {
            raise_dict_descr_error(obj);
            return -1;
        }
        func = descr->ob_type->tp_descr_set;
        if (func == NULL) {
            raise_dict_descr_error(obj);
            return -1;
        }
        return func(descr, obj, value);
    }

    dictptr = _PyObject_GetDictPtr(obj);
    if (dictptr == NULL) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __dict__");
        return -1;
    }
    if (value != NULL && !PyDict_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "__dict__ must be set to a dictionary, "
                     "not a '%.200s'", Py_TYPE(value)->tp_name);
        return -1;
    }
    dict = *dictptr;
    Py_XINCREF(value);
    *dictptr = value;
    Py_XDECREF(dict);
    return 0;
}

static PyObject *
subtype_getweakref(PyObject *obj, void *context)
{
    PyObject **weaklistptr;
    PyObject *result;

    if (Py_TYPE(obj)->tp_weaklistoffset == 0) {
        PyErr_SetString(PyExc_AttributeError,
                        "This object has no __weakref__");
        return NULL;
    }
    assert(Py_TYPE(obj)->tp_weaklistoffset > 0);
    assert(Py_TYPE(obj)->tp_weaklistoffset + sizeof(PyObject *) <=
           (size_t)(Py_TYPE(obj)->tp_basicsize));
    weaklistptr = (PyObject **)
        ((char *)obj + Py_TYPE(obj)->tp_weaklistoffset);
    if (*weaklistptr == NULL)
        result = Py_None;
    else
        result = *weaklistptr;
    Py_INCREF(result);
    return result;
}

static PyGetSetDef subtype_getsets_full[] = {
    {"__dict__", subtype_dict, subtype_setdict,
     PyDoc_STR("dictionary for instance variables (if defined)")},
    {"__weakref__", subtype_getweakref, NULL,
     PyDoc_STR("list of weak references to the object (if defined)")},
    {0}
};

static PyGetSetDef subtype_getsets_dict_only[] = {
    {"__dict__", subtype_dict, subtype_setdict,
     PyDoc_STR("dictionary for instance variables (if defined)")},
    {0}
};

static PyGetSetDef subtype_getsets_weakref_only[] = {
    {"__weakref__", subtype_getweakref, NULL,
     PyDoc_STR("list of weak references to the object (if defined)")},
    {0}
};

static int
valid_identifier(PyObject *s)
{
    unsigned char *p;
    Py_ssize_t i, n;

    if (!PyString_Check(s)) {
        PyErr_Format(PyExc_TypeError,
                     "__slots__ items must be strings, not '%.200s'",
                     Py_TYPE(s)->tp_name);
        return 0;
    }
    p = (unsigned char *) PyString_AS_STRING(s);
    n = PyString_GET_SIZE(s);

    if (n == 0)
        n = 1;
    for (i = 0; i < n; i++, p++) {
        if (!(i == 0 ? isalpha(*p) : isalnum(*p)) && *p != '_') {
            PyErr_SetString(PyExc_TypeError,
                            "__slots__ must be identifiers");
            return 0;
        }
    }
    return 1;
}

#ifdef Py_USING_UNICODE

static PyObject *
_unicode_to_string(PyObject *slots, Py_ssize_t nslots)
{
    PyObject *tmp = NULL;
    PyObject *slot_name, *new_name;
    Py_ssize_t i;

    for (i = 0; i < nslots; i++) {
        if (PyUnicode_Check(slot_name = PyTuple_GET_ITEM(slots, i))) {
            if (tmp == NULL) {
                tmp = PySequence_List(slots);
                if (tmp == NULL)
                    return NULL;
            }
            new_name = _PyUnicode_AsDefaultEncodedString(slot_name,
                                                         NULL);
            if (new_name == NULL) {
                Py_DECREF(tmp);
                return NULL;
            }
            Py_INCREF(new_name);
            PyList_SET_ITEM(tmp, i, new_name);
            Py_DECREF(slot_name);
        }
    }
    if (tmp != NULL) {
        slots = PyList_AsTuple(tmp);
        Py_DECREF(tmp);
    }
    return slots;
}
#endif

static int
object_init(PyObject *self, PyObject *args, PyObject *kwds);

static int
type_init(PyObject *cls, PyObject *args, PyObject *kwds)
{
    int res;

    assert(args != NULL && PyTuple_Check(args));
    assert(kwds == NULL || PyDict_Check(kwds));

    if (kwds != NULL && PyDict_Check(kwds) && PyDict_Size(kwds) != 0) {
        PyErr_SetString(PyExc_TypeError,
                        "type.__init__() takes no keyword arguments");
        return -1;
    }

    if (args != NULL && PyTuple_Check(args) &&
        (PyTuple_GET_SIZE(args) != 1 && PyTuple_GET_SIZE(args) != 3)) {
        PyErr_SetString(PyExc_TypeError,
                        "type.__init__() takes 1 or 3 arguments");
        return -1;
    }

    args = PyTuple_GetSlice(args, 0, 0);
    res = object_init(cls, args, NULL);
    Py_DECREF(args);
    return res;
}

static PyObject *
type_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds)
{
    PyObject *name, *bases, *dict;
    static char *kwlist[] = {"name", "bases", "dict", 0};
    PyObject *slots, *tmp, *newslots;
    PyTypeObject *type, *base, *tmptype, *winner;
    PyHeapTypeObject *et;
    PyMemberDef *mp;
    Py_ssize_t i, nbases, nslots, slotoffset;
    int j, may_add_dict, may_add_weak, add_dict, add_weak;

    assert(args != NULL && PyTuple_Check(args));
    assert(kwds == NULL || PyDict_Check(kwds));

    {
        const Py_ssize_t nargs = PyTuple_GET_SIZE(args);
        const Py_ssize_t nkwds = kwds == NULL ? 0 : PyDict_Size(kwds);

        if (PyType_CheckExact(metatype) && nargs == 1 && nkwds == 0) {
            PyObject *x = PyTuple_GET_ITEM(args, 0);
            Py_INCREF(Py_TYPE(x));
            return (PyObject *) Py_TYPE(x);
        }

        if (nargs + nkwds != 3) {
            PyErr_SetString(PyExc_TypeError,
                            "type() takes 1 or 3 arguments");
            return NULL;
        }
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "SO!O!:type", kwlist,
                                     &name,
                                     &PyTuple_Type, &bases,
                                     &PyDict_Type, &dict))
        return NULL;

    nbases = PyTuple_GET_SIZE(bases);
    winner = metatype;
    for (i = 0; i < nbases; i++) {
        tmp = PyTuple_GET_ITEM(bases, i);
        tmptype = tmp->ob_type;
        if (tmptype == &PyClass_Type)
            continue;
        if (PyType_IsSubtype(winner, tmptype))
            continue;
        if (PyType_IsSubtype(tmptype, winner)) {
            winner = tmptype;
            continue;
        }
        PyErr_SetString(PyExc_TypeError,
                        "metaclass conflict: "
                        "the metaclass of a derived class "
                        "must be a (non-strict) subclass "
                        "of the metaclasses of all its bases");
        return NULL;
    }
    if (winner != metatype) {
        if (winner->tp_new != type_new)
            return winner->tp_new(winner, args, kwds);
        metatype = winner;
    }

    if (nbases == 0) {
        bases = PyTuple_Pack(1, &PyBaseObject_Type);
        if (bases == NULL)
            return NULL;
        nbases = 1;
    }
    else
        Py_INCREF(bases);

    base = best_base(bases);
    if (base == NULL) {
        Py_DECREF(bases);
        return NULL;
    }

    slots = PyDict_GetItemString(dict, "__slots__");
    nslots = 0;
    add_dict = 0;
    add_weak = 0;
    may_add_dict = base->tp_dictoffset == 0;
    may_add_weak = base->tp_weaklistoffset == 0 && base->tp_itemsize == 0;
    if (slots == NULL) {
        if (may_add_dict) {
            add_dict++;
        }
        if (may_add_weak) {
            add_weak++;
        }
    }
    else {

        if (PyString_Check(slots) || PyUnicode_Check(slots))
            slots = PyTuple_Pack(1, slots);
        else
            slots = PySequence_Tuple(slots);
        if (slots == NULL) {
            Py_DECREF(bases);
            return NULL;
        }
        assert(PyTuple_Check(slots));

        nslots = PyTuple_GET_SIZE(slots);
        if (nslots > 0 && base->tp_itemsize != 0) {
            PyErr_Format(PyExc_TypeError,
                         "nonempty __slots__ "
                         "not supported for subtype of '%s'",
                         base->tp_name);
          bad_slots:
            Py_DECREF(bases);
            Py_DECREF(slots);
            return NULL;
        }

#ifdef Py_USING_UNICODE
        tmp = _unicode_to_string(slots, nslots);
        if (tmp == NULL)
            goto bad_slots;
        if (tmp != slots) {
            Py_DECREF(slots);
            slots = tmp;
        }
#endif

        for (i = 0; i < nslots; i++) {
            PyObject *tmp = PyTuple_GET_ITEM(slots, i);
            char *s;
            if (!valid_identifier(tmp))
                goto bad_slots;
            assert(PyString_Check(tmp));
            s = PyString_AS_STRING(tmp);
            if (strcmp(s, "__dict__") == 0) {
                if (!may_add_dict || add_dict) {
                    PyErr_SetString(PyExc_TypeError,
                        "__dict__ slot disallowed: "
                        "we already got one");
                    goto bad_slots;
                }
                add_dict++;
            }
            if (strcmp(s, "__weakref__") == 0) {
                if (!may_add_weak || add_weak) {
                    PyErr_SetString(PyExc_TypeError,
                        "__weakref__ slot disallowed: "
                        "either we already got one, "
                        "or __itemsize__ != 0");
                    goto bad_slots;
                }
                add_weak++;
            }
        }

        newslots = PyList_New(nslots - add_dict - add_weak);
        if (newslots == NULL)
            goto bad_slots;
        for (i = j = 0; i < nslots; i++) {
            char *s;
            tmp = PyTuple_GET_ITEM(slots, i);
            s = PyString_AS_STRING(tmp);
            if ((add_dict && strcmp(s, "__dict__") == 0) ||
                (add_weak && strcmp(s, "__weakref__") == 0))
                continue;
            tmp =_Py_Mangle(name, tmp);
            if (!tmp) {
                Py_DECREF(newslots);
                goto bad_slots;
            }
            PyList_SET_ITEM(newslots, j, tmp);
            j++;
        }
        assert(j == nslots - add_dict - add_weak);
        nslots = j;
        Py_DECREF(slots);
        if (PyList_Sort(newslots) == -1) {
            Py_DECREF(bases);
            Py_DECREF(newslots);
            return NULL;
        }
        slots = PyList_AsTuple(newslots);
        Py_DECREF(newslots);
        if (slots == NULL) {
            Py_DECREF(bases);
            return NULL;
        }

        if (nbases > 1 &&
            ((may_add_dict && !add_dict) ||
             (may_add_weak && !add_weak))) {
            for (i = 0; i < nbases; i++) {
                tmp = PyTuple_GET_ITEM(bases, i);
                if (tmp == (PyObject *)base)
                    continue;
                if (PyClass_Check(tmp)) {

                    if (may_add_dict && !add_dict)
                        add_dict++;
                    if (may_add_weak && !add_weak)
                        add_weak++;
                    break;
                }
                assert(PyType_Check(tmp));
                tmptype = (PyTypeObject *)tmp;
                if (may_add_dict && !add_dict &&
                    tmptype->tp_dictoffset != 0)
                    add_dict++;
                if (may_add_weak && !add_weak &&
                    tmptype->tp_weaklistoffset != 0)
                    add_weak++;
                if (may_add_dict && !add_dict)
                    continue;
                if (may_add_weak && !add_weak)
                    continue;

                break;
            }
        }
    }

    type = (PyTypeObject *)metatype->tp_alloc(metatype, nslots);
    if (type == NULL) {
        Py_XDECREF(slots);
        Py_DECREF(bases);
        return NULL;
    }

    et = (PyHeapTypeObject *)type;
    Py_INCREF(name);
    et->ht_name = name;
    et->ht_slots = slots;

    type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
        Py_TPFLAGS_BASETYPE;
    if (base->tp_flags & Py_TPFLAGS_HAVE_GC)
        type->tp_flags |= Py_TPFLAGS_HAVE_GC;
    if (base->tp_flags & Py_TPFLAGS_HAVE_NEWBUFFER)
        type->tp_flags |= Py_TPFLAGS_HAVE_NEWBUFFER;

    if ((base->tp_flags & Py_TPFLAGS_CHECKTYPES) ||
        (base->tp_as_number == NULL))
        type->tp_flags |= Py_TPFLAGS_CHECKTYPES;

    type->tp_as_number = &et->as_number;
    type->tp_as_sequence = &et->as_sequence;
    type->tp_as_mapping = &et->as_mapping;
    type->tp_as_buffer = &et->as_buffer;
    type->tp_name = PyString_AS_STRING(name);
    if (!type->tp_name) {
        Py_DECREF(bases);
        Py_DECREF(type);
        return NULL;
    }
    if (strlen(type->tp_name) != (size_t)PyString_GET_SIZE(name)) {
        PyErr_SetString(PyExc_ValueError,
                        "type name must not contain null characters");
        Py_DECREF(bases);
        Py_DECREF(type);
        return NULL;
    }

    type->tp_bases = bases;
    Py_INCREF(base);
    type->tp_base = base;

    type->tp_dict = dict = PyDict_Copy(dict);
    if (dict == NULL) {
        Py_DECREF(type);
        return NULL;
    }

    if (PyDict_GetItemString(dict, "__module__") == NULL) {
        tmp = PyEval_GetGlobals();
        if (tmp != NULL) {
            tmp = PyDict_GetItemString(tmp, "__name__");
            if (tmp != NULL) {
                if (PyDict_SetItemString(dict, "__module__",
                                         tmp) < 0) {
                    Py_DECREF(type);
                    return NULL;
                }
            }
        }
    }

    {
        PyObject *doc = PyDict_GetItemString(dict, "__doc__");
        if (doc != NULL && PyString_Check(doc)) {
            const size_t n = (size_t)PyString_GET_SIZE(doc);
            char *tp_doc = (char *)PyObject_MALLOC(n+1);
            if (tp_doc == NULL) {
                Py_DECREF(type);
                return NULL;
            }
            memcpy(tp_doc, PyString_AS_STRING(doc), n+1);
            type->tp_doc = tp_doc;
        }
    }

    tmp = PyDict_GetItemString(dict, "__new__");
    if (tmp != NULL && PyFunction_Check(tmp)) {
        tmp = PyStaticMethod_New(tmp);
        if (tmp == NULL) {
            Py_DECREF(type);
            return NULL;
        }
        if (PyDict_SetItemString(dict, "__new__", tmp) < 0) {
            Py_DECREF(tmp);
            Py_DECREF(type);
            return NULL;
        }
        Py_DECREF(tmp);
    }

    mp = PyHeapType_GET_MEMBERS(et);
    slotoffset = base->tp_basicsize;
    if (slots != NULL) {
        for (i = 0; i < nslots; i++, mp++) {
            mp->name = PyString_AS_STRING(
                PyTuple_GET_ITEM(slots, i));
            mp->type = T_OBJECT_EX;
            mp->offset = slotoffset;

            assert(strcmp(mp->name, "__dict__") != 0);
            assert(strcmp(mp->name, "__weakref__") != 0);

            slotoffset += sizeof(PyObject *);
        }
    }
    if (add_dict) {
        if (base->tp_itemsize)
            type->tp_dictoffset = -(long)sizeof(PyObject *);
        else
            type->tp_dictoffset = slotoffset;
        slotoffset += sizeof(PyObject *);
    }
    if (add_weak) {
        assert(!base->tp_itemsize);
        type->tp_weaklistoffset = slotoffset;
        slotoffset += sizeof(PyObject *);
    }
    type->tp_basicsize = slotoffset;
    type->tp_itemsize = base->tp_itemsize;
    type->tp_members = PyHeapType_GET_MEMBERS(et);

    if (type->tp_weaklistoffset && type->tp_dictoffset)
        type->tp_getset = subtype_getsets_full;
    else if (type->tp_weaklistoffset && !type->tp_dictoffset)
        type->tp_getset = subtype_getsets_weakref_only;
    else if (!type->tp_weaklistoffset && type->tp_dictoffset)
        type->tp_getset = subtype_getsets_dict_only;
    else
        type->tp_getset = NULL;

    if (type->tp_dictoffset != 0 || nslots > 0) {
        if (base->tp_getattr == NULL && base->tp_getattro == NULL)
            type->tp_getattro = PyObject_GenericGetAttr;
        if (base->tp_setattr == NULL && base->tp_setattro == NULL)
            type->tp_setattro = PyObject_GenericSetAttr;
    }
    type->tp_dealloc = subtype_dealloc;

    if (!(type->tp_basicsize == sizeof(PyObject) &&
          type->tp_itemsize == 0))
        type->tp_flags |= Py_TPFLAGS_HAVE_GC;

    type->tp_alloc = PyType_GenericAlloc;
    if (type->tp_flags & Py_TPFLAGS_HAVE_GC) {
        type->tp_free = PyObject_GC_Del;
        type->tp_traverse = subtype_traverse;
        type->tp_clear = subtype_clear;
    }
    else
        type->tp_free = PyObject_Del;

    if (PyType_Ready(type) < 0) {
        Py_DECREF(type);
        return NULL;
    }

    fixup_slot_dispatchers(type);

    return (PyObject *)type;
}

PyObject *
_PyType_Lookup(PyTypeObject *type, PyObject *name)
{
    Py_ssize_t i, n;
    PyObject *mro, *res, *base, *dict;
    unsigned int h;

    if (MCACHE_CACHEABLE_NAME(name) &&
        PyType_HasFeature(type, Py_TPFLAGS_VALID_VERSION_TAG)) {

        h = MCACHE_HASH_METHOD(type, name);
        if (method_cache[h].version == type->tp_version_tag &&
            method_cache[h].name == name) {
#if MCACHE_STATS
            method_cache_hits++;
#endif
            return method_cache[h].value;
        }
    }

    mro = type->tp_mro;

    if (mro == NULL) {
        if ((type->tp_flags & Py_TPFLAGS_READYING) == 0 &&
            PyType_Ready(type) < 0) {

            PyErr_Clear();
            return NULL;
        }
        mro = type->tp_mro;
        if (mro == NULL) {
            return NULL;
        }
    }

    res = NULL;
    assert(PyTuple_Check(mro));
    n = PyTuple_GET_SIZE(mro);
    for (i = 0; i < n; i++) {
        base = PyTuple_GET_ITEM(mro, i);
        if (PyClass_Check(base))
            dict = ((PyClassObject *)base)->cl_dict;
        else {
            assert(PyType_Check(base));
            dict = ((PyTypeObject *)base)->tp_dict;
        }
        assert(dict && PyDict_Check(dict));
        res = PyDict_GetItem(dict, name);
        if (res != NULL)
            break;
    }

    if (MCACHE_CACHEABLE_NAME(name) && assign_version_tag(type)) {
        h = MCACHE_HASH_METHOD(type, name);
        method_cache[h].version = type->tp_version_tag;
        method_cache[h].value = res;
        Py_INCREF(name);
        assert(((PyStringObject *)(name))->ob_shash != -1);
#if MCACHE_STATS
        if (method_cache[h].name != Py_None && method_cache[h].name != name)
            method_cache_collisions++;
        else
            method_cache_misses++;
#endif
        Py_DECREF(method_cache[h].name);
        method_cache[h].name = name;
    }
    return res;
}

static PyObject *
type_getattro(PyTypeObject *type, PyObject *name)
{
    PyTypeObject *metatype = Py_TYPE(type);
    PyObject *meta_attribute, *attribute;
    descrgetfunc meta_get;

    if (!PyString_Check(name)) {
        PyErr_Format(PyExc_TypeError,
                     "attribute name must be string, not '%.200s'",
                     name->ob_type->tp_name);
        return NULL;
    }

    if (type->tp_dict == NULL) {
        if (PyType_Ready(type) < 0)
            return NULL;
    }

    meta_get = NULL;

    meta_attribute = _PyType_Lookup(metatype, name);

    if (meta_attribute != NULL) {
        meta_get = Py_TYPE(meta_attribute)->tp_descr_get;

        if (meta_get != NULL && PyDescr_IsData(meta_attribute)) {

            return meta_get(meta_attribute, (PyObject *)type,
                            (PyObject *)metatype);
        }
        Py_INCREF(meta_attribute);
    }

    attribute = _PyType_Lookup(type, name);
    if (attribute != NULL) {

        descrgetfunc local_get = Py_TYPE(attribute)->tp_descr_get;

        Py_XDECREF(meta_attribute);

        if (local_get != NULL) {

            return local_get(attribute, (PyObject *)NULL,
                             (PyObject *)type);
        }

        Py_INCREF(attribute);
        return attribute;
    }

    if (meta_get != NULL) {
        PyObject *res;
        res = meta_get(meta_attribute, (PyObject *)type,
                       (PyObject *)metatype);
        Py_DECREF(meta_attribute);
        return res;
    }

    if (meta_attribute != NULL) {
        return meta_attribute;
    }

    PyErr_Format(PyExc_AttributeError,
                     "type object '%.50s' has no attribute '%.400s'",
                     type->tp_name, PyString_AS_STRING(name));
    return NULL;
}

static int
type_setattro(PyTypeObject *type, PyObject *name, PyObject *value)
{
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(
            PyExc_TypeError,
            "can't set attributes of built-in/extension type '%s'",
            type->tp_name);
        return -1;
    }
    if (PyObject_GenericSetAttr((PyObject *)type, name, value) < 0)
        return -1;
    return update_slot(type, name);
}

static void
type_dealloc(PyTypeObject *type)
{
    PyHeapTypeObject *et;

    assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);
    _PyObject_GC_UNTRACK(type);
    PyObject_ClearWeakRefs((PyObject *)type);
    et = (PyHeapTypeObject *)type;
    Py_XDECREF(type->tp_base);
    Py_XDECREF(type->tp_dict);
    Py_XDECREF(type->tp_bases);
    Py_XDECREF(type->tp_mro);
    Py_XDECREF(type->tp_cache);
    Py_XDECREF(type->tp_subclasses);

    PyObject_Free((char *)type->tp_doc);
    Py_XDECREF(et->ht_name);
    Py_XDECREF(et->ht_slots);
    Py_TYPE(type)->tp_free((PyObject *)type);
}

static PyObject *
type_subclasses(PyTypeObject *type, PyObject *args_ignored)
{
    PyObject *list, *raw, *ref;
    Py_ssize_t i, n;

    list = PyList_New(0);
    if (list == NULL)
        return NULL;
    raw = type->tp_subclasses;
    if (raw == NULL)
        return list;
    assert(PyList_Check(raw));
    n = PyList_GET_SIZE(raw);
    for (i = 0; i < n; i++) {
        ref = PyList_GET_ITEM(raw, i);
        assert(PyWeakref_CheckRef(ref));
        ref = PyWeakref_GET_OBJECT(ref);
        if (ref != Py_None) {
            if (PyList_Append(list, ref) < 0) {
                Py_DECREF(list);
                return NULL;
            }
        }
    }
    return list;
}

static PyMethodDef type_methods[] = {
    {"mro", (PyCFunction)mro_external, METH_NOARGS,
     PyDoc_STR("mro() -> list\nreturn a type's method resolution order")},
    {"__subclasses__", (PyCFunction)type_subclasses, METH_NOARGS,
     PyDoc_STR("__subclasses__() -> list of immediate subclasses")},
    {"__instancecheck__", type___instancecheck__, METH_O,
     PyDoc_STR("__instancecheck__() -> bool\ncheck if an object is an instance")},
    {"__subclasscheck__", type___subclasscheck__, METH_O,
     PyDoc_STR("__subclasscheck__() -> bool\ncheck if a class is a subclass")},
    {0}
};

PyDoc_STRVAR(type_doc,
"type(object) -> the object's type\n"
"type(name, bases, dict) -> a new type");

static int
type_traverse(PyTypeObject *type, visitproc visit, void *arg)
{

    assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);

    Py_VISIT(type->tp_dict);
    Py_VISIT(type->tp_cache);
    Py_VISIT(type->tp_mro);
    Py_VISIT(type->tp_bases);
    Py_VISIT(type->tp_base);

    return 0;
}

static int
type_clear(PyTypeObject *type)
{

    assert(type->tp_flags & Py_TPFLAGS_HEAPTYPE);

    PyType_Modified(type);
    if (type->tp_dict)
        PyDict_Clear(type->tp_dict);
    Py_CLEAR(type->tp_mro);

    return 0;
}

static int
type_is_gc(PyTypeObject *type)
{
    return type->tp_flags & Py_TPFLAGS_HEAPTYPE;
}

PyTypeObject PyType_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "type",
    sizeof(PyHeapTypeObject),
    sizeof(PyMemberDef),
    (destructor)type_dealloc,
    0,
    0,
    0,
    0,
    (reprfunc)type_repr,
    0,
    0,
    0,
    (hashfunc)_Py_HashPointer,
    (ternaryfunc)type_call,
    0,
    (getattrofunc)type_getattro,
    (setattrofunc)type_setattro,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE | Py_TPFLAGS_TYPE_SUBCLASS,
    type_doc,
    (traverseproc)type_traverse,
    (inquiry)type_clear,
    type_richcompare,
    offsetof(PyTypeObject, tp_weaklist),
    0,
    0,
    type_methods,
    type_members,
    type_getsets,
    0,
    0,
    0,
    0,
    offsetof(PyTypeObject, tp_dict),
    type_init,
    0,
    type_new,
    PyObject_GC_Del,
    (inquiry)type_is_gc,
};

static PyObject *
object_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static int
excess_args(PyObject *args, PyObject *kwds)
{
    return PyTuple_GET_SIZE(args) ||
        (kwds && PyDict_Check(kwds) && PyDict_Size(kwds));
}

static int
object_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    int err = 0;
    if (excess_args(args, kwds)) {
        PyTypeObject *type = Py_TYPE(self);
        if (type->tp_init != object_init &&
            type->tp_new != object_new)
        {
            err = PyErr_WarnEx(PyExc_DeprecationWarning,
                       "object.__init__() takes no parameters",
                       1);
        }
        else if (type->tp_init != object_init ||
                 type->tp_new == object_new)
        {
            PyErr_SetString(PyExc_TypeError,
                "object.__init__() takes no parameters");
            err = -1;
        }
    }
    return err;
}

static PyObject *
object_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    int err = 0;
    if (excess_args(args, kwds)) {
        if (type->tp_new != object_new &&
            type->tp_init != object_init)
        {
            err = PyErr_WarnEx(PyExc_DeprecationWarning,
                       "object() takes no parameters",
                       1);
        }
        else if (type->tp_new != object_new ||
                 type->tp_init == object_init)
        {
            PyErr_SetString(PyExc_TypeError,
                "object() takes no parameters");
            err = -1;
        }
    }
    if (err < 0)
        return NULL;

    if (type->tp_flags & Py_TPFLAGS_IS_ABSTRACT) {
        static PyObject *comma = NULL;
        PyObject *abstract_methods = NULL;
        PyObject *builtins;
        PyObject *sorted;
        PyObject *sorted_methods = NULL;
        PyObject *joined = NULL;
        const char *joined_str;

        abstract_methods = type_abstractmethods(type, NULL);
        if (abstract_methods == NULL)
            goto error;
        builtins = PyEval_GetBuiltins();
        if (builtins == NULL)
            goto error;
        sorted = PyDict_GetItemString(builtins, "sorted");
        if (sorted == NULL)
            goto error;
        sorted_methods = PyObject_CallFunctionObjArgs(sorted,
                                                      abstract_methods,
                                                      NULL);
        if (sorted_methods == NULL)
            goto error;
        if (comma == NULL) {
            comma = PyString_InternFromString(", ");
            if (comma == NULL)
                goto error;
        }
        joined = PyObject_CallMethod(comma, "join",
                                     "O",  sorted_methods);
        if (joined == NULL)
            goto error;
        joined_str = PyString_AsString(joined);
        if (joined_str == NULL)
            goto error;

        PyErr_Format(PyExc_TypeError,
                     "Can't instantiate abstract class %s "
                     "with abstract methods %s",
                     type->tp_name,
                     joined_str);
    error:
        Py_XDECREF(joined);
        Py_XDECREF(sorted_methods);
        Py_XDECREF(abstract_methods);
        return NULL;
    }
    return type->tp_alloc(type, 0);
}

static void
object_dealloc(PyObject *self)
{
    Py_TYPE(self)->tp_free(self);
}

static PyObject *
object_repr(PyObject *self)
{
    PyTypeObject *type;
    PyObject *mod, *name, *rtn;

    type = Py_TYPE(self);
    mod = type_module(type, NULL);
    if (mod == NULL)
        PyErr_Clear();
    else if (!PyString_Check(mod)) {
        Py_DECREF(mod);
        mod = NULL;
    }
    name = type_name(type, NULL);
    if (name == NULL) {
        Py_XDECREF(mod);
        return NULL;
    }
    if (mod != NULL && strcmp(PyString_AS_STRING(mod), "__builtin__"))
        rtn = PyString_FromFormat("<%s.%s object at %p>",
                                  PyString_AS_STRING(mod),
                                  PyString_AS_STRING(name),
                                  self);
    else
        rtn = PyString_FromFormat("<%s object at %p>",
                                  type->tp_name, self);
    Py_XDECREF(mod);
    Py_DECREF(name);
    return rtn;
}

static PyObject *
object_str(PyObject *self)
{
    unaryfunc f;

    f = Py_TYPE(self)->tp_repr;
    if (f == NULL)
        f = object_repr;
    return f(self);
}

static PyObject *
object_get_class(PyObject *self, void *closure)
{
    Py_INCREF(Py_TYPE(self));
    return (PyObject *)(Py_TYPE(self));
}

static int
equiv_structs(PyTypeObject *a, PyTypeObject *b)
{
    return a == b ||
           (a != NULL &&
        b != NULL &&
        a->tp_basicsize == b->tp_basicsize &&
        a->tp_itemsize == b->tp_itemsize &&
        a->tp_dictoffset == b->tp_dictoffset &&
        a->tp_weaklistoffset == b->tp_weaklistoffset &&
        ((a->tp_flags & Py_TPFLAGS_HAVE_GC) ==
         (b->tp_flags & Py_TPFLAGS_HAVE_GC)));
}

static int
same_slots_added(PyTypeObject *a, PyTypeObject *b)
{
    PyTypeObject *base = a->tp_base;
    Py_ssize_t size;
    PyObject *slots_a, *slots_b;

    assert(base == b->tp_base);
    size = base->tp_basicsize;
    if (a->tp_dictoffset == size && b->tp_dictoffset == size)
        size += sizeof(PyObject *);
    if (a->tp_weaklistoffset == size && b->tp_weaklistoffset == size)
        size += sizeof(PyObject *);

    slots_a = ((PyHeapTypeObject *)a)->ht_slots;
    slots_b = ((PyHeapTypeObject *)b)->ht_slots;
    if (slots_a && slots_b) {
        if (PyObject_Compare(slots_a, slots_b) != 0)
            return 0;
        size += sizeof(PyObject *) * PyTuple_GET_SIZE(slots_a);
    }
    return size == a->tp_basicsize && size == b->tp_basicsize;
}

static int
compatible_for_assignment(PyTypeObject* oldto, PyTypeObject* newto, char* attr)
{
    PyTypeObject *newbase, *oldbase;

    if (newto->tp_dealloc != oldto->tp_dealloc ||
        newto->tp_free != oldto->tp_free)
    {
        PyErr_Format(PyExc_TypeError,
                     "%s assignment: "
                     "'%s' deallocator differs from '%s'",
                     attr,
                     newto->tp_name,
                     oldto->tp_name);
        return 0;
    }
    newbase = newto;
    oldbase = oldto;
    while (equiv_structs(newbase, newbase->tp_base))
        newbase = newbase->tp_base;
    while (equiv_structs(oldbase, oldbase->tp_base))
        oldbase = oldbase->tp_base;
    if (newbase != oldbase &&
        (newbase->tp_base != oldbase->tp_base ||
         !same_slots_added(newbase, oldbase))) {
        PyErr_Format(PyExc_TypeError,
                     "%s assignment: "
                     "'%s' object layout differs from '%s'",
                     attr,
                     newto->tp_name,
                     oldto->tp_name);
        return 0;
    }

    return 1;
}

static int
object_set_class(PyObject *self, PyObject *value, void *closure)
{
    PyTypeObject *oldto = Py_TYPE(self);
    PyTypeObject *newto;

    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "can't delete __class__ attribute");
        return -1;
    }
    if (!PyType_Check(value)) {
        PyErr_Format(PyExc_TypeError,
          "__class__ must be set to new-style class, not '%s' object",
          Py_TYPE(value)->tp_name);
        return -1;
    }
    newto = (PyTypeObject *)value;
    if (!(newto->tp_flags & Py_TPFLAGS_HEAPTYPE) ||
        !(oldto->tp_flags & Py_TPFLAGS_HEAPTYPE))
    {
        PyErr_Format(PyExc_TypeError,
                     "__class__ assignment: only for heap types");
        return -1;
    }
    if (compatible_for_assignment(newto, oldto, "__class__")) {
        Py_INCREF(newto);
        Py_TYPE(self) = newto;
        Py_DECREF(oldto);
        return 0;
    }
    else {
        return -1;
    }
}

static PyGetSetDef object_getsets[] = {
    {"__class__", object_get_class, object_set_class,
     PyDoc_STR("the object's class")},
    {0}
};

static PyObject *
import_copyreg(void)
{
    static PyObject *copyreg_str;

    if (!copyreg_str) {
        copyreg_str = PyString_InternFromString("copy_reg");
        if (copyreg_str == NULL)
            return NULL;
    }

    return PyImport_Import(copyreg_str);
}

static PyObject *
slotnames(PyObject *cls)
{
    PyObject *clsdict;
    PyObject *copyreg;
    PyObject *slotnames;

    if (!PyType_Check(cls)) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    clsdict = ((PyTypeObject *)cls)->tp_dict;
    slotnames = PyDict_GetItemString(clsdict, "__slotnames__");
    if (slotnames != NULL && PyList_Check(slotnames)) {
        Py_INCREF(slotnames);
        return slotnames;
    }

    copyreg = import_copyreg();
    if (copyreg == NULL)
        return NULL;

    slotnames = PyObject_CallMethod(copyreg, "_slotnames", "O", cls);
    Py_DECREF(copyreg);
    if (slotnames != NULL &&
        slotnames != Py_None &&
        !PyList_Check(slotnames))
    {
        PyErr_SetString(PyExc_TypeError,
            "copy_reg._slotnames didn't return a list or None");
        Py_DECREF(slotnames);
        slotnames = NULL;
    }

    return slotnames;
}

static PyObject *
reduce_2(PyObject *obj)
{
    PyObject *cls, *getnewargs;
    PyObject *args = NULL, *args2 = NULL;
    PyObject *getstate = NULL, *state = NULL, *names = NULL;
    PyObject *slots = NULL, *listitems = NULL, *dictitems = NULL;
    PyObject *copyreg = NULL, *newobj = NULL, *res = NULL;
    Py_ssize_t i, n;
    int required_state = 0;

    cls = PyObject_GetAttrString(obj, "__class__");
    if (cls == NULL)
        return NULL;

    if (PyType_Check(cls) && ((PyTypeObject *)cls)->tp_new == NULL) {
        PyErr_Format(PyExc_TypeError,
                     "can't pickle %.200s objects",
                     ((PyTypeObject *)cls)->tp_name);
        goto end;
    }

    getnewargs = PyObject_GetAttrString(obj, "__getnewargs__");
    if (getnewargs != NULL) {
        args = PyObject_CallObject(getnewargs, NULL);
        Py_DECREF(getnewargs);
        if (args == NULL)
            goto end;
        if (!PyTuple_Check(args)) {
            PyErr_Format(PyExc_TypeError,
                "__getnewargs__ should return a tuple, "
                "not '%.200s'", Py_TYPE(args)->tp_name);
            goto end;
        }
    }
    else {
        PyErr_Clear();
        required_state = !PyList_Check(obj) && !PyDict_Check(obj);
    }

    getstate = PyObject_GetAttrString(obj, "__getstate__");
    if (getstate != NULL) {
        state = PyObject_CallObject(getstate, NULL);
        Py_DECREF(getstate);
        if (state == NULL)
            goto end;
    }
    else {
        PyErr_Clear();

        if (required_state && obj->ob_type->tp_itemsize) {
            PyErr_Format(PyExc_TypeError,
                         "can't pickle %.200s objects",
                         Py_TYPE(obj)->tp_name);
            goto end;
        }

        state = PyObject_GetAttrString(obj, "__dict__");
        if (state == NULL) {
            PyErr_Clear();
            state = Py_None;
            Py_INCREF(state);
        }
        names = slotnames(cls);
        if (names == NULL)
            goto end;
        assert(names == Py_None || PyList_Check(names));

        if (names != Py_None) {
            slots = PyDict_New();
            if (slots == NULL)
                goto end;
            n = 0;

            for (i = 0; i < PyList_GET_SIZE(names); i++) {
                PyObject *name, *value;
                name = PyList_GET_ITEM(names, i);
                Py_INCREF(name);
                value = PyObject_GetAttr(obj, name);
                if (value == NULL) {
                    Py_DECREF(name);
                    PyErr_Clear();
                }
                else {
                    int err = PyDict_SetItem(slots, name,
                                             value);
                    Py_DECREF(name);
                    Py_DECREF(value);
                    if (err)
                        goto end;
                    n++;
                }
            }
            if (n) {
                state = Py_BuildValue("(NO)", state, slots);
                if (state == NULL)
                    goto end;
            }
        }
    }

    if (!PyList_Check(obj)) {
        listitems = Py_None;
        Py_INCREF(listitems);
    }
    else {
        listitems = PyObject_GetIter(obj);
        if (listitems == NULL)
            goto end;
    }

    if (!PyDict_Check(obj)) {
        dictitems = Py_None;
        Py_INCREF(dictitems);
    }
    else {
        dictitems = PyObject_CallMethod(obj, "iteritems", "");
        if (dictitems == NULL)
            goto end;
    }

    copyreg = import_copyreg();
    if (copyreg == NULL)
        goto end;
    newobj = PyObject_GetAttrString(copyreg, "__newobj__");
    if (newobj == NULL)
        goto end;

    n = args ? PyTuple_GET_SIZE(args) : 0;
    args2 = PyTuple_New(n+1);
    if (args2 == NULL)
        goto end;
    PyTuple_SET_ITEM(args2, 0, cls);
    cls = NULL;
    for (i = 0; i < n; i++) {
        PyObject *v = PyTuple_GET_ITEM(args, i);
        Py_INCREF(v);
        PyTuple_SET_ITEM(args2, i+1, v);
    }

    res = PyTuple_Pack(5, newobj, args2, state, listitems, dictitems);

  end:
    Py_XDECREF(cls);
    Py_XDECREF(args);
    Py_XDECREF(args2);
    Py_XDECREF(slots);
    Py_XDECREF(state);
    Py_XDECREF(names);
    Py_XDECREF(listitems);
    Py_XDECREF(dictitems);
    Py_XDECREF(copyreg);
    Py_XDECREF(newobj);
    return res;
}

static PyObject *
_common_reduce(PyObject *self, int proto)
{
    PyObject *copyreg, *res;

    if (proto >= 2)
        return reduce_2(self);

    copyreg = import_copyreg();
    if (!copyreg)
        return NULL;

    res = PyEval_CallMethod(copyreg, "_reduce_ex", "(Oi)", self, proto);
    Py_DECREF(copyreg);

    return res;
}

static PyObject *
object_reduce(PyObject *self, PyObject *args)
{
    int proto = 0;

    if (!PyArg_ParseTuple(args, "|i:__reduce__", &proto))
        return NULL;

    return _common_reduce(self, proto);
}

static PyObject *
object_reduce_ex(PyObject *self, PyObject *args)
{
    PyObject *reduce, *res;
    int proto = 0;

    if (!PyArg_ParseTuple(args, "|i:__reduce_ex__", &proto))
        return NULL;

    reduce = PyObject_GetAttrString(self, "__reduce__");
    if (reduce == NULL)
        PyErr_Clear();
    else {
        PyObject *cls, *clsreduce, *objreduce;
        int override;
        cls = PyObject_GetAttrString(self, "__class__");
        if (cls == NULL) {
            Py_DECREF(reduce);
            return NULL;
        }
        clsreduce = PyObject_GetAttrString(cls, "__reduce__");
        Py_DECREF(cls);
        if (clsreduce == NULL) {
            Py_DECREF(reduce);
            return NULL;
        }
        objreduce = PyDict_GetItemString(PyBaseObject_Type.tp_dict,
                                         "__reduce__");
        override = (clsreduce != objreduce);
        Py_DECREF(clsreduce);
        if (override) {
            res = PyObject_CallObject(reduce, NULL);
            Py_DECREF(reduce);
            return res;
        }
        else
            Py_DECREF(reduce);
    }

    return _common_reduce(self, proto);
}

static PyObject *
object_subclasshook(PyObject *cls, PyObject *args)
{
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyDoc_STRVAR(object_subclasshook_doc,
"Abstract classes can override this to customize issubclass().\n"
"\n"
"This is invoked early on by abc.ABCMeta.__subclasscheck__().\n"
"It should return True, False or NotImplemented.  If it returns\n"
"NotImplemented, the normal algorithm is used.  Otherwise, it\n"
"overrides the normal algorithm (and the outcome is cached).\n");

static PyObject *
object_format(PyObject *self, PyObject *args)
{
    PyObject *format_spec;
    PyObject *self_as_str = NULL;
    PyObject *result = NULL;
    Py_ssize_t format_len;

    if (!PyArg_ParseTuple(args, "O:__format__", &format_spec))
        return NULL;
#ifdef Py_USING_UNICODE
    if (PyUnicode_Check(format_spec)) {
        format_len = PyUnicode_GET_SIZE(format_spec);
        self_as_str = PyObject_Unicode(self);
    } else if (PyString_Check(format_spec)) {
#else
    if (PyString_Check(format_spec)) {
#endif
        format_len = PyString_GET_SIZE(format_spec);
        self_as_str = PyObject_Str(self);
    } else {
        PyErr_SetString(PyExc_TypeError,
                 "argument to __format__ must be unicode or str");
        return NULL;
    }

    if (self_as_str != NULL) {

        if (format_len > 0) {
            if (PyErr_WarnEx(PyExc_PendingDeprecationWarning,
             "object.__format__ with a non-empty format "
             "string is deprecated", 1) < 0) {
                goto done;
            }

        }
        result = PyObject_Format(self_as_str, format_spec);
    }

done:
    Py_XDECREF(self_as_str);

    return result;
}

static PyObject *
object_sizeof(PyObject *self, PyObject *args)
{
    Py_ssize_t res, isize;

    res = 0;
    isize = self->ob_type->tp_itemsize;
    if (isize > 0)
        res = Py_SIZE(self) * isize;
    res += self->ob_type->tp_basicsize;

    return PyInt_FromSsize_t(res);
}

static PyMethodDef object_methods[] = {
    {"__reduce_ex__", object_reduce_ex, METH_VARARGS,
     PyDoc_STR("helper for pickle")},
    {"__reduce__", object_reduce, METH_VARARGS,
     PyDoc_STR("helper for pickle")},
    {"__subclasshook__", object_subclasshook, METH_CLASS | METH_VARARGS,
     object_subclasshook_doc},
    {"__format__", object_format, METH_VARARGS,
     PyDoc_STR("default object formatter")},
    {"__sizeof__", object_sizeof, METH_NOARGS,
     PyDoc_STR("__sizeof__() -> int\nsize of object in memory, in bytes")},
    {0}
};

PyTypeObject PyBaseObject_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "object",
    sizeof(PyObject),
    0,
    object_dealloc,
    0,
    0,
    0,
    0,
    object_repr,
    0,
    0,
    0,
    (hashfunc)_Py_HashPointer,
    0,
    object_str,
    PyObject_GenericGetAttr,
    PyObject_GenericSetAttr,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    PyDoc_STR("The most base type"),
    0,
    0,
    0,
    0,
    0,
    0,
    object_methods,
    0,
    object_getsets,
    0,
    0,
    0,
    0,
    0,
    object_init,
    PyType_GenericAlloc,
    object_new,
    PyObject_Del,
};

static int
add_methods(PyTypeObject *type, PyMethodDef *meth)
{
    PyObject *dict = type->tp_dict;

    for (; meth->ml_name != NULL; meth++) {
        PyObject *descr;
        int err;
        if (PyDict_GetItemString(dict, meth->ml_name) &&
            !(meth->ml_flags & METH_COEXIST))
                continue;
        if (meth->ml_flags & METH_CLASS) {
            if (meth->ml_flags & METH_STATIC) {
                PyErr_SetString(PyExc_ValueError,
                     "method cannot be both class and static");
                return -1;
            }
            descr = PyDescr_NewClassMethod(type, meth);
        }
        else if (meth->ml_flags & METH_STATIC) {
            PyObject *cfunc = PyCFunction_New(meth, NULL);
            if (cfunc == NULL)
                return -1;
            descr = PyStaticMethod_New(cfunc);
            Py_DECREF(cfunc);
        }
        else {
            descr = PyDescr_NewMethod(type, meth);
        }
        if (descr == NULL)
            return -1;
        err = PyDict_SetItemString(dict, meth->ml_name, descr);
        Py_DECREF(descr);
        if (err < 0)
            return -1;
    }
    return 0;
}

static int
add_members(PyTypeObject *type, PyMemberDef *memb)
{
    PyObject *dict = type->tp_dict;

    for (; memb->name != NULL; memb++) {
        PyObject *descr;
        if (PyDict_GetItemString(dict, memb->name))
            continue;
        descr = PyDescr_NewMember(type, memb);
        if (descr == NULL)
            return -1;
        if (PyDict_SetItemString(dict, memb->name, descr) < 0) {
            Py_DECREF(descr);
            return -1;
        }
        Py_DECREF(descr);
    }
    return 0;
}

static int
add_getset(PyTypeObject *type, PyGetSetDef *gsp)
{
    PyObject *dict = type->tp_dict;

    for (; gsp->name != NULL; gsp++) {
        PyObject *descr;
        if (PyDict_GetItemString(dict, gsp->name))
            continue;
        descr = PyDescr_NewGetSet(type, gsp);

        if (descr == NULL)
            return -1;
        if (PyDict_SetItemString(dict, gsp->name, descr) < 0) {
            Py_DECREF(descr);
            return -1;
        }
        Py_DECREF(descr);
    }
    return 0;
}

#define BUFFER_FLAGS (Py_TPFLAGS_HAVE_GETCHARBUFFER | Py_TPFLAGS_HAVE_NEWBUFFER)

static void
inherit_special(PyTypeObject *type, PyTypeObject *base)
{
    Py_ssize_t oldsize, newsize;

    if (!type->tp_as_buffer && base->tp_as_buffer) {
        type->tp_flags &= ~BUFFER_FLAGS;
        type->tp_flags |=
            base->tp_flags & BUFFER_FLAGS;
    }
    if (!type->tp_as_sequence && base->tp_as_sequence) {
        type->tp_flags &= ~Py_TPFLAGS_HAVE_SEQUENCE_IN;
        type->tp_flags |= base->tp_flags & Py_TPFLAGS_HAVE_SEQUENCE_IN;
    }
    if ((type->tp_flags & Py_TPFLAGS_HAVE_INPLACEOPS) !=
        (base->tp_flags & Py_TPFLAGS_HAVE_INPLACEOPS)) {
        if ((!type->tp_as_number && base->tp_as_number) ||
            (!type->tp_as_sequence && base->tp_as_sequence)) {
            type->tp_flags &= ~Py_TPFLAGS_HAVE_INPLACEOPS;
            if (!type->tp_as_number && !type->tp_as_sequence) {
                type->tp_flags |= base->tp_flags &
                    Py_TPFLAGS_HAVE_INPLACEOPS;
            }
        }

    }
    if (!type->tp_as_number && base->tp_as_number) {
        type->tp_flags &= ~Py_TPFLAGS_CHECKTYPES;
        type->tp_flags |= base->tp_flags & Py_TPFLAGS_CHECKTYPES;
    }

    oldsize = base->tp_basicsize;
    newsize = type->tp_basicsize ? type->tp_basicsize : oldsize;
    if (!(type->tp_flags & Py_TPFLAGS_HAVE_GC) &&
        (base->tp_flags & Py_TPFLAGS_HAVE_GC) &&
        (type->tp_flags & Py_TPFLAGS_HAVE_RICHCOMPARE ) &&
        (!type->tp_traverse && !type->tp_clear)) {
        type->tp_flags |= Py_TPFLAGS_HAVE_GC;
        if (type->tp_traverse == NULL)
            type->tp_traverse = base->tp_traverse;
        if (type->tp_clear == NULL)
            type->tp_clear = base->tp_clear;
    }
    if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_CLASS) {

        if (base != &PyBaseObject_Type ||
            (type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
            if (type->tp_new == NULL)
                type->tp_new = base->tp_new;
        }
    }
    type->tp_basicsize = newsize;

#undef COPYVAL
#define COPYVAL(SLOT) \
    if (type->SLOT == 0) type->SLOT = base->SLOT

    COPYVAL(tp_itemsize);
    if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_WEAKREFS) {
        COPYVAL(tp_weaklistoffset);
    }
    if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_CLASS) {
        COPYVAL(tp_dictoffset);
    }

    if (PyType_IsSubtype(base, (PyTypeObject*)PyExc_BaseException))
        type->tp_flags |= Py_TPFLAGS_BASE_EXC_SUBCLASS;
    else if (PyType_IsSubtype(base, &PyType_Type))
        type->tp_flags |= Py_TPFLAGS_TYPE_SUBCLASS;
    else if (PyType_IsSubtype(base, &PyInt_Type))
        type->tp_flags |= Py_TPFLAGS_INT_SUBCLASS;
    else if (PyType_IsSubtype(base, &PyLong_Type))
        type->tp_flags |= Py_TPFLAGS_LONG_SUBCLASS;
    else if (PyType_IsSubtype(base, &PyString_Type))
        type->tp_flags |= Py_TPFLAGS_STRING_SUBCLASS;
#ifdef Py_USING_UNICODE
    else if (PyType_IsSubtype(base, &PyUnicode_Type))
        type->tp_flags |= Py_TPFLAGS_UNICODE_SUBCLASS;
#endif
    else if (PyType_IsSubtype(base, &PyTuple_Type))
        type->tp_flags |= Py_TPFLAGS_TUPLE_SUBCLASS;
    else if (PyType_IsSubtype(base, &PyList_Type))
        type->tp_flags |= Py_TPFLAGS_LIST_SUBCLASS;
    else if (PyType_IsSubtype(base, &PyDict_Type))
        type->tp_flags |= Py_TPFLAGS_DICT_SUBCLASS;
}

static int
overrides_name(PyTypeObject *type, char *name)
{
    PyObject *dict = type->tp_dict;

    assert(dict != NULL);
    if (PyDict_GetItemString(dict, name) != NULL) {
        return 1;
    }
    return 0;
}

#define OVERRIDES_HASH(x)       overrides_name(x, "__hash__")
#define OVERRIDES_EQ(x)         overrides_name(x, "__eq__")

static void
inherit_slots(PyTypeObject *type, PyTypeObject *base)
{
    PyTypeObject *basebase;

#undef SLOTDEFINED
#undef COPYSLOT
#undef COPYNUM
#undef COPYSEQ
#undef COPYMAP
#undef COPYBUF

#define SLOTDEFINED(SLOT) \
    (base->SLOT != 0 && \
     (basebase == NULL || base->SLOT != basebase->SLOT))

#define COPYSLOT(SLOT) \
    if (!type->SLOT && SLOTDEFINED(SLOT)) type->SLOT = base->SLOT

#define COPYNUM(SLOT) COPYSLOT(tp_as_number->SLOT)
#define COPYSEQ(SLOT) COPYSLOT(tp_as_sequence->SLOT)
#define COPYMAP(SLOT) COPYSLOT(tp_as_mapping->SLOT)
#define COPYBUF(SLOT) COPYSLOT(tp_as_buffer->SLOT)

    if (type->tp_as_number != NULL && base->tp_as_number != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_number == NULL)
            basebase = NULL;
        COPYNUM(nb_add);
        COPYNUM(nb_subtract);
        COPYNUM(nb_multiply);
        COPYNUM(nb_divide);
        COPYNUM(nb_remainder);
        COPYNUM(nb_divmod);
        COPYNUM(nb_power);
        COPYNUM(nb_negative);
        COPYNUM(nb_positive);
        COPYNUM(nb_absolute);
        COPYNUM(nb_nonzero);
        COPYNUM(nb_invert);
        COPYNUM(nb_lshift);
        COPYNUM(nb_rshift);
        COPYNUM(nb_and);
        COPYNUM(nb_xor);
        COPYNUM(nb_or);
        COPYNUM(nb_coerce);
        COPYNUM(nb_int);
        COPYNUM(nb_long);
        COPYNUM(nb_float);
        COPYNUM(nb_oct);
        COPYNUM(nb_hex);
        COPYNUM(nb_inplace_add);
        COPYNUM(nb_inplace_subtract);
        COPYNUM(nb_inplace_multiply);
        COPYNUM(nb_inplace_divide);
        COPYNUM(nb_inplace_remainder);
        COPYNUM(nb_inplace_power);
        COPYNUM(nb_inplace_lshift);
        COPYNUM(nb_inplace_rshift);
        COPYNUM(nb_inplace_and);
        COPYNUM(nb_inplace_xor);
        COPYNUM(nb_inplace_or);
        if (base->tp_flags & Py_TPFLAGS_CHECKTYPES) {
            COPYNUM(nb_true_divide);
            COPYNUM(nb_floor_divide);
            COPYNUM(nb_inplace_true_divide);
            COPYNUM(nb_inplace_floor_divide);
        }
        if (base->tp_flags & Py_TPFLAGS_HAVE_INDEX) {
            COPYNUM(nb_index);
        }
    }

    if (type->tp_as_sequence != NULL && base->tp_as_sequence != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_sequence == NULL)
            basebase = NULL;
        COPYSEQ(sq_length);
        COPYSEQ(sq_concat);
        COPYSEQ(sq_repeat);
        COPYSEQ(sq_item);
        COPYSEQ(sq_slice);
        COPYSEQ(sq_ass_item);
        COPYSEQ(sq_ass_slice);
        COPYSEQ(sq_contains);
        COPYSEQ(sq_inplace_concat);
        COPYSEQ(sq_inplace_repeat);
    }

    if (type->tp_as_mapping != NULL && base->tp_as_mapping != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_mapping == NULL)
            basebase = NULL;
        COPYMAP(mp_length);
        COPYMAP(mp_subscript);
        COPYMAP(mp_ass_subscript);
    }

    if (type->tp_as_buffer != NULL && base->tp_as_buffer != NULL) {
        basebase = base->tp_base;
        if (basebase->tp_as_buffer == NULL)
            basebase = NULL;
        COPYBUF(bf_getreadbuffer);
        COPYBUF(bf_getwritebuffer);
        COPYBUF(bf_getsegcount);
        COPYBUF(bf_getcharbuffer);
        COPYBUF(bf_getbuffer);
        COPYBUF(bf_releasebuffer);
    }

    basebase = base->tp_base;

    COPYSLOT(tp_dealloc);
    COPYSLOT(tp_print);
    if (type->tp_getattr == NULL && type->tp_getattro == NULL) {
        type->tp_getattr = base->tp_getattr;
        type->tp_getattro = base->tp_getattro;
    }
    if (type->tp_setattr == NULL && type->tp_setattro == NULL) {
        type->tp_setattr = base->tp_setattr;
        type->tp_setattro = base->tp_setattro;
    }

    COPYSLOT(tp_repr);

    COPYSLOT(tp_call);
    COPYSLOT(tp_str);
    if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_RICHCOMPARE) {
        if (type->tp_compare == NULL &&
            type->tp_richcompare == NULL &&
            type->tp_hash == NULL)
        {
            type->tp_compare = base->tp_compare;
            type->tp_richcompare = base->tp_richcompare;
            type->tp_hash = base->tp_hash;

            if (Py_Py3kWarningFlag) {
                if (base->tp_hash &&
                                (base->tp_hash != PyObject_HashNotImplemented) &&
                                !OVERRIDES_HASH(type)) {
                    if (OVERRIDES_EQ(type)) {
                        if (PyErr_WarnPy3k("Overriding "
                                           "__eq__ blocks inheritance "
                                           "of __hash__ in 3.x",
                                           1) < 0)

                            PyErr_Clear();
                    }
                }
            }
        }
    }
    else {
        COPYSLOT(tp_compare);
    }
    if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_ITER) {
        COPYSLOT(tp_iter);
        COPYSLOT(tp_iternext);
    }
    if (type->tp_flags & base->tp_flags & Py_TPFLAGS_HAVE_CLASS) {
        COPYSLOT(tp_descr_get);
        COPYSLOT(tp_descr_set);
        COPYSLOT(tp_dictoffset);
        COPYSLOT(tp_init);
        COPYSLOT(tp_alloc);
        COPYSLOT(tp_is_gc);
        if ((type->tp_flags & Py_TPFLAGS_HAVE_GC) ==
            (base->tp_flags & Py_TPFLAGS_HAVE_GC)) {

            COPYSLOT(tp_free);
        }
        else if ((type->tp_flags & Py_TPFLAGS_HAVE_GC) &&
                 type->tp_free == NULL &&
                 base->tp_free == _PyObject_Del) {

            type->tp_free = PyObject_GC_Del;
        }

    }
}

static int add_operators(PyTypeObject *);

int
PyType_Ready(PyTypeObject *type)
{
    PyObject *dict, *bases;
    PyTypeObject *base;
    Py_ssize_t i, n;

    if (type->tp_flags & Py_TPFLAGS_READY) {
        assert(type->tp_dict != NULL);
        return 0;
    }
    assert((type->tp_flags & Py_TPFLAGS_READYING) == 0);

    type->tp_flags |= Py_TPFLAGS_READYING;

#ifdef Py_TRACE_REFS

    _Py_AddToAllObjects((PyObject *)type, 0);
#endif

    if (type->tp_name == NULL) {
        PyErr_Format(PyExc_SystemError,
                     "Type does not define the tp_name field.");
        goto error;
    }

    base = type->tp_base;
    if (base == NULL && type != &PyBaseObject_Type) {
        base = type->tp_base = &PyBaseObject_Type;
        Py_INCREF(base);
    }

    if (base && base->tp_dict == NULL) {
        if (PyType_Ready(base) < 0)
            goto error;
    }

    if (Py_TYPE(type) == NULL && base != NULL)
        Py_TYPE(type) = Py_TYPE(base);

    bases = type->tp_bases;
    if (bases == NULL) {
        if (base == NULL)
            bases = PyTuple_New(0);
        else
            bases = PyTuple_Pack(1, base);
        if (bases == NULL)
            goto error;
        type->tp_bases = bases;
    }

    dict = type->tp_dict;
    if (dict == NULL) {
        dict = PyDict_New();
        if (dict == NULL)
            goto error;
        type->tp_dict = dict;
    }

    if (add_operators(type) < 0)
        goto error;
    if (type->tp_methods != NULL) {
        if (add_methods(type, type->tp_methods) < 0)
            goto error;
    }
    if (type->tp_members != NULL) {
        if (add_members(type, type->tp_members) < 0)
            goto error;
    }
    if (type->tp_getset != NULL) {
        if (add_getset(type, type->tp_getset) < 0)
            goto error;
    }

    if (mro_internal(type) < 0) {
        goto error;
    }

    if (type->tp_base != NULL)
        inherit_special(type, type->tp_base);

    bases = type->tp_mro;
    assert(bases != NULL);
    assert(PyTuple_Check(bases));
    n = PyTuple_GET_SIZE(bases);
    for (i = 1; i < n; i++) {
        PyObject *b = PyTuple_GET_ITEM(bases, i);
        if (PyType_Check(b))
            inherit_slots(type, (PyTypeObject *)b);
    }

    if (Py_Py3kWarningFlag && !(type->tp_flags & Py_TPFLAGS_HEAPTYPE))
        for (i = 0; i < n; i++) {
            PyObject *b = PyTuple_GET_ITEM(bases, i);
            if (PyType_Check(b) &&
                (((PyTypeObject *)b)->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
                char buf[300];
                PyOS_snprintf(buf, sizeof(buf),
                              "type '%.100s' is not dynamically allocated but "
                              "its base type '%.100s' is dynamically allocated",
                              type->tp_name, ((PyTypeObject *)b)->tp_name);
                if (PyErr_WarnPy3k(buf, 1) < 0)
                    goto error;
                break;
            }
        }

    if (PyType_IS_GC(type) && (type->tp_flags & Py_TPFLAGS_BASETYPE) &&
        (type->tp_free == NULL || type->tp_free == PyObject_Del)) {

        PyErr_Format(PyExc_TypeError, "type '%.100s' participates in "
                     "gc and is a base type but has inappropriate "
                     "tp_free slot",
                     type->tp_name);
        goto error;
    }

    if (PyDict_GetItemString(type->tp_dict, "__doc__") == NULL) {
        if (type->tp_doc != NULL) {
            PyObject *doc = PyString_FromString(type->tp_doc);
            if (doc == NULL)
                goto error;
            PyDict_SetItemString(type->tp_dict, "__doc__", doc);
            Py_DECREF(doc);
        } else {
            PyDict_SetItemString(type->tp_dict,
                                 "__doc__", Py_None);
        }
    }

    base = type->tp_base;
    if (base != NULL) {
        if (type->tp_as_number == NULL)
            type->tp_as_number = base->tp_as_number;
        if (type->tp_as_sequence == NULL)
            type->tp_as_sequence = base->tp_as_sequence;
        if (type->tp_as_mapping == NULL)
            type->tp_as_mapping = base->tp_as_mapping;
        if (type->tp_as_buffer == NULL)
            type->tp_as_buffer = base->tp_as_buffer;
    }

    bases = type->tp_bases;
    n = PyTuple_GET_SIZE(bases);
    for (i = 0; i < n; i++) {
        PyObject *b = PyTuple_GET_ITEM(bases, i);
        if (PyType_Check(b) &&
            add_subclass((PyTypeObject *)b, type) < 0)
            goto error;
    }

    assert(type->tp_dict != NULL);
    type->tp_flags =
        (type->tp_flags & ~Py_TPFLAGS_READYING) | Py_TPFLAGS_READY;
    return 0;

  error:
    type->tp_flags &= ~Py_TPFLAGS_READYING;
    return -1;
}

static int
add_subclass(PyTypeObject *base, PyTypeObject *type)
{
    Py_ssize_t i;
    int result;
    PyObject *list, *ref, *newobj;

    list = base->tp_subclasses;
    if (list == NULL) {
        base->tp_subclasses = list = PyList_New(0);
        if (list == NULL)
            return -1;
    }
    assert(PyList_Check(list));
    newobj = PyWeakref_NewRef((PyObject *)type, NULL);
    i = PyList_GET_SIZE(list);
    while (--i >= 0) {
        ref = PyList_GET_ITEM(list, i);
        assert(PyWeakref_CheckRef(ref));
        if (PyWeakref_GET_OBJECT(ref) == Py_None)
            return PyList_SetItem(list, i, newobj);
    }
    result = PyList_Append(list, newobj);
    Py_DECREF(newobj);
    return result;
}

static void
remove_subclass(PyTypeObject *base, PyTypeObject *type)
{
    Py_ssize_t i;
    PyObject *list, *ref;

    list = base->tp_subclasses;
    if (list == NULL) {
        return;
    }
    assert(PyList_Check(list));
    i = PyList_GET_SIZE(list);
    while (--i >= 0) {
        ref = PyList_GET_ITEM(list, i);
        assert(PyWeakref_CheckRef(ref));
        if (PyWeakref_GET_OBJECT(ref) == (PyObject*)type) {

            PySequence_DelItem(list, i);
            return;
        }
    }
}

static int
check_num_args(PyObject *ob, int n)
{
    if (!PyTuple_CheckExact(ob)) {
        PyErr_SetString(PyExc_SystemError,
            "PyArg_UnpackTuple() argument list is not a tuple");
        return 0;
    }
    if (n == PyTuple_GET_SIZE(ob))
        return 1;
    PyErr_Format(
        PyExc_TypeError,
        "expected %d arguments, got %zd", n, PyTuple_GET_SIZE(ob));
    return 0;
}

static PyObject *
wrap_lenfunc(PyObject *self, PyObject *args, void *wrapped)
{
    lenfunc func = (lenfunc)wrapped;
    Py_ssize_t res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyInt_FromLong((long)res);
}

static PyObject *
wrap_inquirypred(PyObject *self, PyObject *args, void *wrapped)
{
    inquiry func = (inquiry)wrapped;
    int res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyBool_FromLong((long)res);
}

static PyObject *
wrap_binaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(self, other);
}

static PyObject *
wrap_binaryfunc_l(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    if (!(self->ob_type->tp_flags & Py_TPFLAGS_CHECKTYPES) &&
        !PyType_IsSubtype(other->ob_type, self->ob_type)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    return (*func)(self, other);
}

static PyObject *
wrap_binaryfunc_r(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    if (!(self->ob_type->tp_flags & Py_TPFLAGS_CHECKTYPES) &&
        !PyType_IsSubtype(other->ob_type, self->ob_type)) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    return (*func)(other, self);
}

static PyObject *
wrap_coercefunc(PyObject *self, PyObject *args, void *wrapped)
{
    coercion func = (coercion)wrapped;
    PyObject *other, *res;
    int ok;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    ok = func(&self, &other);
    if (ok < 0)
        return NULL;
    if (ok > 0) {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    res = PyTuple_New(2);
    if (res == NULL) {
        Py_DECREF(self);
        Py_DECREF(other);
        return NULL;
    }
    PyTuple_SET_ITEM(res, 0, self);
    PyTuple_SET_ITEM(res, 1, other);
    return res;
}

static PyObject *
wrap_ternaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    ternaryfunc func = (ternaryfunc)wrapped;
    PyObject *other;
    PyObject *third = Py_None;

    if (!PyArg_UnpackTuple(args, "", 1, 2, &other, &third))
        return NULL;
    return (*func)(self, other, third);
}

static PyObject *
wrap_ternaryfunc_r(PyObject *self, PyObject *args, void *wrapped)
{
    ternaryfunc func = (ternaryfunc)wrapped;
    PyObject *other;
    PyObject *third = Py_None;

    if (!PyArg_UnpackTuple(args, "", 1, 2, &other, &third))
        return NULL;
    return (*func)(other, self, third);
}

static PyObject *
wrap_unaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    unaryfunc func = (unaryfunc)wrapped;

    if (!check_num_args(args, 0))
        return NULL;
    return (*func)(self);
}

static PyObject *
wrap_indexargfunc(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeargfunc func = (ssizeargfunc)wrapped;
    PyObject* o;
    Py_ssize_t i;

    if (!PyArg_UnpackTuple(args, "", 1, 1, &o))
        return NULL;
    i = PyNumber_AsSsize_t(o, PyExc_OverflowError);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    return (*func)(self, i);
}

static Py_ssize_t
getindex(PyObject *self, PyObject *arg)
{
    Py_ssize_t i;

    i = PyNumber_AsSsize_t(arg, PyExc_OverflowError);
    if (i == -1 && PyErr_Occurred())
        return -1;
    if (i < 0) {
        PySequenceMethods *sq = Py_TYPE(self)->tp_as_sequence;
        if (sq && sq->sq_length) {
            Py_ssize_t n = (*sq->sq_length)(self);
            if (n < 0)
                return -1;
            i += n;
        }
    }
    return i;
}

static PyObject *
wrap_sq_item(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeargfunc func = (ssizeargfunc)wrapped;
    PyObject *arg;
    Py_ssize_t i;

    if (PyTuple_GET_SIZE(args) == 1) {
        arg = PyTuple_GET_ITEM(args, 0);
        i = getindex(self, arg);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        return (*func)(self, i);
    }
    check_num_args(args, 1);
    assert(PyErr_Occurred());
    return NULL;
}

static PyObject *
wrap_ssizessizeargfunc(PyObject *self, PyObject *args, void *wrapped)
{
    ssizessizeargfunc func = (ssizessizeargfunc)wrapped;
    Py_ssize_t i, j;

    if (!PyArg_ParseTuple(args, "nn", &i, &j))
        return NULL;
    return (*func)(self, i, j);
}

static PyObject *
wrap_sq_setitem(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeobjargproc func = (ssizeobjargproc)wrapped;
    Py_ssize_t i;
    int res;
    PyObject *arg, *value;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &arg, &value))
        return NULL;
    i = getindex(self, arg);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    res = (*func)(self, i, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_sq_delitem(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeobjargproc func = (ssizeobjargproc)wrapped;
    Py_ssize_t i;
    int res;
    PyObject *arg;

    if (!check_num_args(args, 1))
        return NULL;
    arg = PyTuple_GET_ITEM(args, 0);
    i = getindex(self, arg);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    res = (*func)(self, i, NULL);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_ssizessizeobjargproc(PyObject *self, PyObject *args, void *wrapped)
{
    ssizessizeobjargproc func = (ssizessizeobjargproc)wrapped;
    Py_ssize_t i, j;
    int res;
    PyObject *value;

    if (!PyArg_ParseTuple(args, "nnO", &i, &j, &value))
        return NULL;
    res = (*func)(self, i, j, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_delslice(PyObject *self, PyObject *args, void *wrapped)
{
    ssizessizeobjargproc func = (ssizessizeobjargproc)wrapped;
    Py_ssize_t i, j;
    int res;

    if (!PyArg_ParseTuple(args, "nn", &i, &j))
        return NULL;
    res = (*func)(self, i, j, NULL);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_objobjproc(PyObject *self, PyObject *args, void *wrapped)
{
    objobjproc func = (objobjproc)wrapped;
    int res;
    PyObject *value;

    if (!check_num_args(args, 1))
        return NULL;
    value = PyTuple_GET_ITEM(args, 0);
    res = (*func)(self, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    else
        return PyBool_FromLong(res);
}

static PyObject *
wrap_objobjargproc(PyObject *self, PyObject *args, void *wrapped)
{
    objobjargproc func = (objobjargproc)wrapped;
    int res;
    PyObject *key, *value;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &key, &value))
        return NULL;
    res = (*func)(self, key, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_delitem(PyObject *self, PyObject *args, void *wrapped)
{
    objobjargproc func = (objobjargproc)wrapped;
    int res;
    PyObject *key;

    if (!check_num_args(args, 1))
        return NULL;
    key = PyTuple_GET_ITEM(args, 0);
    res = (*func)(self, key, NULL);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_cmpfunc(PyObject *self, PyObject *args, void *wrapped)
{
    cmpfunc func = (cmpfunc)wrapped;
    int res;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    if (Py_TYPE(other)->tp_compare != func &&
        !PyType_IsSubtype(Py_TYPE(other), Py_TYPE(self))) {
        PyErr_Format(
            PyExc_TypeError,
            "%s.__cmp__(x,y) requires y to be a '%s', not a '%s'",
            Py_TYPE(self)->tp_name,
            Py_TYPE(self)->tp_name,
            Py_TYPE(other)->tp_name);
        return NULL;
    }
    res = (*func)(self, other);
    if (PyErr_Occurred())
        return NULL;
    return PyInt_FromLong((long)res);
}

static int
hackcheck(PyObject *self, setattrofunc func, char *what)
{
    PyTypeObject *type = Py_TYPE(self);
    while (type && type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        type = type->tp_base;

    if (type && type->tp_setattro != func) {
        PyErr_Format(PyExc_TypeError,
                     "can't apply this %s to %s object",
                     what,
                     type->tp_name);
        return 0;
    }
    return 1;
}

static PyObject *
wrap_setattr(PyObject *self, PyObject *args, void *wrapped)
{
    setattrofunc func = (setattrofunc)wrapped;
    int res;
    PyObject *name, *value;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &name, &value))
        return NULL;
    if (!hackcheck(self, func, "__setattr__"))
        return NULL;
    res = (*func)(self, name, value);
    if (res < 0)
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_delattr(PyObject *self, PyObject *args, void *wrapped)
{
    setattrofunc func = (setattrofunc)wrapped;
    int res;
    PyObject *name;

    if (!check_num_args(args, 1))
        return NULL;
    name = PyTuple_GET_ITEM(args, 0);
    if (!hackcheck(self, func, "__delattr__"))
        return NULL;
    res = (*func)(self, name, NULL);
    if (res < 0)
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_hashfunc(PyObject *self, PyObject *args, void *wrapped)
{
    hashfunc func = (hashfunc)wrapped;
    long res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyInt_FromLong(res);
}

static PyObject *
wrap_call(PyObject *self, PyObject *args, void *wrapped, PyObject *kwds)
{
    ternaryfunc func = (ternaryfunc)wrapped;

    return (*func)(self, args, kwds);
}

static PyObject *
wrap_richcmpfunc(PyObject *self, PyObject *args, void *wrapped, int op)
{
    richcmpfunc func = (richcmpfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(self, other, op);
}

#undef RICHCMP_WRAPPER
#define RICHCMP_WRAPPER(NAME, OP) \
static PyObject * \
richcmp_##NAME(PyObject *self, PyObject *args, void *wrapped) \
{ \
    return wrap_richcmpfunc(self, args, wrapped, OP); \
}

RICHCMP_WRAPPER(lt, Py_LT)
RICHCMP_WRAPPER(le, Py_LE)
RICHCMP_WRAPPER(eq, Py_EQ)
RICHCMP_WRAPPER(ne, Py_NE)
RICHCMP_WRAPPER(gt, Py_GT)
RICHCMP_WRAPPER(ge, Py_GE)

static PyObject *
wrap_next(PyObject *self, PyObject *args, void *wrapped)
{
    unaryfunc func = (unaryfunc)wrapped;
    PyObject *res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == NULL && !PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
    return res;
}

static PyObject *
wrap_descr_get(PyObject *self, PyObject *args, void *wrapped)
{
    descrgetfunc func = (descrgetfunc)wrapped;
    PyObject *obj;
    PyObject *type = NULL;

    if (!PyArg_UnpackTuple(args, "", 1, 2, &obj, &type))
        return NULL;
    if (obj == Py_None)
        obj = NULL;
    if (type == Py_None)
        type = NULL;
    if (type == NULL &&obj == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "__get__(None, None) is invalid");
        return NULL;
    }
    return (*func)(self, obj, type);
}

static PyObject *
wrap_descr_set(PyObject *self, PyObject *args, void *wrapped)
{
    descrsetfunc func = (descrsetfunc)wrapped;
    PyObject *obj, *value;
    int ret;

    if (!PyArg_UnpackTuple(args, "", 2, 2, &obj, &value))
        return NULL;
    ret = (*func)(self, obj, value);
    if (ret < 0)
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_descr_delete(PyObject *self, PyObject *args, void *wrapped)
{
    descrsetfunc func = (descrsetfunc)wrapped;
    PyObject *obj;
    int ret;

    if (!check_num_args(args, 1))
        return NULL;
    obj = PyTuple_GET_ITEM(args, 0);
    ret = (*func)(self, obj, NULL);
    if (ret < 0)
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
wrap_init(PyObject *self, PyObject *args, void *wrapped, PyObject *kwds)
{
    initproc func = (initproc)wrapped;

    if (func(self, args, kwds) < 0)
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
tp_new_wrapper(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyTypeObject *type, *subtype, *staticbase;
    PyObject *arg0, *res;

    if (self == NULL || !PyType_Check(self))
        Py_FatalError("__new__() called with non-type 'self'");
    type = (PyTypeObject *)self;
    if (!PyTuple_Check(args) || PyTuple_GET_SIZE(args) < 1) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(): not enough arguments",
                     type->tp_name);
        return NULL;
    }
    arg0 = PyTuple_GET_ITEM(args, 0);
    if (!PyType_Check(arg0)) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(X): X is not a type object (%s)",
                     type->tp_name,
                     Py_TYPE(arg0)->tp_name);
        return NULL;
    }
    subtype = (PyTypeObject *)arg0;
    if (!PyType_IsSubtype(subtype, type)) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(%s): %s is not a subtype of %s",
                     type->tp_name,
                     subtype->tp_name,
                     subtype->tp_name,
                     type->tp_name);
        return NULL;
    }

    staticbase = subtype;
    while (staticbase && (staticbase->tp_flags & Py_TPFLAGS_HEAPTYPE))
        staticbase = staticbase->tp_base;

    if (staticbase && staticbase->tp_new != type->tp_new) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(%s) is not safe, use %s.__new__()",
                     type->tp_name,
                     subtype->tp_name,
                     staticbase->tp_name);
        return NULL;
    }

    args = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
    if (args == NULL)
        return NULL;
    res = type->tp_new(subtype, args, kwds);
    Py_DECREF(args);
    return res;
}

static struct PyMethodDef tp_new_methoddef[] = {
    {"__new__", (PyCFunction)tp_new_wrapper, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("T.__new__(S, ...) -> "
               "a new object with type S, a subtype of T")},
    {0}
};

static int
add_tp_new_wrapper(PyTypeObject *type)
{
    PyObject *func;

    if (PyDict_GetItemString(type->tp_dict, "__new__") != NULL)
        return 0;
    func = PyCFunction_New(tp_new_methoddef, (PyObject *)type);
    if (func == NULL)
        return -1;
    if (PyDict_SetItemString(type->tp_dict, "__new__", func)) {
        Py_DECREF(func);
        return -1;
    }
    Py_DECREF(func);
    return 0;
}

#define SLOT0(FUNCNAME, OPSTR) \
static PyObject * \
FUNCNAME(PyObject *self) \
{ \
    static PyObject *cache_str; \
    return call_method(self, OPSTR, &cache_str, "()"); \
}

#define SLOT1(FUNCNAME, OPSTR, ARG1TYPE, ARGCODES) \
static PyObject * \
FUNCNAME(PyObject *self, ARG1TYPE arg1) \
{ \
    static PyObject *cache_str; \
    return call_method(self, OPSTR, &cache_str, "(" ARGCODES ")", arg1); \
}

static int
method_is_overloaded(PyObject *left, PyObject *right, char *name)
{
    PyObject *a, *b;
    int ok;

    b = PyObject_GetAttrString((PyObject *)(Py_TYPE(right)), name);
    if (b == NULL) {
        PyErr_Clear();

        return 0;
    }

    a = PyObject_GetAttrString((PyObject *)(Py_TYPE(left)), name);
    if (a == NULL) {
        PyErr_Clear();
        Py_DECREF(b);

        return 1;
    }

    ok = PyObject_RichCompareBool(a, b, Py_NE);
    Py_DECREF(a);
    Py_DECREF(b);
    if (ok < 0) {
        PyErr_Clear();
        return 0;
    }

    return ok;
}

#define SLOT1BINFULL(FUNCNAME, TESTFUNC, SLOTNAME, OPSTR, ROPSTR) \
static PyObject * \
FUNCNAME(PyObject *self, PyObject *other) \
{ \
    static PyObject *cache_str, *rcache_str; \
    int do_other = Py_TYPE(self) != Py_TYPE(other) && \
        Py_TYPE(other)->tp_as_number != NULL && \
        Py_TYPE(other)->tp_as_number->SLOTNAME == TESTFUNC; \
    if (Py_TYPE(self)->tp_as_number != NULL && \
        Py_TYPE(self)->tp_as_number->SLOTNAME == TESTFUNC) { \
        PyObject *r; \
        if (do_other && \
            PyType_IsSubtype(Py_TYPE(other), Py_TYPE(self)) && \
            method_is_overloaded(self, other, ROPSTR)) { \
            r = call_maybe( \
                other, ROPSTR, &rcache_str, "(O)", self); \
            if (r != Py_NotImplemented) \
                return r; \
            Py_DECREF(r); \
            do_other = 0; \
        } \
        r = call_maybe( \
            self, OPSTR, &cache_str, "(O)", other); \
        if (r != Py_NotImplemented || \
            Py_TYPE(other) == Py_TYPE(self)) \
            return r; \
        Py_DECREF(r); \
    } \
    if (do_other) { \
        return call_maybe( \
            other, ROPSTR, &rcache_str, "(O)", self); \
    } \
    Py_INCREF(Py_NotImplemented); \
    return Py_NotImplemented; \
}

#define SLOT1BIN(FUNCNAME, SLOTNAME, OPSTR, ROPSTR) \
    SLOT1BINFULL(FUNCNAME, FUNCNAME, SLOTNAME, OPSTR, ROPSTR)

#define SLOT2(FUNCNAME, OPSTR, ARG1TYPE, ARG2TYPE, ARGCODES) \
static PyObject * \
FUNCNAME(PyObject *self, ARG1TYPE arg1, ARG2TYPE arg2) \
{ \
    static PyObject *cache_str; \
    return call_method(self, OPSTR, &cache_str, \
                       "(" ARGCODES ")", arg1, arg2); \
}

static Py_ssize_t
slot_sq_length(PyObject *self)
{
    static PyObject *len_str;
    PyObject *res = call_method(self, "__len__", &len_str, "()");
    Py_ssize_t len;

    if (res == NULL)
        return -1;
    len = PyInt_AsSsize_t(res);
    Py_DECREF(res);
    if (len < 0) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError,
                            "__len__() should return >= 0");
        return -1;
    }
    return len;
}

static PyObject *
slot_sq_item(PyObject *self, Py_ssize_t i)
{
    static PyObject *getitem_str;
    PyObject *func, *args = NULL, *ival = NULL, *retval = NULL;
    descrgetfunc f;

    if (getitem_str == NULL) {
        getitem_str = PyString_InternFromString("__getitem__");
        if (getitem_str == NULL)
            return NULL;
    }
    func = _PyType_Lookup(Py_TYPE(self), getitem_str);
    if (func != NULL) {
        if ((f = Py_TYPE(func)->tp_descr_get) == NULL)
            Py_INCREF(func);
        else {
            func = f(func, self, (PyObject *)(Py_TYPE(self)));
            if (func == NULL) {
                return NULL;
            }
        }
        ival = PyInt_FromSsize_t(i);
        if (ival != NULL) {
            args = PyTuple_New(1);
            if (args != NULL) {
                PyTuple_SET_ITEM(args, 0, ival);
                retval = PyObject_Call(func, args, NULL);
                Py_XDECREF(args);
                Py_XDECREF(func);
                return retval;
            }
        }
    }
    else {
        PyErr_SetObject(PyExc_AttributeError, getitem_str);
    }
    Py_XDECREF(args);
    Py_XDECREF(ival);
    Py_XDECREF(func);
    return NULL;
}

static PyObject*
slot_sq_slice(PyObject *self, Py_ssize_t i, Py_ssize_t j)
{
    static PyObject *getslice_str;

    if (PyErr_WarnPy3k("in 3.x, __getslice__ has been removed; "
                        "use __getitem__", 1) < 0)
        return NULL;
    return call_method(self, "__getslice__", &getslice_str,
        "nn", i, j);
}

static int
slot_sq_ass_item(PyObject *self, Py_ssize_t index, PyObject *value)
{
    PyObject *res;
    static PyObject *delitem_str, *setitem_str;

    if (value == NULL)
        res = call_method(self, "__delitem__", &delitem_str,
                          "(n)", index);
    else
        res = call_method(self, "__setitem__", &setitem_str,
                          "(nO)", index, value);
    if (res == NULL)
        return -1;
    Py_DECREF(res);
    return 0;
}

static int
slot_sq_ass_slice(PyObject *self, Py_ssize_t i, Py_ssize_t j, PyObject *value)
{
    PyObject *res;
    static PyObject *delslice_str, *setslice_str;

    if (value == NULL) {
        if (PyErr_WarnPy3k("in 3.x, __delslice__ has been removed; "
                           "use __delitem__", 1) < 0)
            return -1;
        res = call_method(self, "__delslice__", &delslice_str,
                          "(nn)", i, j);
    }
    else {
        if (PyErr_WarnPy3k("in 3.x, __setslice__ has been removed; "
                                "use __setitem__", 1) < 0)
            return -1;
        res = call_method(self, "__setslice__", &setslice_str,
                  "(nnO)", i, j, value);
    }
    if (res == NULL)
        return -1;
    Py_DECREF(res);
    return 0;
}

static int
slot_sq_contains(PyObject *self, PyObject *value)
{
    PyObject *func, *res, *args;
    int result = -1;

    static PyObject *contains_str;

    func = lookup_maybe(self, "__contains__", &contains_str);
    if (func != NULL) {
        args = PyTuple_Pack(1, value);
        if (args == NULL)
            res = NULL;
        else {
            res = PyObject_Call(func, args, NULL);
            Py_DECREF(args);
        }
        Py_DECREF(func);
        if (res != NULL) {
            result = PyObject_IsTrue(res);
            Py_DECREF(res);
        }
    }
    else if (! PyErr_Occurred()) {

        result = (int)_PySequence_IterSearch(self, value,
                                         PY_ITERSEARCH_CONTAINS);
    }
    return result;
}

#define slot_mp_length slot_sq_length

SLOT1(slot_mp_subscript, "__getitem__", PyObject *, "O")

static int
slot_mp_ass_subscript(PyObject *self, PyObject *key, PyObject *value)
{
    PyObject *res;
    static PyObject *delitem_str, *setitem_str;

    if (value == NULL)
        res = call_method(self, "__delitem__", &delitem_str,
                          "(O)", key);
    else
        res = call_method(self, "__setitem__", &setitem_str,
                         "(OO)", key, value);
    if (res == NULL)
        return -1;
    Py_DECREF(res);
    return 0;
}

SLOT1BIN(slot_nb_add, nb_add, "__add__", "__radd__")
SLOT1BIN(slot_nb_subtract, nb_subtract, "__sub__", "__rsub__")
SLOT1BIN(slot_nb_multiply, nb_multiply, "__mul__", "__rmul__")
SLOT1BIN(slot_nb_divide, nb_divide, "__div__", "__rdiv__")
SLOT1BIN(slot_nb_remainder, nb_remainder, "__mod__", "__rmod__")
SLOT1BIN(slot_nb_divmod, nb_divmod, "__divmod__", "__rdivmod__")

static PyObject *slot_nb_power(PyObject *, PyObject *, PyObject *);

SLOT1BINFULL(slot_nb_power_binary, slot_nb_power,
             nb_power, "__pow__", "__rpow__")

static PyObject *
slot_nb_power(PyObject *self, PyObject *other, PyObject *modulus)
{
    static PyObject *pow_str;

    if (modulus == Py_None)
        return slot_nb_power_binary(self, other);

    if (Py_TYPE(self)->tp_as_number != NULL &&
        Py_TYPE(self)->tp_as_number->nb_power == slot_nb_power) {
        return call_method(self, "__pow__", &pow_str,
                           "(OO)", other, modulus);
    }
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

SLOT0(slot_nb_negative, "__neg__")
SLOT0(slot_nb_positive, "__pos__")
SLOT0(slot_nb_absolute, "__abs__")

static int
slot_nb_nonzero(PyObject *self)
{
    PyObject *func, *args;
    static PyObject *nonzero_str, *len_str;
    int result = -1;
    int using_len = 0;

    func = lookup_maybe(self, "__nonzero__", &nonzero_str);
    if (func == NULL) {
        if (PyErr_Occurred())
            return -1;
        func = lookup_maybe(self, "__len__", &len_str);
        if (func == NULL)
            return PyErr_Occurred() ? -1 : 1;
        using_len = 1;
    }
    args = PyTuple_New(0);
    if (args != NULL) {
        PyObject *temp = PyObject_Call(func, args, NULL);
        Py_DECREF(args);
        if (temp != NULL) {
            if (PyInt_CheckExact(temp) || PyBool_Check(temp))
                result = PyObject_IsTrue(temp);
            else {
                PyErr_Format(PyExc_TypeError,
                             "%s should return "
                             "bool or int, returned %s",
                             (using_len ? "__len__"
                                        : "__nonzero__"),
                             temp->ob_type->tp_name);
                result = -1;
            }
            Py_DECREF(temp);
        }
    }
    Py_DECREF(func);
    return result;
}

static PyObject *
slot_nb_index(PyObject *self)
{
    static PyObject *index_str;
    return call_method(self, "__index__", &index_str, "()");
}

SLOT0(slot_nb_invert, "__invert__")
SLOT1BIN(slot_nb_lshift, nb_lshift, "__lshift__", "__rlshift__")
SLOT1BIN(slot_nb_rshift, nb_rshift, "__rshift__", "__rrshift__")
SLOT1BIN(slot_nb_and, nb_and, "__and__", "__rand__")
SLOT1BIN(slot_nb_xor, nb_xor, "__xor__", "__rxor__")
SLOT1BIN(slot_nb_or, nb_or, "__or__", "__ror__")

static int
slot_nb_coerce(PyObject **a, PyObject **b)
{
    static PyObject *coerce_str;
    PyObject *self = *a, *other = *b;

    if (self->ob_type->tp_as_number != NULL &&
        self->ob_type->tp_as_number->nb_coerce == slot_nb_coerce) {
        PyObject *r;
        r = call_maybe(
            self, "__coerce__", &coerce_str, "(O)", other);
        if (r == NULL)
            return -1;
        if (r == Py_NotImplemented) {
            Py_DECREF(r);
        }
        else {
            if (!PyTuple_Check(r) || PyTuple_GET_SIZE(r) != 2) {
                PyErr_SetString(PyExc_TypeError,
                    "__coerce__ didn't return a 2-tuple");
                Py_DECREF(r);
                return -1;
            }
            *a = PyTuple_GET_ITEM(r, 0);
            Py_INCREF(*a);
            *b = PyTuple_GET_ITEM(r, 1);
            Py_INCREF(*b);
            Py_DECREF(r);
            return 0;
        }
    }
    if (other->ob_type->tp_as_number != NULL &&
        other->ob_type->tp_as_number->nb_coerce == slot_nb_coerce) {
        PyObject *r;
        r = call_maybe(
            other, "__coerce__", &coerce_str, "(O)", self);
        if (r == NULL)
            return -1;
        if (r == Py_NotImplemented) {
            Py_DECREF(r);
            return 1;
        }
        if (!PyTuple_Check(r) || PyTuple_GET_SIZE(r) != 2) {
            PyErr_SetString(PyExc_TypeError,
                            "__coerce__ didn't return a 2-tuple");
            Py_DECREF(r);
            return -1;
        }
        *a = PyTuple_GET_ITEM(r, 1);
        Py_INCREF(*a);
        *b = PyTuple_GET_ITEM(r, 0);
        Py_INCREF(*b);
        Py_DECREF(r);
        return 0;
    }
    return 1;
}

SLOT0(slot_nb_int, "__int__")
SLOT0(slot_nb_long, "__long__")
SLOT0(slot_nb_float, "__float__")
SLOT0(slot_nb_oct, "__oct__")
SLOT0(slot_nb_hex, "__hex__")
SLOT1(slot_nb_inplace_add, "__iadd__", PyObject *, "O")
SLOT1(slot_nb_inplace_subtract, "__isub__", PyObject *, "O")
SLOT1(slot_nb_inplace_multiply, "__imul__", PyObject *, "O")
SLOT1(slot_nb_inplace_divide, "__idiv__", PyObject *, "O")
SLOT1(slot_nb_inplace_remainder, "__imod__", PyObject *, "O")

static PyObject *
slot_nb_inplace_power(PyObject *self, PyObject * arg1, PyObject *arg2)
{
  static PyObject *cache_str;
  return call_method(self, "__ipow__", &cache_str, "(" "O" ")", arg1);
}
SLOT1(slot_nb_inplace_lshift, "__ilshift__", PyObject *, "O")
SLOT1(slot_nb_inplace_rshift, "__irshift__", PyObject *, "O")
SLOT1(slot_nb_inplace_and, "__iand__", PyObject *, "O")
SLOT1(slot_nb_inplace_xor, "__ixor__", PyObject *, "O")
SLOT1(slot_nb_inplace_or, "__ior__", PyObject *, "O")
SLOT1BIN(slot_nb_floor_divide, nb_floor_divide,
         "__floordiv__", "__rfloordiv__")
SLOT1BIN(slot_nb_true_divide, nb_true_divide, "__truediv__", "__rtruediv__")
SLOT1(slot_nb_inplace_floor_divide, "__ifloordiv__", PyObject *, "O")
SLOT1(slot_nb_inplace_true_divide, "__itruediv__", PyObject *, "O")

static int
half_compare(PyObject *self, PyObject *other)
{
    PyObject *func, *args, *res;
    static PyObject *cmp_str;
    Py_ssize_t c;

    func = lookup_method(self, "__cmp__", &cmp_str);
    if (func == NULL) {
        PyErr_Clear();
    }
    else {
        args = PyTuple_Pack(1, other);
        if (args == NULL)
            res = NULL;
        else {
            res = PyObject_Call(func, args, NULL);
            Py_DECREF(args);
        }
        Py_DECREF(func);
        if (res != Py_NotImplemented) {
            if (res == NULL)
                return -2;
            c = PyInt_AsLong(res);
            Py_DECREF(res);
            if (c == -1 && PyErr_Occurred())
                return -2;
            return (c < 0) ? -1 : (c > 0) ? 1 : 0;
        }
        Py_DECREF(res);
    }
    return 2;
}

int
_PyObject_SlotCompare(PyObject *self, PyObject *other)
{
    int c;

    if (Py_TYPE(self)->tp_compare == _PyObject_SlotCompare) {
        c = half_compare(self, other);
        if (c <= 1)
            return c;
    }
    if (Py_TYPE(other)->tp_compare == _PyObject_SlotCompare) {
        c = half_compare(other, self);
        if (c < -1)
            return -2;
        if (c <= 1)
            return -c;
    }
    return (void *)self < (void *)other ? -1 :
        (void *)self > (void *)other ? 1 : 0;
}

static PyObject *
slot_tp_repr(PyObject *self)
{
    PyObject *func, *res;
    static PyObject *repr_str;

    func = lookup_method(self, "__repr__", &repr_str);
    if (func != NULL) {
        res = PyEval_CallObject(func, NULL);
        Py_DECREF(func);
        return res;
    }
    PyErr_Clear();
    return PyString_FromFormat("<%s object at %p>",
                               Py_TYPE(self)->tp_name, self);
}

static PyObject *
slot_tp_str(PyObject *self)
{
    PyObject *func, *res;
    static PyObject *str_str;

    func = lookup_method(self, "__str__", &str_str);
    if (func != NULL) {
        res = PyEval_CallObject(func, NULL);
        Py_DECREF(func);
        return res;
    }
    else {
        PyErr_Clear();
        return slot_tp_repr(self);
    }
}

static long
slot_tp_hash(PyObject *self)
{
    PyObject *func;
    static PyObject *hash_str, *eq_str, *cmp_str;
    long h;

    func = lookup_method(self, "__hash__", &hash_str);

    if (func != NULL && func != Py_None) {
        PyObject *res = PyEval_CallObject(func, NULL);
        Py_DECREF(func);
        if (res == NULL)
            return -1;
        if (PyLong_Check(res))
            h = PyLong_Type.tp_hash(res);
        else
            h = PyInt_AsLong(res);
        Py_DECREF(res);
    }
    else {
        Py_XDECREF(func);
        PyErr_Clear();
        func = lookup_method(self, "__eq__", &eq_str);
        if (func == NULL) {
            PyErr_Clear();
            func = lookup_method(self, "__cmp__", &cmp_str);
        }
        if (func != NULL) {
            Py_DECREF(func);
            return PyObject_HashNotImplemented(self);
        }
        PyErr_Clear();
        h = _Py_HashPointer((void *)self);
    }
    if (h == -1 && !PyErr_Occurred())
        h = -2;
    return h;
}

static PyObject *
slot_tp_call(PyObject *self, PyObject *args, PyObject *kwds)
{
    static PyObject *call_str;
    PyObject *meth = lookup_method(self, "__call__", &call_str);
    PyObject *res;

    if (meth == NULL)
        return NULL;

    res = PyObject_Call(meth, args, kwds);

    Py_DECREF(meth);
    return res;
}

static PyObject *
slot_tp_getattro(PyObject *self, PyObject *name)
{
    static PyObject *getattribute_str = NULL;
    return call_method(self, "__getattribute__", &getattribute_str,
                       "(O)", name);
}

static PyObject *
call_attribute(PyObject *self, PyObject *attr, PyObject *name)
{
    PyObject *res, *descr = NULL;
    descrgetfunc f = Py_TYPE(attr)->tp_descr_get;

    if (f != NULL) {
        descr = f(attr, self, (PyObject *)(Py_TYPE(self)));
        if (descr == NULL)
            return NULL;
        else
            attr = descr;
    }
    res = PyObject_CallFunctionObjArgs(attr, name, NULL);
    Py_XDECREF(descr);
    return res;
}

static PyObject *
slot_tp_getattr_hook(PyObject *self, PyObject *name)
{
    PyTypeObject *tp = Py_TYPE(self);
    PyObject *getattr, *getattribute, *res;
    static PyObject *getattribute_str = NULL;
    static PyObject *getattr_str = NULL;

    if (getattr_str == NULL) {
        getattr_str = PyString_InternFromString("__getattr__");
        if (getattr_str == NULL)
            return NULL;
    }
    if (getattribute_str == NULL) {
        getattribute_str =
            PyString_InternFromString("__getattribute__");
        if (getattribute_str == NULL)
            return NULL;
    }

    getattr = _PyType_Lookup(tp, getattr_str);
    if (getattr == NULL) {

        tp->tp_getattro = slot_tp_getattro;
        return slot_tp_getattro(self, name);
    }
    Py_INCREF(getattr);

    getattribute = _PyType_Lookup(tp, getattribute_str);
    if (getattribute == NULL ||
        (Py_TYPE(getattribute) == &PyWrapperDescr_Type &&
         ((PyWrapperDescrObject *)getattribute)->d_wrapped ==
         (void *)PyObject_GenericGetAttr))
        res = PyObject_GenericGetAttr(self, name);
    else {
        Py_INCREF(getattribute);
        res = call_attribute(self, getattribute, name);
        Py_DECREF(getattribute);
    }
    if (res == NULL && PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        res = call_attribute(self, getattr, name);
    }
    Py_DECREF(getattr);
    return res;
}

static int
slot_tp_setattro(PyObject *self, PyObject *name, PyObject *value)
{
    PyObject *res;
    static PyObject *delattr_str, *setattr_str;

    if (value == NULL)
        res = call_method(self, "__delattr__", &delattr_str,
                          "(O)", name);
    else
        res = call_method(self, "__setattr__", &setattr_str,
                          "(OO)", name, value);
    if (res == NULL)
        return -1;
    Py_DECREF(res);
    return 0;
}

static char *name_op[] = {
    "__lt__",
    "__le__",
    "__eq__",
    "__ne__",
    "__gt__",
    "__ge__",
};

static PyObject *
half_richcompare(PyObject *self, PyObject *other, int op)
{
    PyObject *func, *args, *res;
    static PyObject *op_str[6];

    func = lookup_method(self, name_op[op], &op_str[op]);
    if (func == NULL) {
        PyErr_Clear();
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    args = PyTuple_Pack(1, other);
    if (args == NULL)
        res = NULL;
    else {
        res = PyObject_Call(func, args, NULL);
        Py_DECREF(args);
    }
    Py_DECREF(func);
    return res;
}

static PyObject *
slot_tp_richcompare(PyObject *self, PyObject *other, int op)
{
    PyObject *res;

    if (Py_TYPE(self)->tp_richcompare == slot_tp_richcompare) {
        res = half_richcompare(self, other, op);
        if (res != Py_NotImplemented)
            return res;
        Py_DECREF(res);
    }
    if (Py_TYPE(other)->tp_richcompare == slot_tp_richcompare) {
        res = half_richcompare(other, self, _Py_SwappedOp[op]);
        if (res != Py_NotImplemented) {
            return res;
        }
        Py_DECREF(res);
    }
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

static PyObject *
slot_tp_iter(PyObject *self)
{
    PyObject *func, *res;
    static PyObject *iter_str, *getitem_str;

    func = lookup_method(self, "__iter__", &iter_str);
    if (func != NULL) {
        PyObject *args;
        args = res = PyTuple_New(0);
        if (args != NULL) {
            res = PyObject_Call(func, args, NULL);
            Py_DECREF(args);
        }
        Py_DECREF(func);
        return res;
    }
    PyErr_Clear();
    func = lookup_method(self, "__getitem__", &getitem_str);
    if (func == NULL) {
        PyErr_Format(PyExc_TypeError,
                     "'%.200s' object is not iterable",
                     Py_TYPE(self)->tp_name);
        return NULL;
    }
    Py_DECREF(func);
    return PySeqIter_New(self);
}

static PyObject *
slot_tp_iternext(PyObject *self)
{
    static PyObject *next_str;
    return call_method(self, "next", &next_str, "()");
}

static PyObject *
slot_tp_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    PyTypeObject *tp = Py_TYPE(self);
    PyObject *get;
    static PyObject *get_str = NULL;

    if (get_str == NULL) {
        get_str = PyString_InternFromString("__get__");
        if (get_str == NULL)
            return NULL;
    }
    get = _PyType_Lookup(tp, get_str);
    if (get == NULL) {

        if (tp->tp_descr_get == slot_tp_descr_get)
            tp->tp_descr_get = NULL;
        Py_INCREF(self);
        return self;
    }
    if (obj == NULL)
        obj = Py_None;
    if (type == NULL)
        type = Py_None;
    return PyObject_CallFunctionObjArgs(get, self, obj, type, NULL);
}

static int
slot_tp_descr_set(PyObject *self, PyObject *target, PyObject *value)
{
    PyObject *res;
    static PyObject *del_str, *set_str;

    if (value == NULL)
        res = call_method(self, "__delete__", &del_str,
                          "(O)", target);
    else
        res = call_method(self, "__set__", &set_str,
                          "(OO)", target, value);
    if (res == NULL)
        return -1;
    Py_DECREF(res);
    return 0;
}

static int
slot_tp_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    static PyObject *init_str;
    PyObject *meth = lookup_method(self, "__init__", &init_str);
    PyObject *res;

    if (meth == NULL)
        return -1;
    res = PyObject_Call(meth, args, kwds);
    Py_DECREF(meth);
    if (res == NULL)
        return -1;
    if (res != Py_None) {
        PyErr_Format(PyExc_TypeError,
                     "__init__() should return None, not '%.200s'",
                     Py_TYPE(res)->tp_name);
        Py_DECREF(res);
        return -1;
    }
    Py_DECREF(res);
    return 0;
}

static PyObject *
slot_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static PyObject *new_str;
    PyObject *func;
    PyObject *newargs, *x;
    Py_ssize_t i, n;

    if (new_str == NULL) {
        new_str = PyString_InternFromString("__new__");
        if (new_str == NULL)
            return NULL;
    }
    func = PyObject_GetAttr((PyObject *)type, new_str);
    if (func == NULL)
        return NULL;
    assert(PyTuple_Check(args));
    n = PyTuple_GET_SIZE(args);
    newargs = PyTuple_New(n+1);
    if (newargs == NULL)
        return NULL;
    Py_INCREF(type);
    PyTuple_SET_ITEM(newargs, 0, (PyObject *)type);
    for (i = 0; i < n; i++) {
        x = PyTuple_GET_ITEM(args, i);
        Py_INCREF(x);
        PyTuple_SET_ITEM(newargs, i+1, x);
    }
    x = PyObject_Call(func, newargs, kwds);
    Py_DECREF(newargs);
    Py_DECREF(func);
    return x;
}

static void
slot_tp_del(PyObject *self)
{
    static PyObject *del_str = NULL;
    PyObject *del, *res;
    PyObject *error_type, *error_value, *error_traceback;

    assert(self->ob_refcnt == 0);
    self->ob_refcnt = 1;

    PyErr_Fetch(&error_type, &error_value, &error_traceback);

    del = lookup_maybe(self, "__del__", &del_str);
    if (del != NULL) {
        res = PyEval_CallObject(del, NULL);
        if (res == NULL)
            PyErr_WriteUnraisable(del);
        else
            Py_DECREF(res);
        Py_DECREF(del);
    }

    PyErr_Restore(error_type, error_value, error_traceback);

    assert(self->ob_refcnt > 0);
    if (--self->ob_refcnt == 0)
        return;

    {
        Py_ssize_t refcnt = self->ob_refcnt;
        _Py_NewReference(self);
        self->ob_refcnt = refcnt;
    }
    assert(!PyType_IS_GC(Py_TYPE(self)) ||
           _Py_AS_GC(self)->gc.gc_refs != _PyGC_REFS_UNTRACKED);

    _Py_DEC_REFTOTAL;

#ifdef COUNT_ALLOCS
    --Py_TYPE(self)->tp_frees;
    --Py_TYPE(self)->tp_allocs;
#endif
}

typedef struct wrapperbase slotdef;

#undef TPSLOT
#undef FLSLOT
#undef ETSLOT
#undef SQSLOT
#undef MPSLOT
#undef NBSLOT
#undef UNSLOT
#undef IBSLOT
#undef BINSLOT
#undef RBINSLOT

#define TPSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    {NAME, offsetof(PyTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
     PyDoc_STR(DOC)}
#define FLSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC, FLAGS) \
    {NAME, offsetof(PyTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
     PyDoc_STR(DOC), FLAGS}
#define ETSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    {NAME, offsetof(PyHeapTypeObject, SLOT), (void *)(FUNCTION), WRAPPER, \
     PyDoc_STR(DOC)}
#define SQSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_sequence.SLOT, FUNCTION, WRAPPER, DOC)
#define MPSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_mapping.SLOT, FUNCTION, WRAPPER, DOC)
#define NBSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, DOC)
#define UNSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, \
           "x." NAME "() <==> " DOC)
#define IBSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, \
           "x." NAME "(y) <==> x" DOC "y")
#define BINSLOT(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_l, \
           "x." NAME "(y) <==> x" DOC "y")
#define RBINSLOT(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_r, \
           "x." NAME "(y) <==> y" DOC "x")
#define BINSLOTNOTINFIX(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_l, \
           "x." NAME "(y) <==> " DOC)
#define RBINSLOTNOTINFIX(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_r, \
           "x." NAME "(y) <==> " DOC)

static slotdef slotdefs[] = {
    TPSLOT("__str__", tp_print, NULL, NULL, ""),
    TPSLOT("__repr__", tp_print, NULL, NULL, ""),
    TPSLOT("__getattribute__", tp_getattr, NULL, NULL, ""),
    TPSLOT("__getattr__", tp_getattr, NULL, NULL, ""),
    TPSLOT("__setattr__", tp_setattr, NULL, NULL, ""),
    TPSLOT("__delattr__", tp_setattr, NULL, NULL, ""),
    TPSLOT("__cmp__", tp_compare, _PyObject_SlotCompare, wrap_cmpfunc,
           "x.__cmp__(y) <==> cmp(x,y)"),
    TPSLOT("__repr__", tp_repr, slot_tp_repr, wrap_unaryfunc,
           "x.__repr__() <==> repr(x)"),
    TPSLOT("__hash__", tp_hash, slot_tp_hash, wrap_hashfunc,
           "x.__hash__() <==> hash(x)"),
    FLSLOT("__call__", tp_call, slot_tp_call, (wrapperfunc)wrap_call,
           "x.__call__(...) <==> x(...)", PyWrapperFlag_KEYWORDS),
    TPSLOT("__str__", tp_str, slot_tp_str, wrap_unaryfunc,
           "x.__str__() <==> str(x)"),
    TPSLOT("__getattribute__", tp_getattro, slot_tp_getattr_hook,
           wrap_binaryfunc, "x.__getattribute__('name') <==> x.name"),
    TPSLOT("__getattr__", tp_getattro, slot_tp_getattr_hook, NULL, ""),
    TPSLOT("__setattr__", tp_setattro, slot_tp_setattro, wrap_setattr,
           "x.__setattr__('name', value) <==> x.name = value"),
    TPSLOT("__delattr__", tp_setattro, slot_tp_setattro, wrap_delattr,
           "x.__delattr__('name') <==> del x.name"),
    TPSLOT("__lt__", tp_richcompare, slot_tp_richcompare, richcmp_lt,
           "x.__lt__(y) <==> x<y"),
    TPSLOT("__le__", tp_richcompare, slot_tp_richcompare, richcmp_le,
           "x.__le__(y) <==> x<=y"),
    TPSLOT("__eq__", tp_richcompare, slot_tp_richcompare, richcmp_eq,
           "x.__eq__(y) <==> x==y"),
    TPSLOT("__ne__", tp_richcompare, slot_tp_richcompare, richcmp_ne,
           "x.__ne__(y) <==> x!=y"),
    TPSLOT("__gt__", tp_richcompare, slot_tp_richcompare, richcmp_gt,
           "x.__gt__(y) <==> x>y"),
    TPSLOT("__ge__", tp_richcompare, slot_tp_richcompare, richcmp_ge,
           "x.__ge__(y) <==> x>=y"),
    TPSLOT("__iter__", tp_iter, slot_tp_iter, wrap_unaryfunc,
           "x.__iter__() <==> iter(x)"),
    TPSLOT("next", tp_iternext, slot_tp_iternext, wrap_next,
           "x.next() -> the next value, or raise StopIteration"),
    TPSLOT("__get__", tp_descr_get, slot_tp_descr_get, wrap_descr_get,
           "descr.__get__(obj[, type]) -> value"),
    TPSLOT("__set__", tp_descr_set, slot_tp_descr_set, wrap_descr_set,
           "descr.__set__(obj, value)"),
    TPSLOT("__delete__", tp_descr_set, slot_tp_descr_set,
           wrap_descr_delete, "descr.__delete__(obj)"),
    FLSLOT("__init__", tp_init, slot_tp_init, (wrapperfunc)wrap_init,
           "x.__init__(...) initializes x; "
           "see help(type(x)) for signature",
           PyWrapperFlag_KEYWORDS),
    TPSLOT("__new__", tp_new, slot_tp_new, NULL, ""),
    TPSLOT("__del__", tp_del, slot_tp_del, NULL, ""),
    BINSLOT("__add__", nb_add, slot_nb_add,
        "+"),
    RBINSLOT("__radd__", nb_add, slot_nb_add,
             "+"),
    BINSLOT("__sub__", nb_subtract, slot_nb_subtract,
        "-"),
    RBINSLOT("__rsub__", nb_subtract, slot_nb_subtract,
             "-"),
    BINSLOT("__mul__", nb_multiply, slot_nb_multiply,
        "*"),
    RBINSLOT("__rmul__", nb_multiply, slot_nb_multiply,
             "*"),
    BINSLOT("__div__", nb_divide, slot_nb_divide,
        "/"),
    RBINSLOT("__rdiv__", nb_divide, slot_nb_divide,
             "/"),
    BINSLOT("__mod__", nb_remainder, slot_nb_remainder,
        "%"),
    RBINSLOT("__rmod__", nb_remainder, slot_nb_remainder,
             "%"),
    BINSLOTNOTINFIX("__divmod__", nb_divmod, slot_nb_divmod,
        "divmod(x, y)"),
    RBINSLOTNOTINFIX("__rdivmod__", nb_divmod, slot_nb_divmod,
             "divmod(y, x)"),
    NBSLOT("__pow__", nb_power, slot_nb_power, wrap_ternaryfunc,
           "x.__pow__(y[, z]) <==> pow(x, y[, z])"),
    NBSLOT("__rpow__", nb_power, slot_nb_power, wrap_ternaryfunc_r,
           "y.__rpow__(x[, z]) <==> pow(x, y[, z])"),
    UNSLOT("__neg__", nb_negative, slot_nb_negative, wrap_unaryfunc, "-x"),
    UNSLOT("__pos__", nb_positive, slot_nb_positive, wrap_unaryfunc, "+x"),
    UNSLOT("__abs__", nb_absolute, slot_nb_absolute, wrap_unaryfunc,
           "abs(x)"),
    UNSLOT("__nonzero__", nb_nonzero, slot_nb_nonzero, wrap_inquirypred,
           "x != 0"),
    UNSLOT("__invert__", nb_invert, slot_nb_invert, wrap_unaryfunc, "~x"),
    BINSLOT("__lshift__", nb_lshift, slot_nb_lshift, "<<"),
    RBINSLOT("__rlshift__", nb_lshift, slot_nb_lshift, "<<"),
    BINSLOT("__rshift__", nb_rshift, slot_nb_rshift, ">>"),
    RBINSLOT("__rrshift__", nb_rshift, slot_nb_rshift, ">>"),
    BINSLOT("__and__", nb_and, slot_nb_and, "&"),
    RBINSLOT("__rand__", nb_and, slot_nb_and, "&"),
    BINSLOT("__xor__", nb_xor, slot_nb_xor, "^"),
    RBINSLOT("__rxor__", nb_xor, slot_nb_xor, "^"),
    BINSLOT("__or__", nb_or, slot_nb_or, "|"),
    RBINSLOT("__ror__", nb_or, slot_nb_or, "|"),
    NBSLOT("__coerce__", nb_coerce, slot_nb_coerce, wrap_coercefunc,
           "x.__coerce__(y) <==> coerce(x, y)"),
    UNSLOT("__int__", nb_int, slot_nb_int, wrap_unaryfunc,
           "int(x)"),
    UNSLOT("__long__", nb_long, slot_nb_long, wrap_unaryfunc,
           "long(x)"),
    UNSLOT("__float__", nb_float, slot_nb_float, wrap_unaryfunc,
           "float(x)"),
    UNSLOT("__oct__", nb_oct, slot_nb_oct, wrap_unaryfunc,
           "oct(x)"),
    UNSLOT("__hex__", nb_hex, slot_nb_hex, wrap_unaryfunc,
           "hex(x)"),
    IBSLOT("__iadd__", nb_inplace_add, slot_nb_inplace_add,
           wrap_binaryfunc, "+="),
    IBSLOT("__isub__", nb_inplace_subtract, slot_nb_inplace_subtract,
           wrap_binaryfunc, "-="),
    IBSLOT("__imul__", nb_inplace_multiply, slot_nb_inplace_multiply,
           wrap_binaryfunc, "*="),
    IBSLOT("__idiv__", nb_inplace_divide, slot_nb_inplace_divide,
           wrap_binaryfunc, "/="),
    IBSLOT("__imod__", nb_inplace_remainder, slot_nb_inplace_remainder,
           wrap_binaryfunc, "%="),
    IBSLOT("__ipow__", nb_inplace_power, slot_nb_inplace_power,
           wrap_binaryfunc, "**="),
    IBSLOT("__ilshift__", nb_inplace_lshift, slot_nb_inplace_lshift,
           wrap_binaryfunc, "<<="),
    IBSLOT("__irshift__", nb_inplace_rshift, slot_nb_inplace_rshift,
           wrap_binaryfunc, ">>="),
    IBSLOT("__iand__", nb_inplace_and, slot_nb_inplace_and,
           wrap_binaryfunc, "&="),
    IBSLOT("__ixor__", nb_inplace_xor, slot_nb_inplace_xor,
           wrap_binaryfunc, "^="),
    IBSLOT("__ior__", nb_inplace_or, slot_nb_inplace_or,
           wrap_binaryfunc, "|="),
    BINSLOT("__floordiv__", nb_floor_divide, slot_nb_floor_divide, "//"),
    RBINSLOT("__rfloordiv__", nb_floor_divide, slot_nb_floor_divide, "//"),
    BINSLOT("__truediv__", nb_true_divide, slot_nb_true_divide, "/"),
    RBINSLOT("__rtruediv__", nb_true_divide, slot_nb_true_divide, "/"),
    IBSLOT("__ifloordiv__", nb_inplace_floor_divide,
           slot_nb_inplace_floor_divide, wrap_binaryfunc, "//="),
    IBSLOT("__itruediv__", nb_inplace_true_divide,
           slot_nb_inplace_true_divide, wrap_binaryfunc, "/="),
    NBSLOT("__index__", nb_index, slot_nb_index, wrap_unaryfunc,
           "x[y:z] <==> x[y.__index__():z.__index__()]"),
    MPSLOT("__len__", mp_length, slot_mp_length, wrap_lenfunc,
           "x.__len__() <==> len(x)"),
    MPSLOT("__getitem__", mp_subscript, slot_mp_subscript,
           wrap_binaryfunc,
           "x.__getitem__(y) <==> x[y]"),
    MPSLOT("__setitem__", mp_ass_subscript, slot_mp_ass_subscript,
           wrap_objobjargproc,
           "x.__setitem__(i, y) <==> x[i]=y"),
    MPSLOT("__delitem__", mp_ass_subscript, slot_mp_ass_subscript,
           wrap_delitem,
           "x.__delitem__(y) <==> del x[y]"),
    SQSLOT("__len__", sq_length, slot_sq_length, wrap_lenfunc,
           "x.__len__() <==> len(x)"),

    SQSLOT("__add__", sq_concat, NULL, wrap_binaryfunc,
      "x.__add__(y) <==> x+y"),
    SQSLOT("__mul__", sq_repeat, NULL, wrap_indexargfunc,
      "x.__mul__(n) <==> x*n"),
    SQSLOT("__rmul__", sq_repeat, NULL, wrap_indexargfunc,
      "x.__rmul__(n) <==> n*x"),
    SQSLOT("__getitem__", sq_item, slot_sq_item, wrap_sq_item,
           "x.__getitem__(y) <==> x[y]"),
    SQSLOT("__getslice__", sq_slice, slot_sq_slice, wrap_ssizessizeargfunc,
           "x.__getslice__(i, j) <==> x[i:j]\n\
           \n\
           Use of negative indices is not supported."),
    SQSLOT("__setitem__", sq_ass_item, slot_sq_ass_item, wrap_sq_setitem,
           "x.__setitem__(i, y) <==> x[i]=y"),
    SQSLOT("__delitem__", sq_ass_item, slot_sq_ass_item, wrap_sq_delitem,
           "x.__delitem__(y) <==> del x[y]"),
    SQSLOT("__setslice__", sq_ass_slice, slot_sq_ass_slice,
           wrap_ssizessizeobjargproc,
           "x.__setslice__(i, j, y) <==> x[i:j]=y\n\
           \n\
           Use  of negative indices is not supported."),
    SQSLOT("__delslice__", sq_ass_slice, slot_sq_ass_slice, wrap_delslice,
           "x.__delslice__(i, j) <==> del x[i:j]\n\
           \n\
           Use of negative indices is not supported."),
    SQSLOT("__contains__", sq_contains, slot_sq_contains, wrap_objobjproc,
           "x.__contains__(y) <==> y in x"),
    SQSLOT("__iadd__", sq_inplace_concat, NULL,
      wrap_binaryfunc, "x.__iadd__(y) <==> x+=y"),
    SQSLOT("__imul__", sq_inplace_repeat, NULL,
      wrap_indexargfunc, "x.__imul__(y) <==> x*=y"),
    {NULL}
};

static void **
slotptr(PyTypeObject *type, int ioffset)
{
    char *ptr;
    long offset = ioffset;

    assert(offset >= 0);
    assert((size_t)offset < offsetof(PyHeapTypeObject, as_buffer));
    if ((size_t)offset >= offsetof(PyHeapTypeObject, as_sequence)) {
        ptr = (char *)type->tp_as_sequence;
        offset -= offsetof(PyHeapTypeObject, as_sequence);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_mapping)) {
        ptr = (char *)type->tp_as_mapping;
        offset -= offsetof(PyHeapTypeObject, as_mapping);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_number)) {
        ptr = (char *)type->tp_as_number;
        offset -= offsetof(PyHeapTypeObject, as_number);
    }
    else {
        ptr = (char *)type;
    }
    if (ptr != NULL)
        ptr += offset;
    return (void **)ptr;
}

#define MAX_EQUIV 10

static void **
resolve_slotdups(PyTypeObject *type, PyObject *name)
{

    static PyObject *pname;
    static slotdef *ptrs[MAX_EQUIV];
    slotdef *p, **pp;
    void **res, **ptr;

    if (pname != name) {

        pname = name;
        pp = ptrs;
        for (p = slotdefs; p->name_strobj; p++) {
            if (p->name_strobj == name)
                *pp++ = p;
        }
        *pp = NULL;
    }

    res = NULL;
    for (pp = ptrs; *pp; pp++) {
        ptr = slotptr(type, (*pp)->offset);
        if (ptr == NULL || *ptr == NULL)
            continue;
        if (res != NULL)
            return NULL;
        res = ptr;
    }
    return res;
}

static slotdef *
update_one_slot(PyTypeObject *type, slotdef *p)
{
    PyObject *descr;
    PyWrapperDescrObject *d;
    void *generic = NULL, *specific = NULL;
    int use_generic = 0;
    int offset = p->offset;
    void **ptr = slotptr(type, offset);

    if (ptr == NULL) {
        do {
            ++p;
        } while (p->offset == offset);
        return p;
    }
    do {
        descr = _PyType_Lookup(type, p->name_strobj);
        if (descr == NULL) {
            if (ptr == (void**)&type->tp_iternext) {
                specific = _PyObject_NextNotImplemented;
            }
            continue;
        }
        if (Py_TYPE(descr) == &PyWrapperDescr_Type &&
            ((PyWrapperDescrObject *)descr)->d_base->name_strobj == p->name_strobj) {
            void **tptr = resolve_slotdups(type, p->name_strobj);
            if (tptr == NULL || tptr == ptr)
                generic = p->function;
            d = (PyWrapperDescrObject *)descr;
            if (d->d_base->wrapper == p->wrapper &&
                PyType_IsSubtype(type, d->d_type))
            {
                if (specific == NULL ||
                    specific == d->d_wrapped)
                    specific = d->d_wrapped;
                else
                    use_generic = 1;
            }
        }
        else if (Py_TYPE(descr) == &PyCFunction_Type &&
                 PyCFunction_GET_FUNCTION(descr) ==
                 (PyCFunction)tp_new_wrapper &&
                 ptr == (void**)&type->tp_new)
        {

            specific = (void *)type->tp_new;

        }
        else if (descr == Py_None &&
                 ptr == (void**)&type->tp_hash) {

            specific = PyObject_HashNotImplemented;
        }
        else {
            use_generic = 1;
            generic = p->function;
        }
    } while ((++p)->offset == offset);
    if (specific && !use_generic)
        *ptr = specific;
    else
        *ptr = generic;
    return p;
}

static int
update_slots_callback(PyTypeObject *type, void *data)
{
    slotdef **pp = (slotdef **)data;

    for (; *pp; pp++)
        update_one_slot(type, *pp);
    return 0;
}

static void
init_slotdefs(void)
{
    slotdef *p;
    static int initialized = 0;

    if (initialized)
        return;
    for (p = slotdefs; p->name; p++) {

        assert(!p[1].name || p->offset <= p[1].offset);
        p->name_strobj = PyString_InternFromString(p->name);
        if (!p->name_strobj)
            Py_FatalError("Out of memory interning slotdef names");
    }
    initialized = 1;
}

static int
update_slot(PyTypeObject *type, PyObject *name)
{
    slotdef *ptrs[MAX_EQUIV];
    slotdef *p;
    slotdef **pp;
    int offset;

    PyType_Modified(type);

    init_slotdefs();
    pp = ptrs;
    for (p = slotdefs; p->name; p++) {

        if (p->name_strobj == name)
            *pp++ = p;
    }
    *pp = NULL;
    for (pp = ptrs; *pp; pp++) {
        p = *pp;
        offset = p->offset;
        while (p > slotdefs && (p-1)->offset == offset)
            --p;
        *pp = p;
    }
    if (ptrs[0] == NULL)
        return 0;
    return update_subclasses(type, name,
                             update_slots_callback, (void *)ptrs);
}

static void
fixup_slot_dispatchers(PyTypeObject *type)
{
    slotdef *p;

    init_slotdefs();
    for (p = slotdefs; p->name; )
        p = update_one_slot(type, p);
}

static void
update_all_slots(PyTypeObject* type)
{
    slotdef *p;

    init_slotdefs();
    for (p = slotdefs; p->name; p++) {

        update_slot(type, p->name_strobj);
    }
}

static int
update_subclasses(PyTypeObject *type, PyObject *name,
                  update_callback callback, void *data)
{
    if (callback(type, data) < 0)
        return -1;
    return recurse_down_subclasses(type, name, callback, data);
}

static int
recurse_down_subclasses(PyTypeObject *type, PyObject *name,
                        update_callback callback, void *data)
{
    PyTypeObject *subclass;
    PyObject *ref, *subclasses, *dict;
    Py_ssize_t i, n;

    subclasses = type->tp_subclasses;
    if (subclasses == NULL)
        return 0;
    assert(PyList_Check(subclasses));
    n = PyList_GET_SIZE(subclasses);
    for (i = 0; i < n; i++) {
        ref = PyList_GET_ITEM(subclasses, i);
        assert(PyWeakref_CheckRef(ref));
        subclass = (PyTypeObject *)PyWeakref_GET_OBJECT(ref);
        assert(subclass != NULL);
        if ((PyObject *)subclass == Py_None)
            continue;
        assert(PyType_Check(subclass));

        dict = subclass->tp_dict;
        if (dict != NULL && PyDict_Check(dict) &&
            PyDict_GetItem(dict, name) != NULL)
            continue;
        if (update_subclasses(subclass, name, callback, data) < 0)
            return -1;
    }
    return 0;
}

static int
add_operators(PyTypeObject *type)
{
    PyObject *dict = type->tp_dict;
    slotdef *p;
    PyObject *descr;
    void **ptr;

    init_slotdefs();
    for (p = slotdefs; p->name; p++) {
        if (p->wrapper == NULL)
            continue;
        ptr = slotptr(type, p->offset);
        if (!ptr || !*ptr)
            continue;
        if (PyDict_GetItem(dict, p->name_strobj))
            continue;
        if (*ptr == PyObject_HashNotImplemented) {

            if (PyDict_SetItem(dict, p->name_strobj, Py_None) < 0)
                return -1;
        }
        else {
            descr = PyDescr_NewWrapper(type, p, *ptr);
            if (descr == NULL)
                return -1;
            if (PyDict_SetItem(dict, p->name_strobj, descr) < 0) {
                Py_DECREF(descr);
                return -1;
            }
            Py_DECREF(descr);
        }
    }
    if (type->tp_new != NULL) {
        if (add_tp_new_wrapper(type) < 0)
            return -1;
    }
    return 0;
}

typedef struct {
    PyObject_HEAD
    PyTypeObject *type;
    PyObject *obj;
    PyTypeObject *obj_type;
} superobject;

static PyMemberDef super_members[] = {
    {"__thisclass__", T_OBJECT, offsetof(superobject, type), READONLY,
     "the class invoking super()"},
    {"__self__",  T_OBJECT, offsetof(superobject, obj), READONLY,
     "the instance invoking super(); may be None"},
    {"__self_class__", T_OBJECT, offsetof(superobject, obj_type), READONLY,
     "the type of the instance invoking super(); may be None"},
    {0}
};

static void
super_dealloc(PyObject *self)
{
    superobject *su = (superobject *)self;

    _PyObject_GC_UNTRACK(self);
    Py_XDECREF(su->obj);
    Py_XDECREF(su->type);
    Py_XDECREF(su->obj_type);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *
super_repr(PyObject *self)
{
    superobject *su = (superobject *)self;

    if (su->obj_type)
        return PyString_FromFormat(
            "<super: <class '%s'>, <%s object>>",
            su->type ? su->type->tp_name : "NULL",
            su->obj_type->tp_name);
    else
        return PyString_FromFormat(
            "<super: <class '%s'>, NULL>",
            su->type ? su->type->tp_name : "NULL");
}

static PyObject *
super_getattro(PyObject *self, PyObject *name)
{
    superobject *su = (superobject *)self;
    int skip = su->obj_type == NULL;

    if (!skip) {

        skip = (PyString_Check(name) &&
            PyString_GET_SIZE(name) == 9 &&
            strcmp(PyString_AS_STRING(name), "__class__") == 0);
    }

    if (!skip) {
        PyObject *mro, *res, *tmp, *dict;
        PyTypeObject *starttype;
        descrgetfunc f;
        Py_ssize_t i, n;

        starttype = su->obj_type;
        mro = starttype->tp_mro;

        if (mro == NULL)
            n = 0;
        else {
            assert(PyTuple_Check(mro));
            n = PyTuple_GET_SIZE(mro);
        }
        for (i = 0; i < n; i++) {
            if ((PyObject *)(su->type) == PyTuple_GET_ITEM(mro, i))
                break;
        }
        i++;
        res = NULL;
        for (; i < n; i++) {
            tmp = PyTuple_GET_ITEM(mro, i);
            if (PyType_Check(tmp))
                dict = ((PyTypeObject *)tmp)->tp_dict;
            else if (PyClass_Check(tmp))
                dict = ((PyClassObject *)tmp)->cl_dict;
            else
                continue;
            res = PyDict_GetItem(dict, name);
            if (res != NULL) {
                Py_INCREF(res);
                f = Py_TYPE(res)->tp_descr_get;
                if (f != NULL) {
                    tmp = f(res,

                        (su->obj == (PyObject *)
                                    su->obj_type
                            ? (PyObject *)NULL
                            : su->obj),
                        (PyObject *)starttype);
                    Py_DECREF(res);
                    res = tmp;
                }
                return res;
            }
        }
    }
    return PyObject_GenericGetAttr(self, name);
}

static PyTypeObject *
supercheck(PyTypeObject *type, PyObject *obj)
{

    if (PyType_Check(obj) && PyType_IsSubtype((PyTypeObject *)obj, type)) {
        Py_INCREF(obj);
        return (PyTypeObject *)obj;
    }

    if (PyType_IsSubtype(Py_TYPE(obj), type)) {
        Py_INCREF(Py_TYPE(obj));
        return Py_TYPE(obj);
    }
    else {

        static PyObject *class_str = NULL;
        PyObject *class_attr;

        if (class_str == NULL) {
            class_str = PyString_FromString("__class__");
            if (class_str == NULL)
                return NULL;
        }

        class_attr = PyObject_GetAttr(obj, class_str);

        if (class_attr != NULL &&
            PyType_Check(class_attr) &&
            (PyTypeObject *)class_attr != Py_TYPE(obj))
        {
            int ok = PyType_IsSubtype(
                (PyTypeObject *)class_attr, type);
            if (ok)
                return (PyTypeObject *)class_attr;
        }

        if (class_attr == NULL)
            PyErr_Clear();
        else
            Py_DECREF(class_attr);
    }

    PyErr_SetString(PyExc_TypeError,
                    "super(type, obj): "
                    "obj must be an instance or subtype of type");
    return NULL;
}

static PyObject *
super_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    superobject *su = (superobject *)self;
    superobject *newobj;

    if (obj == NULL || obj == Py_None || su->obj != NULL) {

        Py_INCREF(self);
        return self;
    }
    if (Py_TYPE(su) != &PySuper_Type)

        return PyObject_CallFunctionObjArgs((PyObject *)Py_TYPE(su),
                                            su->type, obj, NULL);
    else {

        PyTypeObject *obj_type = supercheck(su->type, obj);
        if (obj_type == NULL)
            return NULL;
        newobj = (superobject *)PySuper_Type.tp_new(&PySuper_Type,
                                                 NULL, NULL);
        if (newobj == NULL)
            return NULL;
        Py_INCREF(su->type);
        Py_INCREF(obj);
        newobj->type = su->type;
        newobj->obj = obj;
        newobj->obj_type = obj_type;
        return (PyObject *)newobj;
    }
}

static int
super_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    superobject *su = (superobject *)self;
    PyTypeObject *type;
    PyObject *obj = NULL;
    PyTypeObject *obj_type = NULL;

    if (!_PyArg_NoKeywords("super", kwds))
        return -1;
    if (!PyArg_ParseTuple(args, "O!|O:super", &PyType_Type, &type, &obj))
        return -1;
    if (obj == Py_None)
        obj = NULL;
    if (obj != NULL) {
        obj_type = supercheck(type, obj);
        if (obj_type == NULL)
            return -1;
        Py_INCREF(obj);
    }
    Py_INCREF(type);
    Py_XSETREF(su->type, type);
    Py_XSETREF(su->obj, obj);
    Py_XSETREF(su->obj_type, obj_type);
    return 0;
}

PyDoc_STRVAR(super_doc,
"super(type, obj) -> bound super object; requires isinstance(obj, type)\n"
"super(type) -> unbound super object\n"
"super(type, type2) -> bound super object; requires issubclass(type2, type)\n"
"Typical use to call a cooperative superclass method:\n"
"class C(B):\n"
"    def meth(self, arg):\n"
"        super(C, self).meth(arg)");

static int
super_traverse(PyObject *self, visitproc visit, void *arg)
{
    superobject *su = (superobject *)self;

    Py_VISIT(su->obj);
    Py_VISIT(su->type);
    Py_VISIT(su->obj_type);

    return 0;
}

PyTypeObject PySuper_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "super",
    sizeof(superobject),
    0,

    super_dealloc,
    0,
    0,
    0,
    0,
    super_repr,
    0,
    0,
    0,
    0,
    0,
    0,
    super_getattro,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    super_doc,
    super_traverse,
    0,
    0,
    0,
    0,
    0,
    0,
    super_members,
    0,
    0,
    0,
    super_descr_get,
    0,
    0,
    super_init,
    PyType_GenericAlloc,
    PyType_GenericNew,
    PyObject_GC_Del,
};
