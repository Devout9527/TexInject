#if PY_VERSION_HEX < 0x03000000
#define PyLong_FromSsize_t _PyLong_FromSsize_t
#endif

#define INITIAL_SIZE_INCREMENT 100
#define SIZE_MULTIPLIER 2
#define MAX_SIZE_INCREMENT  3200

typedef struct {
    STRINGLIB_CHAR *ptr;
    STRINGLIB_CHAR *end;
} SubString;

typedef enum {
    ANS_INIT,
    ANS_AUTO,
    ANS_MANUAL
} AutoNumberState;

typedef struct {
    AutoNumberState an_state;
    int an_field_number;
} AutoNumber;

static PyObject *
build_string(SubString *input, PyObject *args, PyObject *kwargs,
             int recursion_depth, AutoNumber *auto_number);

static void
AutoNumber_Init(AutoNumber *auto_number)
{
    auto_number->an_state = ANS_INIT;
    auto_number->an_field_number = 0;
}

Py_LOCAL_INLINE(void)
SubString_init(SubString *str, STRINGLIB_CHAR *p, Py_ssize_t len)
{
    str->ptr = p;
    if (p == NULL)
        str->end = NULL;
    else
        str->end = str->ptr + len;
}

Py_LOCAL_INLINE(PyObject *)
SubString_new_object(SubString *str)
{
    if (str->ptr == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    return STRINGLIB_NEW(str->ptr, str->end - str->ptr);
}

Py_LOCAL_INLINE(PyObject *)
SubString_new_object_or_empty(SubString *str)
{
    if (str->ptr == NULL) {
        return STRINGLIB_NEW(NULL, 0);
    }
    return STRINGLIB_NEW(str->ptr, str->end - str->ptr);
}

static int
autonumber_state_error(AutoNumberState state, int field_name_is_empty)
{
    if (state == ANS_MANUAL) {
        if (field_name_is_empty) {
            PyErr_SetString(PyExc_ValueError, "cannot switch from "
                            "manual field specification to "
                            "automatic field numbering");
            return 1;
        }
    }
    else {
        if (!field_name_is_empty) {
            PyErr_SetString(PyExc_ValueError, "cannot switch from "
                            "automatic field numbering to "
                            "manual field specification");
            return 1;
        }
    }
    return 0;
}

typedef struct {
    STRINGLIB_CHAR *ptr;
    STRINGLIB_CHAR *end;
    PyObject *obj;
    Py_ssize_t size_increment;
} OutputString;

static int
output_initialize(OutputString *output, Py_ssize_t size)
{
    output->obj = STRINGLIB_NEW(NULL, size);
    if (output->obj == NULL)
        return 0;

    output->ptr = STRINGLIB_STR(output->obj);
    output->end = STRINGLIB_LEN(output->obj) + output->ptr;
    output->size_increment = INITIAL_SIZE_INCREMENT;

    return 1;
}

static int
output_extend(OutputString *output, Py_ssize_t count)
{
    STRINGLIB_CHAR *startptr = STRINGLIB_STR(output->obj);
    Py_ssize_t curlen = output->ptr - startptr;
    Py_ssize_t maxlen = curlen + count + output->size_increment;

    if (STRINGLIB_RESIZE(&output->obj, maxlen) < 0)
        return 0;
    startptr = STRINGLIB_STR(output->obj);
    output->ptr = startptr + curlen;
    output->end = startptr + maxlen;
    if (output->size_increment < MAX_SIZE_INCREMENT)
        output->size_increment *= SIZE_MULTIPLIER;
    return 1;
}

static int
output_data(OutputString *output, const STRINGLIB_CHAR *s, Py_ssize_t count)
{
    if ((count > output->end - output->ptr) && !output_extend(output, count))
        return 0;
    memcpy(output->ptr, s, count * sizeof(STRINGLIB_CHAR));
    output->ptr += count;
    return 1;
}

static Py_ssize_t
get_integer(const SubString *str)
{
    Py_ssize_t accumulator = 0;
    Py_ssize_t digitval;
    STRINGLIB_CHAR *p;

    if (str->ptr >= str->end)
        return -1;

    for (p = str->ptr; p < str->end; p++) {
        digitval = STRINGLIB_TODECIMAL(*p);
        if (digitval < 0)
            return -1;

        if (accumulator > (PY_SSIZE_T_MAX - digitval) / 10) {
            PyErr_Format(PyExc_ValueError,
                         "Too many decimal digits in format string");
            return -1;
        }
        accumulator = accumulator * 10 + digitval;
    }
    return accumulator;
}

static PyObject *
getattr(PyObject *obj, SubString *name)
{
    PyObject *newobj;
    PyObject *str = SubString_new_object(name);
    if (str == NULL)
        return NULL;
    newobj = PyObject_GetAttr(obj, str);
    Py_DECREF(str);
    return newobj;
}

static PyObject *
getitem_sequence(PyObject *obj, Py_ssize_t idx)
{
    return PySequence_GetItem(obj, idx);
}

static PyObject *
getitem_idx(PyObject *obj, Py_ssize_t idx)
{
    PyObject *newobj;
    PyObject *idx_obj = PyLong_FromSsize_t(idx);
    if (idx_obj == NULL)
        return NULL;
    newobj = PyObject_GetItem(obj, idx_obj);
    Py_DECREF(idx_obj);
    return newobj;
}

static PyObject *
getitem_str(PyObject *obj, SubString *name)
{
    PyObject *newobj;
    PyObject *str = SubString_new_object(name);
    if (str == NULL)
        return NULL;
    newobj = PyObject_GetItem(obj, str);
    Py_DECREF(str);
    return newobj;
}

typedef struct {

    SubString str;

    STRINGLIB_CHAR *ptr;
} FieldNameIterator;

static int
FieldNameIterator_init(FieldNameIterator *self, STRINGLIB_CHAR *ptr,
                       Py_ssize_t len)
{
    SubString_init(&self->str, ptr, len);
    self->ptr = self->str.ptr;
    return 1;
}

static int
_FieldNameIterator_attr(FieldNameIterator *self, SubString *name)
{
    STRINGLIB_CHAR c;

    name->ptr = self->ptr;

    while (self->ptr < self->str.end) {
        switch (c = *self->ptr++) {
        case '[':
        case '.':

            self->ptr--;
            break;
        default:
            continue;
        }
        break;
    }

    name->end = self->ptr;
    return 1;
}

static int
_FieldNameIterator_item(FieldNameIterator *self, SubString *name)
{
    int bracket_seen = 0;
    STRINGLIB_CHAR c;

    name->ptr = self->ptr;

    while (self->ptr < self->str.end) {
        switch (c = *self->ptr++) {
        case ']':
            bracket_seen = 1;
            break;
        default:
            continue;
        }
        break;
    }

    if (!bracket_seen) {
        PyErr_SetString(PyExc_ValueError, "Missing ']' in format string");
        return 0;
    }

    name->end = self->ptr-1;
    return 1;
}

static int
FieldNameIterator_next(FieldNameIterator *self, int *is_attribute,
                       Py_ssize_t *name_idx, SubString *name)
{

    if (self->ptr >= self->str.end)
        return 1;

    switch (*self->ptr++) {
    case '.':
        *is_attribute = 1;
        if (_FieldNameIterator_attr(self, name) == 0)
            return 0;
        *name_idx = -1;
        break;
    case '[':
        *is_attribute = 0;
        if (_FieldNameIterator_item(self, name) == 0)
            return 0;
        *name_idx = get_integer(name);
        if (*name_idx == -1 && PyErr_Occurred())
            return 0;
        break;
    default:

        PyErr_SetString(PyExc_ValueError, "Only '.' or '[' may "
                        "follow ']' in format field specifier");
        return 0;
    }

    if (name->ptr == name->end) {
        PyErr_SetString(PyExc_ValueError, "Empty attribute in format string");
        return 0;
    }

    return 2;
}

static int
field_name_split(STRINGLIB_CHAR *ptr, Py_ssize_t len, SubString *first,
                 Py_ssize_t *first_idx, FieldNameIterator *rest,
                 AutoNumber *auto_number)
{
    STRINGLIB_CHAR c;
    STRINGLIB_CHAR *p = ptr;
    STRINGLIB_CHAR *end = ptr + len;
    int field_name_is_empty;
    int using_numeric_index;

    while (p < end) {
        switch (c = *p++) {
        case '[':
        case '.':

            p--;
            break;
        default:
            continue;
        }
        break;
    }

    SubString_init(first, ptr, p - ptr);
    FieldNameIterator_init(rest, p, end - p);

    *first_idx = get_integer(first);
    if (*first_idx == -1 && PyErr_Occurred())
        return 0;

    field_name_is_empty = first->ptr >= first->end;

    using_numeric_index = field_name_is_empty || *first_idx != -1;

    if (auto_number) {

        if (auto_number->an_state == ANS_INIT && using_numeric_index)
            auto_number->an_state = field_name_is_empty ?
                ANS_AUTO : ANS_MANUAL;

        if (using_numeric_index)
            if (autonumber_state_error(auto_number->an_state,
                                       field_name_is_empty))
                return 0;

        if (field_name_is_empty)
            *first_idx = (auto_number->an_field_number)++;
    }

    return 1;
}

static PyObject *
get_field_object(SubString *input, PyObject *args, PyObject *kwargs,
                 AutoNumber *auto_number)
{
    PyObject *obj = NULL;
    int ok;
    int is_attribute;
    SubString name;
    SubString first;
    Py_ssize_t index;
    FieldNameIterator rest;

    if (!field_name_split(input->ptr, input->end - input->ptr, &first,
                          &index, &rest, auto_number)) {
        goto error;
    }

    if (index == -1) {

        PyObject *key = SubString_new_object(&first);
        if (key == NULL)
            goto error;
        if ((kwargs == NULL) || (obj = PyDict_GetItem(kwargs, key)) == NULL) {
            PyErr_SetObject(PyExc_KeyError, key);
            Py_DECREF(key);
            goto error;
        }
        Py_DECREF(key);
        Py_INCREF(obj);
    }
    else {

        obj = PySequence_GetItem(args, index);
        if (obj == NULL)
            goto error;
    }

    while ((ok = FieldNameIterator_next(&rest, &is_attribute, &index,
                                        &name)) == 2) {
        PyObject *tmp;

        if (is_attribute)

            tmp = getattr(obj, &name);
        else

            if (index == -1)
                tmp = getitem_str(obj, &name);
            else
                if (PySequence_Check(obj))
                    tmp = getitem_sequence(obj, index);
                else

                    tmp = getitem_idx(obj, index);
        if (tmp == NULL)
            goto error;

        Py_DECREF(obj);
        obj = tmp;
    }

    if (ok == 1)
        return obj;
error:
    Py_XDECREF(obj);
    return NULL;
}

static int
render_field(PyObject *fieldobj, SubString *format_spec, OutputString *output)
{
    int ok = 0;
    PyObject *result = NULL;
    PyObject *format_spec_object = NULL;
    PyObject *(*formatter)(PyObject *, STRINGLIB_CHAR *, Py_ssize_t) = NULL;
    STRINGLIB_CHAR* format_spec_start = format_spec->ptr ?
            format_spec->ptr : NULL;
    Py_ssize_t format_spec_len = format_spec->ptr ?
            format_spec->end - format_spec->ptr : 0;

#if STRINGLIB_IS_UNICODE
    if (PyUnicode_CheckExact(fieldobj))
        formatter = _PyUnicode_FormatAdvanced;

#else
    if (PyString_CheckExact(fieldobj))
        formatter = _PyBytes_FormatAdvanced;
    else if (PyInt_CheckExact(fieldobj))
        formatter =_PyInt_FormatAdvanced;
    else if (PyLong_CheckExact(fieldobj))
        formatter =_PyLong_FormatAdvanced;
    else if (PyFloat_CheckExact(fieldobj))
        formatter = _PyFloat_FormatAdvanced;
#endif

    if (formatter) {

        result = formatter(fieldobj, format_spec_start, format_spec_len);
    }
    else {

        format_spec_object = STRINGLIB_NEW(format_spec_start,
                                           format_spec_len);
        if (format_spec_object == NULL)
            goto done;

        result = PyObject_Format(fieldobj, format_spec_object);
    }
    if (result == NULL)
        goto done;

#if PY_VERSION_HEX >= 0x03000000
    assert(PyUnicode_Check(result));
#else
    assert(PyString_Check(result) || PyUnicode_Check(result));

    {
        PyObject *tmp = STRINGLIB_TOSTR(result);
        if (tmp == NULL)
            goto done;
        Py_DECREF(result);
        result = tmp;
    }
#endif

    ok = output_data(output,
                     STRINGLIB_STR(result), STRINGLIB_LEN(result));
done:
    Py_XDECREF(format_spec_object);
    Py_XDECREF(result);
    return ok;
}

static int
parse_field(SubString *str, SubString *field_name, SubString *format_spec,
            STRINGLIB_CHAR *conversion)
{

    STRINGLIB_CHAR c = 0;

    *conversion = '\0';
    SubString_init(format_spec, NULL, 0);

    field_name->ptr = str->ptr;
    while (str->ptr < str->end) {
        switch (c = *(str->ptr++)) {
        case ':':
        case '!':
            break;
        default:
            continue;
        }
        break;
    }

    if (c == '!' || c == ':') {

        field_name->end = str->ptr-1;

        format_spec->ptr = str->ptr;
        format_spec->end = str->end;

        if (c == '!') {

            if (format_spec->ptr >= format_spec->end) {
                PyErr_SetString(PyExc_ValueError,
                                "end of format while looking for conversion "
                                "specifier");
                return 0;
            }
            *conversion = *(format_spec->ptr++);

            if (format_spec->ptr < format_spec->end) {
                c = *(format_spec->ptr++);
                if (c != ':') {
                    PyErr_SetString(PyExc_ValueError,
                                    "expected ':' after format specifier");
                    return 0;
                }
            }
        }
    }
    else

        field_name->end = str->ptr;

    return 1;
}

typedef struct {
    SubString str;
} MarkupIterator;

static int
MarkupIterator_init(MarkupIterator *self, STRINGLIB_CHAR *ptr, Py_ssize_t len)
{
    SubString_init(&self->str, ptr, len);
    return 1;
}

static int
MarkupIterator_next(MarkupIterator *self, SubString *literal,
                    int *field_present, SubString *field_name,
                    SubString *format_spec, STRINGLIB_CHAR *conversion,
                    int *format_spec_needs_expanding)
{
    int at_end;
    STRINGLIB_CHAR c = 0;
    STRINGLIB_CHAR *start;
    int count;
    Py_ssize_t len;
    int markup_follows = 0;

    SubString_init(literal, NULL, 0);
    SubString_init(field_name, NULL, 0);
    SubString_init(format_spec, NULL, 0);
    *conversion = '\0';
    *format_spec_needs_expanding = 0;
    *field_present = 0;

    if (self->str.ptr >= self->str.end)
        return 1;

    start = self->str.ptr;

    while (self->str.ptr < self->str.end) {
        switch (c = *(self->str.ptr++)) {
        case '{':
        case '}':
            markup_follows = 1;
            break;
        default:
            continue;
        }
        break;
    }

    at_end = self->str.ptr >= self->str.end;
    len = self->str.ptr - start;

    if ((c == '}') && (at_end || (c != *self->str.ptr))) {
        PyErr_SetString(PyExc_ValueError, "Single '}' encountered "
                        "in format string");
        return 0;
    }
    if (at_end && c == '{') {
        PyErr_SetString(PyExc_ValueError, "Single '{' encountered "
                        "in format string");
        return 0;
    }
    if (!at_end) {
        if (c == *self->str.ptr) {

            self->str.ptr++;
            markup_follows = 0;
        }
        else
            len--;
    }

    literal->ptr = start;
    literal->end = start + len;

    if (!markup_follows)
        return 2;

    *field_present = 1;
    count = 1;

    start = self->str.ptr;

    while (self->str.ptr < self->str.end) {
        switch (c = *(self->str.ptr++)) {
        case '{':

            *format_spec_needs_expanding = 1;
            count++;
            break;
        case '}':
            count--;
            if (count <= 0) {

                SubString s;

                SubString_init(&s, start, self->str.ptr - 1 - start);
                if (parse_field(&s, field_name, format_spec, conversion) == 0)
                    return 0;

                return 2;
            }
            break;
        }
    }

    PyErr_SetString(PyExc_ValueError, "unmatched '{' in format");
    return 0;
}

static PyObject *
do_conversion(PyObject *obj, STRINGLIB_CHAR conversion)
{

    switch (conversion) {
    case 'r':
        return PyObject_Repr(obj);
    case 's':
        return STRINGLIB_TOSTR(obj);
    default:
        if (conversion > 32 && conversion < 127) {

                PyErr_Format(PyExc_ValueError,
                     "Unknown conversion specifier %c",
                     (char)conversion);
        } else
                PyErr_Format(PyExc_ValueError,
                     "Unknown conversion specifier \\x%x",
                     (unsigned int)conversion);
        return NULL;
    }
}

static int
output_markup(SubString *field_name, SubString *format_spec,
              int format_spec_needs_expanding, STRINGLIB_CHAR conversion,
              OutputString *output, PyObject *args, PyObject *kwargs,
              int recursion_depth, AutoNumber *auto_number)
{
    PyObject *tmp = NULL;
    PyObject *fieldobj = NULL;
    SubString expanded_format_spec;
    SubString *actual_format_spec;
    int result = 0;

    fieldobj = get_field_object(field_name, args, kwargs, auto_number);
    if (fieldobj == NULL)
        goto done;

    if (conversion != '\0') {
        tmp = do_conversion(fieldobj, conversion);
        if (tmp == NULL)
            goto done;

        Py_DECREF(fieldobj);
        fieldobj = tmp;
        tmp = NULL;
    }

    if (format_spec_needs_expanding) {
        tmp = build_string(format_spec, args, kwargs, recursion_depth-1,
                           auto_number);
        if (tmp == NULL)
            goto done;

        SubString_init(&expanded_format_spec,
                       STRINGLIB_STR(tmp), STRINGLIB_LEN(tmp));
        actual_format_spec = &expanded_format_spec;
    }
    else
        actual_format_spec = format_spec;

    if (render_field(fieldobj, actual_format_spec, output) == 0)
        goto done;

    result = 1;

done:
    Py_XDECREF(fieldobj);
    Py_XDECREF(tmp);

    return result;
}

static int
do_markup(SubString *input, PyObject *args, PyObject *kwargs,
          OutputString *output, int recursion_depth, AutoNumber *auto_number)
{
    MarkupIterator iter;
    int format_spec_needs_expanding;
    int result;
    int field_present;
    SubString literal;
    SubString field_name;
    SubString format_spec;
    STRINGLIB_CHAR conversion;

    MarkupIterator_init(&iter, input->ptr, input->end - input->ptr);
    while ((result = MarkupIterator_next(&iter, &literal, &field_present,
                                         &field_name, &format_spec,
                                         &conversion,
                                         &format_spec_needs_expanding)) == 2) {
        if (!output_data(output, literal.ptr, literal.end - literal.ptr))
            return 0;
        if (field_present)
            if (!output_markup(&field_name, &format_spec,
                               format_spec_needs_expanding, conversion, output,
                               args, kwargs, recursion_depth, auto_number))
                return 0;
    }
    return result;
}

static PyObject *
build_string(SubString *input, PyObject *args, PyObject *kwargs,
             int recursion_depth, AutoNumber *auto_number)
{
    OutputString output;
    PyObject *result = NULL;
    Py_ssize_t count;

    output.obj = NULL;

    if (recursion_depth <= 0) {
        PyErr_SetString(PyExc_ValueError,
                        "Max string recursion exceeded");
        goto done;
    }

    if (!output_initialize(&output,
                           input->end - input->ptr +
                           INITIAL_SIZE_INCREMENT))
        goto done;

    if (!do_markup(input, args, kwargs, &output, recursion_depth,
                   auto_number)) {
        goto done;
    }

    count = output.ptr - STRINGLIB_STR(output.obj);
    if (STRINGLIB_RESIZE(&output.obj, count) < 0) {
        goto done;
    }

    result = output.obj;
    output.obj = NULL;

done:
    Py_XDECREF(output.obj);
    return result;
}

static PyObject *
do_string_format(PyObject *self, PyObject *args, PyObject *kwargs)
{
    SubString input;

    int recursion_depth = 2;

    AutoNumber auto_number;

    AutoNumber_Init(&auto_number);
    SubString_init(&input, STRINGLIB_STR(self), STRINGLIB_LEN(self));
    return build_string(&input, args, kwargs, recursion_depth, &auto_number);
}

typedef struct {
    PyObject_HEAD

    STRINGLIB_OBJECT *str;

    MarkupIterator it_markup;
} formatteriterobject;

static void
formatteriter_dealloc(formatteriterobject *it)
{
    Py_XDECREF(it->str);
    PyObject_FREE(it);
}

static PyObject *
formatteriter_next(formatteriterobject *it)
{
    SubString literal;
    SubString field_name;
    SubString format_spec;
    STRINGLIB_CHAR conversion;
    int format_spec_needs_expanding;
    int field_present;
    int result = MarkupIterator_next(&it->it_markup, &literal, &field_present,
                                     &field_name, &format_spec, &conversion,
                                     &format_spec_needs_expanding);

    assert(0 <= result && result <= 2);
    if (result == 0 || result == 1)

        return NULL;
    else {
        PyObject *literal_str = NULL;
        PyObject *field_name_str = NULL;
        PyObject *format_spec_str = NULL;
        PyObject *conversion_str = NULL;
        PyObject *tuple = NULL;

        literal_str = SubString_new_object(&literal);
        if (literal_str == NULL)
            goto done;

        field_name_str = SubString_new_object(&field_name);
        if (field_name_str == NULL)
            goto done;

        format_spec_str = (field_present ?
                           SubString_new_object_or_empty :
                           SubString_new_object)(&format_spec);
        if (format_spec_str == NULL)
            goto done;

        if (conversion == '\0') {
            conversion_str = Py_None;
            Py_INCREF(conversion_str);
        }
        else
            conversion_str = STRINGLIB_NEW(&conversion, 1);
        if (conversion_str == NULL)
            goto done;

        tuple = PyTuple_Pack(4, literal_str, field_name_str, format_spec_str,
                             conversion_str);
    done:
        Py_XDECREF(literal_str);
        Py_XDECREF(field_name_str);
        Py_XDECREF(format_spec_str);
        Py_XDECREF(conversion_str);
        return tuple;
    }
}

static PyMethodDef formatteriter_methods[] = {
    {NULL,              NULL}
};

static PyTypeObject PyFormatterIter_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "formatteriterator",
    sizeof(formatteriterobject),
    0,

    (destructor)formatteriter_dealloc,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT,
    0,
    0,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)formatteriter_next,
    formatteriter_methods,
    0,
};

