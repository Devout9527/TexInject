#include "Python.h"
#include <time.h>

#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL
#define UPPER_MASK 0x80000000UL
#define LOWER_MASK 0x7fffffffUL

typedef struct {
    PyObject_HEAD
    unsigned long state[N];
    int index;
} RandomObject;

static PyTypeObject Random_Type;

#define RandomObject_Check(v)      (Py_TYPE(v) == &Random_Type)

static unsigned long
genrand_int32(RandomObject *self)
{
    unsigned long y;
    static unsigned long mag01[2]={0x0UL, MATRIX_A};

    unsigned long *mt;

    mt = self->state;
    if (self->index >= N) {
        int kk;

        for (kk=0;kk<N-M;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        for (;kk<N-1;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1UL];

        self->index = 0;
    }

    y = mt[self->index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);
    return y;
}

static PyObject *
random_random(RandomObject *self)
{
    unsigned long a=genrand_int32(self)>>5, b=genrand_int32(self)>>6;
    return PyFloat_FromDouble((a*67108864.0+b)*(1.0/9007199254740992.0));
}

static void
init_genrand(RandomObject *self, unsigned long s)
{
    int mti;
    unsigned long *mt;

    mt = self->state;
    mt[0]= s & 0xffffffffUL;
    for (mti=1; mti<N; mti++) {
        mt[mti] =
        (1812433253UL * (mt[mti-1] ^ (mt[mti-1] >> 30)) + mti);

        mt[mti] &= 0xffffffffUL;

    }
    self->index = mti;
    return;
}

static PyObject *
init_by_array(RandomObject *self, unsigned long init_key[], unsigned long key_length)
{
    unsigned int i, j, k;
    unsigned long *mt;

    mt = self->state;
    init_genrand(self, 19650218UL);
    i=1; j=0;
    k = (N>key_length ? N : key_length);
    for (; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1664525UL))
                 + init_key[j] + j;
        mt[i] &= 0xffffffffUL;
        i++; j++;
        if (i>=N) { mt[0] = mt[N-1]; i=1; }
        if (j>=key_length) j=0;
    }
    for (k=N-1; k; k--) {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1566083941UL))
                 - i;
        mt[i] &= 0xffffffffUL;
        i++;
        if (i>=N) { mt[0] = mt[N-1]; i=1; }
    }

    mt[0] = 0x80000000UL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
random_seed(RandomObject *self, PyObject *args)
{
    PyObject *result = NULL;
    PyObject *masklower = NULL;
    PyObject *thirtytwo = NULL;
    PyObject *n = NULL;
    unsigned long *key = NULL;
    unsigned long keymax;
    unsigned long keyused;
    int err;

    PyObject *arg = NULL;

    if (!PyArg_UnpackTuple(args, "seed", 0, 1, &arg))
        return NULL;

    if (arg == NULL || arg == Py_None) {
        time_t now;

        time(&now);
        init_genrand(self, (unsigned long)now);
        Py_INCREF(Py_None);
        return Py_None;
    }

    if (PyInt_Check(arg) || PyLong_Check(arg))
        n = PyNumber_Absolute(arg);
    else {
        long hash = PyObject_Hash(arg);
        if (hash == -1)
            goto Done;
        n = PyLong_FromUnsignedLong((unsigned long)hash);
    }
    if (n == NULL)
        goto Done;

    keymax = 8;
    keyused = 0;
    key = (unsigned long *)PyMem_Malloc(keymax * sizeof(*key));
    if (key == NULL)
        goto Done;

    masklower = PyLong_FromUnsignedLong(0xffffffffU);
    if (masklower == NULL)
        goto Done;
    thirtytwo = PyInt_FromLong(32L);
    if (thirtytwo == NULL)
        goto Done;
    while ((err=PyObject_IsTrue(n))) {
        PyObject *newn;
        PyObject *pychunk;
        unsigned long chunk;

        if (err == -1)
            goto Done;
        pychunk = PyNumber_And(n, masklower);
        if (pychunk == NULL)
            goto Done;
        chunk = PyLong_AsUnsignedLong(pychunk);
        Py_DECREF(pychunk);
        if (chunk == (unsigned long)-1 && PyErr_Occurred())
            goto Done;
        newn = PyNumber_Rshift(n, thirtytwo);
        if (newn == NULL)
            goto Done;
        Py_DECREF(n);
        n = newn;
        if (keyused >= keymax) {
            unsigned long bigger = keymax << 1;
            if ((bigger >> 1) != keymax) {
                PyErr_NoMemory();
                goto Done;
            }
            key = (unsigned long *)PyMem_Realloc(key,
                                    bigger * sizeof(*key));
            if (key == NULL)
                goto Done;
            keymax = bigger;
        }
        assert(keyused < keymax);
        key[keyused++] = chunk;
    }

    if (keyused == 0)
        key[keyused++] = 0UL;
    result = init_by_array(self, key, keyused);
Done:
    Py_XDECREF(masklower);
    Py_XDECREF(thirtytwo);
    Py_XDECREF(n);
    PyMem_Free(key);
    return result;
}

static PyObject *
random_getstate(RandomObject *self)
{
    PyObject *state;
    PyObject *element;
    int i;

    state = PyTuple_New(N+1);
    if (state == NULL)
        return NULL;
    for (i=0; i<N ; i++) {
        element = PyLong_FromUnsignedLong(self->state[i]);
        if (element == NULL)
            goto Fail;
        PyTuple_SET_ITEM(state, i, element);
    }
    element = PyLong_FromLong((long)(self->index));
    if (element == NULL)
        goto Fail;
    PyTuple_SET_ITEM(state, i, element);
    return state;

Fail:
    Py_DECREF(state);
    return NULL;
}

