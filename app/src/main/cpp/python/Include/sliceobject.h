#ifndef Py_SLICEOBJECT_H
#define Py_SLICEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(PyObject) _Py_EllipsisObject;

#define Py_Ellipsis (&_Py_EllipsisObject)

typedef struct {
    PyObject_HEAD
    PyObject *start, *stop, *step;
} PySliceObject;

PyAPI_DATA(PyTypeObject) PySlice_Type;
PyAPI_DATA(PyTypeObject) PyEllipsis_Type;

#define PySlice_Check(op) (Py_TYPE(op) == &PySlice_Type)

PyAPI_FUNC(PyObject *) PySlice_New(PyObject* start, PyObject* stop,
                                  PyObject* step);
PyAPI_FUNC(PyObject *) _PySlice_FromIndices(Py_ssize_t start, Py_ssize_t stop);
PyAPI_FUNC(int) PySlice_GetIndices(PySliceObject *r, Py_ssize_t length,
                                  Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step);
PyAPI_FUNC(int) PySlice_GetIndicesEx(PySliceObject *r, Py_ssize_t length,
				    Py_ssize_t *start, Py_ssize_t *stop,
				    Py_ssize_t *step, Py_ssize_t *slicelength);

#ifdef __cplusplus
}
#endif
#endif
