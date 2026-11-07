#include "Python.h"
#include "structmember.h"

#ifndef WITH_THREAD
#error "Error!  The rest of Python is not compiled with thread support."
#error "Rerun configure, adding a --with-threads option."
#error "Then run `make clean' followed by `make'."
#endif

#include "pythread.h"

static PyObject *ThreadError;
static PyObject *str_dict;
static long nb_threads = 0;

typedef struct {
    PyObject_HEAD
    PyThread_type_lock lock_lock;
    PyObject *in_weakreflist;
} lockobject;

static void
lock_dealloc(lockobject *self)
{
    if (self->in_weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) self);
    if (self->lock_lock != NULL) {

        PyThread_acquire_lock(self->lock_lock, 0);
        PyThread_release_lock(self->lock_lock);

        PyThread_free_lock(self->lock_lock);
    }
    PyObject_Del(self);
}

static PyObject *
lock_PyThread_acquire_lock(lockobject *self, PyObject *args)
{
    int i = 1;

    if (!PyArg_ParseTuple(args, "|i:acquire", &i))
        return NULL;

    Py_BEGIN_ALLOW_THREADS
    i = PyThread_acquire_lock(self->lock_lock, i);
    Py_END_ALLOW_THREADS

    return PyBool_FromLong((long)i);
}

PyDoc_STRVAR(acquire_doc,
"acquire([wait]) -> bool\n\
(acquire_lock() is an obsolete synonym)\n\
\n\
Lock the lock.  Without argument, this blocks if the lock is already\n\
locked (even by the same thread), waiting for another thread to release\n\
the lock, and return True once the lock is acquired.\n\
With an argument, this will only block if the argument is true,\n\
and the return value reflects whether the lock is acquired.\n\
The blocking operation is not interruptible.");

static PyObject *
lock_PyThread_release_lock(lockobject *self)
{

    if (PyThread_acquire_lock(self->lock_lock, 0)) {
        PyThread_release_lock(self->lock_lock);
        PyErr_SetString(ThreadError, "release unlocked lock");
        return NULL;
    }

    PyThread_release_lock(self->lock_lock);
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(release_doc,
"release()\n\
(release_lock() is an obsolete synonym)\n\
\n\
Release the lock, allowing another thread that is blocked waiting for\n\
the lock to acquire the lock.  The lock must be in the locked state,\n\
but it needn't be locked by the same thread that unlocks it.");

static PyObject *
lock_locked_lock(lockobject *self)
{
    if (PyThread_acquire_lock(self->lock_lock, 0)) {
        PyThread_release_lock(self->lock_lock);
        return PyBool_FromLong(0L);
    }
    return PyBool_FromLong(1L);
}

PyDoc_STRVAR(locked_doc,
"locked() -> bool\n\
(locked_lock() is an obsolete synonym)\n\
\n\
Return whether the lock is in the locked state.");

static PyMethodDef lock_methods[] = {
    {"acquire_lock", (PyCFunction)lock_PyThread_acquire_lock,
     METH_VARARGS, acquire_doc},
    {"acquire",      (PyCFunction)lock_PyThread_acquire_lock,
     METH_VARARGS, acquire_doc},
    {"release_lock", (PyCFunction)lock_PyThread_release_lock,
     METH_NOARGS, release_doc},
    {"release",      (PyCFunction)lock_PyThread_release_lock,
     METH_NOARGS, release_doc},
    {"locked_lock",  (PyCFunction)lock_locked_lock,
     METH_NOARGS, locked_doc},
    {"locked",       (PyCFunction)lock_locked_lock,
     METH_NOARGS, locked_doc},
    {"__enter__",    (PyCFunction)lock_PyThread_acquire_lock,
     METH_VARARGS, acquire_doc},
    {"__exit__",    (PyCFunction)lock_PyThread_release_lock,
     METH_VARARGS, release_doc},
    {NULL}
};

static PyTypeObject Locktype = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "thread.lock",
    sizeof(lockobject),
    0,

    (destructor)lock_dealloc,
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
    Py_TPFLAGS_HAVE_WEAKREFS,
    0,
    0,
    0,
    0,
    offsetof(lockobject, in_weakreflist),
    0,
    0,
    lock_methods,
};

