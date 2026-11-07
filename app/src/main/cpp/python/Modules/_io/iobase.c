#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "structmember.h"
#include "_iomodule.h"

typedef struct {
    PyObject_HEAD

    PyObject *dict;
    PyObject *weakreflist;
} iobase;

PyDoc_STRVAR(iobase_doc,
    "The abstract base class for all I/O classes, acting on streams of\n"
    "bytes. There is no public constructor.\n"
    "\n"
    "This class provides dummy implementations for many methods that\n"
    "derived classes can override selectively; the default implementations\n"
    "represent a file that cannot be read, written or seeked.\n"
    "\n"
    "Even though IOBase does not declare read, readinto, or write because\n"
    "their signatures will vary, implementations and clients should\n"
    "consider those methods part of the interface. Also, implementations\n"
    "may raise a IOError when operations they do not support are called.\n"
    "\n"
    "The basic type used for binary data read from or written to a file is\n"
    "bytes. bytearrays are accepted too, and in some cases (such as\n"
    "readinto) needed. Text I/O classes work with str data.\n"
    "\n"
    "Note that calling any method (even inquiries) on a closed stream is\n"
    "undefined. Implementations may raise IOError in this case.\n"
    "\n"
    "IOBase (and its subclasses) support the iterator protocol, meaning\n"
    "that an IOBase object can be iterated over yielding the lines in a\n"
    "stream.\n"
    "\n"
    "IOBase also supports the :keyword:`with` statement. In this example,\n"
    "fp is closed after the suite of the with statement is complete:\n"
    "\n"
    "with open('spam.txt', 'r') as fp:\n"
    "    fp.write('Spam and eggs!')\n");

#define IS_CLOSED(self) \
    PyObject_HasAttrString(self, "__IOBase_closed")

static PyObject *
iobase_unsupported(const char *message)
{
    PyErr_SetString(_PyIO_unsupported_operation, message);
    return NULL;
}

PyDoc_STRVAR(iobase_seek_doc,
    "Change stream position.\n"
    "\n"
    "Change the stream position to the given byte offset. The offset is\n"
    "interpreted relative to the position indicated by whence.  Values\n"
    "for whence are:\n"
    "\n"
    "* 0 -- start of stream (the default); offset should be zero or positive\n"
    "* 1 -- current stream position; offset may be negative\n"
    "* 2 -- end of stream; offset is usually negative\n"
    "\n"
    "Return the new absolute position.");

static PyObject *
iobase_seek(PyObject *self, PyObject *args)
{
    return iobase_unsupported("seek");
}

PyDoc_STRVAR(iobase_tell_doc,
             "Return current stream position.");

static PyObject *
iobase_tell(PyObject *self, PyObject *args)
{
    return PyObject_CallMethod(self, "seek", "ii", 0, 1);
}

PyDoc_STRVAR(iobase_truncate_doc,
    "Truncate file to size bytes.\n"
    "\n"
    "File pointer is left unchanged.  Size defaults to the current IO\n"
    "position as reported by tell().  Returns the new size.");

static PyObject *
iobase_truncate(PyObject *self, PyObject *args)
{
    return iobase_unsupported("truncate");
}

PyDoc_STRVAR(iobase_flush_doc,
    "Flush write buffers, if applicable.\n"
    "\n"
    "This is not implemented for read-only and non-blocking streams.\n");

