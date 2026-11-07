#include "Python.h"
#include "structmember.h"

typedef struct {
    PyObject_HEAD
    PyObject *it;
    PyObject *keyfunc;
    PyObject *tgtkey;
    PyObject *currkey;
    PyObject *currvalue;
} groupbyobject;

static PyTypeObject groupby_type;
static PyObject *_grouper_create(groupbyobject *, PyObject *);

static PyObject *
groupby_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwargs[] = {"iterable", "key", NULL};
    groupbyobject *gbo;
    PyObject *it, *keyfunc = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O:groupby", kwargs,
                                     &it, &keyfunc))
        return NULL;

    gbo = (groupbyobject *)type->tp_alloc(type, 0);
    if (gbo == NULL)
        return NULL;
    gbo->tgtkey = NULL;
    gbo->currkey = NULL;
    gbo->currvalue = NULL;
    gbo->keyfunc = keyfunc;
    Py_INCREF(keyfunc);
    gbo->it = PyObject_GetIter(it);
    if (gbo->it == NULL) {
        Py_DECREF(gbo);
        return NULL;
    }
    return (PyObject *)gbo;
}

static void
groupby_dealloc(groupbyobject *gbo)
{
    PyObject_GC_UnTrack(gbo);
    Py_XDECREF(gbo->it);
    Py_XDECREF(gbo->keyfunc);
    Py_XDECREF(gbo->tgtkey);
    Py_XDECREF(gbo->currkey);
    Py_XDECREF(gbo->currvalue);
    Py_TYPE(gbo)->tp_free(gbo);
}

static int
groupby_traverse(groupbyobject *gbo, visitproc visit, void *arg)
{
    Py_VISIT(gbo->it);
    Py_VISIT(gbo->keyfunc);
    Py_VISIT(gbo->tgtkey);
    Py_VISIT(gbo->currkey);
    Py_VISIT(gbo->currvalue);
    return 0;
}

static PyObject *
groupby_next(groupbyobject *gbo)
{
    PyObject *newvalue, *newkey, *r, *grouper, *tmp;

    for (;;) {
        if (gbo->currkey == NULL)
            ;
        else if (gbo->tgtkey == NULL)
            break;
        else {
            int rcmp;

            rcmp = PyObject_RichCompareBool(gbo->tgtkey,
                                            gbo->currkey, Py_EQ);
            if (rcmp == -1)
                return NULL;
            else if (rcmp == 0)
                break;
        }

        newvalue = PyIter_Next(gbo->it);
        if (newvalue == NULL)
            return NULL;

        if (gbo->keyfunc == Py_None) {
            newkey = newvalue;
            Py_INCREF(newvalue);
        } else {
            newkey = PyObject_CallFunctionObjArgs(gbo->keyfunc,
                                                  newvalue, NULL);
            if (newkey == NULL) {
                Py_DECREF(newvalue);
                return NULL;
            }
        }

        tmp = gbo->currkey;
        gbo->currkey = newkey;
        Py_XDECREF(tmp);

        tmp = gbo->currvalue;
        gbo->currvalue = newvalue;
        Py_XDECREF(tmp);
    }

    Py_INCREF(gbo->currkey);
    tmp = gbo->tgtkey;
    gbo->tgtkey = gbo->currkey;
    Py_XDECREF(tmp);

    grouper = _grouper_create(gbo, gbo->tgtkey);
    if (grouper == NULL)
        return NULL;

    r = PyTuple_Pack(2, gbo->currkey, grouper);
    Py_DECREF(grouper);
    return r;
}

PyDoc_STRVAR(groupby_doc,
"groupby(iterable[, keyfunc]) -> create an iterator which returns\n\
(key, sub-iterator) grouped by each value of key(value).\n");

static PyTypeObject groupby_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.groupby",
    sizeof(groupbyobject),
    0,

    (destructor)groupby_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    groupby_doc,
    (traverseproc)groupby_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)groupby_next,
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
    groupby_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *parent;
    PyObject *tgtkey;
} _grouperobject;

static PyTypeObject _grouper_type;

static PyObject *
_grouper_create(groupbyobject *parent, PyObject *tgtkey)
{
    _grouperobject *igo;

    igo = PyObject_GC_New(_grouperobject, &_grouper_type);
    if (igo == NULL)
        return NULL;
    igo->parent = (PyObject *)parent;
    Py_INCREF(parent);
    igo->tgtkey = tgtkey;
    Py_INCREF(tgtkey);

    PyObject_GC_Track(igo);
    return (PyObject *)igo;
}

static void
_grouper_dealloc(_grouperobject *igo)
{
    PyObject_GC_UnTrack(igo);
    Py_DECREF(igo->parent);
    Py_DECREF(igo->tgtkey);
    PyObject_GC_Del(igo);
}

static int
_grouper_traverse(_grouperobject *igo, visitproc visit, void *arg)
{
    Py_VISIT(igo->parent);
    Py_VISIT(igo->tgtkey);
    return 0;
}

static PyObject *
_grouper_next(_grouperobject *igo)
{
    groupbyobject *gbo = (groupbyobject *)igo->parent;
    PyObject *newvalue, *newkey, *r;
    int rcmp;

    if (gbo->currvalue == NULL) {
        newvalue = PyIter_Next(gbo->it);
        if (newvalue == NULL)
            return NULL;

        if (gbo->keyfunc == Py_None) {
            newkey = newvalue;
            Py_INCREF(newvalue);
        } else {
            newkey = PyObject_CallFunctionObjArgs(gbo->keyfunc,
                                                  newvalue, NULL);
            if (newkey == NULL) {
                Py_DECREF(newvalue);
                return NULL;
            }
        }

        assert(gbo->currkey == NULL);
        gbo->currkey = newkey;
        gbo->currvalue = newvalue;
    }

    assert(gbo->currkey != NULL);
    rcmp = PyObject_RichCompareBool(igo->tgtkey, gbo->currkey, Py_EQ);
    if (rcmp <= 0)

        return NULL;

    r = gbo->currvalue;
    gbo->currvalue = NULL;
    Py_CLEAR(gbo->currkey);

    return r;
}

static PyTypeObject _grouper_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools._grouper",
    sizeof(_grouperobject),
    0,

    (destructor)_grouper_dealloc,
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
    (traverseproc)_grouper_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)_grouper_next,
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
    PyObject_GC_Del,
};

#define LINKCELLS 57

typedef struct {
    PyObject_HEAD
    PyObject *it;
    int numread;
    PyObject *nextlink;
    PyObject *(values[LINKCELLS]);
} teedataobject;

typedef struct {
    PyObject_HEAD
    teedataobject *dataobj;
    int index;
    PyObject *weakreflist;
} teeobject;

static PyTypeObject teedataobject_type;

static PyObject *
teedataobject_new(PyObject *it)
{
    teedataobject *tdo;

    tdo = PyObject_GC_New(teedataobject, &teedataobject_type);
    if (tdo == NULL)
        return NULL;

    tdo->numread = 0;
    tdo->nextlink = NULL;
    Py_INCREF(it);
    tdo->it = it;
    PyObject_GC_Track(tdo);
    return (PyObject *)tdo;
}

static PyObject *
teedataobject_jumplink(teedataobject *tdo)
{
    if (tdo->nextlink == NULL)
        tdo->nextlink = teedataobject_new(tdo->it);
    Py_XINCREF(tdo->nextlink);
    return tdo->nextlink;
}

static PyObject *
teedataobject_getitem(teedataobject *tdo, int i)
{
    PyObject *value;

    assert(i < LINKCELLS);
    if (i < tdo->numread)
        value = tdo->values[i];
    else {

        assert(i == tdo->numread);
        value = PyIter_Next(tdo->it);
        if (value == NULL)
            return NULL;
        tdo->numread++;
        tdo->values[i] = value;
    }
    Py_INCREF(value);
    return value;
}

static int
teedataobject_traverse(teedataobject *tdo, visitproc visit, void * arg)
{
    int i;
    Py_VISIT(tdo->it);
    for (i = 0; i < tdo->numread; i++)
        Py_VISIT(tdo->values[i]);
    Py_VISIT(tdo->nextlink);
    return 0;
}

static void
teedataobject_safe_decref(PyObject *obj)
{
    while (obj && Py_TYPE(obj) == &teedataobject_type &&
           Py_REFCNT(obj) == 1) {
        PyObject *nextlink = ((teedataobject *)obj)->nextlink;
        ((teedataobject *)obj)->nextlink = NULL;
        Py_DECREF(obj);
        obj = nextlink;
    }
    Py_XDECREF(obj);
}

static int
teedataobject_clear(teedataobject *tdo)
{
    int i;
    PyObject *tmp;

    Py_CLEAR(tdo->it);
    for (i=0 ; i<tdo->numread ; i++)
        Py_CLEAR(tdo->values[i]);
    tmp = tdo->nextlink;
    tdo->nextlink = NULL;
    teedataobject_safe_decref(tmp);
    return 0;
}

static void
teedataobject_dealloc(teedataobject *tdo)
{
    PyObject_GC_UnTrack(tdo);
    teedataobject_clear(tdo);
    PyObject_GC_Del(tdo);
}

PyDoc_STRVAR(teedataobject_doc, "Data container common to multiple tee objects.");

static PyTypeObject teedataobject_type = {
    PyVarObject_HEAD_INIT(0, 0)
    "itertools.tee_dataobject",
    sizeof(teedataobject),
    0,

    (destructor)teedataobject_dealloc,
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
    teedataobject_doc,
    (traverseproc)teedataobject_traverse,
    (inquiry)teedataobject_clear,
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
    PyObject_GC_Del,
};

static PyTypeObject tee_type;