static PyObject *
formatter_parser(STRINGLIB_OBJECT *self)
{
    formatteriterobject *it;

    it = PyObject_New(formatteriterobject, &PyFormatterIter_Type);
    if (it == NULL)
        return NULL;

    Py_INCREF(self);
    it->str = self;

    MarkupIterator_init(&it->it_markup,
                        STRINGLIB_STR(self),
                        STRINGLIB_LEN(self));

    return (PyObject *)it;
}

typedef struct {
    PyObject_HEAD

    STRINGLIB_OBJECT *str;

    FieldNameIterator it_field;
} fieldnameiterobject;

static void
fieldnameiter_dealloc(fieldnameiterobject *it)
{
    Py_XDECREF(it->str);
    PyObject_FREE(it);
}

static PyObject *
fieldnameiter_next(fieldnameiterobject *it)
{
    int result;
    int is_attr;
    Py_ssize_t idx;
    SubString name;

    result = FieldNameIterator_next(&it->it_field, &is_attr,
                                    &idx, &name);
    if (result == 0 || result == 1)

        return NULL;
    else {
        PyObject* result = NULL;
        PyObject* is_attr_obj = NULL;
        PyObject* obj = NULL;

        is_attr_obj = PyBool_FromLong(is_attr);
        if (is_attr_obj == NULL)
            goto done;

        if (idx != -1)
            obj = PyLong_FromSsize_t(idx);
        else
            obj = SubString_new_object(&name);
        if (obj == NULL)
            goto done;

        result = PyTuple_Pack(2, is_attr_obj, obj);

    done:
        Py_XDECREF(is_attr_obj);
        Py_XDECREF(obj);
        return result;
    }
}

