#include "Python.h"
#include "frameobject.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef Py_REF_DEBUG
Py_ssize_t _Py_RefTotal;

Py_ssize_t
_Py_GetRefTotal(void)
{
    PyObject *o;
    Py_ssize_t total = _Py_RefTotal;

    o = _PyDict_Dummy();
    if (o != NULL)
        total -= o->ob_refcnt;
    o = _PySet_Dummy();
    if (o != NULL)
        total -= o->ob_refcnt;
    return total;
}
#endif

int Py_DivisionWarningFlag;
int Py_Py3kWarningFlag;

#ifdef Py_TRACE_REFS

static PyObject refchain = {&refchain, &refchain};

void
_Py_AddToAllObjects(PyObject *op, int force)
{
#ifdef  Py_DEBUG
    if (!force) {

        assert((op->_ob_prev == NULL) == (op->_ob_next == NULL));
    }
#endif
    if (force || op->_ob_prev == NULL) {
        op->_ob_next = refchain._ob_next;
        op->_ob_prev = &refchain;
        refchain._ob_next->_ob_prev = op;
        refchain._ob_next = op;
    }
}
#endif

#ifdef COUNT_ALLOCS
static PyTypeObject *type_list;

static int unlist_types_without_objects;
extern Py_ssize_t tuple_zero_allocs, fast_tuple_allocs;
extern Py_ssize_t quick_int_allocs, quick_neg_int_allocs;
extern Py_ssize_t null_strings, one_strings;
void
dump_counts(FILE* f)
{
    PyTypeObject *tp;

    for (tp = type_list; tp; tp = tp->tp_next)
        fprintf(f, "%s alloc'd: %" PY_FORMAT_SIZE_T "d, "
            "freed: %" PY_FORMAT_SIZE_T "d, "
            "max in use: %" PY_FORMAT_SIZE_T "d\n",
            tp->tp_name, tp->tp_allocs, tp->tp_frees,
            tp->tp_maxalloc);
    fprintf(f, "fast tuple allocs: %" PY_FORMAT_SIZE_T "d, "
        "empty: %" PY_FORMAT_SIZE_T "d\n",
        fast_tuple_allocs, tuple_zero_allocs);
    fprintf(f, "fast int allocs: pos: %" PY_FORMAT_SIZE_T "d, "
        "neg: %" PY_FORMAT_SIZE_T "d\n",
        quick_int_allocs, quick_neg_int_allocs);
    fprintf(f, "null strings: %" PY_FORMAT_SIZE_T "d, "
        "1-strings: %" PY_FORMAT_SIZE_T "d\n",
        null_strings, one_strings);
}

