#ifndef STRINGLIB_LOCALEUTIL_H
#define STRINGLIB_LOCALEUTIL_H

#include <locale.h>

#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

typedef struct {
    const char *grouping;
    char previous;
    Py_ssize_t i;
} GroupGenerator;

static void
_GroupGenerator_init(GroupGenerator *self, const char *grouping)
{
    self->grouping = grouping;
    self->i = 0;
    self->previous = 0;
}

static Py_ssize_t
_GroupGenerator_next(GroupGenerator *self)
{

    switch (self->grouping[self->i]) {
    case 0:
        return self->previous;
    case CHAR_MAX:

        return 0;
    default: {
        char ch = self->grouping[self->i];
        self->previous = ch;
        self->i++;
        return (Py_ssize_t)ch;
    }
    }
}

static void
fill(STRINGLIB_CHAR **digits_end, STRINGLIB_CHAR **buffer_end,
     Py_ssize_t n_chars, Py_ssize_t n_zeros, const char* thousands_sep,
     Py_ssize_t thousands_sep_len)
{
#if STRINGLIB_IS_UNICODE
    Py_ssize_t i;
#endif

    if (thousands_sep) {
        *buffer_end -= thousands_sep_len;

#if STRINGLIB_IS_UNICODE

        for (i = 0; i < thousands_sep_len; ++i)
            (*buffer_end)[i] = thousands_sep[i];
#else

        memcpy(*buffer_end, thousands_sep, thousands_sep_len);
#endif
    }

    *buffer_end -= n_chars;
    *digits_end -= n_chars;
    memcpy(*buffer_end, *digits_end, n_chars * sizeof(STRINGLIB_CHAR));

    *buffer_end -= n_zeros;
    STRINGLIB_FILL(*buffer_end, '0', n_zeros);
}

Py_ssize_t
_Py_InsertThousandsGrouping(STRINGLIB_CHAR *buffer,
                            Py_ssize_t n_buffer,
                            STRINGLIB_CHAR *digits,
                            Py_ssize_t n_digits,
                            Py_ssize_t min_width,
                            const char *grouping,
                            const char *thousands_sep)
{
    Py_ssize_t count = 0;
    Py_ssize_t n_zeros;
    int loop_broken = 0;
    int use_separator = 0;

    STRINGLIB_CHAR *buffer_end = NULL;
    STRINGLIB_CHAR *digits_end = NULL;
    Py_ssize_t l;
    Py_ssize_t n_chars;
    Py_ssize_t thousands_sep_len = strlen(thousands_sep);
    Py_ssize_t remaining = n_digits;

    GroupGenerator groupgen;
    _GroupGenerator_init(&groupgen, grouping);

    if (buffer) {
        buffer_end = buffer + n_buffer;
        digits_end = digits + n_digits;
    }

    while ((l = _GroupGenerator_next(&groupgen)) > 0) {
        l = MIN(l, MAX(MAX(remaining, min_width), 1));
        n_zeros = MAX(0, l - remaining);
        n_chars = MAX(0, MIN(remaining, l));

        count += (use_separator ? thousands_sep_len : 0) + n_zeros + n_chars;

        if (buffer) {

            fill(&digits_end, &buffer_end, n_chars, n_zeros,
                 use_separator ? thousands_sep : NULL, thousands_sep_len);
        }

        use_separator = 1;

        remaining -= n_chars;
        min_width -= l;

        if (remaining <= 0 && min_width <= 0) {
            loop_broken = 1;
            break;
        }
        min_width -= thousands_sep_len;
    }
    if (!loop_broken) {

        l = MAX(MAX(remaining, min_width), 1);
        n_zeros = MAX(0, l - remaining);
        n_chars = MAX(0, MIN(remaining, l));

        count += (use_separator ? thousands_sep_len : 0) + n_zeros + n_chars;
        if (buffer) {

            fill(&digits_end, &buffer_end, n_chars, n_zeros,
                 use_separator ? thousands_sep : NULL, thousands_sep_len);
        }
    }
    return count;
}

Py_ssize_t
_Py_InsertThousandsGroupingLocale(STRINGLIB_CHAR *buffer,
                                  Py_ssize_t n_buffer,
                                  STRINGLIB_CHAR *digits,
                                  Py_ssize_t n_digits,
                                  Py_ssize_t min_width)
{
        struct lconv *locale_data = localeconv();
        const char *grouping = locale_data->grouping;
        const char *thousands_sep = locale_data->thousands_sep;

        return _Py_InsertThousandsGrouping(buffer, n_buffer, digits, n_digits,
                                           min_width, grouping, thousands_sep);
}
#endif
