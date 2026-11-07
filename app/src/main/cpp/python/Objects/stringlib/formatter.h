#include <locale.h>

static void
unknown_presentation_type(STRINGLIB_CHAR presentation_type,
                          const char* type_name)
{
#if STRINGLIB_IS_UNICODE

    if (presentation_type > 32 && presentation_type < 128)
#endif
        PyErr_Format(PyExc_ValueError,
                     "Unknown format code '%c' "
                     "for object of type '%.200s'",
                     (char)presentation_type,
                     type_name);
#if STRINGLIB_IS_UNICODE
    else
        PyErr_Format(PyExc_ValueError,
                     "Unknown format code '\\x%x' "
                     "for object of type '%.200s'",
                     (unsigned int)presentation_type,
                     type_name);
#endif
}

static void
invalid_comma_type(STRINGLIB_CHAR presentation_type)
{
#if STRINGLIB_IS_UNICODE

    if (presentation_type > 32 && presentation_type < 128)
#endif
        PyErr_Format(PyExc_ValueError,
                     "Cannot specify ',' with '%c'.",
                     (char)presentation_type);
#if STRINGLIB_IS_UNICODE
    else
        PyErr_Format(PyExc_ValueError,
                     "Cannot specify ',' with '\\x%x'.",
                     (unsigned int)presentation_type);
#endif
}

static int
get_integer(STRINGLIB_CHAR **ptr, STRINGLIB_CHAR *end,
                  Py_ssize_t *result)
{
    Py_ssize_t accumulator, digitval;
    int numdigits;
    accumulator = numdigits = 0;
    for (;;(*ptr)++, numdigits++) {
        if (*ptr >= end)
            break;
        digitval = STRINGLIB_TODECIMAL(**ptr);
        if (digitval < 0)
            break;

        if (accumulator > (PY_SSIZE_T_MAX - digitval) / 10) {
            PyErr_Format(PyExc_ValueError,
                         "Too many decimal digits in format string");
            return -1;
        }
        accumulator = accumulator * 10 + digitval;
    }
    *result = accumulator;
    return numdigits;
}

Py_LOCAL_INLINE(int)
is_alignment_token(STRINGLIB_CHAR c)
{
    switch (c) {
    case '<': case '>': case '=': case '^':
        return 1;
    default:
        return 0;
    }
}

Py_LOCAL_INLINE(int)
is_sign_element(STRINGLIB_CHAR c)
{
    switch (c) {
    case ' ': case '+': case '-':
        return 1;
    default:
        return 0;
    }
}

typedef struct {
    STRINGLIB_CHAR fill_char;
    STRINGLIB_CHAR align;
    int alternate;
    STRINGLIB_CHAR sign;
    Py_ssize_t width;
    int thousands_separators;
    Py_ssize_t precision;
    STRINGLIB_CHAR type;
} InternalFormatSpec;

#if 0

static void
DEBUG_PRINT_FORMAT_SPEC(InternalFormatSpec *format)
{
    printf("internal format spec: fill_char %d\n", format->fill_char);
    printf("internal format spec: align %d\n", format->align);
    printf("internal format spec: alternate %d\n", format->alternate);
    printf("internal format spec: sign %d\n", format->sign);
    printf("internal format spec: width %zd\n", format->width);
    printf("internal format spec: thousands_separators %d\n",
           format->thousands_separators);
    printf("internal format spec: precision %zd\n", format->precision);
    printf("internal format spec: type %c\n", format->type);
    printf("\n");
}
#endif