static PyObject *
tee_next(teeobject *to)
{
    PyObject *value, *link;

    if (to->index >= LINKCELLS) {
        link = teedataobject_jumplink(to->dataobj);
        if (link == NULL)
            return NULL;
        Py_DECREF(to->dataobj);
        to->dataobj = (teedataobject *)link;
        to->index = 0;
    }
    value = teedataobject_getitem(to->dataobj, to->index);
    if (value == NULL)
        return NULL;
    to->index++;
    return value;
}

static int
tee_traverse(teeobject *to, visitproc visit, void *arg)
{
    Py_VISIT((PyObject *)to->dataobj);
    return 0;
}

static PyObject *
tee_copy(teeobject *to)
{
    teeobject *newto;

    newto = PyObject_GC_New(teeobject, &tee_type);
    if (newto == NULL)
        return NULL;
    Py_INCREF(to->dataobj);
    newto->dataobj = to->dataobj;
    newto->index = to->index;
    newto->weakreflist = NULL;
    PyObject_GC_Track(newto);
    return (PyObject *)newto;
}

PyDoc_STRVAR(teecopy_doc, "Returns an independent iterator.");

static PyObject *
tee_fromiterable(PyObject *iterable)
{
    teeobject *to;
    PyObject *it = NULL;

    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;
    if (PyObject_TypeCheck(it, &tee_type)) {
        to = (teeobject *)tee_copy((teeobject *)it);
        goto done;
    }

    to = PyObject_GC_New(teeobject, &tee_type);
    if (to == NULL)
        goto done;
    to->dataobj = (teedataobject *)teedataobject_new(it);
    if (!to->dataobj) {
        PyObject_GC_Del(to);
        to = NULL;
        goto done;
    }

    to->index = 0;
    to->weakreflist = NULL;
    PyObject_GC_Track(to);
done:
    Py_XDECREF(it);
    return (PyObject *)to;
}

static PyObject *
tee_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    PyObject *iterable;

    if (!PyArg_UnpackTuple(args, "tee", 1, 1, &iterable))
        return NULL;
    return tee_fromiterable(iterable);
}

static int
tee_clear(teeobject *to)
{
    if (to->weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) to);
    Py_CLEAR(to->dataobj);
    return 0;
}

static void
tee_dealloc(teeobject *to)
{
    PyObject_GC_UnTrack(to);
    tee_clear(to);
    PyObject_GC_Del(to);
}

PyDoc_STRVAR(teeobject_doc,
"Iterator wrapped to make it copyable");

static PyMethodDef tee_methods[] = {
    {"__copy__",        (PyCFunction)tee_copy,  METH_NOARGS, teecopy_doc},
    {NULL,              NULL}
};

static PyTypeObject tee_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.tee",
    sizeof(teeobject),
    0,

    (destructor)tee_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    teeobject_doc,
    (traverseproc)tee_traverse,
    (inquiry)tee_clear,
    0,
    offsetof(teeobject, weakreflist),
    PyObject_SelfIter,
    (iternextfunc)tee_next,
    tee_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    tee_new,
    PyObject_GC_Del,
};

static PyObject *
tee(PyObject *self, PyObject *args)
{
    Py_ssize_t i, n=2;
    PyObject *it, *iterable, *copyable, *result;

    if (!PyArg_ParseTuple(args, "O|n", &iterable, &n))
        return NULL;
    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "n must be >= 0");
        return NULL;
    }
    result = PyTuple_New(n);
    if (result == NULL)
        return NULL;
    if (n == 0)
        return result;
    it = PyObject_GetIter(iterable);
    if (it == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    if (!PyObject_HasAttrString(it, "__copy__")) {
        copyable = tee_fromiterable(it);
        Py_DECREF(it);
        if (copyable == NULL) {
            Py_DECREF(result);
            return NULL;
        }
    } else
        copyable = it;
    PyTuple_SET_ITEM(result, 0, copyable);
    for (i=1 ; i<n ; i++) {
        copyable = PyObject_CallMethod(copyable, "__copy__", NULL);
        if (copyable == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        PyTuple_SET_ITEM(result, i, copyable);
    }
    return result;
}

PyDoc_STRVAR(tee_doc,
"tee(iterable, n=2) --> tuple of n independent iterators.");

typedef struct {
    PyObject_HEAD
    PyObject *it;
    PyObject *saved;
    int firstpass;
} cycleobject;

static PyTypeObject cycle_type;

static PyObject *
cycle_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *it;
    PyObject *iterable;
    PyObject *saved;
    cycleobject *lz;

    if (type == &cycle_type && !_PyArg_NoKeywords("cycle()", kwds))
        return NULL;

    if (!PyArg_UnpackTuple(args, "cycle", 1, 1, &iterable))
        return NULL;

    it = PyObject_GetIter(iterable);
    if (it == NULL)
        return NULL;

    saved = PyList_New(0);
    if (saved == NULL) {
        Py_DECREF(it);
        return NULL;
    }

    lz = (cycleobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        Py_DECREF(saved);
        return NULL;
    }
    lz->it = it;
    lz->saved = saved;
    lz->firstpass = 0;

    return (PyObject *)lz;
}

static void
cycle_dealloc(cycleobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->saved);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
cycle_traverse(cycleobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->saved);
    return 0;
}

static PyObject *
cycle_next(cycleobject *lz)
{
    PyObject *item;
    PyObject *it;
    PyObject *tmp;

    while (1) {
        item = PyIter_Next(lz->it);
        if (item != NULL) {
            if (!lz->firstpass && PyList_Append(lz->saved, item)) {
                Py_DECREF(item);
                return NULL;
            }
            return item;
        }
        if (PyErr_Occurred()) {
            if (PyErr_ExceptionMatches(PyExc_StopIteration))
                PyErr_Clear();
            else
                return NULL;
        }
        if (PyList_Size(lz->saved) == 0)
            return NULL;
        it = PyObject_GetIter(lz->saved);
        if (it == NULL)
            return NULL;
        tmp = lz->it;
        lz->it = it;
        lz->firstpass = 1;
        Py_DECREF(tmp);
    }
}

PyDoc_STRVAR(cycle_doc,
"cycle(iterable) --> cycle object\n\
\n\
Return elements from the iterable until it is exhausted.\n\
Then repeat the sequence indefinitely.");

static PyTypeObject cycle_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.cycle",
    sizeof(cycleobject),
    0,

    (destructor)cycle_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    cycle_doc,
    (traverseproc)cycle_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)cycle_next,
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
    cycle_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
    long         start;
} dropwhileobject;

static PyTypeObject dropwhile_type;

static PyObject *
dropwhile_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *func, *seq;
    PyObject *it;
    dropwhileobject *lz;

    if (type == &dropwhile_type && !_PyArg_NoKeywords("dropwhile()", kwds))
        return NULL;

    if (!PyArg_UnpackTuple(args, "dropwhile", 2, 2, &func, &seq))
        return NULL;

    it = PyObject_GetIter(seq);
    if (it == NULL)
        return NULL;

    lz = (dropwhileobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(func);
    lz->func = func;
    lz->it = it;
    lz->start = 0;

    return (PyObject *)lz;
}

static void
dropwhile_dealloc(dropwhileobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
dropwhile_traverse(dropwhileobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
dropwhile_next(dropwhileobject *lz)
{
    PyObject *item, *good;
    PyObject *it = lz->it;
    long ok;
    PyObject *(*iternext)(PyObject *);

    iternext = *Py_TYPE(it)->tp_iternext;
    for (;;) {
        item = iternext(it);
        if (item == NULL)
            return NULL;
        if (lz->start == 1)
            return item;

        good = PyObject_CallFunctionObjArgs(lz->func, item, NULL);
        if (good == NULL) {
            Py_DECREF(item);
            return NULL;
        }
        ok = PyObject_IsTrue(good);
        Py_DECREF(good);
        if (ok == 0) {
            lz->start = 1;
            return item;
        }
        Py_DECREF(item);
        if (ok < 0)
            return NULL;
    }
}

PyDoc_STRVAR(dropwhile_doc,
"dropwhile(predicate, iterable) --> dropwhile object\n\
\n\
Drop items from the iterable while predicate(item) is true.\n\
Afterwards, return every element until the iterable is exhausted.");

static PyTypeObject dropwhile_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.dropwhile",
    sizeof(dropwhileobject),
    0,

    (destructor)dropwhile_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    dropwhile_doc,
    (traverseproc)dropwhile_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)dropwhile_next,
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
    dropwhile_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
    long         stop;
} takewhileobject;

static PyTypeObject takewhile_type;

static PyObject *
takewhile_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *func, *seq;
    PyObject *it;
    takewhileobject *lz;

    if (type == &takewhile_type && !_PyArg_NoKeywords("takewhile()", kwds))
        return NULL;

    if (!PyArg_UnpackTuple(args, "takewhile", 2, 2, &func, &seq))
        return NULL;

    it = PyObject_GetIter(seq);
    if (it == NULL)
        return NULL;

    lz = (takewhileobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(func);
    lz->func = func;
    lz->it = it;
    lz->stop = 0;

    return (PyObject *)lz;
}

static void
takewhile_dealloc(takewhileobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
takewhile_traverse(takewhileobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
takewhile_next(takewhileobject *lz)
{
    PyObject *item, *good;
    PyObject *it = lz->it;
    long ok;

    if (lz->stop == 1)
        return NULL;

    item = (*Py_TYPE(it)->tp_iternext)(it);
    if (item == NULL)
        return NULL;

    good = PyObject_CallFunctionObjArgs(lz->func, item, NULL);
    if (good == NULL) {
        Py_DECREF(item);
        return NULL;
    }
    ok = PyObject_IsTrue(good);
    Py_DECREF(good);
    if (ok > 0)
        return item;
    Py_DECREF(item);
    if (ok == 0)
        lz->stop = 1;
    return NULL;
}

PyDoc_STRVAR(takewhile_doc,
"takewhile(predicate, iterable) --> takewhile object\n\
\n\
Return successive entries from an iterable as long as the \n\
predicate evaluates to true for each entry.");

static PyTypeObject takewhile_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.takewhile",
    sizeof(takewhileobject),
    0,

    (destructor)takewhile_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    takewhile_doc,
    (traverseproc)takewhile_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)takewhile_next,
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
    takewhile_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *it;
    Py_ssize_t next;
    Py_ssize_t stop;
    Py_ssize_t step;
    Py_ssize_t cnt;
} isliceobject;