PyObject *
get_counts(void)
{
    PyTypeObject *tp;
    PyObject *result;
    PyObject *v;

    result = PyList_New(0);
    if (result == NULL)
        return NULL;
    for (tp = type_list; tp; tp = tp->tp_next) {
        v = Py_BuildValue("(snnn)", tp->tp_name, tp->tp_allocs,
                          tp->tp_frees, tp->tp_maxalloc);
        if (v == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        if (PyList_Append(result, v) < 0) {
            Py_DECREF(v);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(v);
    }
    return result;
}

void
inc_count(PyTypeObject *tp)
{
    if (tp->tp_next == NULL && tp->tp_prev == NULL) {

        if (tp->tp_next != NULL)
            Py_FatalError("XXX inc_count sanity check");
        if (type_list)
            type_list->tp_prev = tp;
        tp->tp_next = type_list;

        Py_INCREF(tp);
        type_list = tp;
#ifdef Py_TRACE_REFS

        _Py_AddToAllObjects((PyObject *)tp, 0);
#endif
    }
    tp->tp_allocs++;
    if (tp->tp_allocs - tp->tp_frees > tp->tp_maxalloc)
        tp->tp_maxalloc = tp->tp_allocs - tp->tp_frees;
}

void dec_count(PyTypeObject *tp)
{
    tp->tp_frees++;
    if (unlist_types_without_objects &&
        tp->tp_allocs == tp->tp_frees) {

        if (tp->tp_prev)
            tp->tp_prev->tp_next = tp->tp_next;
        else
            type_list = tp->tp_next;
        if (tp->tp_next)
            tp->tp_next->tp_prev = tp->tp_prev;
        tp->tp_next = tp->tp_prev = NULL;
        Py_DECREF(tp);
    }
}

#endif

#ifdef Py_REF_DEBUG

void
_Py_NegativeRefcount(const char *fname, int lineno, PyObject *op)
{
    char buf[300];

    PyOS_snprintf(buf, sizeof(buf),
                  "%s:%i object at %p has negative ref count "
                  "%" PY_FORMAT_SIZE_T "d",
                  fname, lineno, op, op->ob_refcnt);
    Py_FatalError(buf);
}

#endif

void
Py_IncRef(PyObject *o)
{
    Py_XINCREF(o);
}

void
Py_DecRef(PyObject *o)
{
    Py_XDECREF(o);
}

PyObject *
PyObject_Init(PyObject *op, PyTypeObject *tp)
{
    if (op == NULL)
        return PyErr_NoMemory();

    Py_TYPE(op) = tp;
    _Py_NewReference(op);
    return op;
}

PyVarObject *
PyObject_InitVar(PyVarObject *op, PyTypeObject *tp, Py_ssize_t size)
{
    if (op == NULL)
        return (PyVarObject *) PyErr_NoMemory();

    op->ob_size = size;
    Py_TYPE(op) = tp;
    _Py_NewReference((PyObject *)op);
    return op;
}

PyObject *
_PyObject_New(PyTypeObject *tp)
{
    PyObject *op;
    op = (PyObject *) PyObject_MALLOC(_PyObject_SIZE(tp));
    if (op == NULL)
        return PyErr_NoMemory();
    return PyObject_INIT(op, tp);
}

PyVarObject *
_PyObject_NewVar(PyTypeObject *tp, Py_ssize_t nitems)
{
    PyVarObject *op;
    const size_t size = _PyObject_VAR_SIZE(tp, nitems);
    op = (PyVarObject *) PyObject_MALLOC(size);
    if (op == NULL)
        return (PyVarObject *)PyErr_NoMemory();
    return PyObject_INIT_VAR(op, tp, nitems);
}

#undef _PyObject_Del
void
_PyObject_Del(PyObject *op)
{
    PyObject_FREE(op);
}

static int
internal_print(PyObject *op, FILE *fp, int flags, int nesting)
{
    int ret = 0;
    if (nesting > 10) {
        PyErr_SetString(PyExc_RuntimeError, "print recursion");
        return -1;
    }
    if (PyErr_CheckSignals())
        return -1;
#ifdef USE_STACKCHECK
    if (PyOS_CheckStack()) {
        PyErr_SetString(PyExc_MemoryError, "stack overflow");
        return -1;
    }
#endif
    clearerr(fp);
    if (op == NULL) {
        Py_BEGIN_ALLOW_THREADS
        fprintf(fp, "<nil>");
        Py_END_ALLOW_THREADS
    }
    else {
        if (op->ob_refcnt <= 0)

            Py_BEGIN_ALLOW_THREADS
            fprintf(fp, "<refcnt %ld at %p>",
                (long)op->ob_refcnt, op);
            Py_END_ALLOW_THREADS
        else if (Py_TYPE(op)->tp_print == NULL) {
            PyObject *s;
            if (flags & Py_PRINT_RAW)
                s = PyObject_Str(op);
            else
                s = PyObject_Repr(op);
            if (s == NULL)
                ret = -1;
            else {
                ret = internal_print(s, fp, Py_PRINT_RAW,
                                     nesting+1);
            }
            Py_XDECREF(s);
        }
        else
            ret = (*Py_TYPE(op)->tp_print)(op, fp, flags);
    }
    if (ret == 0) {
        if (ferror(fp)) {
            PyErr_SetFromErrno(PyExc_IOError);
            clearerr(fp);
            ret = -1;
        }
    }
    return ret;
}

int
PyObject_Print(PyObject *op, FILE *fp, int flags)
{
    return internal_print(op, fp, flags, 0);
}

void _PyObject_Dump(PyObject* op)
{
    if (op == NULL)
        fprintf(stderr, "NULL\n");
    else {
#ifdef WITH_THREAD
        PyGILState_STATE gil;
#endif
        fprintf(stderr, "object  : ");
#ifdef WITH_THREAD
        gil = PyGILState_Ensure();
#endif
        (void)PyObject_Print(op, stderr, 0);
#ifdef WITH_THREAD
        PyGILState_Release(gil);
#endif

        fprintf(stderr, "\n"
            "type    : %s\n"
            "refcount: %ld\n"
            "address : %p\n",
            Py_TYPE(op)==NULL ? "NULL" : Py_TYPE(op)->tp_name,
            (long)op->ob_refcnt,
            op);
    }
}

PyObject *
PyObject_Repr(PyObject *v)
{
    if (PyErr_CheckSignals())
        return NULL;
#ifdef USE_STACKCHECK
    if (PyOS_CheckStack()) {
        PyErr_SetString(PyExc_MemoryError, "stack overflow");
        return NULL;
    }
#endif
    if (v == NULL)
        return PyString_FromString("<NULL>");
    else if (Py_TYPE(v)->tp_repr == NULL)
        return PyString_FromFormat("<%s object at %p>",
                                   Py_TYPE(v)->tp_name, v);
    else {
        PyObject *res;
        res = (*Py_TYPE(v)->tp_repr)(v);
        if (res == NULL)
            return NULL;
#ifdef Py_USING_UNICODE
        if (PyUnicode_Check(res)) {
            PyObject* str;
            str = PyUnicode_AsEncodedString(res, NULL, NULL);
            Py_DECREF(res);
            if (str)
                res = str;
            else
                return NULL;
        }
#endif
        if (!PyString_Check(res)) {
            PyErr_Format(PyExc_TypeError,
                         "__repr__ returned non-string (type %.200s)",
                         Py_TYPE(res)->tp_name);
            Py_DECREF(res);
            return NULL;
        }
        return res;
    }
}

PyObject *
_PyObject_Str(PyObject *v)
{
    PyObject *res;
    int type_ok;
    if (v == NULL)
        return PyString_FromString("<NULL>");
    if (PyString_CheckExact(v)) {
        Py_INCREF(v);
        return v;
    }
#ifdef Py_USING_UNICODE
    if (PyUnicode_CheckExact(v)) {
        Py_INCREF(v);
        return v;
    }
#endif
    if (Py_TYPE(v)->tp_str == NULL)
        return PyObject_Repr(v);

    if (Py_EnterRecursiveCall(" while getting the str of an object"))
        return NULL;
    res = (*Py_TYPE(v)->tp_str)(v);
    Py_LeaveRecursiveCall();
    if (res == NULL)
        return NULL;
    type_ok = PyString_Check(res);
#ifdef Py_USING_UNICODE
    type_ok = type_ok || PyUnicode_Check(res);
#endif
    if (!type_ok) {
        PyErr_Format(PyExc_TypeError,
                     "__str__ returned non-string (type %.200s)",
                     Py_TYPE(res)->tp_name);
        Py_DECREF(res);
        return NULL;
    }
    return res;
}

PyObject *
PyObject_Str(PyObject *v)
{
    PyObject *res = _PyObject_Str(v);
    if (res == NULL)
        return NULL;
#ifdef Py_USING_UNICODE
    if (PyUnicode_Check(res)) {
        PyObject* str;
        str = PyUnicode_AsEncodedString(res, NULL, NULL);
        Py_DECREF(res);
        if (str)
            res = str;
        else
            return NULL;
    }
#endif
    assert(PyString_Check(res));
    return res;
}

#ifdef Py_USING_UNICODE
PyObject *
PyObject_Unicode(PyObject *v)
{
    PyObject *res;
    PyObject *func;
    PyObject *str;
    int unicode_method_found = 0;
    static PyObject *unicodestr = NULL;

    if (v == NULL) {
        res = PyString_FromString("<NULL>");
        if (res == NULL)
            return NULL;
        str = PyUnicode_FromEncodedObject(res, NULL, "strict");
        Py_DECREF(res);
        return str;
    } else if (PyUnicode_CheckExact(v)) {
        Py_INCREF(v);
        return v;
    }

    if (PyInstance_Check(v)) {

        if (!unicodestr) {
            unicodestr = PyString_InternFromString("__unicode__");
            if (!unicodestr)
                return NULL;
        }
        func = PyObject_GetAttr(v, unicodestr);
        if (func != NULL) {
            unicode_method_found = 1;
            res = PyObject_CallFunctionObjArgs(func, NULL);
            Py_DECREF(func);
        }
        else {
            PyErr_Clear();
        }
    }
    else {

        func = _PyObject_LookupSpecial(v, "__unicode__", &unicodestr);
        if (func != NULL) {
            unicode_method_found = 1;
            res = PyObject_CallFunctionObjArgs(func, NULL);
            Py_DECREF(func);
        }
        else if (PyErr_Occurred())
            return NULL;
    }

    if (!unicode_method_found) {
        if (PyUnicode_Check(v)) {

            return PyUnicode_FromUnicode(PyUnicode_AS_UNICODE(v),
                                         PyUnicode_GET_SIZE(v));
        }
        if (PyString_CheckExact(v)) {
            Py_INCREF(v);
            res = v;
        }
        else {
            if (Py_TYPE(v)->tp_str != NULL)
                res = (*Py_TYPE(v)->tp_str)(v);
            else
                res = PyObject_Repr(v);
        }
    }

    if (res == NULL)
        return NULL;
    if (!PyUnicode_Check(res)) {
        str = PyUnicode_FromEncodedObject(res, NULL, "strict");
        Py_DECREF(res);
        res = str;
    }
    return res;
}
#endif

static int
adjust_tp_compare(int c)
{
    if (PyErr_Occurred()) {
        if (c != -1 && c != -2) {
            PyObject *t, *v, *tb;
            PyErr_Fetch(&t, &v, &tb);
            if (PyErr_Warn(PyExc_RuntimeWarning,
                           "tp_compare didn't return -1 or -2 "
                           "for exception") < 0) {
                Py_XDECREF(t);
                Py_XDECREF(v);
                Py_XDECREF(tb);
            }
            else
                PyErr_Restore(t, v, tb);
        }
        return -2;
    }
    else if (c < -1 || c > 1) {
        if (PyErr_Warn(PyExc_RuntimeWarning,
                       "tp_compare didn't return -1, 0 or 1") < 0)
            return -2;
        else
            return c < -1 ? -1 : 1;
    }
    else {
        assert(c >= -1 && c <= 1);
        return c;
    }
}

#define RICHCOMPARE(t) (PyType_HasFeature((t), Py_TPFLAGS_HAVE_RICHCOMPARE) \
             ? (t)->tp_richcompare : NULL)

int _Py_SwappedOp[] = {Py_GT, Py_GE, Py_EQ, Py_NE, Py_LT, Py_LE};

static PyObject *
try_rich_compare(PyObject *v, PyObject *w, int op)
{
    richcmpfunc f;
    PyObject *res;

    if (v->ob_type != w->ob_type &&
        PyType_IsSubtype(w->ob_type, v->ob_type) &&
        (f = RICHCOMPARE(w->ob_type)) != NULL) {
        res = (*f)(w, v, _Py_SwappedOp[op]);
        if (res != Py_NotImplemented)
            return res;
        Py_DECREF(res);
    }
    if ((f = RICHCOMPARE(v->ob_type)) != NULL) {
        res = (*f)(v, w, op);
        if (res != Py_NotImplemented)
            return res;
        Py_DECREF(res);
    }
    if ((f = RICHCOMPARE(w->ob_type)) != NULL) {
        return (*f)(w, v, _Py_SwappedOp[op]);
    }
    res = Py_NotImplemented;
    Py_INCREF(res);
    return res;
}

static int
try_rich_compare_bool(PyObject *v, PyObject *w, int op)
{
    PyObject *res;
    int ok;

    if (RICHCOMPARE(v->ob_type) == NULL && RICHCOMPARE(w->ob_type) == NULL)
        return 2;
    res = try_rich_compare(v, w, op);
    if (res == NULL)
        return -1;
    if (res == Py_NotImplemented) {
        Py_DECREF(res);
        return 2;
    }
    ok = PyObject_IsTrue(res);
    Py_DECREF(res);
    return ok;
}

static int
try_rich_to_3way_compare(PyObject *v, PyObject *w)
{
    static struct { int op; int outcome; } tries[3] = {

        {Py_EQ, 0},
        {Py_LT, -1},
        {Py_GT, 1},
    };
    int i;

    if (RICHCOMPARE(v->ob_type) == NULL && RICHCOMPARE(w->ob_type) == NULL)
        return 2;

    for (i = 0; i < 3; i++) {
        switch (try_rich_compare_bool(v, w, tries[i].op)) {
        case -1:
            return -2;
        case 1:
            return tries[i].outcome;
        }
    }

    return 2;
}

static int
try_3way_compare(PyObject *v, PyObject *w)
{
    int c;
    cmpfunc f;

    f = v->ob_type->tp_compare;
    if (PyInstance_Check(v))
        return (*f)(v, w);
    if (PyInstance_Check(w))
        return (*w->ob_type->tp_compare)(v, w);

    if (f != NULL && f == w->ob_type->tp_compare) {
        c = (*f)(v, w);
        return adjust_tp_compare(c);
    }

    if (f == _PyObject_SlotCompare ||
        w->ob_type->tp_compare == _PyObject_SlotCompare)
        return _PyObject_SlotCompare(v, w);

    c = PyNumber_CoerceEx(&v, &w);
    if (c < 0)
        return -2;
    if (c > 0)
        return 2;
    f = v->ob_type->tp_compare;
    if (f != NULL && f == w->ob_type->tp_compare) {
        c = (*f)(v, w);
        Py_DECREF(v);
        Py_DECREF(w);
        return adjust_tp_compare(c);
    }

    Py_DECREF(v);
    Py_DECREF(w);
    return 2;
}

static int
default_3way_compare(PyObject *v, PyObject *w)
{
    int c;
    const char *vname, *wname;

    if (v->ob_type == w->ob_type) {

        Py_uintptr_t vv = (Py_uintptr_t)v;
        Py_uintptr_t ww = (Py_uintptr_t)w;
        return (vv < ww) ? -1 : (vv > ww) ? 1 : 0;
    }

    if (v == Py_None)
        return -1;
    if (w == Py_None)
        return 1;

    if (PyNumber_Check(v))
        vname = "";
    else
        vname = v->ob_type->tp_name;
    if (PyNumber_Check(w))
        wname = "";
    else
        wname = w->ob_type->tp_name;
    c = strcmp(vname, wname);
    if (c < 0)
        return -1;
    if (c > 0)
        return 1;

    return ((Py_uintptr_t)(v->ob_type) < (
        Py_uintptr_t)(w->ob_type)) ? -1 : 1;
}

static int
do_cmp(PyObject *v, PyObject *w)
{
    int c;
    cmpfunc f;

    if (v->ob_type == w->ob_type
        && (f = v->ob_type->tp_compare) != NULL) {
        c = (*f)(v, w);
        if (PyInstance_Check(v)) {

            if (c != 2)
                return c;

        }
        else
            return adjust_tp_compare(c);
    }

    c = try_rich_to_3way_compare(v, w);
    if (c < 2)
        return c;
    c = try_3way_compare(v, w);
    if (c < 2)
        return c;
    return default_3way_compare(v, w);
}

int
PyObject_Compare(PyObject *v, PyObject *w)
{
    int result;

    if (v == NULL || w == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (v == w)
        return 0;
    if (Py_EnterRecursiveCall(" in cmp"))
        return -1;
    result = do_cmp(v, w);
    Py_LeaveRecursiveCall();
    return result < 0 ? -1 : result;
}

static PyObject *
convert_3way_to_object(int op, int c)
{
    PyObject *result;
    switch (op) {
    case Py_LT: c = c <  0; break;
    case Py_LE: c = c <= 0; break;
    case Py_EQ: c = c == 0; break;
    case Py_NE: c = c != 0; break;
    case Py_GT: c = c >  0; break;
    case Py_GE: c = c >= 0; break;
    }
    result = c ? Py_True : Py_False;
    Py_INCREF(result);
    return result;
}

static PyObject *
try_3way_to_rich_compare(PyObject *v, PyObject *w, int op)
{
    int c;

    c = try_3way_compare(v, w);
    if (c >= 2) {

        if (Py_Py3kWarningFlag &&
            v->ob_type != w->ob_type && op != Py_EQ && op != Py_NE &&
            PyErr_WarnEx(PyExc_DeprecationWarning,
                       "comparing unequal types not supported "
                       "in 3.x", 1) < 0) {
            return NULL;
        }

        c = default_3way_compare(v, w);
    }
    if (c <= -2)
        return NULL;
    return convert_3way_to_object(op, c);
}

static PyObject *
do_richcmp(PyObject *v, PyObject *w, int op)
{
    PyObject *res;

    res = try_rich_compare(v, w, op);
    if (res != Py_NotImplemented)
        return res;
    Py_DECREF(res);

    return try_3way_to_rich_compare(v, w, op);
}

PyObject *
PyObject_RichCompare(PyObject *v, PyObject *w, int op)
{
    PyObject *res;

    assert(Py_LT <= op && op <= Py_GE);
    if (Py_EnterRecursiveCall(" in cmp"))
        return NULL;

    if (v->ob_type == w->ob_type && !PyInstance_Check(v)) {
        cmpfunc fcmp;
        richcmpfunc frich = RICHCOMPARE(v->ob_type);

        if (frich != NULL) {
            res = (*frich)(v, w, op);
            if (res != Py_NotImplemented)
                goto Done;
            Py_DECREF(res);
        }

        fcmp = v->ob_type->tp_compare;
        if (fcmp != NULL) {
            int c = (*fcmp)(v, w);
            c = adjust_tp_compare(c);
            if (c == -2) {
                res = NULL;
                goto Done;
            }
            res = convert_3way_to_object(op, c);
            goto Done;
        }
    }

    res = do_richcmp(v, w, op);
Done:
    Py_LeaveRecursiveCall();
    return res;
}

int
PyObject_RichCompareBool(PyObject *v, PyObject *w, int op)
{
    PyObject *res;
    int ok;

    if (v == w) {
        if (op == Py_EQ)
            return 1;
        else if (op == Py_NE)
            return 0;
    }

    res = PyObject_RichCompare(v, w, op);
    if (res == NULL)
        return -1;
    if (PyBool_Check(res))
        ok = (res == Py_True);
    else
        ok = PyObject_IsTrue(res);
    Py_DECREF(res);
    return ok;
}

long
_Py_HashDouble(double v)
{
    double intpart, fractpart;
    int expo;
    long hipart;
    long x;

    if (!Py_IS_FINITE(v)) {
        if (Py_IS_INFINITY(v))
            return v < 0 ? -271828 : 314159;
        else
            return 0;
    }
    fractpart = modf(v, &intpart);
    if (fractpart == 0.0) {

        if (intpart > LONG_MAX/2 || -intpart > LONG_MAX/2) {

            PyObject *plong;
            plong = PyLong_FromDouble(v);
            if (plong == NULL)
                return -1;
            x = PyObject_Hash(plong);
            Py_DECREF(plong);
            return x;
        }

        x = (long)intpart;
        if (x == -1)
            x = -2;
        return x;
    }

    v = frexp(v, &expo);
    v *= 2147483648.0;
    hipart = (long)v;
    v = (v - (double)hipart) * 2147483648.0;
    x = hipart + (long)v + (expo << 15);
    if (x == -1)
        x = -2;
    return x;
}

long
_Py_HashPointer(void *p)
{
    long x;
    size_t y = (size_t)p;

    y = (y >> 4) | (y << (8 * SIZEOF_VOID_P - 4));
    x = (long)y;
    if (x == -1)
        x = -2;
    return x;
}

long
PyObject_HashNotImplemented(PyObject *self)
{
    PyErr_Format(PyExc_TypeError, "unhashable type: '%.200s'",
                 self->ob_type->tp_name);
    return -1;
}

_Py_HashSecret_t _Py_HashSecret;

long
PyObject_Hash(PyObject *v)
{
    PyTypeObject *tp = v->ob_type;
    if (tp->tp_hash != NULL)
        return (*tp->tp_hash)(v);

    if (tp->tp_dict == NULL) {
        if (PyType_Ready(tp) < 0)
            return -1;
        if (tp->tp_hash != NULL)
            return (*tp->tp_hash)(v);
    }
    if (tp->tp_compare == NULL && RICHCOMPARE(tp) == NULL) {
        return _Py_HashPointer(v);
    }

    return PyObject_HashNotImplemented(v);
}

PyObject *
PyObject_GetAttrString(PyObject *v, const char *name)
{
    PyObject *w, *res;

    if (Py_TYPE(v)->tp_getattr != NULL)
        return (*Py_TYPE(v)->tp_getattr)(v, (char*)name);
    w = PyString_InternFromString(name);
    if (w == NULL)
        return NULL;
    res = PyObject_GetAttr(v, w);
    Py_XDECREF(w);
    return res;
}

int
PyObject_HasAttrString(PyObject *v, const char *name)
{
    PyObject *res = PyObject_GetAttrString(v, name);
    if (res != NULL) {
        Py_DECREF(res);
        return 1;
    }
    PyErr_Clear();
    return 0;
}

int
PyObject_SetAttrString(PyObject *v, const char *name, PyObject *w)
{
    PyObject *s;
    int res;

    if (Py_TYPE(v)->tp_setattr != NULL)
        return (*Py_TYPE(v)->tp_setattr)(v, (char*)name, w);
    s = PyString_InternFromString(name);
    if (s == NULL)
        return -1;
    res = PyObject_SetAttr(v, s, w);
    Py_XDECREF(s);
    return res;
}

PyObject *
PyObject_GetAttr(PyObject *v, PyObject *name)
{
    PyTypeObject *tp = Py_TYPE(v);

    if (!PyString_Check(name)) {
#ifdef Py_USING_UNICODE

        if (PyUnicode_Check(name)) {
            name = _PyUnicode_AsDefaultEncodedString(name, NULL);
            if (name == NULL)
                return NULL;
        }
        else
#endif
        {
            PyErr_Format(PyExc_TypeError,
                         "attribute name must be string, not '%.200s'",
                         Py_TYPE(name)->tp_name);
            return NULL;
        }
    }
    if (tp->tp_getattro != NULL)
        return (*tp->tp_getattro)(v, name);
    if (tp->tp_getattr != NULL)
        return (*tp->tp_getattr)(v, PyString_AS_STRING(name));
    PyErr_Format(PyExc_AttributeError,
                 "'%.50s' object has no attribute '%.400s'",
                 tp->tp_name, PyString_AS_STRING(name));
    return NULL;
}

int
PyObject_HasAttr(PyObject *v, PyObject *name)
{
    PyObject *res = PyObject_GetAttr(v, name);
    if (res != NULL) {
        Py_DECREF(res);
        return 1;
    }
    PyErr_Clear();
    return 0;
}

int
PyObject_SetAttr(PyObject *v, PyObject *name, PyObject *value)
{
    PyTypeObject *tp = Py_TYPE(v);
    int err;

    if (!PyString_Check(name)){
#ifdef Py_USING_UNICODE

        if (PyUnicode_Check(name)) {
            name = PyUnicode_AsEncodedString(name, NULL, NULL);
            if (name == NULL)
                return -1;
        }
        else
#endif
        {
            PyErr_Format(PyExc_TypeError,
                         "attribute name must be string, not '%.200s'",
                         Py_TYPE(name)->tp_name);
            return -1;
        }
    }
    else
        Py_INCREF(name);

    PyString_InternInPlace(&name);
    if (tp->tp_setattro != NULL) {
        err = (*tp->tp_setattro)(v, name, value);
        Py_DECREF(name);
        return err;
    }
    if (tp->tp_setattr != NULL) {
        err = (*tp->tp_setattr)(v, PyString_AS_STRING(name), value);
        Py_DECREF(name);
        return err;
    }
    Py_DECREF(name);
    if (tp->tp_getattr == NULL && tp->tp_getattro == NULL)
        PyErr_Format(PyExc_TypeError,
                     "'%.100s' object has no attributes "
                     "(%s .%.100s)",
                     tp->tp_name,
                     value==NULL ? "del" : "assign to",
                     PyString_AS_STRING(name));
    else
        PyErr_Format(PyExc_TypeError,
                     "'%.100s' object has only read-only attributes "
                     "(%s .%.100s)",
                     tp->tp_name,
                     value==NULL ? "del" : "assign to",
                     PyString_AS_STRING(name));
    return -1;
}

PyObject **
_PyObject_GetDictPtr(PyObject *obj)
{
    Py_ssize_t dictoffset;
    PyTypeObject *tp = Py_TYPE(obj);

    if (!(tp->tp_flags & Py_TPFLAGS_HAVE_CLASS))
        return NULL;
    dictoffset = tp->tp_dictoffset;
    if (dictoffset == 0)
        return NULL;
    if (dictoffset < 0) {
        Py_ssize_t tsize;
        size_t size;

        tsize = ((PyVarObject *)obj)->ob_size;
        if (tsize < 0)
            tsize = -tsize;
        size = _PyObject_VAR_SIZE(tp, tsize);

        dictoffset += (long)size;
        assert(dictoffset > 0);
        assert(dictoffset % SIZEOF_VOID_P == 0);
    }
    return (PyObject **) ((char *)obj + dictoffset);
}

PyObject *
PyObject_SelfIter(PyObject *obj)
{
    Py_INCREF(obj);
    return obj;
}

PyObject *
_PyObject_NextNotImplemented(PyObject *self)
{
    PyErr_Format(PyExc_TypeError,
                 "'%.200s' object is not iterable",
                 Py_TYPE(self)->tp_name);
    return NULL;
}

PyObject *
_PyObject_GenericGetAttrWithDict(PyObject *obj, PyObject *name, PyObject *dict)
{
    PyTypeObject *tp = Py_TYPE(obj);
    PyObject *descr = NULL;
    PyObject *res = NULL;
    descrgetfunc f;
    Py_ssize_t dictoffset;
    PyObject **dictptr;

    if (!PyString_Check(name)){
#ifdef Py_USING_UNICODE

        if (PyUnicode_Check(name)) {
            name = PyUnicode_AsEncodedString(name, NULL, NULL);
            if (name == NULL)
                return NULL;
        }
        else
#endif
        {
            PyErr_Format(PyExc_TypeError,
                         "attribute name must be string, not '%.200s'",
                         Py_TYPE(name)->tp_name);
            return NULL;
        }
    }
    else
        Py_INCREF(name);

    if (tp->tp_dict == NULL) {
        if (PyType_Ready(tp) < 0)
            goto done;
    }

#if 0

    {
        Py_ssize_t i, n;
        PyObject *mro, *base, *dict;

        mro = tp->tp_mro;
        assert(mro != NULL);
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
            descr = PyDict_GetItem(dict, name);
            if (descr != NULL)
                break;
        }
    }
#else
    descr = _PyType_Lookup(tp, name);
#endif

    Py_XINCREF(descr);

    f = NULL;
    if (descr != NULL &&
        PyType_HasFeature(descr->ob_type, Py_TPFLAGS_HAVE_CLASS)) {
        f = descr->ob_type->tp_descr_get;
        if (f != NULL && PyDescr_IsData(descr)) {
            res = f(descr, obj, (PyObject *)obj->ob_type);
            Py_DECREF(descr);
            goto done;
        }
    }

    if (dict == NULL) {

        dictoffset = tp->tp_dictoffset;
        if (dictoffset != 0) {
            if (dictoffset < 0) {
                Py_ssize_t tsize;
                size_t size;

                tsize = ((PyVarObject *)obj)->ob_size;
                if (tsize < 0)
                    tsize = -tsize;
                size = _PyObject_VAR_SIZE(tp, tsize);

                dictoffset += (long)size;
                assert(dictoffset > 0);
                assert(dictoffset % SIZEOF_VOID_P == 0);
            }
            dictptr = (PyObject **) ((char *)obj + dictoffset);
            dict = *dictptr;
        }
    }
    if (dict != NULL) {
        Py_INCREF(dict);
        res = PyDict_GetItem(dict, name);
        if (res != NULL) {
            Py_INCREF(res);
            Py_XDECREF(descr);
            Py_DECREF(dict);
            goto done;
        }
        Py_DECREF(dict);
    }

    if (f != NULL) {
        res = f(descr, obj, (PyObject *)Py_TYPE(obj));
        Py_DECREF(descr);
        goto done;
    }

    if (descr != NULL) {
        res = descr;

        goto done;
    }

    PyErr_Format(PyExc_AttributeError,
                 "'%.50s' object has no attribute '%.400s'",
                 tp->tp_name, PyString_AS_STRING(name));
  done:
    Py_DECREF(name);
    return res;
}

PyObject *
PyObject_GenericGetAttr(PyObject *obj, PyObject *name)
{
    return _PyObject_GenericGetAttrWithDict(obj, name, NULL);
}

int
_PyObject_GenericSetAttrWithDict(PyObject *obj, PyObject *name,
                                 PyObject *value, PyObject *dict)
{
    PyTypeObject *tp = Py_TYPE(obj);
    PyObject *descr;
    descrsetfunc f;
    PyObject **dictptr;
    int res = -1;

    if (!PyString_Check(name)){
#ifdef Py_USING_UNICODE

        if (PyUnicode_Check(name)) {
            name = PyUnicode_AsEncodedString(name, NULL, NULL);
            if (name == NULL)
                return -1;
        }
        else
#endif
        {
            PyErr_Format(PyExc_TypeError,
                         "attribute name must be string, not '%.200s'",
                         Py_TYPE(name)->tp_name);
            return -1;
        }
    }
    else
        Py_INCREF(name);

    if (tp->tp_dict == NULL) {
        if (PyType_Ready(tp) < 0)
            goto done;
    }

    descr = _PyType_Lookup(tp, name);
    f = NULL;
    if (descr != NULL &&
        PyType_HasFeature(descr->ob_type, Py_TPFLAGS_HAVE_CLASS)) {
        f = descr->ob_type->tp_descr_set;
        if (f != NULL && PyDescr_IsData(descr)) {
            res = f(descr, obj, value);
            goto done;
        }
    }

    if (dict == NULL) {
        dictptr = _PyObject_GetDictPtr(obj);
        if (dictptr != NULL) {
            dict = *dictptr;
            if (dict == NULL && value != NULL) {
                dict = PyDict_New();
                if (dict == NULL)
                    goto done;
                *dictptr = dict;
            }
        }
    }
    if (dict != NULL) {
        Py_INCREF(dict);
        if (value == NULL)
            res = PyDict_DelItem(dict, name);
        else
            res = PyDict_SetItem(dict, name, value);
        if (res < 0 && PyErr_ExceptionMatches(PyExc_KeyError))
            PyErr_SetObject(PyExc_AttributeError, name);
        Py_DECREF(dict);
        goto done;
    }

    if (f != NULL) {
        res = f(descr, obj, value);
        goto done;
    }

    if (descr == NULL) {
        PyErr_Format(PyExc_AttributeError,
                     "'%.100s' object has no attribute '%.200s'",
                     tp->tp_name, PyString_AS_STRING(name));
        goto done;
    }

    PyErr_Format(PyExc_AttributeError,
                 "'%.50s' object attribute '%.400s' is read-only",
                 tp->tp_name, PyString_AS_STRING(name));
  done:
    Py_DECREF(name);
    return res;
}

int
PyObject_GenericSetAttr(PyObject *obj, PyObject *name, PyObject *value)
{
    return _PyObject_GenericSetAttrWithDict(obj, name, value, NULL);
}

int
PyObject_IsTrue(PyObject *v)
{
    Py_ssize_t res;
    if (v == Py_True)
        return 1;
    if (v == Py_False)
        return 0;
    if (v == Py_None)
        return 0;
    else if (v->ob_type->tp_as_number != NULL &&
             v->ob_type->tp_as_number->nb_nonzero != NULL)
        res = (*v->ob_type->tp_as_number->nb_nonzero)(v);
    else if (v->ob_type->tp_as_mapping != NULL &&
             v->ob_type->tp_as_mapping->mp_length != NULL)
        res = (*v->ob_type->tp_as_mapping->mp_length)(v);
    else if (v->ob_type->tp_as_sequence != NULL &&
             v->ob_type->tp_as_sequence->sq_length != NULL)
        res = (*v->ob_type->tp_as_sequence->sq_length)(v);
    else
        return 1;

    return (res > 0) ? 1 : Py_SAFE_DOWNCAST(res, Py_ssize_t, int);
}

int
PyObject_Not(PyObject *v)
{
    int res;
    res = PyObject_IsTrue(v);
    if (res < 0)
        return res;
    return res == 0;
}

int
PyNumber_CoerceEx(PyObject **pv, PyObject **pw)
{
    register PyObject *v = *pv;
    register PyObject *w = *pw;
    int res;

    if (v->ob_type == w->ob_type &&
        !PyType_HasFeature(v->ob_type, Py_TPFLAGS_CHECKTYPES))
    {
        Py_INCREF(v);
        Py_INCREF(w);
        return 0;
    }
    if (v->ob_type->tp_as_number && v->ob_type->tp_as_number->nb_coerce) {
        res = (*v->ob_type->tp_as_number->nb_coerce)(pv, pw);
        if (res <= 0)
            return res;
    }
    if (w->ob_type->tp_as_number && w->ob_type->tp_as_number->nb_coerce) {
        res = (*w->ob_type->tp_as_number->nb_coerce)(pw, pv);
        if (res <= 0)
            return res;
    }
    return 1;
}

int
PyNumber_Coerce(PyObject **pv, PyObject **pw)
{
    int err = PyNumber_CoerceEx(pv, pw);
    if (err <= 0)
        return err;
    PyErr_SetString(PyExc_TypeError, "number coercion failed");
    return -1;
}

int
PyCallable_Check(PyObject *x)
{
    if (x == NULL)
        return 0;
    if (PyInstance_Check(x)) {
        PyObject *call = PyObject_GetAttrString(x, "__call__");
        if (call == NULL) {
            PyErr_Clear();
            return 0;
        }

        Py_DECREF(call);
        return 1;
    }
    else {
        return x->ob_type->tp_call != NULL;
    }
}

static int
merge_class_dict(PyObject* dict, PyObject* aclass)
{
    PyObject *classdict;
    PyObject *bases;

    assert(PyDict_Check(dict));
    assert(aclass);

    classdict = PyObject_GetAttrString(aclass, "__dict__");
    if (classdict == NULL)
        PyErr_Clear();
    else {
        int status = PyDict_Update(dict, classdict);
        Py_DECREF(classdict);
        if (status < 0)
            return -1;
    }

    bases = PyObject_GetAttrString(aclass, "__bases__");
    if (bases == NULL)
        PyErr_Clear();
    else {

        Py_ssize_t i, n;
        n = PySequence_Size(bases);
        if (n < 0)
            PyErr_Clear();
        else {
            for (i = 0; i < n; i++) {
                int status;
                PyObject *base = PySequence_GetItem(bases, i);
                if (base == NULL) {
                    Py_DECREF(bases);
                    return -1;
                }
                status = merge_class_dict(dict, base);
                Py_DECREF(base);
                if (status < 0) {
                    Py_DECREF(bases);
                    return -1;
                }
            }
        }
        Py_DECREF(bases);
    }
    return 0;
}

static int
merge_list_attr(PyObject* dict, PyObject* obj, const char *attrname)
{
    PyObject *list;
    int result = 0;

    assert(PyDict_Check(dict));
    assert(obj);
    assert(attrname);

    list = PyObject_GetAttrString(obj, attrname);
    if (list == NULL)
        PyErr_Clear();

    else if (PyList_Check(list)) {
        int i;
        for (i = 0; i < PyList_GET_SIZE(list); ++i) {
            PyObject *item = PyList_GET_ITEM(list, i);
            if (PyString_Check(item)) {
                result = PyDict_SetItem(dict, item, Py_None);
                if (result < 0)
                    break;
            }
        }
        if (Py_Py3kWarningFlag &&
            (strcmp(attrname, "__members__") == 0 ||
             strcmp(attrname, "__methods__") == 0)) {
            if (PyErr_WarnEx(PyExc_DeprecationWarning,
                           "__members__ and __methods__ not "
                           "supported in 3.x", 1) < 0) {
                Py_XDECREF(list);
                return -1;
            }
        }
    }

    Py_XDECREF(list);
    return result;
}

static PyObject *
_dir_locals(void)
{
    PyObject *names;
    PyObject *locals = PyEval_GetLocals();

    if (locals == NULL) {
        PyErr_SetString(PyExc_SystemError, "frame does not exist");
        return NULL;
    }

    names = PyMapping_Keys(locals);
    if (!names)
        return NULL;
    if (!PyList_Check(names)) {
        PyErr_Format(PyExc_TypeError,
            "dir(): expected keys() of locals to be a list, "
            "not '%.200s'", Py_TYPE(names)->tp_name);
        Py_DECREF(names);
        return NULL;
    }

    return names;
}

static PyObject *
_specialized_dir_type(PyObject *obj)
{
    PyObject *result = NULL;
    PyObject *dict = PyDict_New();

    if (dict != NULL && merge_class_dict(dict, obj) == 0)
        result = PyDict_Keys(dict);

    Py_XDECREF(dict);
    return result;
}

static PyObject *
_specialized_dir_module(PyObject *obj)
{
    PyObject *result = NULL;
    PyObject *dict = PyObject_GetAttrString(obj, "__dict__");

    if (dict != NULL) {
        if (PyDict_Check(dict))
            result = PyDict_Keys(dict);
        else {
            char *name = PyModule_GetName(obj);
            if (name)
                PyErr_Format(PyExc_TypeError,
                             "%.200s.__dict__ is not a dictionary",
                             name);
        }
    }

    Py_XDECREF(dict);
    return result;
}

static PyObject *
_generic_dir(PyObject *obj)
{
    PyObject *result = NULL;
    PyObject *dict = NULL;
    PyObject *itsclass = NULL;

    dict = PyObject_GetAttrString(obj, "__dict__");
    if (dict == NULL) {
        PyErr_Clear();
        dict = PyDict_New();
    }
    else if (!PyDict_Check(dict)) {
        Py_DECREF(dict);
        dict = PyDict_New();
    }
    else {

        PyObject *temp = PyDict_Copy(dict);
        Py_DECREF(dict);
        dict = temp;
    }

    if (dict == NULL)
        goto error;

    if (merge_list_attr(dict, obj, "__members__") < 0)
        goto error;
    if (merge_list_attr(dict, obj, "__methods__") < 0)
        goto error;

    itsclass = PyObject_GetAttrString(obj, "__class__");
    if (itsclass == NULL)

        PyErr_Clear();
    else {
        if (merge_class_dict(dict, itsclass) != 0)
            goto error;
    }

    result = PyDict_Keys(dict);

error:
    Py_XDECREF(itsclass);
    Py_XDECREF(dict);
    return result;
}

static PyObject *
_dir_object(PyObject *obj)
{
    PyObject *result = NULL;
    static PyObject *dir_str = NULL;
    PyObject *dirfunc;

    assert(obj);
    if (PyInstance_Check(obj)) {
        dirfunc = PyObject_GetAttrString(obj, "__dir__");
        if (dirfunc == NULL) {
            if (PyErr_ExceptionMatches(PyExc_AttributeError))
                PyErr_Clear();
            else
                return NULL;
        }
    }
    else {
        dirfunc = _PyObject_LookupSpecial(obj, "__dir__", &dir_str);
        if (PyErr_Occurred())
            return NULL;
    }
    if (dirfunc == NULL) {

        if (PyModule_Check(obj))
            result = _specialized_dir_module(obj);
        else if (PyType_Check(obj) || PyClass_Check(obj))
            result = _specialized_dir_type(obj);
        else
            result = _generic_dir(obj);
    }
    else {

        result = PyObject_CallFunctionObjArgs(dirfunc, NULL);
        Py_DECREF(dirfunc);
        if (result == NULL)
            return NULL;

        if (!PyList_Check(result)) {
            PyErr_Format(PyExc_TypeError,
                         "__dir__() must return a list, not %.200s",
                         Py_TYPE(result)->tp_name);
            Py_DECREF(result);
            result = NULL;
        }
    }

    return result;
}

PyObject *
PyObject_Dir(PyObject *obj)
{
    PyObject * result;

    if (obj == NULL)

        result = _dir_locals();
    else

        result = _dir_object(obj);

    assert(result == NULL || PyList_Check(result));

    if (result != NULL && PyList_Sort(result) != 0) {

        Py_DECREF(result);
        result = NULL;
    }

    return result;
}

static PyObject *
none_repr(PyObject *op)
{
    return PyString_FromString("None");
}

static void
none_dealloc(PyObject* ignore)
{

    Py_FatalError("deallocating None");
}

static PyTypeObject PyNone_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "NoneType",
    0,
    0,
    none_dealloc,
    0,
    0,
    0,
    0,
    none_repr,
    0,
    0,
    0,
    (hashfunc)_Py_HashPointer,
};