static int
parse_internal_render_format_spec(STRINGLIB_CHAR *format_spec,
                                  Py_ssize_t format_spec_len,
                                  InternalFormatSpec *format,
                                  char default_type,
                                  char default_align)
{
    STRINGLIB_CHAR *ptr = format_spec;
    STRINGLIB_CHAR *end = format_spec + format_spec_len;

    Py_ssize_t consumed;
    int align_specified = 0;
    int fill_char_specified = 0;

    format->fill_char = ' ';
    format->align = default_align;
    format->alternate = 0;
    format->sign = '\0';
    format->width = -1;
    format->thousands_separators = 0;
    format->precision = -1;
    format->type = default_type;

    if (end-ptr >= 2 && is_alignment_token(ptr[1])) {
        format->align = ptr[1];
        format->fill_char = ptr[0];
        fill_char_specified = 1;
        align_specified = 1;
        ptr += 2;
    }
    else if (end-ptr >= 1 && is_alignment_token(ptr[0])) {
        format->align = ptr[0];
        align_specified = 1;
        ++ptr;
    }

    if (end-ptr >= 1 && is_sign_element(ptr[0])) {
        format->sign = ptr[0];
        ++ptr;
    }

    if (end-ptr >= 1 && ptr[0] == '#') {
        format->alternate = 1;
        ++ptr;
    }

    if (!fill_char_specified && end-ptr >= 1 && ptr[0] == '0') {
        format->fill_char = '0';
        if (!align_specified) {
            format->align = '=';
        }
        ++ptr;
    }

    consumed = get_integer(&ptr, end, &format->width);
    if (consumed == -1)

        return 0;

    if (consumed == 0)
        format->width = -1;

    if (end-ptr && ptr[0] == ',') {
        format->thousands_separators = 1;
        ++ptr;
    }

    if (end-ptr && ptr[0] == '.') {
        ++ptr;

        consumed = get_integer(&ptr, end, &format->precision);
        if (consumed == -1)

            return 0;

        if (consumed == 0) {
            PyErr_Format(PyExc_ValueError,
                         "Format specifier missing precision");
            return 0;
        }

    }

    if (end-ptr > 1) {

        PyErr_Format(PyExc_ValueError, "Invalid conversion specification");
        return 0;
    }

    if (end-ptr == 1) {
        format->type = ptr[0];
        ++ptr;
    }

    if (format->thousands_separators) {
        switch (format->type) {
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'E':
        case 'G':
        case '%':
        case 'F':
        case '\0':

            break;
        default:
            invalid_comma_type(format->type);
            return 0;
        }
    }

    return 1;
}

static void
calc_padding(Py_ssize_t nchars, Py_ssize_t width, STRINGLIB_CHAR align,
             Py_ssize_t *n_lpadding, Py_ssize_t *n_rpadding,
             Py_ssize_t *n_total)
{
    if (width >= 0) {
        if (nchars > width)
            *n_total = nchars;
        else
            *n_total = width;
    }
    else {

        *n_total = nchars;
    }

    if (align == '>')
        *n_lpadding = *n_total - nchars;
    else if (align == '^')
        *n_lpadding = (*n_total - nchars) / 2;
    else if (align == '<' || align == '=')
        *n_lpadding = 0;
    else {

        *n_lpadding = 0;
        assert(0);
    }

    *n_rpadding = *n_total - nchars - *n_lpadding;
}

static STRINGLIB_CHAR *
fill_padding(STRINGLIB_CHAR *p, Py_ssize_t nchars, STRINGLIB_CHAR fill_char,
             Py_ssize_t n_lpadding, Py_ssize_t n_rpadding)
{

    if (n_lpadding)
        STRINGLIB_FILL(p, fill_char, n_lpadding);

    if (n_rpadding)
        STRINGLIB_FILL(p + nchars + n_lpadding, fill_char, n_rpadding);

    return p + n_lpadding;
}

#if defined FORMAT_FLOAT || defined FORMAT_LONG || defined FORMAT_COMPLEX

#define LT_CURRENT_LOCALE 0
#define LT_DEFAULT_LOCALE 1
#define LT_NO_LOCALE 2

typedef struct {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
} LocaleInfo;

typedef struct {
    Py_ssize_t n_lpadding;
    Py_ssize_t n_prefix;
    Py_ssize_t n_spadding;
    Py_ssize_t n_rpadding;
    char sign;
    Py_ssize_t n_sign;
    Py_ssize_t n_grouped_digits;

    Py_ssize_t n_decimal;
    Py_ssize_t n_remainder;

    Py_ssize_t n_digits;

    Py_ssize_t n_min_width;

} NumberFieldWidths;

static void
parse_number(STRINGLIB_CHAR *ptr, Py_ssize_t len,
             Py_ssize_t *n_remainder, int *has_decimal)
{
    STRINGLIB_CHAR *end = ptr + len;
    STRINGLIB_CHAR *remainder;

    while (ptr<end && isdigit(*ptr))
        ++ptr;
    remainder = ptr;

    *has_decimal = ptr<end && *remainder == '.';

    if (*has_decimal)
        remainder++;

    *n_remainder = end - remainder;
}

