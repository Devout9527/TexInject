#ifndef Py_COBJECT_H
#define Py_COBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(PyTypeObject) PyCObject_Type;

#define PyCObject_Check(op) (Py_TYPE(op) == &PyCObject_Type)

PyAPI_FUNC(PyObject *) PyCObject_FromVoidPtr(
	void *cobj, void (*destruct)(void*));

PyAPI_FUNC(PyObject *) PyCObject_FromVoidPtrAndDesc(
	void *cobj, void *desc, void (*destruct)(void*,void*));

PyAPI_FUNC(void *) PyCObject_AsVoidPtr(PyObject *);

PyAPI_FUNC(void *) PyCObject_GetDesc(PyObject *);

PyAPI_FUNC(void *) PyCObject_Import(char *module_name, char *cobject_name);

PyAPI_FUNC(int) PyCObject_SetVoidPtr(PyObject *self, void *cobj);

typedef struct {
    PyObject_HEAD
    void *cobject;
    void *desc;
    void (*destructor)(void *);
} PyCObject;

#ifdef __cplusplus
}
#endif
#endif