PyObject _Py_NoneStruct = {
  _PyObject_EXTRA_INIT
  1, &PyNone_Type
};

static PyObject *
NotImplemented_repr(PyObject *op)
{
    return PyString_FromString("NotImplemented");
}

static PyTypeObject PyNotImplemented_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "NotImplementedType",
    0,
    0,
    none_dealloc,
    0,
    0,
    0,
    0,
    NotImplemented_repr,
    0,
    0,
    0,
    0,
};

PyObject _Py_NotImplementedStruct = {
    _PyObject_EXTRA_INIT
    1, &PyNotImplemented_Type
};

void
_Py_ReadyTypes(void)
{
    if (PyType_Ready(&PyType_Type) < 0)
        Py_FatalError("Can't initialize type type");

    if (PyType_Ready(&_PyWeakref_RefType) < 0)
        Py_FatalError("Can't initialize weakref type");

    if (PyType_Ready(&_PyWeakref_CallableProxyType) < 0)
        Py_FatalError("Can't initialize callable weakref proxy type");

    if (PyType_Ready(&_PyWeakref_ProxyType) < 0)
        Py_FatalError("Can't initialize weakref proxy type");

    if (PyType_Ready(&PyBool_Type) < 0)
        Py_FatalError("Can't initialize bool type");

    if (PyType_Ready(&PyString_Type) < 0)
        Py_FatalError("Can't initialize str type");

    if (PyType_Ready(&PyByteArray_Type) < 0)
        Py_FatalError("Can't initialize bytearray type");

    if (PyType_Ready(&PyList_Type) < 0)
        Py_FatalError("Can't initialize list type");

    if (PyType_Ready(&PyNone_Type) < 0)
        Py_FatalError("Can't initialize None type");

    if (PyType_Ready(&PyNotImplemented_Type) < 0)
        Py_FatalError("Can't initialize NotImplemented type");

    if (PyType_Ready(&PyTraceBack_Type) < 0)
        Py_FatalError("Can't initialize traceback type");

    if (PyType_Ready(&PySuper_Type) < 0)
        Py_FatalError("Can't initialize super type");

    if (PyType_Ready(&PyBaseObject_Type) < 0)
        Py_FatalError("Can't initialize object type");

    if (PyType_Ready(&PyRange_Type) < 0)
        Py_FatalError("Can't initialize xrange type");

    if (PyType_Ready(&PyDict_Type) < 0)
        Py_FatalError("Can't initialize dict type");

    if (PyType_Ready(&PySet_Type) < 0)
        Py_FatalError("Can't initialize set type");

#ifdef Py_USING_UNICODE
    if (PyType_Ready(&PyUnicode_Type) < 0)
        Py_FatalError("Can't initialize unicode type");
#endif

    if (PyType_Ready(&PySlice_Type) < 0)
        Py_FatalError("Can't initialize slice type");

    if (PyType_Ready(&PyStaticMethod_Type) < 0)
        Py_FatalError("Can't initialize static method type");

#ifndef WITHOUT_COMPLEX
    if (PyType_Ready(&PyComplex_Type) < 0)
        Py_FatalError("Can't initialize complex type");
#endif

    if (PyType_Ready(&PyFloat_Type) < 0)
        Py_FatalError("Can't initialize float type");

    if (PyType_Ready(&PyBuffer_Type) < 0)
        Py_FatalError("Can't initialize buffer type");

    if (PyType_Ready(&PyLong_Type) < 0)
        Py_FatalError("Can't initialize long type");

    if (PyType_Ready(&PyInt_Type) < 0)
        Py_FatalError("Can't initialize int type");

    if (PyType_Ready(&PyFrozenSet_Type) < 0)
        Py_FatalError("Can't initialize frozenset type");

    if (PyType_Ready(&PyProperty_Type) < 0)
        Py_FatalError("Can't initialize property type");

    if (PyType_Ready(&PyMemoryView_Type) < 0)
        Py_FatalError("Can't initialize memoryview type");

    if (PyType_Ready(&PyTuple_Type) < 0)
        Py_FatalError("Can't initialize tuple type");

    if (PyType_Ready(&PyEnum_Type) < 0)
        Py_FatalError("Can't initialize enumerate type");

    if (PyType_Ready(&PyReversed_Type) < 0)
        Py_FatalError("Can't initialize reversed type");

    if (PyType_Ready(&PyCode_Type) < 0)
        Py_FatalError("Can't initialize code type");

    if (PyType_Ready(&PyFrame_Type) < 0)
        Py_FatalError("Can't initialize frame type");

    if (PyType_Ready(&PyCFunction_Type) < 0)
        Py_FatalError("Can't initialize builtin function type");

    if (PyType_Ready(&PyMethod_Type) < 0)
        Py_FatalError("Can't initialize method type");

    if (PyType_Ready(&PyFunction_Type) < 0)
        Py_FatalError("Can't initialize function type");

    if (PyType_Ready(&PyClass_Type) < 0)
        Py_FatalError("Can't initialize class type");

    if (PyType_Ready(&PyDictProxy_Type) < 0)
        Py_FatalError("Can't initialize dict proxy type");

    if (PyType_Ready(&PyGen_Type) < 0)
        Py_FatalError("Can't initialize generator type");

    if (PyType_Ready(&PyGetSetDescr_Type) < 0)
        Py_FatalError("Can't initialize get-set descriptor type");

    if (PyType_Ready(&PyWrapperDescr_Type) < 0)
        Py_FatalError("Can't initialize wrapper type");

    if (PyType_Ready(&PyInstance_Type) < 0)
        Py_FatalError("Can't initialize instance type");

    if (PyType_Ready(&PyEllipsis_Type) < 0)
        Py_FatalError("Can't initialize ellipsis type");

    if (PyType_Ready(&PyMemberDescr_Type) < 0)
        Py_FatalError("Can't initialize member descriptor type");

    if (PyType_Ready(&PyFile_Type) < 0)
        Py_FatalError("Can't initialize file type");

    if (PyType_Ready(&PyCapsule_Type) < 0)
        Py_FatalError("Can't initialize capsule type");

    if (PyType_Ready(&PyCell_Type) < 0)
        Py_FatalError("Can't initialize cell type");

    if (PyType_Ready(&PyCallIter_Type) < 0)
        Py_FatalError("Can't initialize call iter type");

    if (PyType_Ready(&PySeqIter_Type) < 0)
        Py_FatalError("Can't initialize sequence iterator type");
}

