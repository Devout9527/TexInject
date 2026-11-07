#ifndef Py_PYARENA_H
#define Py_PYARENA_H

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct _arena PyArena;

  PyAPI_FUNC(PyArena *) PyArena_New(void);
  PyAPI_FUNC(void) PyArena_Free(PyArena *);

  PyAPI_FUNC(void *) PyArena_Malloc(PyArena *, size_t size);

  PyAPI_FUNC(int) PyArena_AddPyObject(PyArena *, PyObject *);

#ifdef __cplusplus
}
#endif

#endif
