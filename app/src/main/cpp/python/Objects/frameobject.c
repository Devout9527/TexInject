#include "Python.h"

#include "code.h"
#include "frameobject.h"
#include "opcode.h"
#include "structmember.h"

#undef MIN
#undef MAX
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define OFF(x) offsetof(PyFrameObject, x)

static PyMemberDef frame_memberlist[] = {
    {"f_back",          T_OBJECT,       OFF(f_back),    RO},
    {"f_code",          T_OBJECT,       OFF(f_code),    RO},
    {"f_builtins",      T_OBJECT,       OFF(f_builtins),RO},
    {"f_globals",       T_OBJECT,       OFF(f_globals), RO},
    {"f_lasti",         T_INT,          OFF(f_lasti),   RO},
    {NULL}
};

#define WARN_GET_SET(NAME) \
static PyObject * frame_get_ ## NAME(PyFrameObject *f) { \
    if (PyErr_WarnPy3k(#NAME " has been removed in 3.x", 2) < 0) \
        return NULL; \
    if (f->NAME) { \
        Py_INCREF(f->NAME); \
        return f->NAME; \
    } \
    Py_RETURN_NONE;     \
} \
static int frame_set_ ## NAME(PyFrameObject *f, PyObject *new) { \
    if (PyErr_WarnPy3k(#NAME " has been removed in 3.x", 2) < 0) \
        return -1; \
    if (f->NAME) { \
        Py_CLEAR(f->NAME); \
    } \
    if (new == Py_None) \
        new = NULL; \
    Py_XINCREF(new); \
    f->NAME = new; \
    return 0; \
}

WARN_GET_SET(f_exc_traceback)
WARN_GET_SET(f_exc_type)
WARN_GET_SET(f_exc_value)

static PyObject *
frame_getlocals(PyFrameObject *f, void *closure)
{
    PyFrame_FastToLocals(f);
    Py_INCREF(f->f_locals);
    return f->f_locals;
}

int
PyFrame_GetLineNumber(PyFrameObject *f)
{
    if (f->f_trace)
        return f->f_lineno;
    else
        return PyCode_Addr2Line(f->f_code, f->f_lasti);
}

static PyObject *
frame_getlineno(PyFrameObject *f, void *closure)
{
    return PyInt_FromLong(PyFrame_GetLineNumber(f));
}

static int
frame_setlineno(PyFrameObject *f, PyObject* p_new_lineno)
{
    int new_lineno = 0;
    int new_lasti = 0;
    int new_iblock = 0;
    unsigned char *code = NULL;
    Py_ssize_t code_len = 0;
    unsigned char *lnotab = NULL;
    Py_ssize_t lnotab_len = 0;
    int offset = 0;
    int line = 0;
    int addr = 0;
    int min_addr = 0;
    int max_addr = 0;
    int delta_iblock = 0;
    int min_delta_iblock = 0;
    int min_iblock = 0;
    int f_lasti_setup_addr = 0;
    int new_lasti_setup_addr = 0;
    int blockstack[CO_MAXBLOCKS];
    int in_finally[CO_MAXBLOCKS];
    int blockstack_top = 0;
    unsigned char setup_op = 0;

    if (!PyInt_Check(p_new_lineno)) {
        PyErr_SetString(PyExc_ValueError,
                        "lineno must be an integer");
        return -1;
    }

    if (!f->f_trace)
    {
        PyErr_Format(PyExc_ValueError,
                     "f_lineno can only be set by a"
                     " line trace function");
        return -1;
    }

    new_lineno = (int) PyInt_AsLong(p_new_lineno);
    if (new_lineno < f->f_code->co_firstlineno) {
        PyErr_Format(PyExc_ValueError,
                     "line %d comes before the current code block",
                     new_lineno);
        return -1;
    }
    else if (new_lineno == f->f_code->co_firstlineno) {
        new_lasti = 0;
        new_lineno = f->f_code->co_firstlineno;
    }
    else {

        char *tmp;
        PyString_AsStringAndSize(f->f_code->co_lnotab,
                                 &tmp, &lnotab_len);
        lnotab = (unsigned char *) tmp;
        addr = 0;
        line = f->f_code->co_firstlineno;
        new_lasti = -1;
        for (offset = 0; offset < lnotab_len; offset += 2) {
            addr += lnotab[offset];
            line += lnotab[offset+1];
            if (line >= new_lineno) {
                new_lasti = addr;
                new_lineno = line;
                break;
            }
        }
    }

    if (new_lasti == -1) {
        PyErr_Format(PyExc_ValueError,
                     "line %d comes after the current code block",
                     new_lineno);
        return -1;
    }

    PyString_AsStringAndSize(f->f_code->co_code, (char **)&code, &code_len);
    min_addr = MIN(new_lasti, f->f_lasti);
    max_addr = MAX(new_lasti, f->f_lasti);

    if (code[new_lasti] == DUP_TOP || code[new_lasti] == POP_TOP) {
        PyErr_SetString(PyExc_ValueError,
            "can't jump to 'except' line as there's no exception");
        return -1;
    }

    f_lasti_setup_addr = -1;
    new_lasti_setup_addr = -1;
    memset(blockstack, '\0', sizeof(blockstack));
    memset(in_finally, '\0', sizeof(in_finally));
    blockstack_top = 0;
    for (addr = 0; addr < code_len; addr++) {
        unsigned char op = code[addr];
        switch (op) {
        case SETUP_LOOP:
        case SETUP_EXCEPT:
        case SETUP_FINALLY:
        case SETUP_WITH:
            blockstack[blockstack_top++] = addr;
            in_finally[blockstack_top-1] = 0;
            break;

        case POP_BLOCK:
            assert(blockstack_top > 0);
            setup_op = code[blockstack[blockstack_top-1]];
            if (setup_op == SETUP_FINALLY || setup_op == SETUP_WITH) {
                in_finally[blockstack_top-1] = 1;
            }
            else {
                blockstack_top--;
            }
            break;

        case END_FINALLY:

            if (blockstack_top > 0) {
                setup_op = code[blockstack[blockstack_top-1]];
                if (setup_op == SETUP_FINALLY || setup_op == SETUP_WITH) {
                    blockstack_top--;
                }
            }
            break;
        }

        if (addr == new_lasti || addr == f->f_lasti) {
            int i = 0;
            int setup_addr = -1;
            for (i = blockstack_top-1; i >= 0; i--) {
                if (in_finally[i]) {
                    setup_addr = blockstack[i];
                    break;
                }
            }

            if (setup_addr != -1) {
                if (addr == new_lasti) {
                    new_lasti_setup_addr = setup_addr;
                }

                if (addr == f->f_lasti) {
                    f_lasti_setup_addr = setup_addr;
                }
            }
        }

        if (op >= HAVE_ARGUMENT) {
            addr += 2;
        }
    }

    assert(blockstack_top == 0);

    if (new_lasti_setup_addr != f_lasti_setup_addr) {
        PyErr_SetString(PyExc_ValueError,
                    "can't jump into or out of a 'finally' block");
        return -1;
    }

    delta_iblock = 0;
    for (addr = min_addr; addr < max_addr; addr++) {
        unsigned char op = code[addr];
        switch (op) {
        case SETUP_LOOP:
        case SETUP_EXCEPT:
        case SETUP_FINALLY:
        case SETUP_WITH:
            delta_iblock++;
            break;

        case POP_BLOCK:
            delta_iblock--;
            break;
        }

        min_delta_iblock = MIN(min_delta_iblock, delta_iblock);

        if (op >= HAVE_ARGUMENT) {
            addr += 2;
        }
    }

    min_iblock = f->f_iblock + min_delta_iblock;
    if (new_lasti > f->f_lasti) {

        new_iblock = f->f_iblock + delta_iblock;
    }
    else {

        new_iblock = f->f_iblock - delta_iblock;
    }

    if (new_iblock > min_iblock) {
        PyErr_SetString(PyExc_ValueError,
                        "can't jump into the middle of a block");
        return -1;
    }

    while (f->f_iblock > new_iblock) {
        PyTryBlock *b = &f->f_blockstack[--f->f_iblock];
        while ((f->f_stacktop - f->f_valuestack) > b->b_level) {
            PyObject *v = (*--f->f_stacktop);
            Py_DECREF(v);
        }
    }

    f->f_lineno = new_lineno;
    f->f_lasti = new_lasti;
    return 0;
}

static PyObject *
frame_gettrace(PyFrameObject *f, void *closure)
{
    PyObject* trace = f->f_trace;

    if (trace == NULL)
        trace = Py_None;

    Py_INCREF(trace);

    return trace;
}

static int
frame_settrace(PyFrameObject *f, PyObject* v, void *closure)
{

    f->f_lineno = PyFrame_GetLineNumber(f);

    if (v == Py_None)
        v = NULL;
    Py_XINCREF(v);
    Py_XSETREF(f->f_trace, v);

    return 0;
}

static PyObject *
frame_getrestricted(PyFrameObject *f, void *closure)
{
    return PyBool_FromLong(PyFrame_IsRestricted(f));
}

static PyGetSetDef frame_getsetlist[] = {
    {"f_locals",        (getter)frame_getlocals, NULL, NULL},
    {"f_lineno",        (getter)frame_getlineno,
                    (setter)frame_setlineno, NULL},
    {"f_trace",         (getter)frame_gettrace, (setter)frame_settrace, NULL},
    {"f_restricted",(getter)frame_getrestricted,NULL, NULL},
    {"f_exc_traceback", (getter)frame_get_f_exc_traceback,
                    (setter)frame_set_f_exc_traceback, NULL},
    {"f_exc_type",  (getter)frame_get_f_exc_type,
                    (setter)frame_set_f_exc_type, NULL},
    {"f_exc_value", (getter)frame_get_f_exc_value,
                    (setter)frame_set_f_exc_value, NULL},
    {0}
};

static PyFrameObject *free_list = NULL;
static int numfree = 0;

#define PyFrame_MAXFREELIST 200

static void
frame_dealloc(PyFrameObject *f)
{
    PyObject **p, **valuestack;
    PyCodeObject *co;

    PyObject_GC_UnTrack(f);
    Py_TRASHCAN_SAFE_BEGIN(f)

    valuestack = f->f_valuestack;
    for (p = f->f_localsplus; p < valuestack; p++)
        Py_CLEAR(*p);

    if (f->f_stacktop != NULL) {
        for (p = valuestack; p < f->f_stacktop; p++)
            Py_XDECREF(*p);
    }

    Py_XDECREF(f->f_back);
    Py_DECREF(f->f_builtins);
    Py_DECREF(f->f_globals);
    Py_CLEAR(f->f_locals);
    Py_CLEAR(f->f_trace);
    Py_CLEAR(f->f_exc_type);
    Py_CLEAR(f->f_exc_value);
    Py_CLEAR(f->f_exc_traceback);

    co = f->f_code;
    if (co->co_zombieframe == NULL)
        co->co_zombieframe = f;
    else if (numfree < PyFrame_MAXFREELIST) {
        ++numfree;
        f->f_back = free_list;
        free_list = f;
    }
    else
        PyObject_GC_Del(f);

    Py_DECREF(co);
    Py_TRASHCAN_SAFE_END(f)
}

static int
frame_traverse(PyFrameObject *f, visitproc visit, void *arg)
{
    PyObject **fastlocals, **p;
    int i, slots;

    Py_VISIT(f->f_back);
    Py_VISIT(f->f_code);
    Py_VISIT(f->f_builtins);
    Py_VISIT(f->f_globals);
    Py_VISIT(f->f_locals);
    Py_VISIT(f->f_trace);
    Py_VISIT(f->f_exc_type);
    Py_VISIT(f->f_exc_value);
    Py_VISIT(f->f_exc_traceback);

    slots = f->f_code->co_nlocals + PyTuple_GET_SIZE(f->f_code->co_cellvars) + PyTuple_GET_SIZE(f->f_code->co_freevars);
    fastlocals = f->f_localsplus;
    for (i = slots; --i >= 0; ++fastlocals)
        Py_VISIT(*fastlocals);

    if (f->f_stacktop != NULL) {
        for (p = f->f_valuestack; p < f->f_stacktop; p++)
            Py_VISIT(*p);
    }
    return 0;
}

static void
frame_clear(PyFrameObject *f)
{
    PyObject **fastlocals, **p, **oldtop;
    int i, slots;

    oldtop = f->f_stacktop;
    f->f_stacktop = NULL;

    Py_CLEAR(f->f_exc_type);
    Py_CLEAR(f->f_exc_value);
    Py_CLEAR(f->f_exc_traceback);
    Py_CLEAR(f->f_trace);

    slots = f->f_code->co_nlocals + PyTuple_GET_SIZE(f->f_code->co_cellvars) + PyTuple_GET_SIZE(f->f_code->co_freevars);
    fastlocals = f->f_localsplus;
    for (i = slots; --i >= 0; ++fastlocals)
        Py_CLEAR(*fastlocals);

    if (oldtop != NULL) {
        for (p = f->f_valuestack; p < oldtop; p++)
            Py_CLEAR(*p);
    }
}

static PyObject *
frame_sizeof(PyFrameObject *f)
{
    Py_ssize_t res, extras, ncells, nfrees;

    ncells = PyTuple_GET_SIZE(f->f_code->co_cellvars);
    nfrees = PyTuple_GET_SIZE(f->f_code->co_freevars);
    extras = f->f_code->co_stacksize + f->f_code->co_nlocals +
             ncells + nfrees;

    res = sizeof(PyFrameObject) + (extras-1) * sizeof(PyObject *);

    return PyInt_FromSsize_t(res);
}

PyDoc_STRVAR(sizeof__doc__,
"F.__sizeof__() -> size of F in memory, in bytes");

static PyMethodDef frame_methods[] = {
    {"__sizeof__",      (PyCFunction)frame_sizeof,      METH_NOARGS,
     sizeof__doc__},
    {NULL,              NULL}
};

PyTypeObject PyFrame_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "frame",
    sizeof(PyFrameObject),
    sizeof(PyObject *),
    (destructor)frame_dealloc,
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
    PyObject_GenericSetAttr,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    0,
    (traverseproc)frame_traverse,
    (inquiry)frame_clear,
    0,
    0,
    0,
    0,
    frame_methods,
    frame_memberlist,
    frame_getsetlist,
    0,
    0,
};

static PyObject *builtin_object;

int _PyFrame_Init()
{
    builtin_object = PyString_InternFromString("__builtins__");
    if (builtin_object == NULL)
        return 0;
    return 1;
}

PyFrameObject *
PyFrame_New(PyThreadState *tstate, PyCodeObject *code, PyObject *globals,
            PyObject *locals)
{
    PyFrameObject *back = tstate->frame;
    PyFrameObject *f;
    PyObject *builtins;
    Py_ssize_t i;

#ifdef Py_DEBUG
    if (code == NULL || globals == NULL || !PyDict_Check(globals) ||
        (locals != NULL && !PyMapping_Check(locals))) {
        PyErr_BadInternalCall();
        return NULL;
    }
#endif
    if (back == NULL || back->f_globals != globals) {
        builtins = PyDict_GetItem(globals, builtin_object);
        if (builtins) {
            if (PyModule_Check(builtins)) {
                builtins = PyModule_GetDict(builtins);
                assert(!builtins || PyDict_Check(builtins));
            }
            else if (!PyDict_Check(builtins))
                builtins = NULL;
        }
        if (builtins == NULL) {

            builtins = PyDict_New();
            if (builtins == NULL ||
                PyDict_SetItemString(
                    builtins, "None", Py_None) < 0)
                return NULL;
        }
        else
            Py_INCREF(builtins);

    }
    else {

        builtins = back->f_builtins;
        assert(builtins != NULL && PyDict_Check(builtins));
        Py_INCREF(builtins);
    }
    if (code->co_zombieframe != NULL) {
        f = code->co_zombieframe;
        code->co_zombieframe = NULL;
        _Py_NewReference((PyObject *)f);
        assert(f->f_code == code);
    }
    else {
        Py_ssize_t extras, ncells, nfrees;
        ncells = PyTuple_GET_SIZE(code->co_cellvars);
        nfrees = PyTuple_GET_SIZE(code->co_freevars);
        extras = code->co_stacksize + code->co_nlocals + ncells +
            nfrees;
        if (free_list == NULL) {
            f = PyObject_GC_NewVar(PyFrameObject, &PyFrame_Type,
            extras);
            if (f == NULL) {
                Py_DECREF(builtins);
                return NULL;
            }
        }
        else {
            assert(numfree > 0);
            --numfree;
            f = free_list;
            free_list = free_list->f_back;
            if (Py_SIZE(f) < extras) {
                f = PyObject_GC_Resize(PyFrameObject, f, extras);
                if (f == NULL) {
                    Py_DECREF(builtins);
                    return NULL;
                }
            }
            _Py_NewReference((PyObject *)f);
        }

        f->f_code = code;
        extras = code->co_nlocals + ncells + nfrees;
        f->f_valuestack = f->f_localsplus + extras;
        for (i=0; i<extras; i++)
            f->f_localsplus[i] = NULL;
        f->f_locals = NULL;
        f->f_trace = NULL;
        f->f_exc_type = f->f_exc_value = f->f_exc_traceback = NULL;
    }
    f->f_stacktop = f->f_valuestack;
    f->f_builtins = builtins;
    Py_XINCREF(back);
    f->f_back = back;
    Py_INCREF(code);
    Py_INCREF(globals);
    f->f_globals = globals;

    if ((code->co_flags & (CO_NEWLOCALS | CO_OPTIMIZED)) ==
        (CO_NEWLOCALS | CO_OPTIMIZED))
        ;
    else if (code->co_flags & CO_NEWLOCALS) {
        locals = PyDict_New();
        if (locals == NULL) {
            Py_DECREF(f);
            return NULL;
        }
        f->f_locals = locals;
    }
    else {
        if (locals == NULL)
            locals = globals;
        Py_INCREF(locals);
        f->f_locals = locals;
    }
    f->f_tstate = tstate;

    f->f_lasti = -1;
    f->f_lineno = code->co_firstlineno;
    f->f_iblock = 0;

    _PyObject_GC_TRACK(f);
    return f;
}

void
PyFrame_BlockSetup(PyFrameObject *f, int type, int handler, int level)
{
    PyTryBlock *b;
    if (f->f_iblock >= CO_MAXBLOCKS)
        Py_FatalError("XXX block stack overflow");
    b = &f->f_blockstack[f->f_iblock++];
    b->b_type = type;
    b->b_level = level;
    b->b_handler = handler;
}

PyTryBlock *
PyFrame_BlockPop(PyFrameObject *f)
{
    PyTryBlock *b;
    if (f->f_iblock <= 0)
        Py_FatalError("XXX block stack underflow");
    b = &f->f_blockstack[--f->f_iblock];
    return b;
}

static void
map_to_dict(PyObject *map, Py_ssize_t nmap, PyObject *dict, PyObject **values,
            int deref)
{
    Py_ssize_t j;
    assert(PyTuple_Check(map));
    assert(PyDict_Check(dict));
    assert(PyTuple_Size(map) >= nmap);
    for (j = nmap; --j >= 0; ) {
        PyObject *key = PyTuple_GET_ITEM(map, j);
        PyObject *value = values[j];
        assert(PyString_Check(key));
        if (deref) {
            assert(PyCell_Check(value));
            value = PyCell_GET(value);
        }
        if (value == NULL) {
            if (PyObject_DelItem(dict, key) != 0)
                PyErr_Clear();
        }
        else {
            if (PyObject_SetItem(dict, key, value) != 0)
                PyErr_Clear();
        }
    }
}

static void
dict_to_map(PyObject *map, Py_ssize_t nmap, PyObject *dict, PyObject **values,
            int deref, int clear)
{
    Py_ssize_t j;
    assert(PyTuple_Check(map));
    assert(PyDict_Check(dict));
    assert(PyTuple_Size(map) >= nmap);
    for (j = nmap; --j >= 0; ) {
        PyObject *key = PyTuple_GET_ITEM(map, j);
        PyObject *value = PyObject_GetItem(dict, key);
        assert(PyString_Check(key));

        if (value == NULL) {
            PyErr_Clear();
            if (!clear)
                continue;
        }
        if (deref) {
            assert(PyCell_Check(values[j]));
            if (PyCell_GET(values[j]) != value) {
                if (PyCell_Set(values[j], value) < 0)
                    PyErr_Clear();
            }
        } else if (values[j] != value) {
            Py_XINCREF(value);
            Py_XSETREF(values[j], value);
        }
        Py_XDECREF(value);
    }
}

void
PyFrame_FastToLocals(PyFrameObject *f)
{

    PyObject *locals, *map;
    PyObject **fast;
    PyObject *error_type, *error_value, *error_traceback;
    PyCodeObject *co;
    Py_ssize_t j;
    int ncells, nfreevars;
    if (f == NULL)
        return;
    locals = f->f_locals;
    if (locals == NULL) {
        locals = f->f_locals = PyDict_New();
        if (locals == NULL) {
            PyErr_Clear();
            return;
        }
    }
    co = f->f_code;
    map = co->co_varnames;
    if (!PyTuple_Check(map))
        return;
    PyErr_Fetch(&error_type, &error_value, &error_traceback);
    fast = f->f_localsplus;
    j = PyTuple_GET_SIZE(map);
    if (j > co->co_nlocals)
        j = co->co_nlocals;
    if (co->co_nlocals)
        map_to_dict(map, j, locals, fast, 0);
    ncells = PyTuple_GET_SIZE(co->co_cellvars);
    nfreevars = PyTuple_GET_SIZE(co->co_freevars);
    if (ncells || nfreevars) {
        map_to_dict(co->co_cellvars, ncells,
                    locals, fast + co->co_nlocals, 1);

        if (co->co_flags & CO_OPTIMIZED) {
            map_to_dict(co->co_freevars, nfreevars,
                        locals, fast + co->co_nlocals + ncells, 1);
        }
    }
    PyErr_Restore(error_type, error_value, error_traceback);
}

void
PyFrame_LocalsToFast(PyFrameObject *f, int clear)
{

    PyObject *locals, *map;
    PyObject **fast;
    PyObject *error_type, *error_value, *error_traceback;
    PyCodeObject *co;
    Py_ssize_t j;
    int ncells, nfreevars;
    if (f == NULL)
        return;
    locals = f->f_locals;
    co = f->f_code;
    map = co->co_varnames;
    if (locals == NULL)
        return;
    if (!PyTuple_Check(map))
        return;
    PyErr_Fetch(&error_type, &error_value, &error_traceback);
    fast = f->f_localsplus;
    j = PyTuple_GET_SIZE(map);
    if (j > co->co_nlocals)
        j = co->co_nlocals;
    if (co->co_nlocals)
        dict_to_map(co->co_varnames, j, locals, fast, 0, clear);
    ncells = PyTuple_GET_SIZE(co->co_cellvars);
    nfreevars = PyTuple_GET_SIZE(co->co_freevars);
    if (ncells || nfreevars) {
        dict_to_map(co->co_cellvars, ncells,
                    locals, fast + co->co_nlocals, 1, clear);

        if (co->co_flags & CO_OPTIMIZED) {
            dict_to_map(co->co_freevars, nfreevars,
                locals, fast + co->co_nlocals + ncells, 1,
                clear);
        }
    }
    PyErr_Restore(error_type, error_value, error_traceback);
}

int
PyFrame_ClearFreeList(void)
{
    int freelist_size = numfree;

    while (free_list != NULL) {
        PyFrameObject *f = free_list;
        free_list = free_list->f_back;
        PyObject_GC_Del(f);
        --numfree;
    }
    assert(numfree == 0);
    return freelist_size;
}

void
PyFrame_Fini(void)
{
    (void)PyFrame_ClearFreeList();
    Py_XDECREF(builtin_object);
    builtin_object = NULL;
}