#ifdef Py_TRACE_REFS

void
_Py_NewReference(PyObject *op)
{
    _Py_INC_REFTOTAL;
    op->ob_refcnt = 1;
    _Py_AddToAllObjects(op, 1);
    _Py_INC_TPALLOCS(op);
}

void
_Py_ForgetReference(register PyObject *op)
{
#ifdef SLOW_UNREF_CHECK
    register PyObject *p;
#endif
    if (op->ob_refcnt < 0)
        Py_FatalError("UNREF negative refcnt");
    if (op == &refchain ||
        op->_ob_prev->_ob_next != op || op->_ob_next->_ob_prev != op)
        Py_FatalError("UNREF invalid object");
#ifdef SLOW_UNREF_CHECK
    for (p = refchain._ob_next; p != &refchain; p = p->_ob_next) {
        if (p == op)
            break;
    }
    if (p == &refchain)
        Py_FatalError("UNREF unknown object");
#endif
    op->_ob_next->_ob_prev = op->_ob_prev;
    op->_ob_prev->_ob_next = op->_ob_next;
    op->_ob_next = op->_ob_prev = NULL;
    _Py_INC_TPFREES(op);
}

void
_Py_Dealloc(PyObject *op)
{
    destructor dealloc = Py_TYPE(op)->tp_dealloc;
    _Py_ForgetReference(op);
    (*dealloc)(op);
}