static PyObject *
random_setstate(RandomObject *self, PyObject *state)
{
    int i;
    unsigned long element;
    long index;

    if (!PyTuple_Check(state)) {
        PyErr_SetString(PyExc_TypeError,
            "state vector must be a tuple");
        return NULL;
    }
    if (PyTuple_Size(state) != N+1) {
        PyErr_SetString(PyExc_ValueError,
            "state vector is the wrong size");
        return NULL;
    }

    for (i=0; i<N ; i++) {
        element = PyLong_AsUnsignedLong(PyTuple_GET_ITEM(state, i));
        if (element == (unsigned long)-1 && PyErr_Occurred())
            return NULL;
        self->state[i] = element & 0xffffffffUL;
    }

    index = PyLong_AsLong(PyTuple_GET_ITEM(state, i));
    if (index == -1 && PyErr_Occurred())
        return NULL;
    self->index = (int)index;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
random_jumpahead(RandomObject *self, PyObject *n)
{
    long i, j;
    PyObject *iobj;
    PyObject *remobj;
    unsigned long *mt, tmp, nonzero;

    if (!PyInt_Check(n) && !PyLong_Check(n)) {
        PyErr_Format(PyExc_TypeError, "jumpahead requires an "
                     "integer, not '%s'",
                     Py_TYPE(n)->tp_name);
        return NULL;
    }

    mt = self->state;
    for (i = N-1; i > 1; i--) {
        iobj = PyInt_FromLong(i);
        if (iobj == NULL)
            return NULL;
        remobj = PyNumber_Remainder(n, iobj);
        Py_DECREF(iobj);
        if (remobj == NULL)
            return NULL;
        j = PyInt_AsLong(remobj);
        Py_DECREF(remobj);
        if (j == -1L && PyErr_Occurred())
            return NULL;
        tmp = mt[i];
        mt[i] = mt[j];
        mt[j] = tmp;
    }

    nonzero = 0;
    for (i = 1; i < N; i++) {
        mt[i] += i+1;
        mt[i] &= 0xffffffffUL;
        nonzero |= mt[i];
    }

    if (nonzero) {
        mt[0] += 1;
        mt[0] &= 0xffffffffUL;
    }
    else {
        mt[0] = 0x80000000UL;
    }

    self->index = N;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
random_getrandbits(RandomObject *self, PyObject *args)
{
    int k, i, bytes;
    unsigned long r;
    unsigned char *bytearray;
    PyObject *result;

    if (!PyArg_ParseTuple(args, "i:getrandbits", &k))
        return NULL;

    if (k <= 0) {
        PyErr_SetString(PyExc_ValueError,
                        "number of bits must be greater than zero");
        return NULL;
    }

    bytes = ((k - 1) / 32 + 1) * 4;
    bytearray = (unsigned char *)PyMem_Malloc(bytes);
    if (bytearray == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    for (i=0 ; i<bytes ; i+=4, k-=32) {
        r = genrand_int32(self);
        if (k < 32)
            r >>= (32 - k);
        bytearray[i+0] = (unsigned char)r;
        bytearray[i+1] = (unsigned char)(r >> 8);
        bytearray[i+2] = (unsigned char)(r >> 16);
        bytearray[i+3] = (unsigned char)(r >> 24);
    }

    result = _PyLong_FromByteArray(bytearray, bytes, 1, 0);
    PyMem_Free(bytearray);
    return result;
}

static PyObject *
random_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    RandomObject *self;
    PyObject *tmp;

    if (type == &Random_Type && !_PyArg_NoKeywords("Random()", kwds))
        return NULL;

    self = (RandomObject *)type->tp_alloc(type, 0);
    if (self == NULL)
        return NULL;
    tmp = random_seed(self, args);
    if (tmp == NULL) {
        Py_DECREF(self);
        return NULL;
    }
    Py_DECREF(tmp);
    return (PyObject *)self;
}

static PyMethodDef random_methods[] = {
    {"random",          (PyCFunction)random_random,  METH_NOARGS,
        PyDoc_STR("random() -> x in the interval [0, 1).")},
    {"seed",            (PyCFunction)random_seed,  METH_VARARGS,
        PyDoc_STR("seed([n]) -> None.  Defaults to current time.")},
    {"getstate",        (PyCFunction)random_getstate,  METH_NOARGS,
        PyDoc_STR("getstate() -> tuple containing the current state.")},
    {"setstate",          (PyCFunction)random_setstate,  METH_O,
        PyDoc_STR("setstate(state) -> None.  Restores generator state.")},
    {"jumpahead",       (PyCFunction)random_jumpahead,  METH_O,
        PyDoc_STR("jumpahead(int) -> None.  Create new state from "
                  "existing state and integer.")},
    {"getrandbits",     (PyCFunction)random_getrandbits,  METH_VARARGS,
        PyDoc_STR("getrandbits(k) -> x.  Generates a long int with "
                  "k random bits.")},
    {NULL,              NULL}
};

PyDoc_STRVAR(random_doc,
"Random() -> create a random number generator with its own internal state.");

static PyTypeObject Random_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_random.Random",
    sizeof(RandomObject),
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
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    random_doc,
    0,
    0,
    0,
    0,
    0,
    0,
    random_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    random_new,
    _PyObject_Del,
    0,
};

PyDoc_STRVAR(module_doc,
"Module implements the Mersenne Twister random number generator.");

PyMODINIT_FUNC
init_random(void)
{
    PyObject *m;

    if (PyType_Ready(&Random_Type) < 0)
        return;
    m = Py_InitModule3("_random", NULL, module_doc);
    if (m == NULL)
        return;
    Py_INCREF(&Random_Type);
    PyModule_AddObject(m, "Random", (PyObject *)&Random_Type);
}
