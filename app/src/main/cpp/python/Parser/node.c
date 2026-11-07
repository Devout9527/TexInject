#include "Python.h"
#include "node.h"
#include "errcode.h"

node *
PyNode_New(int type)
{
    node *n = (node *) PyObject_MALLOC(1 * sizeof(node));
    if (n == NULL)
        return NULL;
    n->n_type = type;
    n->n_str = NULL;
    n->n_lineno = 0;
    n->n_nchildren = 0;
    n->n_child = NULL;
    return n;
}

static int
fancy_roundup(int n)
{

    int result = 256;
    assert(n > 128);
    while (result < n) {
        result <<= 1;
        if (result <= 0)
            return -1;
    }
    return result;
}

#define XXXROUNDUP(n) ((n) <= 1 ? (n) :                 \
               (n) <= 128 ? (((n) + 3) & ~3) :          \
               fancy_roundup(n))

int
PyNode_AddChild(register node *n1, int type, char *str, int lineno, int col_offset)
{
    const int nch = n1->n_nchildren;
    int current_capacity;
    int required_capacity;
    node *n;

    if (nch == INT_MAX || nch < 0)
        return E_OVERFLOW;

    current_capacity = XXXROUNDUP(nch);
    required_capacity = XXXROUNDUP(nch + 1);
    if (current_capacity < 0 || required_capacity < 0)
        return E_OVERFLOW;
    if (current_capacity < required_capacity) {
        if (required_capacity > PY_SIZE_MAX / sizeof(node)) {
            return E_NOMEM;
        }
        n = n1->n_child;
        n = (node *) PyObject_REALLOC(n,
                                      required_capacity * sizeof(node));
        if (n == NULL)
            return E_NOMEM;
        n1->n_child = n;
    }

    n = &n1->n_child[n1->n_nchildren++];
    n->n_type = type;
    n->n_str = str;
    n->n_lineno = lineno;
    n->n_col_offset = col_offset;
    n->n_nchildren = 0;
    n->n_child = NULL;
    return 0;
}

static void freechildren(node *);
static Py_ssize_t sizeofchildren(node *n);

void
PyNode_Free(node *n)
{
    if (n != NULL) {
        freechildren(n);
        PyObject_FREE(n);
    }
}

Py_ssize_t
_PyNode_SizeOf(node *n)
{
    Py_ssize_t res = 0;

    if (n != NULL)
        res = sizeof(node) + sizeofchildren(n);
    return res;
}

static void
freechildren(node *n)
{
    int i;
    for (i = NCH(n); --i >= 0; )
        freechildren(CHILD(n, i));
    if (n->n_child != NULL)
        PyObject_FREE(n->n_child);
    if (STR(n) != NULL)
        PyObject_FREE(STR(n));
}

static Py_ssize_t
sizeofchildren(node *n)
{
    Py_ssize_t res = 0;
    int i;
    for (i = NCH(n); --i >= 0; )
        res += sizeofchildren(CHILD(n, i));
    if (n->n_child != NULL)

        res += XXXROUNDUP(NCH(n)) * sizeof(node);
    if (STR(n) != NULL)
        res += strlen(STR(n)) + 1;
    return res;
}
