#include <Python.h>
#include <locale.h>

static int
case_insensitive_match(const char *s, const char *t)
{
    while(*t && Py_TOLOWER(*s) == *t) {
        s++;
        t++;
    }
    return *t ? 0 : 1;
}

double
_Py_parse_inf_or_nan(const char *p, char **endptr)
{
    double retval;
    const char *s;
    int negate = 0;

    s = p;
    if (*s == '-') {
        negate = 1;
        s++;
    }
    else if (*s == '+') {
        s++;
    }
    if (case_insensitive_match(s, "inf")) {
        s += 3;
        if (case_insensitive_match(s, "inity"))
            s += 5;
        retval = negate ? -Py_HUGE_VAL : Py_HUGE_VAL;
    }
#ifdef Py_NAN
    else if (case_insensitive_match(s, "nan")) {
        s += 3;
        retval = negate ? -Py_NAN : Py_NAN;
    }
#endif
    else {
        s = p;
        retval = -1.0;
    }
    *endptr = (char *)s;
    return retval;
}

#ifndef PY_NO_SHORT_FLOAT_REPR

double
_PyOS_ascii_strtod(const char *nptr, char **endptr)
{
    double result;
    _Py_SET_53BIT_PRECISION_HEADER;

    assert(nptr != NULL);

    errno = 0;

    _Py_SET_53BIT_PRECISION_START;
    result = _Py_dg_strtod(nptr, endptr);
    _Py_SET_53BIT_PRECISION_END;

    if (*endptr == nptr)

        result = _Py_parse_inf_or_nan(nptr, endptr);

    return result;

}

#else

double
_PyOS_ascii_strtod(const char *nptr, char **endptr)
{
    char *fail_pos;
    double val = -1.0;
    struct lconv *locale_data;
    const char *decimal_point;
    size_t decimal_point_len;
    const char *p, *decimal_point_pos;
    const char *end = NULL;
    const char *digits_pos = NULL;
    int negate = 0;

    assert(nptr != NULL);

    fail_pos = NULL;

    locale_data = localeconv();
    decimal_point = locale_data->decimal_point;
    decimal_point_len = strlen(decimal_point);

    assert(decimal_point_len != 0);

    decimal_point_pos = NULL;

    val = _Py_parse_inf_or_nan(nptr, endptr);
    if (*endptr != nptr)
        return val;

    errno = 0;

    p = nptr;

    if (*p == '-') {
        negate = 1;
        p++;
    }
    else if (*p == '+') {
        p++;
    }

    if (*p == '0' && (*(p+1) == 'x' || *(p+1) == 'X'))
        goto invalid_string;

    if (!Py_ISDIGIT(*p) && *p != '.')
        goto invalid_string;

    digits_pos = p;
    if (decimal_point[0] != '.' ||
        decimal_point[1] != 0)
    {

        while (Py_ISDIGIT(*p))
            p++;

        if (*p == '.')
        {
            decimal_point_pos = p++;

            while (Py_ISDIGIT(*p))
                p++;

            if (*p == 'e' || *p == 'E')
                p++;
            if (*p == '+' || *p == '-')
                p++;
            while (Py_ISDIGIT(*p))
                p++;
            end = p;
        }
        else if (strncmp(p, decimal_point, decimal_point_len) == 0)

            goto invalid_string;

    }

    if (decimal_point_pos) {
        char *copy, *c;

        copy = (char *)PyMem_MALLOC(end - digits_pos +
                                    1 + decimal_point_len);
        if (copy == NULL) {
            *endptr = (char *)nptr;
            errno = ENOMEM;
            return val;
        }

        c = copy;
        memcpy(c, digits_pos, decimal_point_pos - digits_pos);
        c += decimal_point_pos - digits_pos;
        memcpy(c, decimal_point, decimal_point_len);
        c += decimal_point_len;
        memcpy(c, decimal_point_pos + 1,
               end - (decimal_point_pos + 1));
        c += end - (decimal_point_pos + 1);
        *c = 0;

        val = strtod(copy, &fail_pos);

        if (fail_pos)
        {
            if (fail_pos > decimal_point_pos)
                fail_pos = (char *)digits_pos +
                    (fail_pos - copy) -
                    (decimal_point_len - 1);
            else
                fail_pos = (char *)digits_pos +
                    (fail_pos - copy);
        }

        PyMem_FREE(copy);

    }
    else {
        val = strtod(digits_pos, &fail_pos);
    }

    if (fail_pos == digits_pos)
        goto invalid_string;

    if (negate && fail_pos != nptr)
        val = -val;
    *endptr = fail_pos;

    return val;

  invalid_string:
    *endptr = (char*)nptr;
    errno = EINVAL;
    return -1.0;
}