static Py_ssize_t
calc_number_widths(NumberFieldWidths *spec, Py_ssize_t n_prefix,
                   STRINGLIB_CHAR sign_char, STRINGLIB_CHAR *number,
                   Py_ssize_t n_number, Py_ssize_t n_remainder,
                   int has_decimal, const LocaleInfo *locale,
                   const InternalFormatSpec *format)
{
    Py_ssize_t n_non_digit_non_padding;
    Py_ssize_t n_padding;

    spec->n_digits = n_number - n_remainder - (has_decimal?1:0);
    spec->n_lpadding = 0;
    spec->n_prefix = n_prefix;
    spec->n_decimal = has_decimal ? strlen(locale->decimal_point) : 0;
    spec->n_remainder = n_remainder;
    spec->n_spadding = 0;
    spec->n_rpadding = 0;
    spec->sign = '\0';
    spec->n_sign = 0;

    switch (format->sign) {
    case '+':

        spec->n_sign = 1;
        spec->sign = (sign_char == '-' ? '-' : '+');
        break;
    case ' ':
        spec->n_sign = 1;
        spec->sign = (sign_char == '-' ? '-' : ' ');
        break;
    default:

        if (sign_char == '-') {
            spec->n_sign = 1;
            spec->sign = '-';
        }
    }

    n_non_digit_non_padding = spec->n_sign + spec->n_prefix + spec->n_decimal +
        spec->n_remainder;

    if (format->fill_char == '0' && format->align == '=')
        spec->n_min_width = format->width - n_non_digit_non_padding;
    else
        spec->n_min_width = 0;

    if (spec->n_digits == 0)

        spec->n_grouped_digits = 0;
    else
        spec->n_grouped_digits = STRINGLIB_GROUPING(NULL, 0, NULL,
                                                    spec->n_digits,
                                                    spec->n_min_width,
                                                    locale->grouping,
                                                    locale->thousands_sep);

    n_padding = format->width -
                        (n_non_digit_non_padding + spec->n_grouped_digits);
    if (n_padding > 0) {

        switch (format->align) {
        case '<':
            spec->n_rpadding = n_padding;
            break;
        case '^':
            spec->n_lpadding = n_padding / 2;
            spec->n_rpadding = n_padding - spec->n_lpadding;
            break;
        case '=':
            spec->n_spadding = n_padding;
            break;
        case '>':
            spec->n_lpadding = n_padding;
            break;
        default:

            spec->n_lpadding = n_padding;
            assert(0);
            break;
        }
    }
    return spec->n_lpadding + spec->n_sign + spec->n_prefix +
        spec->n_spadding + spec->n_grouped_digits + spec->n_decimal +
        spec->n_remainder + spec->n_rpadding;
}

static void
fill_number(STRINGLIB_CHAR *buf, const NumberFieldWidths *spec,
            STRINGLIB_CHAR *digits, Py_ssize_t n_digits,
            STRINGLIB_CHAR *prefix, STRINGLIB_CHAR fill_char,
            LocaleInfo *locale, int toupper)
{

    STRINGLIB_CHAR *p = digits;

#ifndef NDEBUG
    Py_ssize_t r;
#endif

    if (spec->n_lpadding) {
        STRINGLIB_FILL(buf, fill_char, spec->n_lpadding);
        buf += spec->n_lpadding;
    }
    if (spec->n_sign == 1) {
        *buf++ = spec->sign;
    }
    if (spec->n_prefix) {
        memmove(buf,
                prefix,
                spec->n_prefix * sizeof(STRINGLIB_CHAR));
        if (toupper) {
            Py_ssize_t t;
            for (t = 0; t < spec->n_prefix; ++t)
                buf[t] = STRINGLIB_TOUPPER(buf[t]);
        }
        buf += spec->n_prefix;
    }
    if (spec->n_spadding) {
        STRINGLIB_FILL(buf, fill_char, spec->n_spadding);
        buf += spec->n_spadding;
    }

    if (spec->n_digits != 0) {

#ifndef NDEBUG
        r =
#endif
            STRINGLIB_GROUPING(buf, spec->n_grouped_digits, digits,
                               spec->n_digits, spec->n_min_width,
                               locale->grouping, locale->thousands_sep);
#ifndef NDEBUG
        assert(r == spec->n_grouped_digits);
#endif
        p += spec->n_digits;
    }
    if (toupper) {
        Py_ssize_t t;
        for (t = 0; t < spec->n_grouped_digits; ++t)
            buf[t] = STRINGLIB_TOUPPER(buf[t]);
    }
    buf += spec->n_grouped_digits;

    if (spec->n_decimal) {
        Py_ssize_t t;
        for (t = 0; t < spec->n_decimal; ++t)
            buf[t] = locale->decimal_point[t];
        buf += spec->n_decimal;
        p += 1;
    }

    if (spec->n_remainder) {
        memcpy(buf, p, spec->n_remainder * sizeof(STRINGLIB_CHAR));
        buf += spec->n_remainder;
        p += spec->n_remainder;
    }

    if (spec->n_rpadding) {
        STRINGLIB_FILL(buf, fill_char, spec->n_rpadding);
        buf += spec->n_rpadding;
    }
}

