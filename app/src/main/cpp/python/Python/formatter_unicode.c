#include "Python.h"

#ifdef Py_USING_UNICODE

#include "../Objects/stringlib/unicodedefs.h"

#define FORMAT_STRING _PyUnicode_FormatAdvanced

#include "../Objects/stringlib/formatter.h"

#endif
