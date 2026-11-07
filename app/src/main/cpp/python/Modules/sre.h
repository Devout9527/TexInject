#ifndef SRE_INCLUDED
#define SRE_INCLUDED

#include "sre_constants.h"

#ifdef Py_USING_UNICODE
# define SRE_CODE Py_UCS4
# if SIZEOF_SIZE_T > 4
#  define SRE_MAXREPEAT (~(SRE_CODE)0)
# else
#  define SRE_MAXREPEAT ((SRE_CODE)PY_SSIZE_T_MAX + 1u)
# endif
#else
# define SRE_CODE unsigned long
# if SIZEOF_SIZE_T > SIZEOF_LONG
#  define SRE_MAXREPEAT (~(SRE_CODE)0)
# else
#  define SRE_MAXREPEAT ((SRE_CODE)PY_SSIZE_T_MAX + 1u)
# endif
#endif

typedef struct {
    PyObject_VAR_HEAD
    Py_ssize_t groups;
    PyObject* groupindex;
    PyObject* indexgroup;

    PyObject* pattern;
    int flags;
    PyObject *weakreflist;

    Py_ssize_t codesize;
    SRE_CODE code[1];
} PatternObject;

#define PatternObject_GetCode(o) (((PatternObject*)(o))->code)

typedef struct {
    PyObject_VAR_HEAD
    PyObject* string;
    PyObject* regs;
    PatternObject* pattern;
    Py_ssize_t pos, endpos;
    Py_ssize_t lastindex;
    Py_ssize_t groups;
    Py_ssize_t mark[1];
} MatchObject;

typedef unsigned int (*SRE_TOLOWER_HOOK)(unsigned int ch);

#define SRE_MARK_SIZE 200

typedef struct SRE_REPEAT_T {
    Py_ssize_t count;
    SRE_CODE* pattern;
    void* last_ptr;
    struct SRE_REPEAT_T *prev;
} SRE_REPEAT;

typedef struct {

    void* ptr;
    void* beginning;
    void* start;
    void* end;

    PyObject* string;
    Py_ssize_t pos, endpos;

    int charsize;

    Py_ssize_t lastindex;
    Py_ssize_t lastmark;
    void* mark[SRE_MARK_SIZE];

    char* data_stack;
    size_t data_stack_size;
    size_t data_stack_base;

    SRE_REPEAT *repeat;

    SRE_TOLOWER_HOOK lower;
} SRE_STATE;

typedef struct {
    PyObject_HEAD
    PyObject* pattern;
    SRE_STATE state;
} ScannerObject;

#endif