static char no_grouping[1] = {CHAR_MAX};

static void
get_locale_info(int type, LocaleInfo *locale_info)
{
    switch (type) {
    case LT_CURRENT_LOCALE: {
        struct lconv *locale_data = localeconv();
        locale_info->decimal_point = locale_data->decimal_point;
        locale_info->thousands_sep = locale_data->thousands_sep;
        locale_info->grouping = locale_data->grouping;
        break;
    }
    case LT_DEFAULT_LOCALE:
        locale_info->decimal_point = ".";
        locale_info->thousands_sep = ",";
        locale_info->grouping = "\3";

        break;
    case LT_NO_LOCALE:
        locale_info->decimal_point = ".";
        locale_info->thousands_sep = "";
        locale_info->grouping = no_grouping;
        break;
    default:
        assert(0);
    }
}

#endif

static PyObject *
format_string_internal(PyObject *value, const InternalFormatSpec *format)
{
    Py_ssize_t lpad;
    Py_ssize_t rpad;
    Py_ssize_t total;
    STRINGLIB_CHAR *p;
    Py_ssize_t len = STRINGLIB_LEN(value);
    PyObject *result = NULL;

    if (format->sign != '\0') {
        PyErr_SetString(PyExc_ValueError,
                        "Sign not allowed in string format specifier");
        goto done;
    }

    if (format->alternate) {
        PyErr_SetString(PyExc_ValueError,
                        "Alternate form (#) not allowed in string format "
                        "specifier");
        goto done;
    }

    if (format->align == '=') {
        PyErr_SetString(PyExc_ValueError,
                        "'=' alignment not allowed "
                        "in string format specifier");
        goto done;
    }

    if (format->precision >= 0 && len >= format->precision) {
        len = format->precision;
    }

    calc_padding(len, format->width, format->align, &lpad, &rpad, &total);

    result = STRINGLIB_NEW(NULL, total);
    if (result == NULL)
        goto done;

    p = fill_padding(STRINGLIB_STR(result), len,
                     format->fill_char, lpad, rpad);

    memcpy(p, STRINGLIB_STR(value), len * sizeof(STRINGLIB_CHAR));

done:
    return result;
}

#if defined FORMAT_LONG || defined FORMAT_INT
typedef PyObject*
(*IntOrLongToString)(PyObject *value, int base);

