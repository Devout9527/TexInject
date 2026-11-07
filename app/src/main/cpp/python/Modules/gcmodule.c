#include "Python.h"
#include "frameobject.h"

#define AS_GC(o) ((PyGC_Head *)(o)-1)

#define FROM_GC(g) ((PyObject *)(((PyGC_Head *)g)+1))

struct gc_generation {
    PyGC_Head head;
    int threshold;
    int count;

};

#define NUM_GENERATIONS 3
#define GEN_HEAD(n) (&generations[n].head)

static struct gc_generation generations[NUM_GENERATIONS] = {

    {{{GEN_HEAD(0), GEN_HEAD(0), 0}},           700,            0},
    {{{GEN_HEAD(1), GEN_HEAD(1), 0}},           10,             0},
    {{{GEN_HEAD(2), GEN_HEAD(2), 0}},           10,             0},
};

PyGC_Head *_PyGC_generation0 = GEN_HEAD(0);

static int enabled = 1;

static int collecting = 0;

static PyObject *garbage = NULL;

static PyObject *gc_str = NULL;

static PyObject *delstr = NULL;

static Py_ssize_t long_lived_total = 0;

static Py_ssize_t long_lived_pending = 0;

#define DEBUG_STATS             (1<<0)
#define DEBUG_COLLECTABLE       (1<<1)
#define DEBUG_UNCOLLECTABLE     (1<<2)
#define DEBUG_INSTANCES         (1<<3)
#define DEBUG_OBJECTS           (1<<4)
#define DEBUG_SAVEALL           (1<<5)
#define DEBUG_LEAK              DEBUG_COLLECTABLE | \
                DEBUG_UNCOLLECTABLE | \
                DEBUG_INSTANCES | \
                DEBUG_OBJECTS | \
                DEBUG_SAVEALL
static int debug;
static PyObject *tmod = NULL;

#define GC_UNTRACKED                    _PyGC_REFS_UNTRACKED
#define GC_REACHABLE                    _PyGC_REFS_REACHABLE
#define GC_TENTATIVELY_UNREACHABLE      _PyGC_REFS_TENTATIVELY_UNREACHABLE

#define IS_TRACKED(o) ((AS_GC(o))->gc.gc_refs != GC_UNTRACKED)
#define IS_REACHABLE(o) ((AS_GC(o))->gc.gc_refs == GC_REACHABLE)
#define IS_TENTATIVELY_UNREACHABLE(o) ( \
    (AS_GC(o))->gc.gc_refs == GC_TENTATIVELY_UNREACHABLE)

static void
gc_list_init(PyGC_Head *list)
{
    list->gc.gc_prev = list;
    list->gc.gc_next = list;
}

static int
gc_list_is_empty(PyGC_Head *list)
{
    return (list->gc.gc_next == list);
}

#if 0

static void
gc_list_append(PyGC_Head *node, PyGC_Head *list)
{
    node->gc.gc_next = list;
    node->gc.gc_prev = list->gc.gc_prev;
    node->gc.gc_prev->gc.gc_next = node;
    list->gc.gc_prev = node;
}
#endif

static void
gc_list_remove(PyGC_Head *node)
{
    node->gc.gc_prev->gc.gc_next = node->gc.gc_next;
    node->gc.gc_next->gc.gc_prev = node->gc.gc_prev;
    node->gc.gc_next = NULL;
}

static void
gc_list_move(PyGC_Head *node, PyGC_Head *list)
{
    PyGC_Head *new_prev;
    PyGC_Head *current_prev = node->gc.gc_prev;
    PyGC_Head *current_next = node->gc.gc_next;

    current_prev->gc.gc_next = current_next;
    current_next->gc.gc_prev = current_prev;

    new_prev = node->gc.gc_prev = list->gc.gc_prev;
    new_prev->gc.gc_next = list->gc.gc_prev = node;
    node->gc.gc_next = list;
}

static void
gc_list_merge(PyGC_Head *from, PyGC_Head *to)
{
    PyGC_Head *tail;
    assert(from != to);
    if (!gc_list_is_empty(from)) {
        tail = to->gc.gc_prev;
        tail->gc.gc_next = from->gc.gc_next;
        tail->gc.gc_next->gc.gc_prev = tail;
        to->gc.gc_prev = from->gc.gc_prev;
        to->gc.gc_prev->gc.gc_next = to;
    }
    gc_list_init(from);
}

