#ifndef Py_FRAMEOBJECT_H
#define Py_FRAMEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int b_type;
    int b_handler;
    int b_level;
} PyTryBlock;

typedef struct _frame {
    PyObject_VAR_HEAD
    struct _frame *f_back;
    PyCodeObject *f_code;
    PyObject *f_builtins;
    PyObject *f_globals;
    PyObject *f_locals;
    PyObject **f_valuestack;

    PyObject **f_stacktop;
    PyObject *f_trace;

    PyObject *f_exc_type, *f_exc_value, *f_exc_traceback;

    PyThreadState *f_tstate;
    int f_lasti;

    int f_lineno;
    int f_iblock;
    PyTryBlock f_blockstack[CO_MAXBLOCKS];
    PyObject *f_localsplus[1];
} PyFrameObject;

PyAPI_DATA(PyTypeObject) PyFrame_Type;

#define PyFrame_Check(op) ((op)->ob_type == &PyFrame_Type)
#define PyFrame_IsRestricted(f) \
	((f)->f_builtins != (f)->f_tstate->interp->builtins)

PyAPI_FUNC(PyFrameObject *) PyFrame_New(PyThreadState *, PyCodeObject *,
                                       PyObject *, PyObject *);

PyAPI_FUNC(void) PyFrame_BlockSetup(PyFrameObject *, int, int, int);
PyAPI_FUNC(PyTryBlock *) PyFrame_BlockPop(PyFrameObject *);

PyAPI_FUNC(PyObject **) PyFrame_ExtendStack(PyFrameObject *, int, int);

PyAPI_FUNC(void) PyFrame_LocalsToFast(PyFrameObject *, int);
PyAPI_FUNC(void) PyFrame_FastToLocals(PyFrameObject *);

PyAPI_FUNC(int) PyFrame_ClearFreeList(void);

PyAPI_FUNC(int) PyFrame_GetLineNumber(PyFrameObject *);

#ifdef __cplusplus
}
#endif
#endif