static PyObject *
format_int_or_long_internal(PyObject *value, const InternalFormatSpec *format,
                            IntOrLongToString tostring)
{
    PyObject *result = NULL;
    PyObject *tmp = NULL;
    STRINGLIB_CHAR *pnumeric_chars;
    STRINGLIB_CHAR numeric_char;
    STRINGLIB_CHAR sign_char = '\0';
    Py_ssize_t n_digits;

    Py_ssize_t n_remainder = 0;

    Py_ssize_t n_prefix = 0;
    Py_ssize_t n_total;
    STRINGLIB_CHAR *prefix = NULL;
    NumberFieldWidths spec;
    long x;

    LocaleInfo locale;

    if (format->precision != -1) {
        PyErr_SetString(PyExc_ValueError,
                        "Precision not allowed in integer format specifier");
        goto done;
    }

    if (format->type == 'c') {

        if (format->sign != '\0') {
            PyErr_SetString(PyExc_ValueError,
                            "Sign not allowed with integer"
                            " format specifier 'c'");
            goto done;
        }

        if (format->thousands_separators) {
            PyErr_SetString(PyExc_ValueError,
                            "Thousands separators not allowed with integer"
                            " format specifier 'c'");
            goto done;
        }

        x = PyLong_AsLong(value);
        if (x == -1 && PyErr_Occurred())
            goto done;
#if STRINGLIB_IS_UNICODE
#ifdef Py_UNICODE_WIDE
        if (x < 0 || x > 0x10ffff) {
            PyErr_SetString(PyExc_OverflowError,
                            "%c arg not in range(0x110000) "
                            "(wide Python build)");
            goto done;
        }
#else
        if (x < 0 || x > 0xffff) {
            PyErr_SetString(PyExc_OverflowError,
                            "%c arg not in range(0x10000) "
                            "(narrow Python build)");
            goto done;
        }
#endif
#else
        if (x < 0 || x > 0xff) {
            PyErr_SetString(PyExc_OverflowError,
                            "%c arg not in range(0x100)");
            goto done;
        }
#endif
        numeric_char = (STRINGLIB_CHAR)x;
        pnumeric_chars = &numeric_char;
        n_digits = 1;

        n_remainder = 1;
    }
    else {
        int base;
        int leading_chars_to_skip = 0;

        switch (format->type) {
        case 'b':
            base = 2;
            leading_chars_to_skip = 2;
            break;
        case 'o':
            base = 8;
            leading_chars_to_skip = 2;
            break;
        case 'x':
        case 'X':
            base = 16;
            leading_chars_to_skip = 2;
            break;
        default:
        case 'd':
        case 'n':
            base = 10;
            break;
        }

        if (format->alternate)
            n_prefix = leading_chars_to_skip;

        tmp = tostring(value, base);
        if (tmp == NULL)
            goto done;

        pnumeric_chars = STRINGLIB_STR(tmp);
        n_digits = STRINGLIB_LEN(tmp);

        prefix = pnumeric_chars;

        if (pnumeric_chars[0] == '-') {
            sign_char = pnumeric_chars[0];
            ++prefix;
            ++leading_chars_to_skip;
        }

        n_digits -= leading_chars_to_skip;
        pnumeric_chars += leading_chars_to_skip;
    }

    get_locale_info(format->type == 'n' ? LT_CURRENT_LOCALE :
                    (format->thousands_separators ?
                     LT_DEFAULT_LOCALE :
                     LT_NO_LOCALE),
                    &locale);

    n_total = calc_number_widths(&spec, n_prefix, sign_char, pnumeric_chars,
                       n_digits, n_remainder, 0, &locale, format);

    result = STRINGLIB_NEW(NULL, n_total);
    if (!result)
        goto done;

    fill_number(STRINGLIB_STR(result), &spec, pnumeric_chars, n_digits,
                prefix, format->fill_char, &locale, format->type == 'X');

done:
    Py_XDECREF(tmp);
    return result;
}
#endif

#ifdef FORMAT_FLOAT
#if STRINGLIB_IS_UNICODE
static void
strtounicode(Py_UNICODE *buffer, const char *charbuffer, Py_ssize_t len)
{
    Py_ssize_t i;
    for (i = 0; i < len; ++i)
        buffer[i] = (Py_UNICODE)charbuffer[i];
}
#endif