static PyTypeObject islice_type;

static PyObject *
islice_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *seq;
    Py_ssize_t start=0, stop=-1, step=1;
    PyObject *it, *a1=NULL, *a2=NULL, *a3=NULL;
    Py_ssize_t numargs;
    isliceobject *lz;

    if (type == &islice_type && !_PyArg_NoKeywords("islice()", kwds))
        return NULL;

    if (!PyArg_UnpackTuple(args, "islice", 2, 4, &seq, &a1, &a2, &a3))
        return NULL;

    numargs = PyTuple_Size(args);
    if (numargs == 2) {
        if (a1 != Py_None) {
            stop = PyInt_AsSsize_t(a1);
            if (stop == -1) {
                if (PyErr_Occurred())
                    PyErr_Clear();
                PyErr_SetString(PyExc_ValueError,
                    "Stop argument for islice() must be None or an integer: 0 <= x <= maxint.");
                return NULL;
            }
        }
    } else {
        if (a1 != Py_None)
            start = PyInt_AsSsize_t(a1);
        if (start == -1 && PyErr_Occurred())
            PyErr_Clear();
        if (a2 != Py_None) {
            stop = PyInt_AsSsize_t(a2);
            if (stop == -1) {
                if (PyErr_Occurred())
                    PyErr_Clear();
                PyErr_SetString(PyExc_ValueError,
                   "Stop argument for islice() must be None or an integer: 0 <= x <= maxint.");
                return NULL;
            }
        }
    }
    if (start<0 || stop<-1) {
        PyErr_SetString(PyExc_ValueError,
           "Indices for islice() must be None or an integer: 0 <= x <= maxint.");
        return NULL;
    }

    if (a3 != NULL) {
        if (a3 != Py_None)
            step = PyInt_AsSsize_t(a3);
        if (step == -1 && PyErr_Occurred())
            PyErr_Clear();
    }
    if (step<1) {
        PyErr_SetString(PyExc_ValueError,
           "Step for islice() must be a positive integer or None.");
        return NULL;
    }

    it = PyObject_GetIter(seq);
    if (it == NULL)
        return NULL;

    lz = (isliceobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    lz->it = it;
    lz->next = start;
    lz->stop = stop;
    lz->step = step;
    lz->cnt = 0L;

    return (PyObject *)lz;
}

static void
islice_dealloc(isliceobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
islice_traverse(isliceobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    return 0;
}

static PyObject *
islice_next(isliceobject *lz)
{
    PyObject *item;
    PyObject *it = lz->it;
    Py_ssize_t stop = lz->stop;
    Py_ssize_t oldnext;
    PyObject *(*iternext)(PyObject *);

    iternext = *Py_TYPE(it)->tp_iternext;
    while (lz->cnt < lz->next) {
        item = iternext(it);
        if (item == NULL)
            return NULL;
        Py_DECREF(item);
        lz->cnt++;
    }
    if (stop != -1 && lz->cnt >= stop)
        return NULL;
    item = iternext(it);
    if (item == NULL)
        return NULL;
    lz->cnt++;
    oldnext = lz->next;

    lz->next += (size_t)lz->step;
    if (lz->next < oldnext || (stop != -1 && lz->next > stop))
        lz->next = stop;
    return item;
}

PyDoc_STRVAR(islice_doc,
"islice(iterable, [start,] stop [, step]) --> islice object\n\
\n\
Return an iterator whose next() method returns selected values from an\n\
iterable.  If start is specified, will skip all preceding elements;\n\
otherwise, start defaults to zero.  Step defaults to one.  If\n\
specified as another value, step determines how many values are \n\
skipped between successive calls.  Works like a slice() on a list\n\
but returns an iterator.");

static PyTypeObject islice_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.islice",
    sizeof(isliceobject),
    0,

    (destructor)islice_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    islice_doc,
    (traverseproc)islice_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)islice_next,
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
    islice_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
} starmapobject;

static PyTypeObject starmap_type;

static PyObject *
starmap_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *func, *seq;
    PyObject *it;
    starmapobject *lz;

    if (type == &starmap_type && !_PyArg_NoKeywords("starmap()", kwds))
        return NULL;

    if (!PyArg_UnpackTuple(args, "starmap", 2, 2, &func, &seq))
        return NULL;

    it = PyObject_GetIter(seq);
    if (it == NULL)
        return NULL;

    lz = (starmapobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(func);
    lz->func = func;
    lz->it = it;

    return (PyObject *)lz;
}

static void
starmap_dealloc(starmapobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
starmap_traverse(starmapobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
starmap_next(starmapobject *lz)
{
    PyObject *args;
    PyObject *result;
    PyObject *it = lz->it;

    args = (*Py_TYPE(it)->tp_iternext)(it);
    if (args == NULL)
        return NULL;
    if (!PyTuple_CheckExact(args)) {
        PyObject *newargs = PySequence_Tuple(args);
        Py_DECREF(args);
        if (newargs == NULL)
            return NULL;
        args = newargs;
    }
    result = PyObject_Call(lz->func, args, NULL);
    Py_DECREF(args);
    return result;
}

PyDoc_STRVAR(starmap_doc,
"starmap(function, sequence) --> starmap object\n\
\n\
Return an iterator whose values are returned from the function evaluated\n\
with a argument tuple taken from the given sequence.");

static PyTypeObject starmap_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.starmap",
    sizeof(starmapobject),
    0,

    (destructor)starmap_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    starmap_doc,
    (traverseproc)starmap_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)starmap_next,
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
    starmap_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *iters;
    PyObject *func;
} imapobject;

static PyTypeObject imap_type;

static PyObject *
imap_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *it, *iters, *func;
    imapobject *lz;
    Py_ssize_t numargs, i;

    if (type == &imap_type && !_PyArg_NoKeywords("imap()", kwds))
        return NULL;

    numargs = PyTuple_Size(args);
    if (numargs < 2) {
        PyErr_SetString(PyExc_TypeError,
           "imap() must have at least two arguments.");
        return NULL;
    }

    iters = PyTuple_New(numargs-1);
    if (iters == NULL)
        return NULL;

    for (i=1 ; i<numargs ; i++) {

        it = PyObject_GetIter(PyTuple_GET_ITEM(args, i));
        if (it == NULL) {
            Py_DECREF(iters);
            return NULL;
        }
        PyTuple_SET_ITEM(iters, i-1, it);
    }

    lz = (imapobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(iters);
        return NULL;
    }
    lz->iters = iters;
    func = PyTuple_GET_ITEM(args, 0);
    Py_INCREF(func);
    lz->func = func;

    return (PyObject *)lz;
}

static void
imap_dealloc(imapobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->iters);
    Py_XDECREF(lz->func);
    Py_TYPE(lz)->tp_free(lz);
}

static int
imap_traverse(imapobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->iters);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
imap_next(imapobject *lz)
{
    PyObject *val;
    PyObject *argtuple;
    PyObject *result;
    Py_ssize_t numargs, i;

    numargs = PyTuple_Size(lz->iters);
    argtuple = PyTuple_New(numargs);
    if (argtuple == NULL)
        return NULL;

    for (i=0 ; i<numargs ; i++) {
        val = PyIter_Next(PyTuple_GET_ITEM(lz->iters, i));
        if (val == NULL) {
            Py_DECREF(argtuple);
            return NULL;
        }
        PyTuple_SET_ITEM(argtuple, i, val);
    }
    if (lz->func == Py_None)
        return argtuple;
    result = PyObject_Call(lz->func, argtuple, NULL);
    Py_DECREF(argtuple);
    return result;
}

PyDoc_STRVAR(imap_doc,
"imap(func, *iterables) --> imap object\n\
\n\
Make an iterator that computes the function using arguments from\n\
each of the iterables.  Like map() except that it returns\n\
an iterator instead of a list and that it stops when the shortest\n\
iterable is exhausted instead of filling in None for shorter\n\
iterables.");

static PyTypeObject imap_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.imap",
    sizeof(imapobject),
    0,

    (destructor)imap_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    imap_doc,
    (traverseproc)imap_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)imap_next,
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
    imap_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *source;
    PyObject *active;
} chainobject;

static PyTypeObject chain_type;

static PyObject *
chain_new_internal(PyTypeObject *type, PyObject *source)
{
    chainobject *lz;

    lz = (chainobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(source);
        return NULL;
    }

    lz->source = source;
    lz->active = NULL;
    return (PyObject *)lz;
}

static PyObject *
chain_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *source;

    if (type == &chain_type && !_PyArg_NoKeywords("chain()", kwds))
        return NULL;

    source = PyObject_GetIter(args);
    if (source == NULL)
        return NULL;

    return chain_new_internal(type, source);
}

static PyObject *
chain_new_from_iterable(PyTypeObject *type, PyObject *arg)
{
    PyObject *source;

    source = PyObject_GetIter(arg);
    if (source == NULL)
        return NULL;

    return chain_new_internal(type, source);
}

static void
chain_dealloc(chainobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->active);
    Py_XDECREF(lz->source);
    Py_TYPE(lz)->tp_free(lz);
}

static int
chain_traverse(chainobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->source);
    Py_VISIT(lz->active);
    return 0;
}