static Py_ssize_t
gc_list_size(PyGC_Head *list)
{
    PyGC_Head *gc;
    Py_ssize_t n = 0;
    for (gc = list->gc.gc_next; gc != list; gc = gc->gc.gc_next) {
        n++;
    }
    return n;
}

static int
append_objects(PyObject *py_list, PyGC_Head *gc_list)
{
    PyGC_Head *gc;
    for (gc = gc_list->gc.gc_next; gc != gc_list; gc = gc->gc.gc_next) {
        PyObject *op = FROM_GC(gc);
        if (op != py_list) {
            if (PyList_Append(py_list, op)) {
                return -1;
            }
        }
    }
    return 0;
}

static void
update_refs(PyGC_Head *containers)
{
    PyGC_Head *gc = containers->gc.gc_next;
    for (; gc != containers; gc = gc->gc.gc_next) {
        assert(gc->gc.gc_refs == GC_REACHABLE);
        gc->gc.gc_refs = Py_REFCNT(FROM_GC(gc));

        assert(gc->gc.gc_refs != 0);
    }
}

static int
visit_decref(PyObject *op, void *data)
{
    assert(op != NULL);
    if (PyObject_IS_GC(op)) {
        PyGC_Head *gc = AS_GC(op);

        assert(gc->gc.gc_refs != 0);
        if (gc->gc.gc_refs > 0)
            gc->gc.gc_refs--;
    }
    return 0;
}

static void
subtract_refs(PyGC_Head *containers)
{
    traverseproc traverse;
    PyGC_Head *gc = containers->gc.gc_next;
    for (; gc != containers; gc=gc->gc.gc_next) {
        traverse = Py_TYPE(FROM_GC(gc))->tp_traverse;
        (void) traverse(FROM_GC(gc),
                       (visitproc)visit_decref,
                       NULL);
    }
}

static int
visit_reachable(PyObject *op, PyGC_Head *reachable)
{
    if (PyObject_IS_GC(op)) {
        PyGC_Head *gc = AS_GC(op);
        const Py_ssize_t gc_refs = gc->gc.gc_refs;

        if (gc_refs == 0) {

            gc->gc.gc_refs = 1;
        }
        else if (gc_refs == GC_TENTATIVELY_UNREACHABLE) {

            gc_list_move(gc, reachable);
            gc->gc.gc_refs = 1;
        }

         else {
            assert(gc_refs > 0
                   || gc_refs == GC_REACHABLE
                   || gc_refs == GC_UNTRACKED);
         }
    }
    return 0;
}

static void
move_unreachable(PyGC_Head *young, PyGC_Head *unreachable)
{
    PyGC_Head *gc = young->gc.gc_next;

    while (gc != young) {
        PyGC_Head *next;

        if (gc->gc.gc_refs) {

            PyObject *op = FROM_GC(gc);
            traverseproc traverse = Py_TYPE(op)->tp_traverse;
            assert(gc->gc.gc_refs > 0);
            gc->gc.gc_refs = GC_REACHABLE;
            (void) traverse(op,
                            (visitproc)visit_reachable,
                            (void *)young);
            next = gc->gc.gc_next;
            if (PyTuple_CheckExact(op)) {
                _PyTuple_MaybeUntrack(op);
            }
        }
        else {

            next = gc->gc.gc_next;
            gc_list_move(gc, unreachable);
            gc->gc.gc_refs = GC_TENTATIVELY_UNREACHABLE;
        }
        gc = next;
    }
}

static int
has_finalizer(PyObject *op)
{
    if (PyInstance_Check(op)) {
        assert(delstr != NULL);
        return _PyInstance_Lookup(op, delstr) != NULL;
    }
    else if (PyType_HasFeature(op->ob_type, Py_TPFLAGS_HEAPTYPE))
        return op->ob_type->tp_del != NULL;
    else if (PyGen_CheckExact(op))
        return PyGen_NeedsFinalizing((PyGenObject *)op);
    else
        return 0;
}

static void
untrack_dicts(PyGC_Head *head)
{
    PyGC_Head *next, *gc = head->gc.gc_next;
    while (gc != head) {
        PyObject *op = FROM_GC(gc);
        next = gc->gc.gc_next;
        if (PyDict_CheckExact(op))
            _PyDict_MaybeUntrack(op);
        gc = next;
    }
}