static PyObject *
format_float_internal(PyObject *value,
                      const InternalFormatSpec *format)
{
    char *buf = NULL;
    Py_ssize_t n_digits;
    Py_ssize_t n_remainder;
    Py_ssize_t n_total;
    int has_decimal;
    double val;
    Py_ssize_t precision;
    Py_ssize_t default_precision = 6;
    STRINGLIB_CHAR type = format->type;
    int add_pct = 0;
    STRINGLIB_CHAR *p;
    NumberFieldWidths spec;
    int flags = 0;
    PyObject *result = NULL;
    STRINGLIB_CHAR sign_char = '\0';
    int float_type;

#if STRINGLIB_IS_UNICODE
    Py_UNICODE *unicode_tmp = NULL;
#endif

    LocaleInfo locale;

    if (format->precision > INT_MAX) {
        PyErr_SetString(PyExc_ValueError, "precision too big");
        goto done;
    }
    precision = (int)format->precision;

    if (format->alternate) {
        PyErr_SetString(PyExc_ValueError,
                        "Alternate form (#) not allowed in float format "
                        "specifier");
        goto done;
    }

    if (type == '\0') {

        type = 'g';
        default_precision = PyFloat_STR_PRECISION;
        flags |= Py_DTSF_ADD_DOT_0;
    }

    if (type == 'n')

        type = 'g';

    val = PyFloat_AsDouble(value);
    if (val == -1.0 && PyErr_Occurred())
        goto done;

    if (type == '%') {
        type = 'f';
        val *= 100;
        add_pct = 1;
    }

    if (precision < 0)
        precision = default_precision;

    buf = PyOS_double_to_string(val, (char)type, precision, flags,
                                &float_type);
    if (buf == NULL)
        goto done;
    n_digits = strlen(buf);

    if (add_pct) {

        buf[n_digits] = '%';
        n_digits += 1;
    }

#if STRINGLIB_IS_UNICODE
    unicode_tmp = (Py_UNICODE*)PyMem_Malloc((n_digits)*sizeof(Py_UNICODE));
    if (unicode_tmp == NULL) {
        PyErr_NoMemory();
        goto done;
    }
    strtounicode(unicode_tmp, buf, n_digits);
    p = unicode_tmp;
#else
    p = buf;
#endif

    if (*p == '-') {
        sign_char = *p;
        ++p;
        --n_digits;
    }

    parse_number(p, n_digits, &n_remainder, &has_decimal);

    get_locale_info(format->type == 'n' ? LT_CURRENT_LOCALE :
                    (format->thousands_separators ?
                     LT_DEFAULT_LOCALE :
                     LT_NO_LOCALE),
                    &locale);

    n_total = calc_number_widths(&spec, 0, sign_char, p, n_digits,
                                 n_remainder, has_decimal, &locale, format);

    result = STRINGLIB_NEW(NULL, n_total);
    if (result == NULL)
        goto done;

    fill_number(STRINGLIB_STR(result), &spec, p, n_digits, NULL,
                format->fill_char, &locale, 0);

done:
    PyMem_Free(buf);
#if STRINGLIB_IS_UNICODE
    PyMem_Free(unicode_tmp);
#endif
    return result;
}
#endif

#ifdef FORMAT_COMPLEX