static PyObject *
chain_next(chainobject *lz)
{
    PyObject *item;

    if (lz->source == NULL)
        return NULL;

    if (lz->active == NULL) {
        PyObject *iterable = PyIter_Next(lz->source);
        if (iterable == NULL) {
            Py_CLEAR(lz->source);
            return NULL;
        }
        lz->active = PyObject_GetIter(iterable);
        Py_DECREF(iterable);
        if (lz->active == NULL) {
            Py_CLEAR(lz->source);
            return NULL;
        }
    }
    item = PyIter_Next(lz->active);
    if (item != NULL)
        return item;
    if (PyErr_Occurred()) {
        if (PyErr_ExceptionMatches(PyExc_StopIteration))
            PyErr_Clear();
        else
            return NULL;
    }
    Py_CLEAR(lz->active);
    return chain_next(lz);
}

PyDoc_STRVAR(chain_doc,
"chain(*iterables) --> chain object\n\
\n\
Return a chain object whose .next() method returns elements from the\n\
first iterable until it is exhausted, then elements from the next\n\
iterable, until all of the iterables are exhausted.");

PyDoc_STRVAR(chain_from_iterable_doc,
"chain.from_iterable(iterable) --> chain object\n\
\n\
Alternate chain() contructor taking a single iterable argument\n\
that evaluates lazily.");

static PyMethodDef chain_methods[] = {
    {"from_iterable", (PyCFunction) chain_new_from_iterable,            METH_O | METH_CLASS,
        chain_from_iterable_doc},
    {NULL,              NULL}
};

static PyTypeObject chain_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.chain",
    sizeof(chainobject),
    0,

    (destructor)chain_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    chain_doc,
    (traverseproc)chain_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)chain_next,
    chain_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    chain_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *pools;
    Py_ssize_t *indices;
    PyObject *result;
    int stopped;
} productobject;

static PyTypeObject product_type;

static PyObject *
product_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    productobject *lz;
    Py_ssize_t nargs, npools, repeat=1;
    PyObject *pools = NULL;
    Py_ssize_t *indices = NULL;
    Py_ssize_t i;

    if (kwds != NULL) {
        char *kwlist[] = {"repeat", 0};
        PyObject *tmpargs = PyTuple_New(0);
        if (tmpargs == NULL)
            return NULL;
        if (!PyArg_ParseTupleAndKeywords(tmpargs, kwds, "|n:product", kwlist, &repeat)) {
            Py_DECREF(tmpargs);
            return NULL;
        }
        Py_DECREF(tmpargs);
        if (repeat < 0) {
            PyErr_SetString(PyExc_ValueError,
                            "repeat argument cannot be negative");
            return NULL;
        }
    }

    assert(PyTuple_Check(args));
    nargs = (repeat == 0) ? 0 : PyTuple_GET_SIZE(args);
    npools = nargs * repeat;

    indices = PyMem_Malloc(npools * sizeof(Py_ssize_t));
    if (indices == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    pools = PyTuple_New(npools);
    if (pools == NULL)
        goto error;

    for (i=0; i < nargs ; ++i) {
        PyObject *item = PyTuple_GET_ITEM(args, i);
        PyObject *pool = PySequence_Tuple(item);
        if (pool == NULL)
            goto error;
        PyTuple_SET_ITEM(pools, i, pool);
        indices[i] = 0;
    }
    for ( ; i < npools; ++i) {
        PyObject *pool = PyTuple_GET_ITEM(pools, i - nargs);
        Py_INCREF(pool);
        PyTuple_SET_ITEM(pools, i, pool);
        indices[i] = 0;
    }

    lz = (productobject *)type->tp_alloc(type, 0);
    if (lz == NULL)
        goto error;

    lz->pools = pools;
    lz->indices = indices;
    lz->result = NULL;
    lz->stopped = 0;

    return (PyObject *)lz;

error:
    if (indices != NULL)
        PyMem_Free(indices);
    Py_XDECREF(pools);
    return NULL;
}

static void
product_dealloc(productobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->pools);
    Py_XDECREF(lz->result);
    if (lz->indices != NULL)
        PyMem_Free(lz->indices);
    Py_TYPE(lz)->tp_free(lz);
}

static int
product_traverse(productobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->pools);
    Py_VISIT(lz->result);
    return 0;
}

static PyObject *
product_next(productobject *lz)
{
    PyObject *pool;
    PyObject *elem;
    PyObject *oldelem;
    PyObject *pools = lz->pools;
    PyObject *result = lz->result;
    Py_ssize_t npools = PyTuple_GET_SIZE(pools);
    Py_ssize_t i;

    if (lz->stopped)
        return NULL;

    if (result == NULL) {

        result = PyTuple_New(npools);
        if (result == NULL)
            goto empty;
        lz->result = result;
        for (i=0; i < npools; i++) {
            pool = PyTuple_GET_ITEM(pools, i);
            if (PyTuple_GET_SIZE(pool) == 0)
                goto empty;
            elem = PyTuple_GET_ITEM(pool, 0);
            Py_INCREF(elem);
            PyTuple_SET_ITEM(result, i, elem);
        }
    } else {
        Py_ssize_t *indices = lz->indices;

        if (Py_REFCNT(result) > 1) {
            PyObject *old_result = result;
            result = PyTuple_New(npools);
            if (result == NULL)
                goto empty;
            lz->result = result;
            for (i=0; i < npools; i++) {
                elem = PyTuple_GET_ITEM(old_result, i);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
            Py_DECREF(old_result);
        }

        assert (npools==0 || Py_REFCNT(result) == 1);

        for (i=npools-1 ; i >= 0 ; i--) {
            pool = PyTuple_GET_ITEM(pools, i);
            indices[i]++;
            if (indices[i] == PyTuple_GET_SIZE(pool)) {

                indices[i] = 0;
                elem = PyTuple_GET_ITEM(pool, 0);
                Py_INCREF(elem);
                oldelem = PyTuple_GET_ITEM(result, i);
                PyTuple_SET_ITEM(result, i, elem);
                Py_DECREF(oldelem);
            } else {

                elem = PyTuple_GET_ITEM(pool, indices[i]);
                Py_INCREF(elem);
                oldelem = PyTuple_GET_ITEM(result, i);
                PyTuple_SET_ITEM(result, i, elem);
                Py_DECREF(oldelem);
                break;
            }
        }

        if (i < 0)
            goto empty;
    }

    Py_INCREF(result);
    return result;

empty:
    lz->stopped = 1;
    return NULL;
}

PyDoc_STRVAR(product_doc,
"product(*iterables) --> product object\n\
\n\
Cartesian product of input iterables.  Equivalent to nested for-loops.\n\n\
For example, product(A, B) returns the same as:  ((x,y) for x in A for y in B).\n\
The leftmost iterators are in the outermost for-loop, so the output tuples\n\
cycle in a manner similar to an odometer (with the rightmost element changing\n\
on every iteration).\n\n\
To compute the product of an iterable with itself, specify the number\n\
of repetitions with the optional repeat keyword argument. For example,\n\
product(A, repeat=4) means the same as product(A, A, A, A).\n\n\
product('ab', range(3)) --> ('a',0) ('a',1) ('a',2) ('b',0) ('b',1) ('b',2)\n\
product((0,1), (0,1), (0,1)) --> (0,0,0) (0,0,1) (0,1,0) (0,1,1) (1,0,0) ...");

static PyTypeObject product_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.product",
    sizeof(productobject),
    0,

    (destructor)product_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    product_doc,
    (traverseproc)product_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)product_next,
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
    product_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *pool;
    Py_ssize_t *indices;
    PyObject *result;
    Py_ssize_t r;
    int stopped;
} combinationsobject;

static PyTypeObject combinations_type;

static PyObject *
combinations_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    combinationsobject *co;
    Py_ssize_t n;
    Py_ssize_t r;
    PyObject *pool = NULL;
    PyObject *iterable = NULL;
    Py_ssize_t *indices = NULL;
    Py_ssize_t i;
    static char *kwargs[] = {"iterable", "r", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "On:combinations", kwargs,
                                     &iterable, &r))
        return NULL;

    pool = PySequence_Tuple(iterable);
    if (pool == NULL)
        goto error;
    n = PyTuple_GET_SIZE(pool);
    if (r < 0) {
        PyErr_SetString(PyExc_ValueError, "r must be non-negative");
        goto error;
    }

    indices = PyMem_Malloc(r * sizeof(Py_ssize_t));
    if (indices == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (i=0 ; i<r ; i++)
        indices[i] = i;

    co = (combinationsobject *)type->tp_alloc(type, 0);
    if (co == NULL)
        goto error;

    co->pool = pool;
    co->indices = indices;
    co->result = NULL;
    co->r = r;
    co->stopped = r > n ? 1 : 0;

    return (PyObject *)co;

error:
    if (indices != NULL)
        PyMem_Free(indices);
    Py_XDECREF(pool);
    return NULL;
}

static void
combinations_dealloc(combinationsobject *co)
{
    PyObject_GC_UnTrack(co);
    Py_XDECREF(co->pool);
    Py_XDECREF(co->result);
    if (co->indices != NULL)
        PyMem_Free(co->indices);
    Py_TYPE(co)->tp_free(co);
}

static int
combinations_traverse(combinationsobject *co, visitproc visit, void *arg)
{
    Py_VISIT(co->pool);
    Py_VISIT(co->result);
    return 0;
}