static void
move_finalizers(PyGC_Head *unreachable, PyGC_Head *finalizers)
{
    PyGC_Head *gc;
    PyGC_Head *next;

    for (gc = unreachable->gc.gc_next; gc != unreachable; gc = next) {
        PyObject *op = FROM_GC(gc);

        assert(IS_TENTATIVELY_UNREACHABLE(op));
        next = gc->gc.gc_next;

        if (has_finalizer(op)) {
            gc_list_move(gc, finalizers);
            gc->gc.gc_refs = GC_REACHABLE;
        }
    }
}

static int
visit_move(PyObject *op, PyGC_Head *tolist)
{
    if (PyObject_IS_GC(op)) {
        if (IS_TENTATIVELY_UNREACHABLE(op)) {
            PyGC_Head *gc = AS_GC(op);
            gc_list_move(gc, tolist);
            gc->gc.gc_refs = GC_REACHABLE;
        }
    }
    return 0;
}

static void
move_finalizer_reachable(PyGC_Head *finalizers)
{
    traverseproc traverse;
    PyGC_Head *gc = finalizers->gc.gc_next;
    for (; gc != finalizers; gc = gc->gc.gc_next) {

        traverse = Py_TYPE(FROM_GC(gc))->tp_traverse;
        (void) traverse(FROM_GC(gc),
                        (visitproc)visit_move,
                        (void *)finalizers);
    }
}

static int
handle_weakrefs(PyGC_Head *unreachable, PyGC_Head *old)
{
    PyGC_Head *gc;
    PyObject *op;
    PyWeakReference *wr;
    PyGC_Head wrcb_to_call;
    PyGC_Head *next;
    int num_freed = 0;

    gc_list_init(&wrcb_to_call);

    for (gc = unreachable->gc.gc_next; gc != unreachable; gc = next) {
        PyWeakReference **wrlist;

        op = FROM_GC(gc);
        assert(IS_TENTATIVELY_UNREACHABLE(op));
        next = gc->gc.gc_next;

        if (! PyType_SUPPORTS_WEAKREFS(Py_TYPE(op)))
            continue;

        wrlist = (PyWeakReference **)
                                PyObject_GET_WEAKREFS_LISTPTR(op);

        for (wr = *wrlist; wr != NULL; wr = *wrlist) {
            PyGC_Head *wrasgc;

            assert(wr->wr_object == op);
            _PyWeakref_ClearRef(wr);
            assert(wr->wr_object == Py_None);
            if (wr->wr_callback == NULL)
                continue;

            if (IS_TENTATIVELY_UNREACHABLE(wr))
                continue;
            assert(IS_REACHABLE(wr));

            Py_INCREF(wr);

            wrasgc = AS_GC(wr);
            assert(wrasgc != next);

            gc_list_move(wrasgc, &wrcb_to_call);
        }
    }

    while (! gc_list_is_empty(&wrcb_to_call)) {
        PyObject *temp;
        PyObject *callback;

        gc = wrcb_to_call.gc.gc_next;
        op = FROM_GC(gc);
        assert(IS_REACHABLE(op));
        assert(PyWeakref_Check(op));
        wr = (PyWeakReference *)op;
        callback = wr->wr_callback;
        assert(callback != NULL);

        temp = PyObject_CallFunctionObjArgs(callback, wr, NULL);
        if (temp == NULL)
            PyErr_WriteUnraisable(callback);
        else
            Py_DECREF(temp);

        Py_DECREF(op);
        if (wrcb_to_call.gc.gc_next == gc) {

            gc_list_move(gc, old);
        }
        else
            ++num_freed;
    }

    return num_freed;
}

static void
debug_instance(char *msg, PyInstanceObject *inst)
{
    char *cname;

    PyObject *classname = inst->in_class->cl_name;
    if (classname != NULL && PyString_Check(classname))
        cname = PyString_AsString(classname);
    else
        cname = "?";
    PySys_WriteStderr("gc: %.100s <%.100s instance at %p>\n",
                      msg, cname, inst);
}