static PyObject *
format_complex_internal(PyObject *value,
                        const InternalFormatSpec *format)
{
    double re;
    double im;
    char *re_buf = NULL;
    char *im_buf = NULL;

    InternalFormatSpec tmp_format = *format;
    Py_ssize_t n_re_digits;
    Py_ssize_t n_im_digits;
    Py_ssize_t n_re_remainder;
    Py_ssize_t n_im_remainder;
    Py_ssize_t n_re_total;
    Py_ssize_t n_im_total;
    int re_has_decimal;
    int im_has_decimal;
    Py_ssize_t precision;
    Py_ssize_t default_precision = 6;
    STRINGLIB_CHAR type = format->type;
    STRINGLIB_CHAR *p_re;
    STRINGLIB_CHAR *p_im;
    NumberFieldWidths re_spec;
    NumberFieldWidths im_spec;
    int flags = 0;
    PyObject *result = NULL;
    STRINGLIB_CHAR *p;
    STRINGLIB_CHAR re_sign_char = '\0';
    STRINGLIB_CHAR im_sign_char = '\0';
    int re_float_type;
    int im_float_type;
    int add_parens = 0;
    int skip_re = 0;
    Py_ssize_t lpad;
    Py_ssize_t rpad;
    Py_ssize_t total;

#if STRINGLIB_IS_UNICODE
    Py_UNICODE *re_unicode_tmp = NULL;
    Py_UNICODE *im_unicode_tmp = NULL;
#endif

    LocaleInfo locale;

    if (format->precision > INT_MAX) {
        PyErr_SetString(PyExc_ValueError, "precision too big");
        goto done;
    }
    precision = (int)format->precision;

    if (format->alternate) {
        PyErr_SetString(PyExc_ValueError,
                        "Alternate form (#) not allowed in complex format "
                        "specifier");
        goto done;
    }

    if (format->fill_char == '0') {
        PyErr_SetString(PyExc_ValueError,
                        "Zero padding is not allowed in complex format "
                        "specifier");
        goto done;
    }

    if (format->align == '=') {
        PyErr_SetString(PyExc_ValueError,
                        "'=' alignment flag is not allowed in complex format "
                        "specifier");
        goto done;
    }

    re = PyComplex_RealAsDouble(value);
    if (re == -1.0 && PyErr_Occurred())
        goto done;
    im = PyComplex_ImagAsDouble(value);
    if (im == -1.0 && PyErr_Occurred())
        goto done;

    if (type == '\0') {

        type = 'g';
        default_precision = PyFloat_STR_PRECISION;
        if (re == 0.0 && copysign(1.0, re) == 1.0)
            skip_re = 1;
        else
            add_parens = 1;
    }

    if (type == 'n')

        type = 'g';

    if (precision < 0)
        precision = default_precision;

    re_buf = PyOS_double_to_string(re, (char)type, precision, flags,
                                   &re_float_type);
    if (re_buf == NULL)
        goto done;
    im_buf = PyOS_double_to_string(im, (char)type, precision, flags,
                                   &im_float_type);
    if (im_buf == NULL)
        goto done;

    n_re_digits = strlen(re_buf);
    n_im_digits = strlen(im_buf);

#if STRINGLIB_IS_UNICODE
    re_unicode_tmp = (Py_UNICODE*)PyMem_Malloc((n_re_digits)*sizeof(Py_UNICODE));
    if (re_unicode_tmp == NULL) {
        PyErr_NoMemory();
        goto done;
    }
    strtounicode(re_unicode_tmp, re_buf, n_re_digits);
    p_re = re_unicode_tmp;

    im_unicode_tmp = (Py_UNICODE*)PyMem_Malloc((n_im_digits)*sizeof(Py_UNICODE));
    if (im_unicode_tmp == NULL) {
        PyErr_NoMemory();
        goto done;
    }
    strtounicode(im_unicode_tmp, im_buf, n_im_digits);
    p_im = im_unicode_tmp;
#else
    p_re = re_buf;
    p_im = im_buf;
#endif

    if (*p_re == '-') {
        re_sign_char = *p_re;
        ++p_re;
        --n_re_digits;
    }
    if (*p_im == '-') {
        im_sign_char = *p_im;
        ++p_im;
        --n_im_digits;
    }

    parse_number(p_re, n_re_digits, &n_re_remainder, &re_has_decimal);
    parse_number(p_im, n_im_digits, &n_im_remainder, &im_has_decimal);

    get_locale_info(format->type == 'n' ? LT_CURRENT_LOCALE :
                    (format->thousands_separators ?
                     LT_DEFAULT_LOCALE :
                     LT_NO_LOCALE),
                    &locale);

    tmp_format.fill_char = '\0';
    tmp_format.align = '<';
    tmp_format.width = -1;

    n_re_total = calc_number_widths(&re_spec, 0, re_sign_char, p_re,
                                    n_re_digits, n_re_remainder,
                                    re_has_decimal, &locale, &tmp_format);

    if (!skip_re)
        tmp_format.sign = '+';
    n_im_total = calc_number_widths(&im_spec, 0, im_sign_char, p_im,
                                    n_im_digits, n_im_remainder,
                                    im_has_decimal, &locale, &tmp_format);

    if (skip_re)
        n_re_total = 0;

    calc_padding(n_re_total + n_im_total + 1 + add_parens * 2,
                 format->width, format->align, &lpad, &rpad, &total);

    result = STRINGLIB_NEW(NULL, total);
    if (result == NULL)
        goto done;

    p = fill_padding(STRINGLIB_STR(result),
                     n_re_total + n_im_total + 1 + add_parens * 2,
                     format->fill_char, lpad, rpad);

    if (add_parens)
        *p++ = '(';

    if (!skip_re) {
        fill_number(p, &re_spec, p_re, n_re_digits, NULL, 0, &locale, 0);
        p += n_re_total;
    }
    fill_number(p, &im_spec, p_im, n_im_digits, NULL, 0, &locale, 0);
    p += n_im_total;
    *p++ = 'j';

    if (add_parens)
        *p++ = ')';

done:
    PyMem_Free(re_buf);
    PyMem_Free(im_buf);
#if STRINGLIB_IS_UNICODE
    PyMem_Free(re_unicode_tmp);
    PyMem_Free(im_unicode_tmp);
#endif
    return result;
}
#endif