static PyObject *
combinations_next(combinationsobject *co)
{
    PyObject *elem;
    PyObject *oldelem;
    PyObject *pool = co->pool;
    Py_ssize_t *indices = co->indices;
    PyObject *result = co->result;
    Py_ssize_t n = PyTuple_GET_SIZE(pool);
    Py_ssize_t r = co->r;
    Py_ssize_t i, j, index;

    if (co->stopped)
        return NULL;

    if (result == NULL) {

        result = PyTuple_New(r);
        if (result == NULL)
            goto empty;
        co->result = result;
        for (i=0; i<r ; i++) {
            index = indices[i];
            elem = PyTuple_GET_ITEM(pool, index);
            Py_INCREF(elem);
            PyTuple_SET_ITEM(result, i, elem);
        }
    } else {

        if (Py_REFCNT(result) > 1) {
            PyObject *old_result = result;
            result = PyTuple_New(r);
            if (result == NULL)
                goto empty;
            co->result = result;
            for (i=0; i<r ; i++) {
                elem = PyTuple_GET_ITEM(old_result, i);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
            Py_DECREF(old_result);
        }

        assert(r == 0 || Py_REFCNT(result) == 1);

        for (i=r-1 ; i >= 0 && indices[i] == i+n-r ; i--)
            ;

        if (i < 0)
            goto empty;

        indices[i]++;
        for (j=i+1 ; j<r ; j++)
            indices[j] = indices[j-1] + 1;

        for ( ; i<r ; i++) {
            index = indices[i];
            elem = PyTuple_GET_ITEM(pool, index);
            Py_INCREF(elem);
            oldelem = PyTuple_GET_ITEM(result, i);
            PyTuple_SET_ITEM(result, i, elem);
            Py_DECREF(oldelem);
        }
    }

    Py_INCREF(result);
    return result;

empty:
    co->stopped = 1;
    return NULL;
}

PyDoc_STRVAR(combinations_doc,
"combinations(iterable, r) --> combinations object\n\
\n\
Return successive r-length combinations of elements in the iterable.\n\n\
combinations(range(4), 3) --> (0,1,2), (0,1,3), (0,2,3), (1,2,3)");

static PyTypeObject combinations_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.combinations",
    sizeof(combinationsobject),
    0,

    (destructor)combinations_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    combinations_doc,
    (traverseproc)combinations_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)combinations_next,
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
    combinations_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *pool;
    Py_ssize_t *indices;
    PyObject *result;
    Py_ssize_t r;
    int stopped;
} cwrobject;

static PyTypeObject cwr_type;

static PyObject *
cwr_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    cwrobject *co;
    Py_ssize_t n;
    Py_ssize_t r;
    PyObject *pool = NULL;
    PyObject *iterable = NULL;
    Py_ssize_t *indices = NULL;
    Py_ssize_t i;
    static char *kwargs[] = {"iterable", "r", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "On:combinations_with_replacement", kwargs,
                                     &iterable, &r))
        return NULL;

    pool = PySequence_Tuple(iterable);
    if (pool == NULL)
        goto error;
    n = PyTuple_GET_SIZE(pool);
    if (r < 0) {
        PyErr_SetString(PyExc_ValueError, "r must be non-negative");
        goto error;
    }

    indices = PyMem_Malloc(r * sizeof(Py_ssize_t));
    if (indices == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (i=0 ; i<r ; i++)
        indices[i] = 0;

    co = (cwrobject *)type->tp_alloc(type, 0);
    if (co == NULL)
        goto error;

    co->pool = pool;
    co->indices = indices;
    co->result = NULL;
    co->r = r;
    co->stopped = !n && r;

    return (PyObject *)co;

error:
    if (indices != NULL)
        PyMem_Free(indices);
    Py_XDECREF(pool);
    return NULL;
}

static void
cwr_dealloc(cwrobject *co)
{
    PyObject_GC_UnTrack(co);
    Py_XDECREF(co->pool);
    Py_XDECREF(co->result);
    if (co->indices != NULL)
        PyMem_Free(co->indices);
    Py_TYPE(co)->tp_free(co);
}

static int
cwr_traverse(cwrobject *co, visitproc visit, void *arg)
{
    Py_VISIT(co->pool);
    Py_VISIT(co->result);
    return 0;
}

static PyObject *
cwr_next(cwrobject *co)
{
    PyObject *elem;
    PyObject *oldelem;
    PyObject *pool = co->pool;
    Py_ssize_t *indices = co->indices;
    PyObject *result = co->result;
    Py_ssize_t n = PyTuple_GET_SIZE(pool);
    Py_ssize_t r = co->r;
    Py_ssize_t i, j, index;

    if (co->stopped)
        return NULL;

    if (result == NULL) {

        result = PyTuple_New(r);
        if (result == NULL)
            goto empty;
        co->result = result;
        for (i=0; i<r ; i++) {
            index = indices[i];
            elem = PyTuple_GET_ITEM(pool, index);
            Py_INCREF(elem);
            PyTuple_SET_ITEM(result, i, elem);
        }
    } else {

        if (Py_REFCNT(result) > 1) {
            PyObject *old_result = result;
            result = PyTuple_New(r);
            if (result == NULL)
                goto empty;
            co->result = result;
            for (i=0; i<r ; i++) {
                elem = PyTuple_GET_ITEM(old_result, i);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
            Py_DECREF(old_result);
        }

        assert(r == 0 || Py_REFCNT(result) == 1);

        for (i=r-1 ; i >= 0 && indices[i] == n-1; i--)
            ;

        if (i < 0)
            goto empty;

        indices[i]++;
        for (j=i+1 ; j<r ; j++)
            indices[j] = indices[j-1];

        for ( ; i<r ; i++) {
            index = indices[i];
            elem = PyTuple_GET_ITEM(pool, index);
            Py_INCREF(elem);
            oldelem = PyTuple_GET_ITEM(result, i);
            PyTuple_SET_ITEM(result, i, elem);
            Py_DECREF(oldelem);
        }
    }

    Py_INCREF(result);
    return result;

empty:
    co->stopped = 1;
    return NULL;
}

PyDoc_STRVAR(cwr_doc,
"combinations_with_replacement(iterable, r) --> combinations_with_replacement object\n\
\n\
Return successive r-length combinations of elements in the iterable\n\
allowing individual elements to have successive repeats.\n\
combinations_with_replacement('ABC', 2) --> AA AB AC BB BC CC");

static PyTypeObject cwr_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.combinations_with_replacement",
    sizeof(cwrobject),
    0,

    (destructor)cwr_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    cwr_doc,
    (traverseproc)cwr_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)cwr_next,
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
    cwr_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *pool;
    Py_ssize_t *indices;
    Py_ssize_t *cycles;
    PyObject *result;
    Py_ssize_t r;
    int stopped;
} permutationsobject;

static PyTypeObject permutations_type;

static PyObject *
permutations_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    permutationsobject *po;
    Py_ssize_t n;
    Py_ssize_t r;
    PyObject *robj = Py_None;
    PyObject *pool = NULL;
    PyObject *iterable = NULL;
    Py_ssize_t *indices = NULL;
    Py_ssize_t *cycles = NULL;
    Py_ssize_t i;
    static char *kwargs[] = {"iterable", "r", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|O:permutations", kwargs,
                                     &iterable, &robj))
        return NULL;

    pool = PySequence_Tuple(iterable);
    if (pool == NULL)
        goto error;
    n = PyTuple_GET_SIZE(pool);

    r = n;
    if (robj != Py_None) {
        r = PyInt_AsSsize_t(robj);
        if (r == -1 && PyErr_Occurred())
            goto error;
    }
    if (r < 0) {
        PyErr_SetString(PyExc_ValueError, "r must be non-negative");
        goto error;
    }

    indices = PyMem_Malloc(n * sizeof(Py_ssize_t));
    cycles = PyMem_Malloc(r * sizeof(Py_ssize_t));
    if (indices == NULL || cycles == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (i=0 ; i<n ; i++)
        indices[i] = i;
    for (i=0 ; i<r ; i++)
        cycles[i] = n - i;

    po = (permutationsobject *)type->tp_alloc(type, 0);
    if (po == NULL)
        goto error;

    po->pool = pool;
    po->indices = indices;
    po->cycles = cycles;
    po->result = NULL;
    po->r = r;
    po->stopped = r > n ? 1 : 0;

    return (PyObject *)po;

error:
    if (indices != NULL)
        PyMem_Free(indices);
    if (cycles != NULL)
        PyMem_Free(cycles);
    Py_XDECREF(pool);
    return NULL;
}

static void
permutations_dealloc(permutationsobject *po)
{
    PyObject_GC_UnTrack(po);
    Py_XDECREF(po->pool);
    Py_XDECREF(po->result);
    PyMem_Free(po->indices);
    PyMem_Free(po->cycles);
    Py_TYPE(po)->tp_free(po);
}

static int
permutations_traverse(permutationsobject *po, visitproc visit, void *arg)
{
    Py_VISIT(po->pool);
    Py_VISIT(po->result);
    return 0;
}

static PyObject *
permutations_next(permutationsobject *po)
{
    PyObject *elem;
    PyObject *oldelem;
    PyObject *pool = po->pool;
    Py_ssize_t *indices = po->indices;
    Py_ssize_t *cycles = po->cycles;
    PyObject *result = po->result;
    Py_ssize_t n = PyTuple_GET_SIZE(pool);
    Py_ssize_t r = po->r;
    Py_ssize_t i, j, k, index;

    if (po->stopped)
        return NULL;

    if (result == NULL) {

        result = PyTuple_New(r);
        if (result == NULL)
            goto empty;
        po->result = result;
        for (i=0; i<r ; i++) {
            index = indices[i];
            elem = PyTuple_GET_ITEM(pool, index);
            Py_INCREF(elem);
            PyTuple_SET_ITEM(result, i, elem);
        }
    } else {
        if (n == 0)
            goto empty;

        if (Py_REFCNT(result) > 1) {
            PyObject *old_result = result;
            result = PyTuple_New(r);
            if (result == NULL)
                goto empty;
            po->result = result;
            for (i=0; i<r ; i++) {
                elem = PyTuple_GET_ITEM(old_result, i);
                Py_INCREF(elem);
                PyTuple_SET_ITEM(result, i, elem);
            }
            Py_DECREF(old_result);
        }

        assert(r == 0 || Py_REFCNT(result) == 1);

        for (i=r-1 ; i>=0 ; i--) {
            cycles[i] -= 1;
            if (cycles[i] == 0) {

                index = indices[i];
                for (j=i ; j<n-1 ; j++)
                    indices[j] = indices[j+1];
                indices[n-1] = index;
                cycles[i] = n - i;
            } else {
                j = cycles[i];
                index = indices[i];
                indices[i] = indices[n-j];
                indices[n-j] = index;

                for (k=i; k<r ; k++) {

                    index = indices[k];
                    elem = PyTuple_GET_ITEM(pool, index);
                    Py_INCREF(elem);
                    oldelem = PyTuple_GET_ITEM(result, k);
                    PyTuple_SET_ITEM(result, k, elem);
                    Py_DECREF(oldelem);
                }
                break;
            }
        }

        if (i < 0)
            goto empty;
    }
    Py_INCREF(result);
    return result;

empty:
    po->stopped = 1;
    return NULL;
}