static lockobject *
newlockobject(void)
{
    lockobject *self;
    self = PyObject_New(lockobject, &Locktype);
    if (self == NULL)
        return NULL;
    self->lock_lock = PyThread_allocate_lock();
    self->in_weakreflist = NULL;
    if (self->lock_lock == NULL) {
        Py_DECREF(self);
        PyErr_SetString(ThreadError, "can't allocate lock");
        return NULL;
    }
    return self;
}

#include "structmember.h"

typedef struct {
    PyObject_HEAD
    PyObject *localdict;
    PyObject *weakreflist;
} localdummyobject;

static void
localdummy_dealloc(localdummyobject *self)
{
    if (self->weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) self);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject localdummytype = {
    PyVarObject_HEAD_INIT(NULL, 0)
     "_thread._localdummy",
     sizeof(localdummyobject),
     0,
     (destructor)localdummy_dealloc,
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
     Py_TPFLAGS_DEFAULT,
     "Thread-local dummy",
     0,
     0,
     0,
     offsetof(localdummyobject, weakreflist)
};

typedef struct {
    PyObject_HEAD
    PyObject *key;
    PyObject *args;
    PyObject *kw;
    PyObject *weakreflist;

    PyObject *dummies;

    PyObject *wr_callback;
} localobject;

static PyObject *_ldict(localobject *self);
static PyObject *_localdummy_destroyed(PyObject *meth_self, PyObject *dummyweakref);

static PyObject *
_local_create_dummy(localobject *self)
{
    PyObject *tdict, *ldict = NULL, *wr = NULL;
    localdummyobject *dummy = NULL;
    int r;

    tdict = PyThreadState_GetDict();
    if (tdict == NULL) {
        PyErr_SetString(PyExc_SystemError,
                        "Couldn't get thread-state dictionary");
        goto err;
    }

    ldict = PyDict_New();
    if (ldict == NULL)
        goto err;
    dummy = (localdummyobject *) localdummytype.tp_alloc(&localdummytype, 0);
    if (dummy == NULL)
        goto err;
    dummy->localdict = ldict;
    wr = PyWeakref_NewRef((PyObject *) dummy, self->wr_callback);
    if (wr == NULL)
        goto err;

    r = PyDict_SetItem(self->dummies, wr, ldict);
    if (r < 0)
        goto err;
    Py_CLEAR(wr);
    r = PyDict_SetItem(tdict, self->key, (PyObject *) dummy);
    if (r < 0)
        goto err;
    Py_CLEAR(dummy);

    Py_DECREF(ldict);
    return ldict;

err:
    Py_XDECREF(ldict);
    Py_XDECREF(wr);
    Py_XDECREF(dummy);
    return NULL;
}

static PyObject *
local_new(PyTypeObject *type, PyObject *args, PyObject *kw)
{
    localobject *self;
    PyObject *wr;
    static PyMethodDef wr_callback_def = {
        "_localdummy_destroyed", (PyCFunction) _localdummy_destroyed, METH_O
    };

    if (type->tp_init == PyBaseObject_Type.tp_init
        && ((args && PyObject_IsTrue(args))
        || (kw && PyObject_IsTrue(kw)))) {
        PyErr_SetString(PyExc_TypeError,
                  "Initialization arguments are not supported");
        return NULL;
    }

    self = (localobject *)type->tp_alloc(type, 0);
    if (self == NULL)
        return NULL;

    Py_XINCREF(args);
    self->args = args;
    Py_XINCREF(kw);
    self->kw = kw;
    self->key = PyString_FromFormat("thread.local.%p", self);
    if (self->key == NULL)
        goto err;

    self->dummies = PyDict_New();
    if (self->dummies == NULL)
        goto err;

    wr = PyWeakref_NewRef((PyObject *) self, NULL);
    if (wr == NULL)
        goto err;
    self->wr_callback = PyCFunction_New(&wr_callback_def, wr);
    Py_DECREF(wr);
    if (self->wr_callback == NULL)
        goto err;

    if (_local_create_dummy(self) == NULL)
        goto err;

    return (PyObject *)self;

  err:
    Py_DECREF(self);
    return NULL;
}