PyObject *
FORMAT_STRING(PyObject *obj,
              STRINGLIB_CHAR *format_spec,
              Py_ssize_t format_spec_len)
{
    InternalFormatSpec format;
    PyObject *result = NULL;

    if (format_spec_len == 0) {
        result = STRINGLIB_TOSTR(obj);
        goto done;
    }

    if (!parse_internal_render_format_spec(format_spec, format_spec_len,
                                           &format, 's', '<'))
        goto done;

    switch (format.type) {
    case 's':

        result = format_string_internal(obj, &format);
        break;
    default:

        unknown_presentation_type(format.type, obj->ob_type->tp_name);
        goto done;
    }

done:
    return result;
}

#if defined FORMAT_LONG || defined FORMAT_INT
static PyObject*
format_int_or_long(PyObject* obj,
                   STRINGLIB_CHAR *format_spec,
                   Py_ssize_t format_spec_len,
                   IntOrLongToString tostring)
{
    PyObject *result = NULL;
    PyObject *tmp = NULL;
    InternalFormatSpec format;

    if (format_spec_len == 0) {
        result = STRINGLIB_TOSTR(obj);
        goto done;
    }

    if (!parse_internal_render_format_spec(format_spec,
                                           format_spec_len,
                                           &format, 'd', '>'))
        goto done;

    switch (format.type) {
    case 'b':
    case 'c':
    case 'd':
    case 'o':
    case 'x':
    case 'X':
    case 'n':

            result = format_int_or_long_internal(obj, &format, tostring);
        break;

    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case '%':

        tmp = PyNumber_Float(obj);
        if (tmp == NULL)
            goto done;
        result = format_float_internal(tmp, &format);
        break;

    default:

        unknown_presentation_type(format.type, obj->ob_type->tp_name);
        goto done;
    }

done:
    Py_XDECREF(tmp);
    return result;
}
#endif

#ifdef FORMAT_LONG

#if PY_VERSION_HEX >= 0x03000000
#define long_format _PyLong_Format
#else
static PyObject*
long_format(PyObject* value, int base)
{

    assert(PyLong_Check(value));

    return _PyLong_Format(value, base, 0, 1);
}
#endif

PyObject *
FORMAT_LONG(PyObject *obj,
            STRINGLIB_CHAR *format_spec,
            Py_ssize_t format_spec_len)
{
    return format_int_or_long(obj, format_spec, format_spec_len,
                              long_format);
}
#endif

#ifdef FORMAT_INT

static PyObject*
int_format(PyObject* value, int base)
{

    assert(PyInt_Check(value));
    return _PyInt_Format((PyIntObject*)value, base, 1);
}

PyObject *
FORMAT_INT(PyObject *obj,
           STRINGLIB_CHAR *format_spec,
           Py_ssize_t format_spec_len)
{
    return format_int_or_long(obj, format_spec, format_spec_len,
                              int_format);
}
#endif

#ifdef FORMAT_FLOAT
PyObject *
FORMAT_FLOAT(PyObject *obj,
             STRINGLIB_CHAR *format_spec,
             Py_ssize_t format_spec_len)
{
    PyObject *result = NULL;
    InternalFormatSpec format;

    if (format_spec_len == 0) {
        result = STRINGLIB_TOSTR(obj);
        goto done;
    }

    if (!parse_internal_render_format_spec(format_spec,
                                           format_spec_len,
                                           &format, '\0', '>'))
        goto done;

    switch (format.type) {
    case '\0':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'n':
    case '%':

        result = format_float_internal(obj, &format);
        break;

    default:

        unknown_presentation_type(format.type, obj->ob_type->tp_name);
        goto done;
    }

done:
    return result;
}
#endif

#ifdef FORMAT_COMPLEX
PyObject *
FORMAT_COMPLEX(PyObject *obj,
               STRINGLIB_CHAR *format_spec,
               Py_ssize_t format_spec_len)
{
    PyObject *result = NULL;
    InternalFormatSpec format;

    if (format_spec_len == 0) {
        result = STRINGLIB_TOSTR(obj);
        goto done;
    }

    if (!parse_internal_render_format_spec(format_spec,
                                           format_spec_len,
                                           &format, '\0', '>'))
        goto done;

    switch (format.type) {
    case '\0':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'n':

        result = format_complex_internal(obj, &format);
        break;

    default:

        unknown_presentation_type(format.type, obj->ob_type->tp_name);
        goto done;
    }

done:
    return result;
}
#endif