PyDoc_STRVAR(permutations_doc,
"permutations(iterable[, r]) --> permutations object\n\
\n\
Return successive r-length permutations of elements in the iterable.\n\n\
permutations(range(3), 2) --> (0,1), (0,2), (1,0), (1,2), (2,0), (2,1)");

static PyTypeObject permutations_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.permutations",
    sizeof(permutationsobject),
    0,

    (destructor)permutations_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    permutations_doc,
    (traverseproc)permutations_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)permutations_next,
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
    permutations_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *data;
    PyObject *selectors;
} compressobject;

static PyTypeObject compress_type;

static PyObject *
compress_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *seq1, *seq2;
    PyObject *data=NULL, *selectors=NULL;
    compressobject *lz;
    static char *kwargs[] = {"data", "selectors", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO:compress", kwargs, &seq1, &seq2))
        return NULL;

    data = PyObject_GetIter(seq1);
    if (data == NULL)
        goto fail;
    selectors = PyObject_GetIter(seq2);
    if (selectors == NULL)
        goto fail;

    lz = (compressobject *)type->tp_alloc(type, 0);
    if (lz == NULL)
        goto fail;
    lz->data = data;
    lz->selectors = selectors;
    return (PyObject *)lz;

fail:
    Py_XDECREF(data);
    Py_XDECREF(selectors);
    return NULL;
}

static void
compress_dealloc(compressobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->data);
    Py_XDECREF(lz->selectors);
    Py_TYPE(lz)->tp_free(lz);
}

static int
compress_traverse(compressobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->data);
    Py_VISIT(lz->selectors);
    return 0;
}

static PyObject *
compress_next(compressobject *lz)
{
    PyObject *data = lz->data, *selectors = lz->selectors;
    PyObject *datum, *selector;
    PyObject *(*datanext)(PyObject *) = *Py_TYPE(data)->tp_iternext;
    PyObject *(*selectornext)(PyObject *) = *Py_TYPE(selectors)->tp_iternext;
    int ok;

    while (1) {

        datum = datanext(data);
        if (datum == NULL)
            return NULL;

        selector = selectornext(selectors);
        if (selector == NULL) {
            Py_DECREF(datum);
            return NULL;
        }

        ok = PyObject_IsTrue(selector);
        Py_DECREF(selector);
        if (ok == 1)
            return datum;
        Py_DECREF(datum);
        if (ok == -1)
            return NULL;
    }
}

PyDoc_STRVAR(compress_doc,
"compress(data, selectors) --> iterator over selected data\n\
\n\
Return data elements corresponding to true selector elements.\n\
Forms a shorter iterator from selected data elements using the\n\
selectors to choose the data elements.");

static PyTypeObject compress_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.compress",
    sizeof(compressobject),
    0,

    (destructor)compress_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    compress_doc,
    (traverseproc)compress_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)compress_next,
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
    compress_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
} ifilterobject;

static PyTypeObject ifilter_type;

static PyObject *
ifilter_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *func, *seq;
    PyObject *it;
    ifilterobject *lz;

    if (type == &ifilter_type && !_PyArg_NoKeywords("ifilter()", kwds))
        return NULL;

    if (!PyArg_UnpackTuple(args, "ifilter", 2, 2, &func, &seq))
        return NULL;

    it = PyObject_GetIter(seq);
    if (it == NULL)
        return NULL;

    lz = (ifilterobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(func);
    lz->func = func;
    lz->it = it;

    return (PyObject *)lz;
}

static void
ifilter_dealloc(ifilterobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
ifilter_traverse(ifilterobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
ifilter_next(ifilterobject *lz)
{
    PyObject *item;
    PyObject *it = lz->it;
    long ok;
    PyObject *(*iternext)(PyObject *);

    iternext = *Py_TYPE(it)->tp_iternext;
    for (;;) {
        item = iternext(it);
        if (item == NULL)
            return NULL;

        if (lz->func == Py_None || lz->func == (PyObject *)&PyBool_Type) {
            ok = PyObject_IsTrue(item);
        } else {
            PyObject *good;
            good = PyObject_CallFunctionObjArgs(lz->func,
                                                item, NULL);
            if (good == NULL) {
                Py_DECREF(item);
                return NULL;
            }
            ok = PyObject_IsTrue(good);
            Py_DECREF(good);
        }
        if (ok > 0)
            return item;
        Py_DECREF(item);
        if (ok < 0)
            return NULL;
    }
}

PyDoc_STRVAR(ifilter_doc,
"ifilter(function or None, sequence) --> ifilter object\n\
\n\
Return those items of sequence for which function(item) is true.\n\
If function is None, return the items that are true.");

static PyTypeObject ifilter_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.ifilter",
    sizeof(ifilterobject),
    0,

    (destructor)ifilter_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    ifilter_doc,
    (traverseproc)ifilter_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)ifilter_next,
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
    ifilter_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *func;
    PyObject *it;
} ifilterfalseobject;

static PyTypeObject ifilterfalse_type;

static PyObject *
ifilterfalse_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *func, *seq;
    PyObject *it;
    ifilterfalseobject *lz;

    if (type == &ifilterfalse_type &&
        !_PyArg_NoKeywords("ifilterfalse()", kwds))
        return NULL;

    if (!PyArg_UnpackTuple(args, "ifilterfalse", 2, 2, &func, &seq))
        return NULL;

    it = PyObject_GetIter(seq);
    if (it == NULL)
        return NULL;

    lz = (ifilterfalseobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(it);
        return NULL;
    }
    Py_INCREF(func);
    lz->func = func;
    lz->it = it;

    return (PyObject *)lz;
}

static void
ifilterfalse_dealloc(ifilterfalseobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->func);
    Py_XDECREF(lz->it);
    Py_TYPE(lz)->tp_free(lz);
}

static int
ifilterfalse_traverse(ifilterfalseobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->it);
    Py_VISIT(lz->func);
    return 0;
}

static PyObject *
ifilterfalse_next(ifilterfalseobject *lz)
{
    PyObject *item;
    PyObject *it = lz->it;
    long ok;
    PyObject *(*iternext)(PyObject *);

    iternext = *Py_TYPE(it)->tp_iternext;
    for (;;) {
        item = iternext(it);
        if (item == NULL)
            return NULL;

        if (lz->func == Py_None || lz->func == (PyObject *)&PyBool_Type) {
            ok = PyObject_IsTrue(item);
        } else {
            PyObject *good;
            good = PyObject_CallFunctionObjArgs(lz->func,
                                                item, NULL);
            if (good == NULL) {
                Py_DECREF(item);
                return NULL;
            }
            ok = PyObject_IsTrue(good);
            Py_DECREF(good);
        }
        if (ok == 0)
            return item;
        Py_DECREF(item);
        if (ok < 0)
            return NULL;
    }
}

PyDoc_STRVAR(ifilterfalse_doc,
"ifilterfalse(function or None, sequence) --> ifilterfalse object\n\
\n\
Return those items of sequence for which function(item) is false.\n\
If function is None, return the items that are false.");

static PyTypeObject ifilterfalse_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.ifilterfalse",
    sizeof(ifilterfalseobject),
    0,

    (destructor)ifilterfalse_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    ifilterfalse_doc,
    (traverseproc)ifilterfalse_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)ifilterfalse_next,
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
    ifilterfalse_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    Py_ssize_t cnt;
    PyObject *long_cnt;
    PyObject *long_step;
} countobject;

static PyTypeObject count_type;

static PyObject *
count_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    countobject *lz;
    int slow_mode = 0;
    Py_ssize_t cnt = 0;
    PyObject *long_cnt = NULL;
    PyObject *long_step = NULL;
    static char *kwlist[] = {"start", "step", 0};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO:count",
                    kwlist, &long_cnt, &long_step))
        return NULL;

    if ((long_cnt != NULL && !PyNumber_Check(long_cnt)) ||
        (long_step != NULL && !PyNumber_Check(long_step))) {
                    PyErr_SetString(PyExc_TypeError, "a number is required");
                    return NULL;
    }

    if (long_cnt != NULL) {
        cnt = PyInt_AsSsize_t(long_cnt);
        if ((cnt == -1 && PyErr_Occurred()) || !PyInt_Check(long_cnt)) {
            PyErr_Clear();
            slow_mode = 1;
        }
        Py_INCREF(long_cnt);
    } else {
        cnt = 0;
        long_cnt = PyInt_FromLong(0);
    }

    if (long_step == NULL) {
        long_step = PyInt_FromLong(1);
        if (long_step == NULL) {
            Py_DECREF(long_cnt);
            return NULL;
        }
    } else
        Py_INCREF(long_step);

    assert(long_cnt != NULL && long_step != NULL);

    if (!PyInt_Check(long_step) ||
        PyInt_AS_LONG(long_step) != 1) {
            slow_mode = 1;
    }

    if (slow_mode)
        cnt = PY_SSIZE_T_MAX;
    else
        Py_CLEAR(long_cnt);

    assert((cnt != PY_SSIZE_T_MAX && long_cnt == NULL && !slow_mode) ||
           (cnt == PY_SSIZE_T_MAX && long_cnt != NULL && slow_mode));
    assert(slow_mode ||
           (PyInt_Check(long_step) && PyInt_AS_LONG(long_step) == 1));

    lz = (countobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_XDECREF(long_cnt);
        return NULL;
    }
    lz->cnt = cnt;
    lz->long_cnt = long_cnt;
    lz->long_step = long_step;

    return (PyObject *)lz;
}

