#ifndef Py_RANGEOBJECT_H
#define Py_RANGEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

PyAPI_DATA(PyTypeObject) PyRange_Type;

#define PyRange_Check(op) (Py_TYPE(op) == &PyRange_Type)

#ifdef __cplusplus
}
#endif
#endif