#endif

double
PyOS_ascii_strtod(const char *nptr, char **endptr)
{
    char *fail_pos;
    const char *p;
    double x;

    if (PyErr_WarnEx(PyExc_DeprecationWarning,
                     "PyOS_ascii_strtod and PyOS_ascii_atof are "
                     "deprecated.  Use PyOS_string_to_double "
                     "instead.", 1) < 0)
        return -1.0;

    p = nptr;
    while (Py_ISSPACE(*p))
        p++;
    x = _PyOS_ascii_strtod(p, &fail_pos);
    if (fail_pos == p)
        fail_pos = (char *)nptr;
    if (endptr)
        *endptr = (char *)fail_pos;
    return x;
}

double
PyOS_ascii_atof(const char *nptr)
{
    return PyOS_ascii_strtod(nptr, NULL);
}

double
PyOS_string_to_double(const char *s,
                      char **endptr,
                      PyObject *overflow_exception)
{
    double x, result=-1.0;
    char *fail_pos;

    errno = 0;
    PyFPE_START_PROTECT("PyOS_string_to_double", return -1.0)
    x = _PyOS_ascii_strtod(s, &fail_pos);
    PyFPE_END_PROTECT(x)

    if (errno == ENOMEM) {
        PyErr_NoMemory();
        fail_pos = (char *)s;
    }
    else if (!endptr && (fail_pos == s || *fail_pos != '\0'))
        PyErr_Format(PyExc_ValueError,
                      "could not convert string to float: "
                      "%.200s", s);
    else if (fail_pos == s)
        PyErr_Format(PyExc_ValueError,
                      "could not convert string to float: "
                      "%.200s", s);
    else if (errno == ERANGE && fabs(x) >= 1.0 && overflow_exception)
        PyErr_Format(overflow_exception,
                      "value too large to convert to float: "
                      "%.200s", s);
    else
        result = x;

    if (endptr != NULL)
        *endptr = fail_pos;
    return result;
}

Py_LOCAL_INLINE(void)
change_decimal_from_locale_to_dot(char* buffer)
{
    struct lconv *locale_data = localeconv();
    const char *decimal_point = locale_data->decimal_point;

    if (decimal_point[0] != '.' || decimal_point[1] != 0) {
        size_t decimal_point_len = strlen(decimal_point);

        if (*buffer == '+' || *buffer == '-')
            buffer++;
        while (Py_ISDIGIT(*buffer))
            buffer++;
        if (strncmp(buffer, decimal_point, decimal_point_len) == 0) {
            *buffer = '.';
            buffer++;
            if (decimal_point_len > 1) {

                size_t rest_len = strlen(buffer +
                                     (decimal_point_len - 1));
                memmove(buffer,
                    buffer + (decimal_point_len - 1),
                    rest_len);
                buffer[rest_len] = 0;
            }
        }
    }
}

#define MIN_EXPONENT_DIGITS 2

Py_LOCAL_INLINE(void)
ensure_minimum_exponent_length(char* buffer, size_t buf_size)
{
    char *p = strpbrk(buffer, "eE");
    if (p && (*(p + 1) == '-' || *(p + 1) == '+')) {
        char *start = p + 2;
        int exponent_digit_cnt = 0;
        int leading_zero_cnt = 0;
        int in_leading_zeros = 1;
        int significant_digit_cnt;

        p += 2;

        while (*p && Py_ISDIGIT(*p)) {
            if (in_leading_zeros && *p == '0')
                ++leading_zero_cnt;
            if (*p != '0')
                in_leading_zeros = 0;
            ++p;
            ++exponent_digit_cnt;
        }

        significant_digit_cnt = exponent_digit_cnt - leading_zero_cnt;
        if (exponent_digit_cnt == MIN_EXPONENT_DIGITS) {

        }
        else if (exponent_digit_cnt > MIN_EXPONENT_DIGITS) {
            int extra_zeros_cnt;

            if (significant_digit_cnt < MIN_EXPONENT_DIGITS)
                significant_digit_cnt = MIN_EXPONENT_DIGITS;
            extra_zeros_cnt = exponent_digit_cnt -
                significant_digit_cnt;

            assert(extra_zeros_cnt >= 0);

            memmove(start,
                start + extra_zeros_cnt,
                significant_digit_cnt + 1);
        }
        else {

            int zeros = MIN_EXPONENT_DIGITS - exponent_digit_cnt;
            if (start + zeros + exponent_digit_cnt + 1
                  < buffer + buf_size) {
                memmove(start + zeros, start,
                    exponent_digit_cnt + 1);
                memset(start, '0', zeros);
            }
        }
    }
}