static void
debug_cycle(char *msg, PyObject *op)
{
    if ((debug & DEBUG_INSTANCES) && PyInstance_Check(op)) {
        debug_instance(msg, (PyInstanceObject *)op);
    }
    else if (debug & DEBUG_OBJECTS) {
        PySys_WriteStderr("gc: %.100s <%.100s %p>\n",
                          msg, Py_TYPE(op)->tp_name, op);
    }
}

static int
handle_finalizers(PyGC_Head *finalizers, PyGC_Head *old)
{
    PyGC_Head *gc = finalizers->gc.gc_next;

    if (garbage == NULL) {
        garbage = PyList_New(0);
        if (garbage == NULL)
            Py_FatalError("gc couldn't create gc.garbage list");
    }
    for (; gc != finalizers; gc = gc->gc.gc_next) {
        PyObject *op = FROM_GC(gc);

        if ((debug & DEBUG_SAVEALL) || has_finalizer(op)) {
            if (PyList_Append(garbage, op) < 0)
                return -1;
        }
    }

    gc_list_merge(finalizers, old);
    return 0;
}

static void
delete_garbage(PyGC_Head *collectable, PyGC_Head *old)
{
    inquiry clear;

    while (!gc_list_is_empty(collectable)) {
        PyGC_Head *gc = collectable->gc.gc_next;
        PyObject *op = FROM_GC(gc);

        assert(IS_TENTATIVELY_UNREACHABLE(op));
        if (debug & DEBUG_SAVEALL) {
            PyList_Append(garbage, op);
        }
        else {
            if ((clear = Py_TYPE(op)->tp_clear) != NULL) {
                Py_INCREF(op);
                clear(op);
                Py_DECREF(op);
            }
        }
        if (collectable->gc.gc_next == gc) {

            gc_list_move(gc, old);
            gc->gc.gc_refs = GC_REACHABLE;
        }
    }
}

static void
clear_freelists(void)
{
    (void)PyMethod_ClearFreeList();
    (void)PyFrame_ClearFreeList();
    (void)PyCFunction_ClearFreeList();
    (void)PyTuple_ClearFreeList();
#ifdef Py_USING_UNICODE
    (void)PyUnicode_ClearFreeList();
#endif
    (void)PyInt_ClearFreeList();
    (void)PyFloat_ClearFreeList();
}

static double
get_time(void)
{
    double result = 0;
    if (tmod != NULL) {
        PyObject *f = PyObject_CallMethod(tmod, "time", NULL);
        if (f == NULL) {
            PyErr_Clear();
        }
        else {
            if (PyFloat_Check(f))
                result = PyFloat_AsDouble(f);
            Py_DECREF(f);
        }
    }
    return result;
}