static int
local_traverse(localobject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->args);
    Py_VISIT(self->kw);
    Py_VISIT(self->dummies);
    return 0;
}

static int
local_clear(localobject *self)
{
    PyThreadState *tstate;
    Py_CLEAR(self->args);
    Py_CLEAR(self->kw);
    Py_CLEAR(self->dummies);
    Py_CLEAR(self->wr_callback);

    if (self->key
        && (tstate = PyThreadState_Get())
        && tstate->interp) {
        for(tstate = PyInterpreterState_ThreadHead(tstate->interp);
            tstate;
            tstate = PyThreadState_Next(tstate))
            if (tstate->dict &&
                PyDict_GetItem(tstate->dict, self->key))
                PyDict_DelItem(tstate->dict, self->key);
    }
    return 0;
}

static void
local_dealloc(localobject *self)
{

    if (self->weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) self);

    PyObject_GC_UnTrack(self);

    local_clear(self);
    Py_XDECREF(self->key);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject *
_ldict(localobject *self)
{
    PyObject *tdict, *ldict, *dummy;

    tdict = PyThreadState_GetDict();
    if (tdict == NULL) {
        PyErr_SetString(PyExc_SystemError,
                        "Couldn't get thread-state dictionary");
        return NULL;
    }

    dummy = PyDict_GetItem(tdict, self->key);
    if (dummy == NULL) {
        ldict = _local_create_dummy(self);
        if (ldict == NULL)
            return NULL;

        if (Py_TYPE(self)->tp_init != PyBaseObject_Type.tp_init &&
            Py_TYPE(self)->tp_init((PyObject*)self,
                                   self->args, self->kw) < 0) {

            PyDict_DelItem(tdict, self->key);
            return NULL;
        }
    }
    else {
        assert(Py_TYPE(dummy) == &localdummytype);
        ldict = ((localdummyobject *) dummy)->localdict;
    }

    return ldict;
}

static int
local_setattro(localobject *self, PyObject *name, PyObject *v)
{
    PyObject *ldict;
    int r;

    ldict = _ldict(self);
    if (ldict == NULL)
        return -1;

    r = PyObject_RichCompareBool(name, str_dict, Py_EQ);
    if (r == 1) {
        PyErr_Format(PyExc_AttributeError,
                     "'%.50s' object attribute '__dict__' is read-only",
                     Py_TYPE(self)->tp_name);
        return -1;
    }
    if (r == -1)
        return -1;

    return _PyObject_GenericSetAttrWithDict((PyObject *)self, name, v, ldict);
}

static PyObject *local_getattro(localobject *, PyObject *);

static PyTypeObject localtype = {
    PyVarObject_HEAD_INIT(NULL, 0)
     "thread._local",
     sizeof(localobject),
     0,
     (destructor)local_dealloc,
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
     (getattrofunc)local_getattro,
     (setattrofunc)local_setattro,
     0,
     Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE
                                               | Py_TPFLAGS_HAVE_GC,
     "Thread-local data",
     (traverseproc)local_traverse,
     (inquiry)local_clear,
     0,
     offsetof(localobject, weakreflist),
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
     local_new,
     0,
     0,
};

static PyObject *
local_getattro(localobject *self, PyObject *name)
{
    PyObject *ldict, *value;
    int r;

    ldict = _ldict(self);
    if (ldict == NULL)
        return NULL;

    r = PyObject_RichCompareBool(name, str_dict, Py_EQ);
    if (r == 1) {
        Py_INCREF(ldict);
        return ldict;
    }
    if (r == -1)
        return NULL;

    if (Py_TYPE(self) != &localtype)

        return _PyObject_GenericGetAttrWithDict((PyObject *)self, name, ldict);

    value = PyDict_GetItem(ldict, name);
    if (value == NULL)

        return _PyObject_GenericGetAttrWithDict((PyObject *)self, name, ldict);

    Py_INCREF(value);
    return value;
}