void
_Py_PrintReferences(FILE *fp)
{
    PyObject *op;
    fprintf(fp, "Remaining objects:\n");
    for (op = refchain._ob_next; op != &refchain; op = op->_ob_next) {
        fprintf(fp, "%p [%" PY_FORMAT_SIZE_T "d] ", op, op->ob_refcnt);
        if (PyObject_Print(op, fp, 0) != 0)
            PyErr_Clear();
        putc('\n', fp);
    }
}

void
_Py_PrintReferenceAddresses(FILE *fp)
{
    PyObject *op;
    fprintf(fp, "Remaining object addresses:\n");
    for (op = refchain._ob_next; op != &refchain; op = op->_ob_next)
        fprintf(fp, "%p [%" PY_FORMAT_SIZE_T "d] %s\n", op,
            op->ob_refcnt, Py_TYPE(op)->tp_name);
}

PyObject *
_Py_GetObjects(PyObject *self, PyObject *args)
{
    int i, n;
    PyObject *t = NULL;
    PyObject *res, *op;

    if (!PyArg_ParseTuple(args, "i|O", &n, &t))
        return NULL;
    op = refchain._ob_next;
    res = PyList_New(0);
    if (res == NULL)
        return NULL;
    for (i = 0; (n == 0 || i < n) && op != &refchain; i++) {
        while (op == self || op == args || op == res || op == t ||
               (t != NULL && Py_TYPE(op) != (PyTypeObject *) t)) {
            op = op->_ob_next;
            if (op == &refchain)
                return res;
        }
        if (PyList_Append(res, op) < 0) {
            Py_DECREF(res);
            return NULL;
        }
        op = op->_ob_next;
    }
    return res;
}

