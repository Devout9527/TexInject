#ifndef Py_UCNHASH_H
#define Py_UCNHASH_H
#ifdef __cplusplus
extern "C" {
#endif

#define PyUnicodeData_CAPSULE_NAME "unicodedata.ucnhash_CAPI"

typedef struct {

    int size;

    int (*getname)(PyObject *self, Py_UCS4 code, char* buffer, int buflen);

    int (*getcode)(PyObject *self, const char* name, int namelen, Py_UCS4* code);

} _PyUnicode_Name_CAPI;

#ifdef __cplusplus
}
#endif
#endif