static PyMethodDef fieldnameiter_methods[] = {
    {NULL,              NULL}
};

static PyTypeObject PyFieldNameIter_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "fieldnameiterator",
    sizeof(fieldnameiterobject),
    0,

    (destructor)fieldnameiter_dealloc,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT,
    0,
    0,
    0,
    0,
    0,
    PyObject_SelfIter,
    (iternextfunc)fieldnameiter_next,
    fieldnameiter_methods,
    0};

static PyObject *
formatter_field_name_split(STRINGLIB_OBJECT *self)
{
    SubString first;
    Py_ssize_t first_idx;
    fieldnameiterobject *it;

    PyObject *first_obj = NULL;
    PyObject *result = NULL;

    it = PyObject_New(fieldnameiterobject, &PyFieldNameIter_Type);
    if (it == NULL)
        return NULL;

    Py_INCREF(self);
    it->str = self;

    if (!field_name_split(STRINGLIB_STR(self),
                          STRINGLIB_LEN(self),
                          &first, &first_idx, &it->it_field, NULL))
        goto done;

    if (first_idx != -1)
        first_obj = PyLong_FromSsize_t(first_idx);
    else

        first_obj = SubString_new_object(&first);
    if (first_obj == NULL)
        goto done;

    result = PyTuple_Pack(2, first_obj, it);

done:
    Py_XDECREF(it);
    Py_XDECREF(first_obj);
    return result;
}