#endif

PyTypeObject *_Py_capsule_hack = &PyCapsule_Type;

PyTypeObject *_Py_cobject_hack = &PyCObject_Type;

Py_ssize_t (*_Py_abstract_hack)(PyObject *) = PyObject_Size;

void *
PyMem_Malloc(size_t nbytes)
{
    return PyMem_MALLOC(nbytes);
}

void *
PyMem_Realloc(void *p, size_t nbytes)
{
    return PyMem_REALLOC(p, nbytes);
}

void
PyMem_Free(void *p)
{
    PyMem_FREE(p);
}

#define KEY "Py_Repr"

int
Py_ReprEnter(PyObject *obj)
{
    PyObject *dict;
    PyObject *list;
    Py_ssize_t i;

    dict = PyThreadState_GetDict();
    if (dict == NULL)
        return 0;
    list = PyDict_GetItemString(dict, KEY);
    if (list == NULL) {
        list = PyList_New(0);
        if (list == NULL)
            return -1;
        if (PyDict_SetItemString(dict, KEY, list) < 0)
            return -1;
        Py_DECREF(list);
    }
    i = PyList_GET_SIZE(list);
    while (--i >= 0) {
        if (PyList_GET_ITEM(list, i) == obj)
            return 1;
    }
    PyList_Append(list, obj);
    return 0;
}