static Py_ssize_t
collect(int generation)
{
    int i;
    Py_ssize_t m = 0;
    Py_ssize_t n = 0;
    PyGC_Head *young;
    PyGC_Head *old;
    PyGC_Head unreachable;
    PyGC_Head finalizers;
    PyGC_Head *gc;
    double t1 = 0.0;

    if (delstr == NULL) {
        delstr = PyString_InternFromString("__del__");
        if (delstr == NULL)
            Py_FatalError("gc couldn't allocate \"__del__\"");
    }

    if (debug & DEBUG_STATS) {
        PySys_WriteStderr("gc: collecting generation %d...\n",
                          generation);
        PySys_WriteStderr("gc: objects in each generation:");
        for (i = 0; i < NUM_GENERATIONS; i++)
            PySys_WriteStderr(" %" PY_FORMAT_SIZE_T "d",
                              gc_list_size(GEN_HEAD(i)));
        t1 = get_time();
        PySys_WriteStderr("\n");
    }

    if (generation+1 < NUM_GENERATIONS)
        generations[generation+1].count += 1;
    for (i = 0; i <= generation; i++)
        generations[i].count = 0;

    for (i = 0; i < generation; i++) {
        gc_list_merge(GEN_HEAD(i), GEN_HEAD(generation));
    }

    young = GEN_HEAD(generation);
    if (generation < NUM_GENERATIONS-1)
        old = GEN_HEAD(generation+1);
    else
        old = young;

    update_refs(young);
    subtract_refs(young);

    gc_list_init(&unreachable);
    move_unreachable(young, &unreachable);

    if (young != old) {
        if (generation == NUM_GENERATIONS - 2) {
            long_lived_pending += gc_list_size(young);
        }
        gc_list_merge(young, old);
    }
    else {

        untrack_dicts(young);
        long_lived_pending = 0;
        long_lived_total = gc_list_size(young);
    }

    gc_list_init(&finalizers);
    move_finalizers(&unreachable, &finalizers);

    move_finalizer_reachable(&finalizers);

    for (gc = unreachable.gc.gc_next; gc != &unreachable;
                    gc = gc->gc.gc_next) {
        m++;
        if (debug & DEBUG_COLLECTABLE) {
            debug_cycle("collectable", FROM_GC(gc));
        }
    }

    m += handle_weakrefs(&unreachable, old);

    delete_garbage(&unreachable, old);

    for (gc = finalizers.gc.gc_next;
         gc != &finalizers;
         gc = gc->gc.gc_next) {
        n++;
        if (debug & DEBUG_UNCOLLECTABLE)
            debug_cycle("uncollectable", FROM_GC(gc));
    }
    if (debug & DEBUG_STATS) {
        double t2 = get_time();
        if (m == 0 && n == 0)
            PySys_WriteStderr("gc: done");
        else
            PySys_WriteStderr(
                "gc: done, "
                "%" PY_FORMAT_SIZE_T "d unreachable, "
                "%" PY_FORMAT_SIZE_T "d uncollectable",
                n+m, n);
        if (t1 && t2) {
            PySys_WriteStderr(", %.4fs elapsed", t2-t1);
        }
        PySys_WriteStderr(".\n");
    }

    (void)handle_finalizers(&finalizers, old);

    if (generation == NUM_GENERATIONS-1) {
        clear_freelists();
    }

    if (PyErr_Occurred()) {
        if (gc_str == NULL)
            gc_str = PyString_FromString("garbage collection");
        PyErr_WriteUnraisable(gc_str);
        Py_FatalError("unexpected exception during garbage collection");
    }
    return n+m;
}

static Py_ssize_t
collect_generations(void)
{
    int i;
    Py_ssize_t n = 0;

    for (i = NUM_GENERATIONS-1; i >= 0; i--) {
        if (generations[i].count > generations[i].threshold) {

            if (i == NUM_GENERATIONS - 1
                && long_lived_pending < long_lived_total / 4)
                continue;
            n = collect(i);
            break;
        }
    }
    return n;
}

PyDoc_STRVAR(gc_enable__doc__,
"enable() -> None\n"
"\n"
"Enable automatic garbage collection.\n");