static PyObject *
iobase_flush(PyObject *self, PyObject *args)
{

    if (IS_CLOSED(self)) {
        PyErr_SetString(PyExc_ValueError, "I/O operation on closed file.");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(iobase_close_doc,
    "Flush and close the IO object.\n"
    "\n"
    "This method has no effect if the file is already closed.\n");

static int
iobase_closed(PyObject *self)
{
    PyObject *res;
    int closed;

    res = PyObject_GetAttr(self, _PyIO_str_closed);
    if (res == NULL)
        return 0;
    closed = PyObject_IsTrue(res);
    Py_DECREF(res);
    return closed;
}

static PyObject *
iobase_closed_get(PyObject *self, void *context)
{
    return PyBool_FromLong(IS_CLOSED(self));
}

PyObject *
_PyIOBase_check_closed(PyObject *self, PyObject *args)
{
    if (iobase_closed(self)) {
        PyErr_SetString(PyExc_ValueError, "I/O operation on closed file.");
        return NULL;
    }
    if (args == Py_True)
        return Py_None;
    else
        Py_RETURN_NONE;
}

static PyObject *
iobase_close(PyObject *self, PyObject *args)
{
    PyObject *res;

    if (IS_CLOSED(self))
        Py_RETURN_NONE;

    res = PyObject_CallMethodObjArgs(self, _PyIO_str_flush, NULL);
    PyObject_SetAttrString(self, "__IOBase_closed", Py_True);
    if (res == NULL) {
        return NULL;
    }
    Py_XDECREF(res);
    Py_RETURN_NONE;
}

int
_PyIOBase_finalize(PyObject *self)
{
    PyObject *res;
    PyObject *tp, *v, *tb;
    int closed = 1;
    int is_zombie;

    is_zombie = (Py_REFCNT(self) == 0);
    if (is_zombie) {
        ++Py_REFCNT(self);
    }
    PyErr_Fetch(&tp, &v, &tb);

    res = PyObject_GetAttr(self, _PyIO_str_closed);
    if (res == NULL)
        PyErr_Clear();
    else {
        closed = PyObject_IsTrue(res);
        Py_DECREF(res);
        if (closed == -1)
            PyErr_Clear();
    }
    if (closed == 0) {
        res = PyObject_CallMethodObjArgs((PyObject *) self, _PyIO_str_close,
                                          NULL);

        if (res == NULL)
            PyErr_Clear();
        else
            Py_DECREF(res);
    }
    PyErr_Restore(tp, v, tb);
    if (is_zombie) {
        if (--Py_REFCNT(self) != 0) {

            Py_ssize_t refcnt = Py_REFCNT(self);
            _Py_NewReference(self);
            Py_REFCNT(self) = refcnt;

            _Py_DEC_REFTOTAL;

#ifdef COUNT_ALLOCS
            --Py_TYPE(self)->tp_frees;
            --Py_TYPE(self)->tp_allocs;
#endif
            return -1;
        }
    }
    return 0;
}

static int
iobase_traverse(iobase *self, visitproc visit, void *arg)
{
    Py_VISIT(self->dict);
    return 0;
}

static int
iobase_clear(iobase *self)
{
    if (_PyIOBase_finalize((PyObject *) self) < 0)
        return -1;
    Py_CLEAR(self->dict);
    return 0;
}

static void
iobase_dealloc(iobase *self)
{

    if (_PyIOBase_finalize((PyObject *) self) < 0) {

        if (PyType_HasFeature(Py_TYPE(self), Py_TPFLAGS_HEAPTYPE))
            Py_INCREF(Py_TYPE(self));
        return;
    }
    _PyObject_GC_UNTRACK(self);
    if (self->weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *) self);
    Py_CLEAR(self->dict);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

PyDoc_STRVAR(iobase_seekable_doc,
    "Return whether object supports random access.\n"
    "\n"
    "If False, seek(), tell() and truncate() will raise IOError.\n"
    "This method may need to do a test seek().");

static PyObject *
iobase_seekable(PyObject *self, PyObject *args)
{
    Py_RETURN_FALSE;
}

PyObject *
_PyIOBase_check_seekable(PyObject *self, PyObject *args)
{
    PyObject *res  = PyObject_CallMethodObjArgs(self, _PyIO_str_seekable, NULL);
    if (res == NULL)
        return NULL;
    if (res != Py_True) {
        Py_CLEAR(res);
        PyErr_SetString(PyExc_IOError, "File or stream is not seekable.");
        return NULL;
    }
    if (args == Py_True) {
        Py_DECREF(res);
    }
    return res;
}

PyDoc_STRVAR(iobase_readable_doc,
    "Return whether object was opened for reading.\n"
    "\n"
    "If False, read() will raise IOError.");

static PyObject *
iobase_readable(PyObject *self, PyObject *args)
{
    Py_RETURN_FALSE;
}

PyObject *
_PyIOBase_check_readable(PyObject *self, PyObject *args)
{
    PyObject *res  = PyObject_CallMethodObjArgs(self, _PyIO_str_readable, NULL);
    if (res == NULL)
        return NULL;
    if (res != Py_True) {
        Py_CLEAR(res);
        PyErr_SetString(PyExc_IOError, "File or stream is not readable.");
        return NULL;
    }
    if (args == Py_True) {
        Py_DECREF(res);
    }
    return res;
}

PyDoc_STRVAR(iobase_writable_doc,
    "Return whether object was opened for writing.\n"
    "\n"
    "If False, read() will raise IOError.");

static PyObject *
iobase_writable(PyObject *self, PyObject *args)
{
    Py_RETURN_FALSE;
}

PyObject *
_PyIOBase_check_writable(PyObject *self, PyObject *args)
{
    PyObject *res  = PyObject_CallMethodObjArgs(self, _PyIO_str_writable, NULL);
    if (res == NULL)
        return NULL;
    if (res != Py_True) {
        Py_CLEAR(res);
        PyErr_SetString(PyExc_IOError, "File or stream is not writable.");
        return NULL;
    }
    if (args == Py_True) {
        Py_DECREF(res);
    }
    return res;
}

static PyObject *
iobase_enter(PyObject *self, PyObject *args)
{
    if (_PyIOBase_check_closed(self, Py_True) == NULL)
        return NULL;

    Py_INCREF(self);
    return self;
}

static PyObject *
iobase_exit(PyObject *self, PyObject *args)
{
    return PyObject_CallMethodObjArgs(self, _PyIO_str_close, NULL);
}

PyDoc_STRVAR(iobase_fileno_doc,
    "Returns underlying file descriptor if one exists.\n"
    "\n"
    "An IOError is raised if the IO object does not use a file descriptor.\n");

static PyObject *
iobase_fileno(PyObject *self, PyObject *args)
{
    return iobase_unsupported("fileno");
}

PyDoc_STRVAR(iobase_isatty_doc,
    "Return whether this is an 'interactive' stream.\n"
    "\n"
    "Return False if it can't be determined.\n");

static PyObject *
iobase_isatty(PyObject *self, PyObject *args)
{
    if (_PyIOBase_check_closed(self, Py_True) == NULL)
        return NULL;
    Py_RETURN_FALSE;
}

PyDoc_STRVAR(iobase_readline_doc,
    "Read and return a line from the stream.\n"
    "\n"
    "If limit is specified, at most limit bytes will be read.\n"
    "\n"
    "The line terminator is always b'\\n' for binary files; for text\n"
    "files, the newlines argument to open can be used to select the line\n"
    "terminator(s) recognized.\n");

static PyObject *
iobase_readline(PyObject *self, PyObject *args)
{

    Py_ssize_t limit = -1;
    int has_peek = 0;
    PyObject *buffer, *result;
    Py_ssize_t old_size = -1;

    if (!PyArg_ParseTuple(args, "|O&:readline", &_PyIO_ConvertSsize_t, &limit)) {
        return NULL;
    }

    if (PyObject_HasAttrString(self, "peek"))
        has_peek = 1;

    buffer = PyByteArray_FromStringAndSize(NULL, 0);
    if (buffer == NULL)
        return NULL;

    while (limit < 0 || Py_SIZE(buffer) < limit) {
        Py_ssize_t nreadahead = 1;
        PyObject *b;

        if (has_peek) {
            PyObject *readahead = PyObject_CallMethod(self, "peek", "i", 1);
            if (readahead == NULL) {

                if (_PyIO_trap_eintr()) {
                    continue;
                }
                goto fail;
            }
            if (!PyBytes_Check(readahead)) {
                PyErr_Format(PyExc_IOError,
                             "peek() should have returned a bytes object, "
                             "not '%.200s'", Py_TYPE(readahead)->tp_name);
                Py_DECREF(readahead);
                goto fail;
            }
            if (PyBytes_GET_SIZE(readahead) > 0) {
                Py_ssize_t n = 0;
                const char *buf = PyBytes_AS_STRING(readahead);
                if (limit >= 0) {
                    do {
                        if (n >= PyBytes_GET_SIZE(readahead) || n >= limit)
                            break;
                        if (buf[n++] == '\n')
                            break;
                    } while (1);
                }
                else {
                    do {
                        if (n >= PyBytes_GET_SIZE(readahead))
                            break;
                        if (buf[n++] == '\n')
                            break;
                    } while (1);
                }
                nreadahead = n;
            }
            Py_DECREF(readahead);
        }

        b = PyObject_CallMethod(self, "read", "n", nreadahead);
        if (b == NULL) {

            if (_PyIO_trap_eintr()) {
                continue;
            }
            goto fail;
        }
        if (!PyBytes_Check(b)) {
            PyErr_Format(PyExc_IOError,
                         "read() should have returned a bytes object, "
                         "not '%.200s'", Py_TYPE(b)->tp_name);
            Py_DECREF(b);
            goto fail;
        }
        if (PyBytes_GET_SIZE(b) == 0) {
            Py_DECREF(b);
            break;
        }

        old_size = PyByteArray_GET_SIZE(buffer);
        PyByteArray_Resize(buffer, old_size + PyBytes_GET_SIZE(b));
        memcpy(PyByteArray_AS_STRING(buffer) + old_size,
               PyBytes_AS_STRING(b), PyBytes_GET_SIZE(b));

        Py_DECREF(b);

        if (PyByteArray_AS_STRING(buffer)[PyByteArray_GET_SIZE(buffer) - 1] == '\n')
            break;
    }

    result = PyBytes_FromStringAndSize(PyByteArray_AS_STRING(buffer),
                                       PyByteArray_GET_SIZE(buffer));
    Py_DECREF(buffer);
    return result;
  fail:
    Py_DECREF(buffer);
    return NULL;
}

static PyObject *
iobase_iter(PyObject *self)
{
    if (_PyIOBase_check_closed(self, Py_True) == NULL)
        return NULL;

    Py_INCREF(self);
    return self;
}

static PyObject *
iobase_iternext(PyObject *self)
{
    PyObject *line = PyObject_CallMethodObjArgs(self, _PyIO_str_readline, NULL);

    if (line == NULL)
        return NULL;

    if (PyObject_Size(line) == 0) {
        Py_DECREF(line);
        return NULL;
    }

    return line;
}

PyDoc_STRVAR(iobase_readlines_doc,
    "Return a list of lines from the stream.\n"
    "\n"
    "hint can be specified to control the number of lines read: no more\n"
    "lines will be read if the total size (in bytes/characters) of all\n"
    "lines so far exceeds hint.");

static PyObject *
iobase_readlines(PyObject *self, PyObject *args)
{
    Py_ssize_t hint = -1, length = 0;
    PyObject *result;

    if (!PyArg_ParseTuple(args, "|O&:readlines", &_PyIO_ConvertSsize_t, &hint)) {
        return NULL;
    }

    result = PyList_New(0);
    if (result == NULL)
        return NULL;

    if (hint <= 0) {

        PyObject *ret = PyObject_CallMethod(result, "extend", "O", self);
        if (ret == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(ret);
        return result;
    }

    while (1) {
        PyObject *line = PyIter_Next(self);
        if (line == NULL) {
            if (PyErr_Occurred()) {
                Py_DECREF(result);
                return NULL;
            }
            else
                break;
        }

        if (PyList_Append(result, line) < 0) {
            Py_DECREF(line);
            Py_DECREF(result);
            return NULL;
        }
        length += PyObject_Size(line);
        Py_DECREF(line);

        if (length > hint)
            break;
    }
    return result;
}

static PyObject *
iobase_writelines(PyObject *self, PyObject *args)
{
    PyObject *lines, *iter, *res;

    if (!PyArg_ParseTuple(args, "O:writelines", &lines)) {
        return NULL;
    }

    if (_PyIOBase_check_closed(self, Py_True) == NULL)
        return NULL;

    iter = PyObject_GetIter(lines);
    if (iter == NULL)
        return NULL;

    while (1) {
        PyObject *line = PyIter_Next(iter);
        if (line == NULL) {
            if (PyErr_Occurred()) {
                Py_DECREF(iter);
                return NULL;
            }
            else
                break;
        }

        res = NULL;
        do {
            res = PyObject_CallMethodObjArgs(self, _PyIO_str_write, line, NULL);
        } while (res == NULL && _PyIO_trap_eintr());
        Py_DECREF(line);
        if (res == NULL) {
            Py_DECREF(iter);
            return NULL;
        }
        Py_DECREF(res);
    }
    Py_DECREF(iter);
    Py_RETURN_NONE;
}

static PyMethodDef iobase_methods[] = {
    {"seek", iobase_seek, METH_VARARGS, iobase_seek_doc},
    {"tell", iobase_tell, METH_NOARGS, iobase_tell_doc},
    {"truncate", iobase_truncate, METH_VARARGS, iobase_truncate_doc},
    {"flush", iobase_flush, METH_NOARGS, iobase_flush_doc},
    {"close", iobase_close, METH_NOARGS, iobase_close_doc},

    {"seekable", iobase_seekable, METH_NOARGS, iobase_seekable_doc},
    {"readable", iobase_readable, METH_NOARGS, iobase_readable_doc},
    {"writable", iobase_writable, METH_NOARGS, iobase_writable_doc},

    {"_checkClosed",   _PyIOBase_check_closed, METH_NOARGS},
    {"_checkSeekable", _PyIOBase_check_seekable, METH_NOARGS},
    {"_checkReadable", _PyIOBase_check_readable, METH_NOARGS},
    {"_checkWritable", _PyIOBase_check_writable, METH_NOARGS},

    {"fileno", iobase_fileno, METH_NOARGS, iobase_fileno_doc},
    {"isatty", iobase_isatty, METH_NOARGS, iobase_isatty_doc},

    {"__enter__", iobase_enter, METH_NOARGS},
    {"__exit__", iobase_exit, METH_VARARGS},

    {"readline", iobase_readline, METH_VARARGS, iobase_readline_doc},
    {"readlines", iobase_readlines, METH_VARARGS, iobase_readlines_doc},
    {"writelines", iobase_writelines, METH_VARARGS},

    {NULL, NULL}
};

static PyGetSetDef iobase_getset[] = {
    {"closed", (getter)iobase_closed_get, NULL, NULL},
    {NULL}
};

PyTypeObject PyIOBase_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_io._IOBase",
    sizeof(iobase),
    0,
    (destructor)iobase_dealloc,
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
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE
        | Py_TPFLAGS_HAVE_GC,
    iobase_doc,
    (traverseproc)iobase_traverse,
    (inquiry)iobase_clear,
    0,
    offsetof(iobase, weakreflist),
    iobase_iter,
    iobase_iternext,
    iobase_methods,
    0,
    iobase_getset,
    0,
    0,
    0,
    0,
    offsetof(iobase, dict),
    0,
    0,
    PyType_GenericNew,
};

PyDoc_STRVAR(rawiobase_doc,
             "Base class for raw binary I/O.");

static PyObject *
rawiobase_read(PyObject *self, PyObject *args)
{
    Py_ssize_t n = -1;
    PyObject *b, *res;

    if (!PyArg_ParseTuple(args, "|n:read", &n)) {
        return NULL;
    }

    if (n < 0)
        return PyObject_CallMethod(self, "readall", NULL);

    b = PyByteArray_FromStringAndSize(NULL, n);
    if (b == NULL)
        return NULL;

    res = PyObject_CallMethodObjArgs(self, _PyIO_str_readinto, b, NULL);
    if (res == NULL || res == Py_None) {
        Py_DECREF(b);
        return res;
    }

    n = PyNumber_AsSsize_t(res, PyExc_ValueError);
    Py_DECREF(res);
    if (n == -1 && PyErr_Occurred()) {
        Py_DECREF(b);
        return NULL;
    }

    res = PyBytes_FromStringAndSize(PyByteArray_AsString(b), n);
    Py_DECREF(b);
    return res;
}

PyDoc_STRVAR(rawiobase_readall_doc,
             "Read until EOF, using multiple read() call.");

static PyObject *
rawiobase_readall(PyObject *self, PyObject *args)
{
    int r;
    PyObject *chunks = PyList_New(0);
    PyObject *result;

    if (chunks == NULL)
        return NULL;

    while (1) {
        PyObject *data = PyObject_CallMethod(self, "read",
                                             "i", DEFAULT_BUFFER_SIZE);
        if (!data) {

            if (_PyIO_trap_eintr()) {
                continue;
            }
            Py_DECREF(chunks);
            return NULL;
        }
        if (data == Py_None) {
            if (PyList_GET_SIZE(chunks) == 0) {
                Py_DECREF(chunks);
                return data;
            }
            Py_DECREF(data);
            break;
        }
        if (!PyBytes_Check(data)) {
            Py_DECREF(chunks);
            Py_DECREF(data);
            PyErr_SetString(PyExc_TypeError, "read() should return bytes");
            return NULL;
        }
        if (PyBytes_GET_SIZE(data) == 0) {

            Py_DECREF(data);
            break;
        }
        r = PyList_Append(chunks, data);
        Py_DECREF(data);
        if (r < 0) {
            Py_DECREF(chunks);
            return NULL;
        }
    }
    result = _PyBytes_Join(_PyIO_empty_bytes, chunks);
    Py_DECREF(chunks);
    return result;
}

static PyMethodDef rawiobase_methods[] = {
    {"read", rawiobase_read, METH_VARARGS},
    {"readall", rawiobase_readall, METH_NOARGS, rawiobase_readall_doc},
    {NULL, NULL}
};

PyTypeObject PyRawIOBase_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_io._RawIOBase",
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
    0,
    0,
    0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    rawiobase_doc,
    0,
    0,
    0,
    0,
    0,
    0,
    rawiobase_methods,
    0,
    0,
    &PyIOBase_Type,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};