static void
count_dealloc(countobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->long_cnt);
    Py_XDECREF(lz->long_step);
    Py_TYPE(lz)->tp_free(lz);
}

static int
count_traverse(countobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->long_cnt);
    Py_VISIT(lz->long_step);
    return 0;
}

static PyObject *
count_nextlong(countobject *lz)
{
    PyObject *long_cnt;
    PyObject *stepped_up;

    long_cnt = lz->long_cnt;
    if (long_cnt == NULL) {

        long_cnt = PyInt_FromSsize_t(PY_SSIZE_T_MAX);
        if (long_cnt == NULL)
            return NULL;
    }
    assert(lz->cnt == PY_SSIZE_T_MAX && long_cnt != NULL);

    stepped_up = PyNumber_Add(long_cnt, lz->long_step);
    if (stepped_up == NULL)
        return NULL;
    lz->long_cnt = stepped_up;
    return long_cnt;
}

static PyObject *
count_next(countobject *lz)
{
    if (lz->cnt == PY_SSIZE_T_MAX)
        return count_nextlong(lz);
    return PyInt_FromSsize_t(lz->cnt++);
}

static PyObject *
count_repr(countobject *lz)
{
    PyObject *cnt_repr, *step_repr = NULL;
    PyObject *result = NULL;

    if (lz->cnt != PY_SSIZE_T_MAX)
                return PyString_FromFormat("count(%zd)", lz->cnt);

    cnt_repr = PyObject_Repr(lz->long_cnt);
    if (cnt_repr == NULL)
        return NULL;

    if (PyInt_Check(lz->long_step) && PyInt_AS_LONG(lz->long_step) == 1) {

            result = PyString_FromFormat("count(%s)",
                                                                     PyString_AS_STRING(cnt_repr));
    } else {
        step_repr = PyObject_Repr(lz->long_step);
        if (step_repr != NULL)
            result = PyString_FromFormat("count(%s, %s)",
                                                                    PyString_AS_STRING(cnt_repr),
                                                                    PyString_AS_STRING(step_repr));
    }
    Py_DECREF(cnt_repr);
    Py_XDECREF(step_repr);
    return result;
}

static PyObject *
count_reduce(countobject *lz)
{
    if (lz->cnt == PY_SSIZE_T_MAX)
        return Py_BuildValue("O(OO)", Py_TYPE(lz), lz->long_cnt, lz->long_step);
    return Py_BuildValue("O(n)", Py_TYPE(lz), lz->cnt);
}

PyDoc_STRVAR(count_reduce_doc, "Return state information for pickling.");

static PyMethodDef count_methods[] = {
    {"__reduce__",      (PyCFunction)count_reduce,      METH_NOARGS,
     count_reduce_doc},
    {NULL,              NULL}
};

PyDoc_STRVAR(count_doc,
                         "count(start=0, step=1) --> count object\n\
\n\
Return a count object whose .next() method returns consecutive values.\n\
Equivalent to:\n\n\
    def count(firstval=0, step=1):\n\
    x = firstval\n\
    while 1:\n\
        yield x\n\
        x += step\n");

static PyTypeObject count_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.count",
    sizeof(countobject),
    0,

    (destructor)count_dealloc,
    0,
    0,
    0,
    0,
    (reprfunc)count_repr,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    count_doc,
    (traverseproc)count_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)count_next,
    count_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    count_new,
    PyObject_GC_Del,
};

#include "Python.h"

typedef struct {
    PyObject_HEAD
    Py_ssize_t          tuplesize;
    PyObject *ittuple;
    PyObject *result;
} izipobject;

static PyTypeObject izip_type;

static PyObject *
izip_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    izipobject *lz;
    Py_ssize_t i;
    PyObject *ittuple;
    PyObject *result;
    Py_ssize_t tuplesize = PySequence_Length(args);

    if (type == &izip_type && !_PyArg_NoKeywords("izip()", kwds))
        return NULL;

    assert(PyTuple_Check(args));

    ittuple = PyTuple_New(tuplesize);
    if (ittuple == NULL)
        return NULL;
    for (i=0; i < tuplesize; ++i) {
        PyObject *item = PyTuple_GET_ITEM(args, i);
        PyObject *it = PyObject_GetIter(item);
        if (it == NULL) {
            if (PyErr_ExceptionMatches(PyExc_TypeError))
                PyErr_Format(PyExc_TypeError,
                    "izip argument #%zd must support iteration",
                    i+1);
            Py_DECREF(ittuple);
            return NULL;
        }
        PyTuple_SET_ITEM(ittuple, i, it);
    }

    result = PyTuple_New(tuplesize);
    if (result == NULL) {
        Py_DECREF(ittuple);
        return NULL;
    }
    for (i=0 ; i < tuplesize ; i++) {
        Py_INCREF(Py_None);
        PyTuple_SET_ITEM(result, i, Py_None);
    }

    lz = (izipobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(ittuple);
        Py_DECREF(result);
        return NULL;
    }
    lz->ittuple = ittuple;
    lz->tuplesize = tuplesize;
    lz->result = result;

    return (PyObject *)lz;
}

static void
izip_dealloc(izipobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->ittuple);
    Py_XDECREF(lz->result);
    Py_TYPE(lz)->tp_free(lz);
}

static int
izip_traverse(izipobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->ittuple);
    Py_VISIT(lz->result);
    return 0;
}

static PyObject *
izip_next(izipobject *lz)
{
    Py_ssize_t i;
    Py_ssize_t tuplesize = lz->tuplesize;
    PyObject *result = lz->result;
    PyObject *it;
    PyObject *item;
    PyObject *olditem;

    if (tuplesize == 0)
        return NULL;
    if (Py_REFCNT(result) == 1) {
        Py_INCREF(result);
        for (i=0 ; i < tuplesize ; i++) {
            it = PyTuple_GET_ITEM(lz->ittuple, i);
            item = (*Py_TYPE(it)->tp_iternext)(it);
            if (item == NULL) {
                Py_DECREF(result);
                return NULL;
            }
            olditem = PyTuple_GET_ITEM(result, i);
            PyTuple_SET_ITEM(result, i, item);
            Py_DECREF(olditem);
        }
    } else {
        result = PyTuple_New(tuplesize);
        if (result == NULL)
            return NULL;
        for (i=0 ; i < tuplesize ; i++) {
            it = PyTuple_GET_ITEM(lz->ittuple, i);
            item = (*Py_TYPE(it)->tp_iternext)(it);
            if (item == NULL) {
                Py_DECREF(result);
                return NULL;
            }
            PyTuple_SET_ITEM(result, i, item);
        }
    }
    return result;
}

PyDoc_STRVAR(izip_doc,
"izip(iter1 [,iter2 [...]]) --> izip object\n\
\n\
Return a izip object whose .next() method returns a tuple where\n\
the i-th element comes from the i-th iterable argument.  The .next()\n\
method continues until the shortest iterable in the argument sequence\n\
is exhausted and then it raises StopIteration.  Works like the zip()\n\
function but consumes less memory by returning an iterator instead of\n\
a list.");

static PyTypeObject izip_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.izip",
    sizeof(izipobject),
    0,

    (destructor)izip_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    izip_doc,
    (traverseproc)izip_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)izip_next,
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
    izip_new,
    PyObject_GC_Del,
};

typedef struct {
    PyObject_HEAD
    PyObject *element;
    Py_ssize_t cnt;
} repeatobject;

static PyTypeObject repeat_type;

static PyObject *
repeat_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    repeatobject *ro;
    PyObject *element;
    Py_ssize_t cnt = -1;
    static char *kwargs[] = {"object", "times", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|n:repeat", kwargs,
                                     &element, &cnt))
        return NULL;

    if (PyTuple_Size(args) == 2 && cnt < 0)
        cnt = 0;

    ro = (repeatobject *)type->tp_alloc(type, 0);
    if (ro == NULL)
        return NULL;
    Py_INCREF(element);
    ro->element = element;
    ro->cnt = cnt;
    return (PyObject *)ro;
}

static void
repeat_dealloc(repeatobject *ro)
{
    PyObject_GC_UnTrack(ro);
    Py_XDECREF(ro->element);
    Py_TYPE(ro)->tp_free(ro);
}

static int
repeat_traverse(repeatobject *ro, visitproc visit, void *arg)
{
    Py_VISIT(ro->element);
    return 0;
}

static PyObject *
repeat_next(repeatobject *ro)
{
    if (ro->cnt == 0)
        return NULL;
    if (ro->cnt > 0)
        ro->cnt--;
    Py_INCREF(ro->element);
    return ro->element;
}

static PyObject *
repeat_repr(repeatobject *ro)
{
    PyObject *result, *objrepr;

    objrepr = PyObject_Repr(ro->element);
    if (objrepr == NULL)
        return NULL;

    if (ro->cnt == -1)
        result = PyString_FromFormat("repeat(%s)",
            PyString_AS_STRING(objrepr));
    else
        result = PyString_FromFormat("repeat(%s, %zd)",
            PyString_AS_STRING(objrepr), ro->cnt);
    Py_DECREF(objrepr);
    return result;
}

static PyObject *
repeat_len(repeatobject *ro)
{
    if (ro->cnt == -1) {
        PyErr_SetString(PyExc_TypeError, "len() of unsized object");
        return NULL;
    }
    return PyInt_FromSize_t(ro->cnt);
}