Py_LOCAL_INLINE(void)
remove_trailing_zeros(char *buffer)
{
    char *old_fraction_end, *new_fraction_end, *end, *p;

    p = buffer;
    if (*p == '-' || *p == '+')

        ++p;
    while (Py_ISDIGIT(*p))
        ++p;

    if (*p++ != '.')
        return;

    while (Py_ISDIGIT(*p))
        ++p;
    old_fraction_end = p;

    while (*p != '\0')
        p++;

    end = p+1;

    p = old_fraction_end;
    while (*(p-1) == '0')
        --p;

    if (*(p-1) == '.')
        --p;
    new_fraction_end = p;

    memmove(new_fraction_end, old_fraction_end, end-old_fraction_end);
}

Py_LOCAL_INLINE(char *)
ensure_decimal_point(char* buffer, size_t buf_size, int precision)
{
    int digit_count, insert_count = 0, convert_to_exp = 0;
    char *chars_to_insert, *digits_start;

    char *p = buffer;
    if (*p == '-' || *p == '+')

        ++p;
    digits_start = p;
    while (*p && Py_ISDIGIT(*p))
        ++p;
    digit_count = Py_SAFE_DOWNCAST(p - digits_start, Py_ssize_t, int);

    if (*p == '.') {
        if (Py_ISDIGIT(*(p+1))) {

        }
        else {

            assert(precision == -1);
            ++p;
            chars_to_insert = "0";
            insert_count = 1;
        }
    }
    else if (!(*p == 'e' || *p == 'E')) {

        if (digit_count == precision) {

            convert_to_exp = 1;

            assert(*p == '\0');
        }
        else {
            assert(precision == -1 || digit_count < precision);
            chars_to_insert = ".0";
            insert_count = 2;
        }
    }
    if (insert_count) {
        size_t buf_len = strlen(buffer);
        if (buf_len + insert_count + 1 >= buf_size) {

        }
        else {
            memmove(p + insert_count, p,
                buffer + strlen(buffer) - p + 1);
            memcpy(p, chars_to_insert, insert_count);
        }
    }
    if (convert_to_exp) {
        int written;
        size_t buf_avail;
        p = digits_start;

        assert(digit_count >= 1);
        memmove(p+2, p+1, digit_count);
        p[1] = '.';
        p += digit_count+1;
        assert(p <= buf_size+buffer);
        buf_avail = buf_size+buffer-p;
        if (buf_avail == 0)
            return NULL;

        written = PyOS_snprintf(p, buf_avail, "e%+.02d", digit_count-1);
        if (!(0 <= written &&
              written < Py_SAFE_DOWNCAST(buf_avail, size_t, int)))

            return NULL;
        remove_trailing_zeros(buffer);
    }
    return buffer;
}

#define FLOAT_FORMATBUFLEN 120

char *
_PyOS_ascii_formatd(char       *buffer,
                   size_t      buf_size,
                   const char *format,
                   double      d,
                   int         precision)
{
    char format_char;
    size_t format_len = strlen(format);

    char tmp_format[FLOAT_FORMATBUFLEN];

    format_char = format[format_len - 1];

    if (format[0] != '%')
        return NULL;

    if (strpbrk(format + 1, "'l%"))
        return NULL;

    if (!(format_char == 'e' || format_char == 'E' ||
          format_char == 'f' || format_char == 'F' ||
          format_char == 'g' || format_char == 'G' ||
          format_char == 'Z'))
        return NULL;

    if (format_char == 'Z') {
        if (format_len + 1 >= sizeof(tmp_format)) {

            return NULL;
        }
        strcpy(tmp_format, format);
        tmp_format[format_len - 1] = 'g';
        format = tmp_format;
    }

    PyOS_snprintf(buffer, buf_size, format, d);

    change_decimal_from_locale_to_dot(buffer);

    ensure_minimum_exponent_length(buffer, buf_size);

    if (format_char == 'Z')
        buffer = ensure_decimal_point(buffer, buf_size, precision);

    return buffer;
}

char *
PyOS_ascii_formatd(char       *buffer,
                   size_t      buf_size,
                   const char *format,
                   double      d)
{
    if (PyErr_WarnEx(PyExc_DeprecationWarning,
                     "PyOS_ascii_formatd is deprecated, "
                     "use PyOS_double_to_string instead", 1) < 0)
        return NULL;

    return _PyOS_ascii_formatd(buffer, buf_size, format, d, -1);
}

