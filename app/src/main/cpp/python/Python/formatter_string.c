#include "Python.h"
#include "../Objects/stringlib/stringdefs.h"

#define FORMAT_STRING  _PyBytes_FormatAdvanced
#define FORMAT_LONG    _PyLong_FormatAdvanced
#define FORMAT_INT     _PyInt_FormatAdvanced
#define FORMAT_FLOAT   _PyFloat_FormatAdvanced
#ifndef WITHOUT_COMPLEX
#define FORMAT_COMPLEX _PyComplex_FormatAdvanced
#endif

#include "../Objects/stringlib/formatter.h"