PyDoc_STRVAR(length_hint_doc, "Private method returning an estimate of len(list(it)).");

static PyMethodDef repeat_methods[] = {
    {"__length_hint__", (PyCFunction)repeat_len, METH_NOARGS, length_hint_doc},
    {NULL,              NULL}
};

PyDoc_STRVAR(repeat_doc,
"repeat(object [,times]) -> create an iterator which returns the object\n\
for the specified number of times.  If not specified, returns the object\n\
endlessly.");

static PyTypeObject repeat_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.repeat",
    sizeof(repeatobject),
    0,

    (destructor)repeat_dealloc,
    0,
    0,
    0,
    0,
    (reprfunc)repeat_repr,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    repeat_doc,
    (traverseproc)repeat_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)repeat_next,
    repeat_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    repeat_new,
    PyObject_GC_Del,
};

#include "Python.h"

typedef struct {
    PyObject_HEAD
    Py_ssize_t tuplesize;
    Py_ssize_t numactive;
    PyObject *ittuple;
    PyObject *result;
    PyObject *fillvalue;
} iziplongestobject;

static PyTypeObject iziplongest_type;

static PyObject *
izip_longest_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    iziplongestobject *lz;
    Py_ssize_t i;
    PyObject *ittuple;
    PyObject *result;
    PyObject *fillvalue = Py_None;
    Py_ssize_t tuplesize = PySequence_Length(args);

    if (kwds != NULL && PyDict_CheckExact(kwds) && PyDict_Size(kwds) > 0) {
        fillvalue = PyDict_GetItemString(kwds, "fillvalue");
        if (fillvalue == NULL  ||  PyDict_Size(kwds) > 1) {
            PyErr_SetString(PyExc_TypeError,
                "izip_longest() got an unexpected keyword argument");
            return NULL;
        }
    }

    assert(PyTuple_Check(args));

    ittuple = PyTuple_New(tuplesize);
    if (ittuple == NULL)
        return NULL;
    for (i=0; i < tuplesize; ++i) {
        PyObject *item = PyTuple_GET_ITEM(args, i);
        PyObject *it = PyObject_GetIter(item);
        if (it == NULL) {
            if (PyErr_ExceptionMatches(PyExc_TypeError))
                PyErr_Format(PyExc_TypeError,
                    "izip_longest argument #%zd must support iteration",
                    i+1);
            Py_DECREF(ittuple);
            return NULL;
        }
        PyTuple_SET_ITEM(ittuple, i, it);
    }

    result = PyTuple_New(tuplesize);
    if (result == NULL) {
        Py_DECREF(ittuple);
        return NULL;
    }
    for (i=0 ; i < tuplesize ; i++) {
        Py_INCREF(Py_None);
        PyTuple_SET_ITEM(result, i, Py_None);
    }

    lz = (iziplongestobject *)type->tp_alloc(type, 0);
    if (lz == NULL) {
        Py_DECREF(ittuple);
        Py_DECREF(result);
        return NULL;
    }
    lz->ittuple = ittuple;
    lz->tuplesize = tuplesize;
    lz->numactive = tuplesize;
    lz->result = result;
    Py_INCREF(fillvalue);
    lz->fillvalue = fillvalue;
    return (PyObject *)lz;
}

static void
izip_longest_dealloc(iziplongestobject *lz)
{
    PyObject_GC_UnTrack(lz);
    Py_XDECREF(lz->ittuple);
    Py_XDECREF(lz->result);
    Py_XDECREF(lz->fillvalue);
    Py_TYPE(lz)->tp_free(lz);
}

static int
izip_longest_traverse(iziplongestobject *lz, visitproc visit, void *arg)
{
    Py_VISIT(lz->ittuple);
    Py_VISIT(lz->result);
    Py_VISIT(lz->fillvalue);
    return 0;
}

static PyObject *
izip_longest_next(iziplongestobject *lz)
{
    Py_ssize_t i;
    Py_ssize_t tuplesize = lz->tuplesize;
    PyObject *result = lz->result;
    PyObject *it;
    PyObject *item;
    PyObject *olditem;

    if (tuplesize == 0)
        return NULL;
    if (lz->numactive == 0)
        return NULL;
    if (Py_REFCNT(result) == 1) {
        Py_INCREF(result);
        for (i=0 ; i < tuplesize ; i++) {
            it = PyTuple_GET_ITEM(lz->ittuple, i);
            if (it == NULL) {
                Py_INCREF(lz->fillvalue);
                item = lz->fillvalue;
            } else {
                item = PyIter_Next(it);
                if (item == NULL) {
                    lz->numactive -= 1;
                    if (lz->numactive == 0 || PyErr_Occurred()) {
                        lz->numactive = 0;
                        Py_DECREF(result);
                        return NULL;
                    } else {
                        Py_INCREF(lz->fillvalue);
                        item = lz->fillvalue;
                        PyTuple_SET_ITEM(lz->ittuple, i, NULL);
                        Py_DECREF(it);
                    }
                }
            }
            olditem = PyTuple_GET_ITEM(result, i);
            PyTuple_SET_ITEM(result, i, item);
            Py_DECREF(olditem);
        }
    } else {
        result = PyTuple_New(tuplesize);
        if (result == NULL)
            return NULL;
        for (i=0 ; i < tuplesize ; i++) {
            it = PyTuple_GET_ITEM(lz->ittuple, i);
            if (it == NULL) {
                Py_INCREF(lz->fillvalue);
                item = lz->fillvalue;
            } else {
                item = PyIter_Next(it);
                if (item == NULL) {
                    lz->numactive -= 1;
                    if (lz->numactive == 0 || PyErr_Occurred()) {
                        lz->numactive = 0;
                        Py_DECREF(result);
                        return NULL;
                    } else {
                        Py_INCREF(lz->fillvalue);
                        item = lz->fillvalue;
                        PyTuple_SET_ITEM(lz->ittuple, i, NULL);
                        Py_DECREF(it);
                    }
                }
            }
            PyTuple_SET_ITEM(result, i, item);
        }
    }
    return result;
}

PyDoc_STRVAR(izip_longest_doc,
"izip_longest(iter1 [,iter2 [...]], [fillvalue=None]) --> izip_longest object\n\
\n\
Return an izip_longest object whose .next() method returns a tuple where\n\
the i-th element comes from the i-th iterable argument.  The .next()\n\
method continues until the longest iterable in the argument sequence\n\
is exhausted and then it raises StopIteration.  When the shorter iterables\n\
are exhausted, the fillvalue is substituted in their place.  The fillvalue\n\
defaults to None or can be specified by a keyword argument.\n\
");

static PyTypeObject iziplongest_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "itertools.izip_longest",
    sizeof(iziplongestobject),
    0,

    (destructor)izip_longest_dealloc,
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    izip_longest_doc,
    (traverseproc)izip_longest_traverse,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)izip_longest_next,
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
    izip_longest_new,
    PyObject_GC_Del,
};

PyDoc_STRVAR(module_doc,
"Functional tools for creating and using iterators.\n\
\n\
Infinite iterators:\n\
count([n]) --> n, n+1, n+2, ...\n\
cycle(p) --> p0, p1, ... plast, p0, p1, ...\n\
repeat(elem [,n]) --> elem, elem, elem, ... endlessly or up to n times\n\
\n\
Iterators terminating on the shortest input sequence:\n\
chain(p, q, ...) --> p0, p1, ... plast, q0, q1, ... \n\
compress(data, selectors) --> (d[0] if s[0]), (d[1] if s[1]), ...\n\
dropwhile(pred, seq) --> seq[n], seq[n+1], starting when pred fails\n\
groupby(iterable[, keyfunc]) --> sub-iterators grouped by value of keyfunc(v)\n\
ifilter(pred, seq) --> elements of seq where pred(elem) is True\n\
ifilterfalse(pred, seq) --> elements of seq where pred(elem) is False\n\
islice(seq, [start,] stop [, step]) --> elements from\n\
       seq[start:stop:step]\n\
imap(fun, p, q, ...) --> fun(p0, q0), fun(p1, q1), ...\n\
starmap(fun, seq) --> fun(*seq[0]), fun(*seq[1]), ...\n\
tee(it, n=2) --> (it1, it2 , ... itn) splits one iterator into n\n\
takewhile(pred, seq) --> seq[0], seq[1], until pred fails\n\
izip(p, q, ...) --> (p[0], q[0]), (p[1], q[1]), ... \n\
izip_longest(p, q, ...) --> (p[0], q[0]), (p[1], q[1]), ... \n\
\n\
Combinatoric generators:\n\
product(p, q, ... [repeat=1]) --> cartesian product\n\
permutations(p[, r])\n\
combinations(p, r)\n\
combinations_with_replacement(p, r)\n\
");

static PyMethodDef module_methods[] = {
    {"tee",     (PyCFunction)tee,       METH_VARARGS, tee_doc},
    {NULL,              NULL}
};

PyMODINIT_FUNC
inititertools(void)
{
    int i;
    PyObject *m;
    char *name;
    PyTypeObject *typelist[] = {
        &combinations_type,
        &cwr_type,
        &cycle_type,
        &dropwhile_type,
        &takewhile_type,
        &islice_type,
        &starmap_type,
        &imap_type,
        &chain_type,
        &compress_type,
        &ifilter_type,
        &ifilterfalse_type,
        &count_type,
        &izip_type,
        &iziplongest_type,
        &permutations_type,
        &product_type,
        &repeat_type,
        &groupby_type,
        NULL
    };

    Py_TYPE(&teedataobject_type) = &PyType_Type;
    m = Py_InitModule3("itertools", module_methods, module_doc);
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

    if (PyType_Ready(&teedataobject_type) < 0)
        return;
    if (PyType_Ready(&tee_type) < 0)
        return;
    if (PyType_Ready(&_grouper_type) < 0)
        return;
}
