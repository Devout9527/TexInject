#ifndef Py_CLASSOBJECT_H
#define Py_CLASSOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PyObject_HEAD
    PyObject	*cl_bases;
    PyObject	*cl_dict;
    PyObject	*cl_name;

    PyObject	*cl_getattr;
    PyObject	*cl_setattr;
    PyObject	*cl_delattr;
    PyObject    *cl_weakreflist;
} PyClassObject;

typedef struct {
    PyObject_HEAD
    PyClassObject *in_class;
    PyObject	  *in_dict;
    PyObject	  *in_weakreflist;
} PyInstanceObject;

typedef struct {
    PyObject_HEAD
    PyObject *im_func;
    PyObject *im_self;
    PyObject *im_class;
    PyObject *im_weakreflist;
} PyMethodObject;

PyAPI_DATA(PyTypeObject) PyClass_Type, PyInstance_Type, PyMethod_Type;

#define PyClass_Check(op) ((op)->ob_type == &PyClass_Type)
#define PyInstance_Check(op) ((op)->ob_type == &PyInstance_Type)
#define PyMethod_Check(op) ((op)->ob_type == &PyMethod_Type)

PyAPI_FUNC(PyObject *) PyClass_New(PyObject *, PyObject *, PyObject *);
PyAPI_FUNC(PyObject *) PyInstance_New(PyObject *, PyObject *,
                                            PyObject *);
PyAPI_FUNC(PyObject *) PyInstance_NewRaw(PyObject *, PyObject *);
PyAPI_FUNC(PyObject *) PyMethod_New(PyObject *, PyObject *, PyObject *);

PyAPI_FUNC(PyObject *) PyMethod_Function(PyObject *);
PyAPI_FUNC(PyObject *) PyMethod_Self(PyObject *);
PyAPI_FUNC(PyObject *) PyMethod_Class(PyObject *);

PyAPI_FUNC(PyObject *) _PyInstance_Lookup(PyObject *pinst, PyObject *name);

#define PyMethod_GET_FUNCTION(meth) \
        (((PyMethodObject *)meth) -> im_func)
#define PyMethod_GET_SELF(meth) \
	(((PyMethodObject *)meth) -> im_self)
#define PyMethod_GET_CLASS(meth) \
	(((PyMethodObject *)meth) -> im_class)

PyAPI_FUNC(int) PyClass_IsSubclass(PyObject *, PyObject *);

PyAPI_FUNC(int) PyMethod_ClearFreeList(void);

#ifdef __cplusplus
}
#endif
#endif