void
Py_ReprLeave(PyObject *obj)
{
    PyObject *dict;
    PyObject *list;
    Py_ssize_t i;

    dict = PyThreadState_GetDict();
    if (dict == NULL)
        return;
    list = PyDict_GetItemString(dict, KEY);
    if (list == NULL || !PyList_Check(list))
        return;
    i = PyList_GET_SIZE(list);

    while (--i >= 0) {
        if (PyList_GET_ITEM(list, i) == obj) {
            PyList_SetSlice(list, i, i + 1, NULL);
            break;
        }
    }
}

int _PyTrash_delete_nesting = 0;

PyObject *_PyTrash_delete_later = NULL;

void
_PyTrash_deposit_object(PyObject *op)
{
    assert(PyObject_IS_GC(op));
    assert(_Py_AS_GC(op)->gc.gc_refs == _PyGC_REFS_UNTRACKED);
    assert(op->ob_refcnt == 0);
    _Py_AS_GC(op)->gc.gc_prev = (PyGC_Head *)_PyTrash_delete_later;
    _PyTrash_delete_later = op;
}

void
_PyTrash_thread_deposit_object(PyObject *op)
{
    PyThreadState *tstate = PyThreadState_GET();
    assert(PyObject_IS_GC(op));
    assert(_Py_AS_GC(op)->gc.gc_refs == _PyGC_REFS_UNTRACKED);
    assert(op->ob_refcnt == 0);
    _Py_AS_GC(op)->gc.gc_prev = (PyGC_Head *) tstate->trash_delete_later;
    tstate->trash_delete_later = op;
}

void
_PyTrash_destroy_chain(void)
{
    while (_PyTrash_delete_later) {
        PyObject *op = _PyTrash_delete_later;
        destructor dealloc = Py_TYPE(op)->tp_dealloc;

        _PyTrash_delete_later =
            (PyObject*) _Py_AS_GC(op)->gc.gc_prev;

        assert(op->ob_refcnt == 0);
        ++_PyTrash_delete_nesting;
        (*dealloc)(op);
        --_PyTrash_delete_nesting;
    }
}

void
_PyTrash_thread_destroy_chain(void)
{
    PyThreadState *tstate = PyThreadState_GET();
    while (tstate->trash_delete_later) {
        PyObject *op = tstate->trash_delete_later;
        destructor dealloc = Py_TYPE(op)->tp_dealloc;

        tstate->trash_delete_later =
            (PyObject*) _Py_AS_GC(op)->gc.gc_prev;

        assert(op->ob_refcnt == 0);
        ++tstate->trash_delete_nesting;
        (*dealloc)(op);
        --tstate->trash_delete_nesting;
    }
}

#ifdef __cplusplus
}
#endif