static PyObject *
_localdummy_destroyed(PyObject *localweakref, PyObject *dummyweakref)
{
    PyObject *obj;
    localobject *self;
    assert(PyWeakref_CheckRef(localweakref));
    obj = PyWeakref_GET_OBJECT(localweakref);
    if (obj == Py_None)
        Py_RETURN_NONE;
    Py_INCREF(obj);
    assert(PyObject_TypeCheck(obj, &localtype));

    self = (localobject *) obj;
    if (self->dummies != NULL) {
        PyObject *ldict;
        ldict = PyDict_GetItem(self->dummies, dummyweakref);
        if (ldict != NULL) {
            PyDict_DelItem(self->dummies, dummyweakref);
        }
        if (PyErr_Occurred())
            PyErr_WriteUnraisable(obj);
    }
    Py_DECREF(obj);
    Py_RETURN_NONE;
}

struct bootstate {
    PyInterpreterState *interp;
    PyObject *func;
    PyObject *args;
    PyObject *keyw;
    PyThreadState *tstate;
};

static void
t_bootstrap(void *boot_raw)
{
    struct bootstate *boot = (struct bootstate *) boot_raw;
    PyThreadState *tstate;
    PyObject *res;

    tstate = boot->tstate;
    tstate->thread_id = PyThread_get_thread_ident();
    _PyThreadState_Init(tstate);
    PyEval_AcquireThread(tstate);
    nb_threads++;
    res = PyEval_CallObjectWithKeywords(
        boot->func, boot->args, boot->keyw);
    if (res == NULL) {
        if (PyErr_ExceptionMatches(PyExc_SystemExit))
            PyErr_Clear();
        else {
            PyObject *file;
            PyObject *exc, *value, *tb;
            PyErr_Fetch(&exc, &value, &tb);
            PySys_WriteStderr(
                "Unhandled exception in thread started by ");
            file = PySys_GetObject("stderr");
            if (file)
                PyFile_WriteObject(boot->func, file, 0);
            else
                PyObject_Print(boot->func, stderr, 0);
            PySys_WriteStderr("\n");
            PyErr_Restore(exc, value, tb);
            PyErr_PrintEx(0);
        }
    }
    else
        Py_DECREF(res);
    Py_DECREF(boot->func);
    Py_DECREF(boot->args);
    Py_XDECREF(boot->keyw);
    PyMem_DEL(boot_raw);
    nb_threads--;
    PyThreadState_Clear(tstate);
    PyThreadState_DeleteCurrent();
    PyThread_exit_thread();
}

static PyObject *
thread_PyThread_start_new_thread(PyObject *self, PyObject *fargs)
{
    PyObject *func, *args, *keyw = NULL;
    struct bootstate *boot;
    long ident;

    if (!PyArg_UnpackTuple(fargs, "start_new_thread", 2, 3,
                           &func, &args, &keyw))
        return NULL;
    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError,
                        "first arg must be callable");
        return NULL;
    }
    if (!PyTuple_Check(args)) {
        PyErr_SetString(PyExc_TypeError,
                        "2nd arg must be a tuple");
        return NULL;
    }
    if (keyw != NULL && !PyDict_Check(keyw)) {
        PyErr_SetString(PyExc_TypeError,
                        "optional 3rd arg must be a dictionary");
        return NULL;
    }
    boot = PyMem_NEW(struct bootstate, 1);
    if (boot == NULL)
        return PyErr_NoMemory();
    boot->interp = PyThreadState_GET()->interp;
    boot->func = func;
    boot->args = args;
    boot->keyw = keyw;
    boot->tstate = _PyThreadState_Prealloc(boot->interp);
    if (boot->tstate == NULL) {
        PyMem_DEL(boot);
        return PyErr_NoMemory();
    }
    Py_INCREF(func);
    Py_INCREF(args);
    Py_XINCREF(keyw);
    PyEval_InitThreads();
    ident = PyThread_start_new_thread(t_bootstrap, (void*) boot);
    if (ident == -1) {
        PyErr_SetString(ThreadError, "can't start new thread");
        Py_DECREF(func);
        Py_DECREF(args);
        Py_XDECREF(keyw);
        PyThreadState_Clear(boot->tstate);
        PyMem_DEL(boot);
        return NULL;
    }
    return PyInt_FromLong(ident);
}

