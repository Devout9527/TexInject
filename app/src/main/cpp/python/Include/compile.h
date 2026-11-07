#ifndef Py_COMPILE_H
#define Py_COMPILE_H

#include "code.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _node;
PyAPI_FUNC(PyCodeObject *) PyNode_Compile(struct _node *, const char *);

typedef struct {
    int ff_features;
    int ff_lineno;
} PyFutureFeatures;

#define FUTURE_NESTED_SCOPES "nested_scopes"
#define FUTURE_GENERATORS "generators"
#define FUTURE_DIVISION "division"
#define FUTURE_ABSOLUTE_IMPORT "absolute_import"
#define FUTURE_WITH_STATEMENT "with_statement"
#define FUTURE_PRINT_FUNCTION "print_function"
#define FUTURE_UNICODE_LITERALS "unicode_literals"

struct _mod;
PyAPI_FUNC(PyCodeObject *) PyAST_Compile(struct _mod *, const char *,
					PyCompilerFlags *, PyArena *);
PyAPI_FUNC(PyFutureFeatures *) PyFuture_FromAST(struct _mod *, const char *);

#ifdef __cplusplus
}
#endif
#endif