#ifdef PY_NO_SHORT_FLOAT_REPR

PyAPI_FUNC(char *) PyOS_double_to_string(double val,
                                         char format_code,
                                         int precision,
                                         int flags,
                                         int *type)
{
    char format[32];
    Py_ssize_t bufsize;
    char *buf;
    int t, exp;
    int upper = 0;

    switch (format_code) {
    case 'e':
    case 'f':
    case 'g':
        break;
    case 'E':
        upper = 1;
        format_code = 'e';
        break;
    case 'F':
        upper = 1;
        format_code = 'f';
        break;
    case 'G':
        upper = 1;
        format_code = 'g';
        break;
    case 'r':

        if (precision != 0) {
            PyErr_BadInternalCall();
            return NULL;
        }

        precision = 17;
        format_code = 'g';
        break;
    default:
        PyErr_BadInternalCall();
        return NULL;
    }

    if (Py_IS_NAN(val) || Py_IS_INFINITY(val))

        bufsize = 5;
    else {
        bufsize = 25 + precision;
        if (format_code == 'f' && fabs(val) >= 1.0) {
            frexp(val, &exp);
            bufsize += exp/3;
        }
    }

    buf = PyMem_Malloc(bufsize);
    if (buf == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    if (Py_IS_NAN(val)) {
        strcpy(buf, "nan");
        t = Py_DTST_NAN;
    } else if (Py_IS_INFINITY(val)) {
        if (copysign(1., val) == 1.)
            strcpy(buf, "inf");
        else
            strcpy(buf, "-inf");
        t = Py_DTST_INFINITE;
    } else {
        t = Py_DTST_FINITE;
        if (flags & Py_DTSF_ADD_DOT_0)
            format_code = 'Z';

        PyOS_snprintf(format, sizeof(format), "%%%s.%i%c",
                      (flags & Py_DTSF_ALT ? "#" : ""), precision,
                      format_code);
        _PyOS_ascii_formatd(buf, bufsize, format, val, precision);
    }

    if (flags & Py_DTSF_SIGN && buf[0] != '-') {
        size_t len = strlen(buf);

        assert((size_t)bufsize >= len+2);
        memmove(buf+1, buf, len+1);
        buf[0] = '+';
    }
    if (upper) {

        char *p1;
        for (p1 = buf; *p1; p1++)
            *p1 = Py_TOUPPER(*p1);
    }

    if (type)
        *type = t;
    return buf;
}

#else

#define OFS_INF 0
#define OFS_NAN 1
#define OFS_E 2

static char *lc_float_strings[] = {
    "inf",
    "nan",
    "e",
};
static char *uc_float_strings[] = {
    "INF",
    "NAN",
    "E",
};

static char *
format_float_short(double d, char format_code,
                   int mode, Py_ssize_t precision,
                   int always_add_sign, int add_dot_0_if_integer,
                   int use_alt_formatting, char **float_strings, int *type)
{
    char *buf = NULL;
    char *p = NULL;
    Py_ssize_t bufsize = 0;
    char *digits, *digits_end;
    int decpt_as_int, sign, exp_len, exp = 0, use_exp = 0;
    Py_ssize_t decpt, digits_len, vdigits_start, vdigits_end;
    _Py_SET_53BIT_PRECISION_HEADER;

    _Py_SET_53BIT_PRECISION_START;
    digits = _Py_dg_dtoa(d, mode, precision, &decpt_as_int, &sign,
                         &digits_end);
    _Py_SET_53BIT_PRECISION_END;

    decpt = (Py_ssize_t)decpt_as_int;
    if (digits == NULL) {

        PyErr_NoMemory();
        goto exit;
    }
    assert(digits_end != NULL && digits_end >= digits);
    digits_len = digits_end - digits;

    if (digits_len && !Py_ISDIGIT(digits[0])) {

        if (digits[0] == 'n' || digits[0] == 'N')
            sign = 0;

        bufsize = 5;
        buf = (char *)PyMem_Malloc(bufsize);
        if (buf == NULL) {
            PyErr_NoMemory();
            goto exit;
        }
        p = buf;

        if (sign == 1) {
            *p++ = '-';
        }
        else if (always_add_sign) {
            *p++ = '+';
        }
        if (digits[0] == 'i' || digits[0] == 'I') {
            strncpy(p, float_strings[OFS_INF], 3);
            p += 3;

            if (type)
                *type = Py_DTST_INFINITE;
        }
        else if (digits[0] == 'n' || digits[0] == 'N') {
            strncpy(p, float_strings[OFS_NAN], 3);
            p += 3;

            if (type)
                *type = Py_DTST_NAN;
        }
        else {

            strncpy(p, "ERR", 3);
            p += 3;
            assert(0);
        }
        goto exit;
    }

    if (type)
        *type = Py_DTST_FINITE;

    vdigits_end = digits_len;
    switch (format_code) {
    case 'e':
        use_exp = 1;
        vdigits_end = precision;
        break;
    case 'f':
        vdigits_end = decpt + precision;
        break;
    case 'g':
        if (decpt <= -4 || decpt >
            (add_dot_0_if_integer ? precision-1 : precision))
            use_exp = 1;
        if (use_alt_formatting)
            vdigits_end = precision;
        break;
    case 'r':

        if (decpt <= -4 || decpt > 16)
            use_exp = 1;
        break;
    default:
        PyErr_BadInternalCall();
        goto exit;
    }

    if (use_exp) {
        exp = decpt - 1;
        decpt = 1;
    }

    vdigits_start = decpt <= 0 ? decpt-1 : 0;
    if (!use_exp && add_dot_0_if_integer)
        vdigits_end = vdigits_end > decpt ? vdigits_end : decpt + 1;
    else
        vdigits_end = vdigits_end > decpt ? vdigits_end : decpt;

    assert(vdigits_start <= 0 &&
           0 <= digits_len &&
           digits_len <= vdigits_end);

    assert(vdigits_start < decpt && decpt <= vdigits_end);

    bufsize =

        3 +

        (vdigits_end - vdigits_start) +

        (use_exp ? 5 : 0);

    buf = (char *)PyMem_Malloc(bufsize);
    if (buf == NULL) {
        PyErr_NoMemory();
        goto exit;
    }
    p = buf;

    if (sign == 1)
        *p++ = '-';
    else if (always_add_sign)
        *p++ = '+';

    if (decpt <= 0) {
        memset(p, '0', decpt-vdigits_start);
        p += decpt - vdigits_start;
        *p++ = '.';
        memset(p, '0', 0-decpt);
        p += 0-decpt;
    }
    else {
        memset(p, '0', 0-vdigits_start);
        p += 0 - vdigits_start;
    }

    if (0 < decpt && decpt <= digits_len) {
        strncpy(p, digits, decpt-0);
        p += decpt-0;
        *p++ = '.';
        strncpy(p, digits+decpt, digits_len-decpt);
        p += digits_len-decpt;
    }
    else {
        strncpy(p, digits, digits_len);
        p += digits_len;
    }

    if (digits_len < decpt) {
        memset(p, '0', decpt-digits_len);
        p += decpt-digits_len;
        *p++ = '.';
        memset(p, '0', vdigits_end-decpt);
        p += vdigits_end-decpt;
    }
    else {
        memset(p, '0', vdigits_end-digits_len);
        p += vdigits_end-digits_len;
    }

    if (p[-1] == '.' && !use_alt_formatting)
        p--;

    if (use_exp) {
        *p++ = float_strings[OFS_E][0];
        exp_len = sprintf(p, "%+.02d", exp);
        p += exp_len;
    }
  exit:
    if (buf) {
        *p = '\0';

        assert(p-buf < bufsize);
    }
    if (digits)
        _Py_dg_freedtoa(digits);

    return buf;
}

PyAPI_FUNC(char *) PyOS_double_to_string(double val,
                                         char format_code,
                                         int precision,
                                         int flags,
                                         int *type)
{
    char **float_strings = lc_float_strings;
    int mode;

    switch (format_code) {

    case 'E':
        float_strings = uc_float_strings;
        format_code = 'e';

    case 'e':
        mode = 2;
        precision++;
        break;

    case 'F':
        float_strings = uc_float_strings;
        format_code = 'f';

    case 'f':
        mode = 3;
        break;

    case 'G':
        float_strings = uc_float_strings;
        format_code = 'g';

    case 'g':
        mode = 2;

        if (precision == 0)
            precision = 1;
        break;

    case 'r':
        mode = 0;

        if (precision != 0) {
            PyErr_BadInternalCall();
            return NULL;
        }
        break;

    default:
        PyErr_BadInternalCall();
        return NULL;
    }

    return format_float_short(val, format_code, mode, precision,
                              flags & Py_DTSF_SIGN,
                              flags & Py_DTSF_ADD_DOT_0,
                              flags & Py_DTSF_ALT,
                              float_strings, type);
}
#endif
