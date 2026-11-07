#ifndef Py_FILEOBJECT_H
#define Py_FILEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PyObject_HEAD
    FILE *f_fp;
    PyObject *f_name;
    PyObject *f_mode;
    int (*f_close)(FILE *);
    int f_softspace;
    int f_binary;

    char* f_buf;
    char* f_bufend;
    char* f_bufptr;
    char *f_setbuf;
    int f_univ_newline;
    int f_newlinetypes;
    int f_skipnextlf;
    PyObject *f_encoding;
    PyObject *f_errors;
    PyObject *weakreflist;
    int unlocked_count;

    int readable;
    int writable;
} PyFileObject;

PyAPI_DATA(PyTypeObject) PyFile_Type;

#define PyFile_Check(op) PyObject_TypeCheck(op, &PyFile_Type)
#define PyFile_CheckExact(op) (Py_TYPE(op) == &PyFile_Type)

PyAPI_FUNC(PyObject *) PyFile_FromString(char *, char *);
PyAPI_FUNC(void) PyFile_SetBufSize(PyObject *, int);
PyAPI_FUNC(int) PyFile_SetEncoding(PyObject *, const char *);
PyAPI_FUNC(int) PyFile_SetEncodingAndErrors(PyObject *, const char *, char *errors);
PyAPI_FUNC(PyObject *) PyFile_FromFile(FILE *, char *, char *,
                                             int (*)(FILE *));
PyAPI_FUNC(FILE *) PyFile_AsFile(PyObject *);
PyAPI_FUNC(void) PyFile_IncUseCount(PyFileObject *);
PyAPI_FUNC(void) PyFile_DecUseCount(PyFileObject *);
PyAPI_FUNC(PyObject *) PyFile_Name(PyObject *);
PyAPI_FUNC(PyObject *) PyFile_GetLine(PyObject *, int);
PyAPI_FUNC(int) PyFile_WriteObject(PyObject *, PyObject *, int);
PyAPI_FUNC(int) PyFile_SoftSpace(PyObject *, int);
PyAPI_FUNC(int) PyFile_WriteString(const char *, PyObject *);
PyAPI_FUNC(int) PyObject_AsFileDescriptor(PyObject *);

PyAPI_DATA(const char *) Py_FileSystemDefaultEncoding;

#define PY_STDIOTEXTMODE "b"
char *Py_UniversalNewlineFgets(char *, int, FILE*, PyObject *);
size_t Py_UniversalNewlineFread(char *, size_t, FILE *, PyObject *);

int _PyFile_SanitizeMode(char *mode);

#if defined _MSC_VER && _MSC_VER >= 1400

int _PyVerify_fd(int fd);
#elif defined _MSC_VER && _MSC_VER >= 1200

#define _PyVerify_fd(fd) (_get_osfhandle(fd) >= 0)
#else
#define _PyVerify_fd(A) (1)
#endif

#ifdef HAVE_SELECT
 #define _PyIsSelectable_fd(FD) (((FD) >= 0) && ((FD) < FD_SETSIZE))
#else
 #define _PyIsSelectable_fd(FD) (1)
#endif

#ifdef __cplusplus
}
#endif
#endif