static PyObject *
gc_enable(PyObject *self, PyObject *noargs)
{
    enabled = 1;
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(gc_disable__doc__,
"disable() -> None\n"
"\n"
"Disable automatic garbage collection.\n");

static PyObject *
gc_disable(PyObject *self, PyObject *noargs)
{
    enabled = 0;
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(gc_isenabled__doc__,
"isenabled() -> status\n"
"\n"
"Returns true if automatic garbage collection is enabled.\n");

static PyObject *
gc_isenabled(PyObject *self, PyObject *noargs)
{
    return PyBool_FromLong((long)enabled);
}

PyDoc_STRVAR(gc_collect__doc__,
"collect([generation]) -> n\n"
"\n"
"With no arguments, run a full collection.  The optional argument\n"
"may be an integer specifying which generation to collect.  A ValueError\n"
"is raised if the generation number is invalid.\n\n"
"The number of unreachable objects is returned.\n");

static PyObject *
gc_collect(PyObject *self, PyObject *args, PyObject *kws)
{
    static char *keywords[] = {"generation", NULL};
    int genarg = NUM_GENERATIONS - 1;
    Py_ssize_t n;

    if (!PyArg_ParseTupleAndKeywords(args, kws, "|i", keywords, &genarg))
        return NULL;

    else if (genarg < 0 || genarg >= NUM_GENERATIONS) {
        PyErr_SetString(PyExc_ValueError, "invalid generation");
        return NULL;
    }

    if (collecting)
        n = 0;
    else {
        collecting = 1;
        n = collect(genarg);
        collecting = 0;
    }

    return PyInt_FromSsize_t(n);
}

PyDoc_STRVAR(gc_set_debug__doc__,
"set_debug(flags) -> None\n"
"\n"
"Set the garbage collection debugging flags. Debugging information is\n"
"written to sys.stderr.\n"
"\n"
"flags is an integer and can have the following bits turned on:\n"
"\n"
"  DEBUG_STATS - Print statistics during collection.\n"
"  DEBUG_COLLECTABLE - Print collectable objects found.\n"
"  DEBUG_UNCOLLECTABLE - Print unreachable but uncollectable objects found.\n"
"  DEBUG_INSTANCES - Print instance objects.\n"
"  DEBUG_OBJECTS - Print objects other than instances.\n"
"  DEBUG_SAVEALL - Save objects to gc.garbage rather than freeing them.\n"
"  DEBUG_LEAK - Debug leaking programs (everything but STATS).\n");

static PyObject *
gc_set_debug(PyObject *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, "i:set_debug", &debug))
        return NULL;

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(gc_get_debug__doc__,
"get_debug() -> flags\n"
"\n"
"Get the garbage collection debugging flags.\n");

static PyObject *
gc_get_debug(PyObject *self, PyObject *noargs)
{
    return Py_BuildValue("i", debug);
}

PyDoc_STRVAR(gc_set_thresh__doc__,
"set_threshold(threshold0, [threshold1, threshold2]) -> None\n"
"\n"
"Sets the collection thresholds.  Setting threshold0 to zero disables\n"
"collection.\n");

static PyObject *
gc_set_thresh(PyObject *self, PyObject *args)
{
    int i;
    if (!PyArg_ParseTuple(args, "i|ii:set_threshold",
                          &generations[0].threshold,
                          &generations[1].threshold,
                          &generations[2].threshold))
        return NULL;
    for (i = 2; i < NUM_GENERATIONS; i++) {

        generations[i].threshold = generations[2].threshold;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(gc_get_thresh__doc__,
"get_threshold() -> (threshold0, threshold1, threshold2)\n"
"\n"
"Return the current collection thresholds\n");

static PyObject *
gc_get_thresh(PyObject *self, PyObject *noargs)
{
    return Py_BuildValue("(iii)",
                         generations[0].threshold,
                         generations[1].threshold,
                         generations[2].threshold);
}

PyDoc_STRVAR(gc_get_count__doc__,
"get_count() -> (count0, count1, count2)\n"
"\n"
"Return the current collection counts\n");

static PyObject *
gc_get_count(PyObject *self, PyObject *noargs)
{
    return Py_BuildValue("(iii)",
                         generations[0].count,
                         generations[1].count,
                         generations[2].count);
}

static int
referrersvisit(PyObject* obj, PyObject *objs)
{
    Py_ssize_t i;
    for (i = 0; i < PyTuple_GET_SIZE(objs); i++)
        if (PyTuple_GET_ITEM(objs, i) == obj)
            return 1;
    return 0;
}

static int
gc_referrers_for(PyObject *objs, PyGC_Head *list, PyObject *resultlist)
{
    PyGC_Head *gc;
    PyObject *obj;
    traverseproc traverse;
    for (gc = list->gc.gc_next; gc != list; gc = gc->gc.gc_next) {
        obj = FROM_GC(gc);
        traverse = Py_TYPE(obj)->tp_traverse;
        if (obj == objs || obj == resultlist)
            continue;
        if (traverse(obj, (visitproc)referrersvisit, objs)) {
            if (PyList_Append(resultlist, obj) < 0)
                return 0;
        }
    }
    return 1;
}

PyDoc_STRVAR(gc_get_referrers__doc__,
"get_referrers(*objs) -> list\n\
Return the list of objects that directly refer to any of objs.");

static PyObject *
gc_get_referrers(PyObject *self, PyObject *args)
{
    int i;
    PyObject *result = PyList_New(0);
    if (!result) return NULL;

    for (i = 0; i < NUM_GENERATIONS; i++) {
        if (!(gc_referrers_for(args, GEN_HEAD(i), result))) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

static int
referentsvisit(PyObject *obj, PyObject *list)
{
    return PyList_Append(list, obj) < 0;
}

PyDoc_STRVAR(gc_get_referents__doc__,
"get_referents(*objs) -> list\n\
Return the list of objects that are directly referred to by objs.");

static PyObject *
gc_get_referents(PyObject *self, PyObject *args)
{
    Py_ssize_t i;
    PyObject *result = PyList_New(0);

    if (result == NULL)
        return NULL;

    for (i = 0; i < PyTuple_GET_SIZE(args); i++) {
        traverseproc traverse;
        PyObject *obj = PyTuple_GET_ITEM(args, i);

        if (! PyObject_IS_GC(obj))
            continue;
        traverse = Py_TYPE(obj)->tp_traverse;
        if (! traverse)
            continue;
        if (traverse(obj, (visitproc)referentsvisit, result)) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

PyDoc_STRVAR(gc_get_objects__doc__,
"get_objects() -> [...]\n"
"\n"
"Return a list of objects tracked by the collector (excluding the list\n"
"returned).\n");

static PyObject *
gc_get_objects(PyObject *self, PyObject *noargs)
{
    int i;
    PyObject* result;

    result = PyList_New(0);
    if (result == NULL)
        return NULL;
    for (i = 0; i < NUM_GENERATIONS; i++) {
        if (append_objects(result, GEN_HEAD(i))) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

PyDoc_STRVAR(gc_is_tracked__doc__,
"is_tracked(obj) -> bool\n"
"\n"
"Returns true if the object is tracked by the garbage collector.\n"
"Simple atomic objects will return false.\n"
);

static PyObject *
gc_is_tracked(PyObject *self, PyObject *obj)
{
    PyObject *result;

    if (PyObject_IS_GC(obj) && IS_TRACKED(obj))
        result = Py_True;
    else
        result = Py_False;
    Py_INCREF(result);
    return result;
}

PyDoc_STRVAR(gc__doc__,
"This module provides access to the garbage collector for reference cycles.\n"
"\n"
"enable() -- Enable automatic garbage collection.\n"
"disable() -- Disable automatic garbage collection.\n"
"isenabled() -- Returns true if automatic collection is enabled.\n"
"collect() -- Do a full collection right now.\n"
"get_count() -- Return the current collection counts.\n"
"set_debug() -- Set debugging flags.\n"
"get_debug() -- Get debugging flags.\n"
"set_threshold() -- Set the collection thresholds.\n"
"get_threshold() -- Return the current the collection thresholds.\n"
"get_objects() -- Return a list of all objects tracked by the collector.\n"
"is_tracked() -- Returns true if a given object is tracked.\n"
"get_referrers() -- Return the list of objects that refer to an object.\n"
"get_referents() -- Return the list of objects that an object refers to.\n");

static PyMethodDef GcMethods[] = {
    {"enable",             gc_enable,     METH_NOARGS,  gc_enable__doc__},
    {"disable",            gc_disable,    METH_NOARGS,  gc_disable__doc__},
    {"isenabled",          gc_isenabled,  METH_NOARGS,  gc_isenabled__doc__},
    {"set_debug",          gc_set_debug,  METH_VARARGS, gc_set_debug__doc__},
    {"get_debug",          gc_get_debug,  METH_NOARGS,  gc_get_debug__doc__},
    {"get_count",          gc_get_count,  METH_NOARGS,  gc_get_count__doc__},
    {"set_threshold",  gc_set_thresh, METH_VARARGS, gc_set_thresh__doc__},
    {"get_threshold",  gc_get_thresh, METH_NOARGS,  gc_get_thresh__doc__},
    {"collect",            (PyCFunction)gc_collect,
        METH_VARARGS | METH_KEYWORDS,           gc_collect__doc__},
    {"get_objects",    gc_get_objects,METH_NOARGS,  gc_get_objects__doc__},
    {"is_tracked",     gc_is_tracked, METH_O,       gc_is_tracked__doc__},
    {"get_referrers",  gc_get_referrers, METH_VARARGS,
        gc_get_referrers__doc__},
    {"get_referents",  gc_get_referents, METH_VARARGS,
        gc_get_referents__doc__},
    {NULL,      NULL}
};

PyMODINIT_FUNC
initgc(void)
{
    PyObject *m;

    m = Py_InitModule4("gc",
                          GcMethods,
                          gc__doc__,
                          NULL,
                          PYTHON_API_VERSION);
    if (m == NULL)
        return;

    if (garbage == NULL) {
        garbage = PyList_New(0);
        if (garbage == NULL)
            return;
    }
    Py_INCREF(garbage);
    if (PyModule_AddObject(m, "garbage", garbage) < 0)
        return;

    if (tmod == NULL) {
        tmod = PyImport_ImportModuleNoBlock("time");
        if (tmod == NULL)
            PyErr_Clear();
    }

#define ADD_INT(NAME) if (PyModule_AddIntConstant(m, #NAME, NAME) < 0) return
    ADD_INT(DEBUG_STATS);
    ADD_INT(DEBUG_COLLECTABLE);
    ADD_INT(DEBUG_UNCOLLECTABLE);
    ADD_INT(DEBUG_INSTANCES);
    ADD_INT(DEBUG_OBJECTS);
    ADD_INT(DEBUG_SAVEALL);
    ADD_INT(DEBUG_LEAK);
#undef ADD_INT
}

Py_ssize_t
PyGC_Collect(void)
{
    Py_ssize_t n;

    if (collecting)
        n = 0;
    else {
        collecting = 1;
        n = collect(NUM_GENERATIONS - 1);
        collecting = 0;
    }

    return n;
}

void
_PyGC_Dump(PyGC_Head *g)
{
    _PyObject_Dump(FROM_GC(g));
}

#undef PyObject_GC_Track
#undef PyObject_GC_UnTrack
#undef PyObject_GC_Del
#undef _PyObject_GC_Malloc

void
PyObject_GC_Track(void *op)
{
    _PyObject_GC_TRACK(op);
}

void
_PyObject_GC_Track(PyObject *op)
{
    PyObject_GC_Track(op);
}

void
PyObject_GC_UnTrack(void *op)
{

    if (IS_TRACKED(op))
        _PyObject_GC_UNTRACK(op);
}

void
_PyObject_GC_UnTrack(PyObject *op)
{
    PyObject_GC_UnTrack(op);
}

PyObject *
_PyObject_GC_Malloc(size_t basicsize)
{
    PyObject *op;
    PyGC_Head *g;
    if (basicsize > PY_SSIZE_T_MAX - sizeof(PyGC_Head))
        return PyErr_NoMemory();
    g = (PyGC_Head *)PyObject_MALLOC(
        sizeof(PyGC_Head) + basicsize);
    if (g == NULL)
        return PyErr_NoMemory();
    g->gc.gc_refs = GC_UNTRACKED;
    generations[0].count++;
    if (generations[0].count > generations[0].threshold &&
        enabled &&
        generations[0].threshold &&
        !collecting &&
        !PyErr_Occurred()) {
        collecting = 1;
        collect_generations();
        collecting = 0;
    }
    op = FROM_GC(g);
    return op;
}

PyObject *
_PyObject_GC_New(PyTypeObject *tp)
{
    PyObject *op = _PyObject_GC_Malloc(_PyObject_SIZE(tp));
    if (op != NULL)
        op = PyObject_INIT(op, tp);
    return op;
}

PyVarObject *
_PyObject_GC_NewVar(PyTypeObject *tp, Py_ssize_t nitems)
{
    const size_t size = _PyObject_VAR_SIZE(tp, nitems);
    PyVarObject *op = (PyVarObject *) _PyObject_GC_Malloc(size);
    if (op != NULL)
        op = PyObject_INIT_VAR(op, tp, nitems);
    return op;
}

PyVarObject *
_PyObject_GC_Resize(PyVarObject *op, Py_ssize_t nitems)
{
    const size_t basicsize = _PyObject_VAR_SIZE(Py_TYPE(op), nitems);
    PyGC_Head *g = AS_GC(op);
    if (basicsize > PY_SSIZE_T_MAX - sizeof(PyGC_Head))
        return (PyVarObject *)PyErr_NoMemory();
    g = (PyGC_Head *)PyObject_REALLOC(g,  sizeof(PyGC_Head) + basicsize);
    if (g == NULL)
        return (PyVarObject *)PyErr_NoMemory();
    op = (PyVarObject *) FROM_GC(g);
    Py_SIZE(op) = nitems;
    return op;
}

void
PyObject_GC_Del(void *op)
{
    PyGC_Head *g = AS_GC(op);
    if (IS_TRACKED(op))
        gc_list_remove(g);
    if (generations[0].count > 0) {
        generations[0].count--;
    }
    PyObject_FREE(g);
}

#undef _PyObject_GC_Del
void
_PyObject_GC_Del(PyObject *op)
{
    PyObject_GC_Del(op);
}