PyDoc_STRVAR(start_new_doc,
"start_new_thread(function, args[, kwargs])\n\
(start_new() is an obsolete synonym)\n\
\n\
Start a new thread and return its identifier.  The thread will call the\n\
function with positional arguments from the tuple args and keyword arguments\n\
taken from the optional dictionary kwargs.  The thread exits when the\n\
function returns; the return value is ignored.  The thread will also exit\n\
when the function raises an unhandled exception; a stack trace will be\n\
printed unless the exception is SystemExit.\n");

static PyObject *
thread_PyThread_exit_thread(PyObject *self)
{
    PyErr_SetNone(PyExc_SystemExit);
    return NULL;
}

PyDoc_STRVAR(exit_doc,
"exit()\n\
(exit_thread() is an obsolete synonym)\n\
\n\
This is synonymous to ``raise SystemExit''.  It will cause the current\n\
thread to exit silently unless the exception is caught.");

static PyObject *
thread_PyThread_interrupt_main(PyObject * self)
{
    PyErr_SetInterrupt();
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(interrupt_doc,
"interrupt_main()\n\
\n\
Raise a KeyboardInterrupt in the main thread.\n\
A subthread can use this function to interrupt the main thread."
);

static lockobject *newlockobject(void);

static PyObject *
thread_PyThread_allocate_lock(PyObject *self)
{
    return (PyObject *) newlockobject();
}

PyDoc_STRVAR(allocate_doc,
"allocate_lock() -> lock object\n\
(allocate() is an obsolete synonym)\n\
\n\
Create a new lock object.  See help(LockType) for information about locks.");

static PyObject *
thread_get_ident(PyObject *self)
{
    long ident;
    ident = PyThread_get_thread_ident();
    if (ident == -1) {
        PyErr_SetString(ThreadError, "no current thread ident");
        return NULL;
    }
    return PyInt_FromLong(ident);
}

PyDoc_STRVAR(get_ident_doc,
"get_ident() -> integer\n\
\n\
Return a non-zero integer that uniquely identifies the current thread\n\
amongst other threads that exist simultaneously.\n\
This may be used to identify per-thread resources.\n\
Even though on some platforms threads identities may appear to be\n\
allocated consecutive numbers starting at 1, this behavior should not\n\
be relied upon, and the number should be seen purely as a magic cookie.\n\
A thread's identity may be reused for another thread after it exits.");

static PyObject *
thread__count(PyObject *self)
{
    return PyInt_FromLong(nb_threads);
}

PyDoc_STRVAR(_count_doc,
"_count() -> integer\n\
\n\
\
Return the number of currently running Python threads, excluding \n\
the main thread. The returned number comprises all threads created\n\
through `start_new_thread()` as well as `threading.Thread`, and not\n\
yet finished.\n\
\n\
This function is meant for internal and specialized purposes only.\n\
In most applications `threading.enumerate()` should be used instead.");

static PyObject *
thread_stack_size(PyObject *self, PyObject *args)
{
    size_t old_size;
    Py_ssize_t new_size = 0;
    int rc;

    if (!PyArg_ParseTuple(args, "|n:stack_size", &new_size))
        return NULL;

    if (new_size < 0) {
        PyErr_SetString(PyExc_ValueError,
                        "size must be 0 or a positive value");
        return NULL;
    }

    old_size = PyThread_get_stacksize();

    rc = PyThread_set_stacksize((size_t) new_size);
    if (rc == -1) {
        PyErr_Format(PyExc_ValueError,
                     "size not valid: %zd bytes",
                     new_size);
        return NULL;
    }
    if (rc == -2) {
        PyErr_SetString(ThreadError,
                        "setting stack size not supported");
        return NULL;
    }

    return PyInt_FromSsize_t((Py_ssize_t) old_size);
}

PyDoc_STRVAR(stack_size_doc,
"stack_size([size]) -> size\n\
\n\
Return the thread stack size used when creating new threads.  The\n\
optional size argument specifies the stack size (in bytes) to be used\n\
for subsequently created threads, and must be 0 (use platform or\n\
configured default) or a positive integer value of at least 32,768 (32k).\n\
If changing the thread stack size is unsupported, a ThreadError\n\
exception is raised.  If the specified size is invalid, a ValueError\n\
exception is raised, and the stack size is unmodified.  32k bytes\n\
 currently the minimum supported stack size value to guarantee\n\
sufficient stack space for the interpreter itself.\n\
\n\
Note that some platforms may have particular restrictions on values for\n\
the stack size, such as requiring a minimum stack size larger than 32kB or\n\
requiring allocation in multiples of the system memory page size\n\
- platform documentation should be referred to for more information\n\
(4kB pages are common; using multiples of 4096 for the stack size is\n\
the suggested approach in the absence of more specific information).");

static PyMethodDef thread_methods[] = {
    {"start_new_thread",        (PyCFunction)thread_PyThread_start_new_thread,
                            METH_VARARGS,
                            start_new_doc},
    {"start_new",               (PyCFunction)thread_PyThread_start_new_thread,
                            METH_VARARGS,
                            start_new_doc},
    {"allocate_lock",           (PyCFunction)thread_PyThread_allocate_lock,
     METH_NOARGS, allocate_doc},
    {"allocate",                (PyCFunction)thread_PyThread_allocate_lock,
     METH_NOARGS, allocate_doc},
    {"exit_thread",             (PyCFunction)thread_PyThread_exit_thread,
     METH_NOARGS, exit_doc},
    {"exit",                    (PyCFunction)thread_PyThread_exit_thread,
     METH_NOARGS, exit_doc},
    {"interrupt_main",          (PyCFunction)thread_PyThread_interrupt_main,
     METH_NOARGS, interrupt_doc},
    {"get_ident",               (PyCFunction)thread_get_ident,
     METH_NOARGS, get_ident_doc},
    {"_count",                  (PyCFunction)thread__count,
     METH_NOARGS, _count_doc},
    {"stack_size",              (PyCFunction)thread_stack_size,
                            METH_VARARGS,
                            stack_size_doc},
    {NULL,                      NULL}
};

PyDoc_STRVAR(thread_doc,
"This module provides primitive operations to write multi-threaded programs.\n\
The 'threading' module provides a more convenient interface.");

PyDoc_STRVAR(lock_doc,
"A lock object is a synchronization primitive.  To create a lock,\n\
call the PyThread_allocate_lock() function.  Methods are:\n\
\n\
acquire() -- lock the lock, possibly blocking until it can be obtained\n\
release() -- unlock of the lock\n\
locked() -- test whether the lock is currently locked\n\
\n\
A lock is not owned by the thread that locked it; another thread may\n\
unlock it.  A thread attempting to lock a lock that it has already locked\n\
will block until another thread unlocks it.  Deadlocks may ensue.");

PyMODINIT_FUNC
initthread(void)
{
    PyObject *m, *d;

    if (PyType_Ready(&localdummytype) < 0)
        return;
    if (PyType_Ready(&localtype) < 0)
        return;

    m = Py_InitModule3("thread", thread_methods, thread_doc);
    if (m == NULL)
        return;

    d = PyModule_GetDict(m);
    ThreadError = PyErr_NewException("thread.error", NULL, NULL);
    PyDict_SetItemString(d, "error", ThreadError);
    Locktype.tp_doc = lock_doc;
    if (PyType_Ready(&Locktype) < 0)
        return;
    Py_INCREF(&Locktype);
    PyDict_SetItemString(d, "LockType", (PyObject *)&Locktype);

    Py_INCREF(&localtype);
    if (PyModule_AddObject(m, "_local", (PyObject *)&localtype) < 0)
        return;

    nb_threads = 0;

    str_dict = PyString_InternFromString("__dict__");
    if (str_dict == NULL)
        return;

    PyThread_init_thread();
}
