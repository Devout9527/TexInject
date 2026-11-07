#ifndef Py_GENOBJECT_H
#define Py_GENOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

struct _frame;

typedef struct {
	PyObject_HEAD

	struct _frame *gi_frame;

	int gi_running;

	PyObject *gi_code;

	PyObject *gi_weakreflist;
} PyGenObject;

PyAPI_DATA(PyTypeObject) PyGen_Type;

#define PyGen_Check(op) PyObject_TypeCheck(op, &PyGen_Type)
#define PyGen_CheckExact(op) (Py_TYPE(op) == &PyGen_Type)

PyAPI_FUNC(PyObject *) PyGen_New(struct _frame *);
PyAPI_FUNC(int) PyGen_NeedsFinalizing(PyGenObject *);

#ifdef __cplusplus
}
#endif
#endif
