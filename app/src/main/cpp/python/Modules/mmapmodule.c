#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifndef MS_WINDOWS
#define UNIX
# ifdef __APPLE__
#  include <fcntl.h>
# endif
#endif

#ifdef MS_WINDOWS
#include <windows.h>
static int
my_getpagesize(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}

static int
my_getallocationgranularity (void)
{

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwAllocationGranularity;
}

#endif

#ifdef UNIX
#include <sys/mman.h>
#include <sys/stat.h>

#if defined(HAVE_SYSCONF) && defined(_SC_PAGESIZE)
static int
my_getpagesize(void)
{
    return sysconf(_SC_PAGESIZE);
}

#define my_getallocationgranularity my_getpagesize
#else
#define my_getpagesize getpagesize
#endif

#endif

#include <string.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#  define MAP_ANONYMOUS MAP_ANON
#endif

static PyObject *mmap_module_error;

typedef enum
{
    ACCESS_DEFAULT,
    ACCESS_READ,
    ACCESS_WRITE,
    ACCESS_COPY
} access_mode;

typedef struct {
    PyObject_HEAD
    char *      data;
    size_t      size;
    size_t      pos;
#ifdef MS_WINDOWS
    PY_LONG_LONG offset;
#else
    off_t       offset;
#endif

#ifdef MS_WINDOWS
    HANDLE      map_handle;
    HANDLE      file_handle;
    char *      tagname;
#endif

#ifdef UNIX
    int fd;
#endif

    access_mode access;
} mmap_object;

static void
mmap_object_dealloc(mmap_object *m_obj)
{
#ifdef MS_WINDOWS
    if (m_obj->data != NULL)
        UnmapViewOfFile (m_obj->data);
    if (m_obj->map_handle != NULL)
        CloseHandle (m_obj->map_handle);
    if (m_obj->file_handle != INVALID_HANDLE_VALUE)
        CloseHandle (m_obj->file_handle);
    if (m_obj->tagname)
        PyMem_Free(m_obj->tagname);
#endif

#ifdef UNIX
    if (m_obj->fd >= 0)
        (void) close(m_obj->fd);
    if (m_obj->data!=NULL) {
        if (m_obj->access != ACCESS_READ && m_obj->access != ACCESS_COPY)
            msync(m_obj->data, m_obj->size, MS_SYNC);
        munmap(m_obj->data, m_obj->size);
    }
#endif

    Py_TYPE(m_obj)->tp_free((PyObject*)m_obj);
}

static PyObject *
mmap_close_method(mmap_object *self, PyObject *unused)
{
#ifdef MS_WINDOWS

    if (self->data != NULL) {
        UnmapViewOfFile(self->data);
        self->data = NULL;
    }
    if (self->map_handle != NULL) {
        CloseHandle(self->map_handle);
        self->map_handle = NULL;
    }
    if (self->file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(self->file_handle);
        self->file_handle = INVALID_HANDLE_VALUE;
    }
#endif

#ifdef UNIX
    if (0 <= self->fd)
        (void) close(self->fd);
    self->fd = -1;
    if (self->data != NULL) {
        munmap(self->data, self->size);
        self->data = NULL;
    }
#endif

    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef MS_WINDOWS
#define CHECK_VALID(err)                                                \
do {                                                                    \
    if (self->map_handle == NULL) {                                     \
    PyErr_SetString(PyExc_ValueError, "mmap closed or invalid");        \
    return err;                                                         \
    }                                                                   \
} while (0)
#endif

#ifdef UNIX
#define CHECK_VALID(err)                                                \
do {                                                                    \
    if (self->data == NULL) {                                           \
    PyErr_SetString(PyExc_ValueError, "mmap closed or invalid");        \
    return err;                                                         \
    }                                                                   \
} while (0)
#endif

static PyObject *
mmap_read_byte_method(mmap_object *self,
                      PyObject *unused)
{
    CHECK_VALID(NULL);
    if (self->pos < self->size) {
        char value = self->data[self->pos];
        self->pos += 1;
        return Py_BuildValue("c", value);
    } else {
        PyErr_SetString(PyExc_ValueError, "read byte out of range");
        return NULL;
    }
}

static PyObject *
mmap_read_line_method(mmap_object *self,
                      PyObject *unused)
{
    char *start = self->data+self->pos;
    char *eof = self->data+self->size;
    char *eol;
    PyObject *result;

    CHECK_VALID(NULL);

    eol = memchr(start, '\n', self->size - self->pos);
    if (!eol)
        eol = eof;
    else
        ++eol;

    result = PyString_FromStringAndSize(start, (eol - start));
    self->pos += (eol - start);
    return result;
}

static PyObject *
mmap_read_method(mmap_object *self,
                 PyObject *args)
{
    Py_ssize_t num_bytes, n;
    PyObject *result;

    CHECK_VALID(NULL);
    if (!PyArg_ParseTuple(args, "n:read", &num_bytes))
        return(NULL);

    assert(self->size >= self->pos);
    n = self->size - self->pos;

    if (n < 0)
        n = PY_SSIZE_T_MAX;
    if (num_bytes < 0 || num_bytes > n) {
        num_bytes = n;
    }
    result = Py_BuildValue("s#", self->data+self->pos, num_bytes);
    self->pos += num_bytes;
    return result;
}

static PyObject *
mmap_gfind(mmap_object *self,
           PyObject *args,
           int reverse)
{
    Py_ssize_t start = self->pos;
    Py_ssize_t end = self->size;
    const char *needle;
    Py_ssize_t len;

    CHECK_VALID(NULL);
    if (!PyArg_ParseTuple(args, reverse ? "s#|nn:rfind" : "s#|nn:find",
                          &needle, &len, &start, &end)) {
        return NULL;
    } else {
        const char *p, *start_p, *end_p;
        int sign = reverse ? -1 : 1;

        if (start < 0)
            start += self->size;
        if (start < 0)
            start = 0;
        else if ((size_t)start > self->size)
            start = self->size;

        if (end < 0)
            end += self->size;
        if (end < 0)
            end = 0;
        else if ((size_t)end > self->size)
            end = self->size;

        start_p = self->data + start;
        end_p = self->data + end;

        for (p = (reverse ? end_p - len : start_p);
             (p >= start_p) && (p + len <= end_p); p += sign) {
            Py_ssize_t i;
            for (i = 0; i < len && needle[i] == p[i]; ++i)
                ;
            if (i == len) {
                return PyInt_FromSsize_t(p - self->data);
            }
        }
        return PyInt_FromLong(-1);
    }
}

static PyObject *
mmap_find_method(mmap_object *self,
                 PyObject *args)
{
    return mmap_gfind(self, args, 0);
}

static PyObject *
mmap_rfind_method(mmap_object *self,
                 PyObject *args)
{
    return mmap_gfind(self, args, 1);
}

static int
is_writeable(mmap_object *self)
{
    if (self->access != ACCESS_READ)
        return 1;
    PyErr_Format(PyExc_TypeError, "mmap can't modify a readonly memory map.");
    return 0;
}

static int
is_resizeable(mmap_object *self)
{
    if ((self->access == ACCESS_WRITE) || (self->access == ACCESS_DEFAULT))
        return 1;
    PyErr_Format(PyExc_TypeError,
                 "mmap can't resize a readonly or copy-on-write memory map.");
    return 0;
}

static PyObject *
mmap_write_method(mmap_object *self,
                  PyObject *args)
{
    Py_ssize_t length;
    char *data;

    CHECK_VALID(NULL);
    if (!PyArg_ParseTuple(args, "s#:write", &data, &length))
        return(NULL);

    if (!is_writeable(self))
        return NULL;

    if ((self->pos + length) > self->size) {
        PyErr_SetString(PyExc_ValueError, "data out of range");
        return NULL;
    }
    memcpy(self->data+self->pos, data, length);
    self->pos = self->pos+length;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
mmap_write_byte_method(mmap_object *self,
                       PyObject *args)
{
    char value;

    CHECK_VALID(NULL);
    if (!PyArg_ParseTuple(args, "c:write_byte", &value))
        return(NULL);

    if (!is_writeable(self))
        return NULL;

    if (self->pos < self->size) {
        *(self->data+self->pos) = value;
        self->pos += 1;
        Py_INCREF(Py_None);
        return Py_None;
    }
    else {
        PyErr_SetString(PyExc_ValueError, "write byte out of range");
        return NULL;
    }
}

static PyObject *
mmap_size_method(mmap_object *self,
                 PyObject *unused)
{
    CHECK_VALID(NULL);

#ifdef MS_WINDOWS
    if (self->file_handle != INVALID_HANDLE_VALUE) {
        DWORD low,high;
        PY_LONG_LONG size;
        low = GetFileSize(self->file_handle, &high);
        if (low == INVALID_FILE_SIZE) {

            DWORD error = GetLastError();
            if (error != NO_ERROR)
                return PyErr_SetFromWindowsErr(error);
        }
        if (!high && low < LONG_MAX)
            return PyInt_FromLong((long)low);
        size = (((PY_LONG_LONG)high)<<32) + low;
        return PyLong_FromLongLong(size);
    } else {
        return PyInt_FromSsize_t(self->size);
    }
#endif

#ifdef UNIX
    {
        struct stat buf;
        if (-1 == fstat(self->fd, &buf)) {
            PyErr_SetFromErrno(mmap_module_error);
            return NULL;
        }
#ifdef HAVE_LARGEFILE_SUPPORT
        return PyLong_FromLongLong(buf.st_size);
#else
        return PyInt_FromLong(buf.st_size);
#endif
    }
#endif
}

static PyObject *
mmap_resize_method(mmap_object *self,
                   PyObject *args)
{
    Py_ssize_t new_size;
    CHECK_VALID(NULL);
    if (!PyArg_ParseTuple(args, "n:resize", &new_size) ||
        !is_resizeable(self)) {
        return NULL;
#ifdef MS_WINDOWS
    } else {
        DWORD dwErrCode = 0;
        DWORD off_hi, off_lo, newSizeLow, newSizeHigh;

        UnmapViewOfFile(self->data);
        self->data = NULL;

        CloseHandle(self->map_handle);
        self->map_handle = NULL;

        newSizeHigh = (DWORD)((self->offset + new_size) >> 32);
        newSizeLow = (DWORD)((self->offset + new_size) & 0xFFFFFFFF);
        off_hi = (DWORD)(self->offset >> 32);
        off_lo = (DWORD)(self->offset & 0xFFFFFFFF);
        SetFilePointer(self->file_handle,
                       newSizeLow, &newSizeHigh, FILE_BEGIN);

        SetEndOfFile(self->file_handle);

        self->map_handle = CreateFileMapping(
            self->file_handle,
            NULL,
            PAGE_READWRITE,
            0,
            0,
            self->tagname);
        if (self->map_handle != NULL) {
            self->data = (char *) MapViewOfFile(self->map_handle,
                                                FILE_MAP_WRITE,
                                                off_hi,
                                                off_lo,
                                                new_size);
            if (self->data != NULL) {
                self->size = new_size;
                Py_INCREF(Py_None);
                return Py_None;
            } else {
                dwErrCode = GetLastError();
                CloseHandle(self->map_handle);
                self->map_handle = NULL;
            }
        } else {
            dwErrCode = GetLastError();
        }
        PyErr_SetFromWindowsErr(dwErrCode);
        return NULL;
#endif

#ifdef UNIX
#ifndef HAVE_MREMAP
    } else {
        PyErr_SetString(PyExc_SystemError,
                        "mmap: resizing not available--no mremap()");
        return NULL;
#else
    } else {
        void *newmap;

        if (ftruncate(self->fd, self->offset + new_size) == -1) {
            PyErr_SetFromErrno(mmap_module_error);
            return NULL;
        }

#ifdef MREMAP_MAYMOVE
        newmap = mremap(self->data, self->size, new_size, MREMAP_MAYMOVE);
#else
        #if defined(__NetBSD__)
            newmap = mremap(self->data, self->size, self->data, new_size, 0);
        #else
            newmap = mremap(self->data, self->size, new_size, 0);
        #endif
#endif
        if (newmap == (void *)-1)
        {
            PyErr_SetFromErrno(mmap_module_error);
            return NULL;
        }
        self->data = newmap;
        self->size = new_size;
        Py_INCREF(Py_None);
        return Py_None;
#endif
#endif
    }
}

static PyObject *
mmap_tell_method(mmap_object *self, PyObject *unused)
{
    CHECK_VALID(NULL);
    return PyInt_FromSize_t(self->pos);
}

static PyObject *
mmap_flush_method(mmap_object *self, PyObject *args)
{
    Py_ssize_t offset = 0;
    Py_ssize_t size = self->size;
    CHECK_VALID(NULL);
    if (!PyArg_ParseTuple(args, "|nn:flush", &offset, &size))
        return NULL;
    if ((size_t)(offset + size) > self->size) {
        PyErr_SetString(PyExc_ValueError, "flush values out of range");
        return NULL;
    }

    if (self->access == ACCESS_READ || self->access == ACCESS_COPY)
        return PyLong_FromLong(0);

#ifdef MS_WINDOWS
    return PyInt_FromLong((long) FlushViewOfFile(self->data+offset, size));
#elif defined(UNIX)

    if (-1 == msync(self->data + offset, size, MS_SYNC)) {
        PyErr_SetFromErrno(mmap_module_error);
        return NULL;
    }
    return PyInt_FromLong(0);
#else
    PyErr_SetString(PyExc_ValueError, "flush not supported on this system");
    return NULL;
#endif
}

static PyObject *
mmap_seek_method(mmap_object *self, PyObject *args)
{
    Py_ssize_t dist;
    int how=0;
    CHECK_VALID(NULL);
    if (!PyArg_ParseTuple(args, "n|i:seek", &dist, &how))
        return NULL;
    else {
        size_t where;
        switch (how) {
        case 0:
            if (dist < 0)
                goto onoutofrange;
            where = dist;
            break;
        case 1:
            if ((Py_ssize_t)self->pos + dist < 0)
                goto onoutofrange;
            where = self->pos + dist;
            break;
        case 2:
            if ((Py_ssize_t)self->size + dist < 0)
                goto onoutofrange;
            where = self->size + dist;
            break;
        default:
            PyErr_SetString(PyExc_ValueError, "unknown seek type");
            return NULL;
        }
        if (where > self->size)
            goto onoutofrange;
        self->pos = where;
        Py_INCREF(Py_None);
        return Py_None;
    }

  onoutofrange:
    PyErr_SetString(PyExc_ValueError, "seek out of range");
    return NULL;
}

static PyObject *
mmap_move_method(mmap_object *self, PyObject *args)
{
    unsigned long dest, src, cnt;
    CHECK_VALID(NULL);
    if (!PyArg_ParseTuple(args, "kkk:move", &dest, &src, &cnt) ||
        !is_writeable(self)) {
        return NULL;
    } else {

        if (cnt < 0 || (cnt + dest) < cnt || (cnt + src) < cnt ||
           src < 0 || src > self->size || (src + cnt) > self->size ||
           dest < 0 || dest > self->size || (dest + cnt) > self->size) {
            PyErr_SetString(PyExc_ValueError,
                "source, destination, or count out of range");
            return NULL;
        }
        memmove(self->data+dest, self->data+src, cnt);
        Py_INCREF(Py_None);
        return Py_None;
    }
}

static struct PyMethodDef mmap_object_methods[] = {
    {"close",           (PyCFunction) mmap_close_method,        METH_NOARGS},
    {"find",            (PyCFunction) mmap_find_method,         METH_VARARGS},
    {"rfind",           (PyCFunction) mmap_rfind_method,        METH_VARARGS},
    {"flush",           (PyCFunction) mmap_flush_method,        METH_VARARGS},
    {"move",            (PyCFunction) mmap_move_method,         METH_VARARGS},
    {"read",            (PyCFunction) mmap_read_method,         METH_VARARGS},
    {"read_byte",       (PyCFunction) mmap_read_byte_method,    METH_NOARGS},
    {"readline",        (PyCFunction) mmap_read_line_method,    METH_NOARGS},
    {"resize",          (PyCFunction) mmap_resize_method,       METH_VARARGS},
    {"seek",            (PyCFunction) mmap_seek_method,         METH_VARARGS},
    {"size",            (PyCFunction) mmap_size_method,         METH_NOARGS},
    {"tell",            (PyCFunction) mmap_tell_method,         METH_NOARGS},
    {"write",           (PyCFunction) mmap_write_method,        METH_VARARGS},
    {"write_byte",      (PyCFunction) mmap_write_byte_method,   METH_VARARGS},
    {NULL,         NULL}
};

static Py_ssize_t
mmap_buffer_getreadbuf(mmap_object *self, Py_ssize_t index, const void **ptr)
{
    CHECK_VALID(-1);
    if (index != 0) {
        PyErr_SetString(PyExc_SystemError,
                        "Accessing non-existent mmap segment");
        return -1;
    }
    *ptr = self->data;
    return self->size;
}

static Py_ssize_t
mmap_buffer_getwritebuf(mmap_object *self, Py_ssize_t index, const void **ptr)
{
    CHECK_VALID(-1);
    if (index != 0) {
        PyErr_SetString(PyExc_SystemError,
                        "Accessing non-existent mmap segment");
        return -1;
    }
    if (!is_writeable(self))
        return -1;
    *ptr = self->data;
    return self->size;
}

static Py_ssize_t
mmap_buffer_getsegcount(mmap_object *self, Py_ssize_t *lenp)
{
    CHECK_VALID(-1);
    if (lenp)
        *lenp = self->size;
    return 1;
}

static Py_ssize_t
mmap_buffer_getcharbuffer(mmap_object *self, Py_ssize_t index, const void **ptr)
{
    if (index != 0) {
        PyErr_SetString(PyExc_SystemError,
                        "accessing non-existent buffer segment");
        return -1;
    }
    *ptr = (const char *)self->data;
    return self->size;
}

static Py_ssize_t
mmap_length(mmap_object *self)
{
    CHECK_VALID(-1);
    return self->size;
}

static PyObject *
mmap_item(mmap_object *self, Py_ssize_t i)
{
    CHECK_VALID(NULL);
    if (i < 0 || (size_t)i >= self->size) {
        PyErr_SetString(PyExc_IndexError, "mmap index out of range");
        return NULL;
    }
    return PyString_FromStringAndSize(self->data + i, 1);
}

static PyObject *
mmap_slice(mmap_object *self, Py_ssize_t ilow, Py_ssize_t ihigh)
{
    CHECK_VALID(NULL);
    if (ilow < 0)
        ilow = 0;
    else if ((size_t)ilow > self->size)
        ilow = self->size;
    if (ihigh < 0)
        ihigh = 0;
    if (ihigh < ilow)
        ihigh = ilow;
    else if ((size_t)ihigh > self->size)
        ihigh = self->size;

    return PyString_FromStringAndSize(self->data + ilow, ihigh-ilow);
}

static PyObject *
mmap_subscript(mmap_object *self, PyObject *item)
{
    CHECK_VALID(NULL);
    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        if (i < 0)
            i += self->size;
        if (i < 0 || (size_t)i >= self->size) {
            PyErr_SetString(PyExc_IndexError,
                "mmap index out of range");
            return NULL;
        }
        return PyString_FromStringAndSize(self->data + i, 1);
    }
    else if (PySlice_Check(item)) {
        Py_ssize_t start, stop, step, slicelen;

        if (PySlice_GetIndicesEx((PySliceObject *)item, self->size,
                         &start, &stop, &step, &slicelen) < 0) {
            return NULL;
        }

        if (slicelen <= 0)
            return PyString_FromStringAndSize("", 0);
        else if (step == 1)
            return PyString_FromStringAndSize(self->data + start,
                                              slicelen);
        else {
            char *result_buf = (char *)PyMem_Malloc(slicelen);
            Py_ssize_t cur, i;
            PyObject *result;

            if (result_buf == NULL)
                return PyErr_NoMemory();
            for (cur = start, i = 0; i < slicelen;
                 cur += step, i++) {
                result_buf[i] = self->data[cur];
            }
            result = PyString_FromStringAndSize(result_buf,
                                                slicelen);
            PyMem_Free(result_buf);
            return result;
        }
    }
    else {
        PyErr_SetString(PyExc_TypeError,
                        "mmap indices must be integers");
        return NULL;
    }
}

static PyObject *
mmap_concat(mmap_object *self, PyObject *bb)
{
    CHECK_VALID(NULL);
    PyErr_SetString(PyExc_SystemError,
                    "mmaps don't support concatenation");
    return NULL;
}

static PyObject *
mmap_repeat(mmap_object *self, Py_ssize_t n)
{
    CHECK_VALID(NULL);
    PyErr_SetString(PyExc_SystemError,
                    "mmaps don't support repeat operation");
    return NULL;
}

static int
mmap_ass_slice(mmap_object *self, Py_ssize_t ilow, Py_ssize_t ihigh, PyObject *v)
{
    const char *buf;

    CHECK_VALID(-1);
    if (ilow < 0)
        ilow = 0;
    else if ((size_t)ilow > self->size)
        ilow = self->size;
    if (ihigh < 0)
        ihigh = 0;
    if (ihigh < ilow)
        ihigh = ilow;
    else if ((size_t)ihigh > self->size)
        ihigh = self->size;

    if (v == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "mmap object doesn't support slice deletion");
        return -1;
    }
    if (! (PyString_Check(v)) ) {
        PyErr_SetString(PyExc_IndexError,
                        "mmap slice assignment must be a string");
        return -1;
    }
    if (PyString_Size(v) != (ihigh - ilow)) {
        PyErr_SetString(PyExc_IndexError,
                        "mmap slice assignment is wrong size");
        return -1;
    }
    if (!is_writeable(self))
        return -1;
    buf = PyString_AsString(v);
    memcpy(self->data + ilow, buf, ihigh-ilow);
    return 0;
}

static int
mmap_ass_item(mmap_object *self, Py_ssize_t i, PyObject *v)
{
    const char *buf;

    CHECK_VALID(-1);
    if (i < 0 || (size_t)i >= self->size) {
        PyErr_SetString(PyExc_IndexError, "mmap index out of range");
        return -1;
    }
    if (v == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "mmap object doesn't support item deletion");
        return -1;
    }
    if (! (PyString_Check(v) && PyString_Size(v)==1) ) {
        PyErr_SetString(PyExc_IndexError,
                        "mmap assignment must be single-character string");
        return -1;
    }
    if (!is_writeable(self))
        return -1;
    buf = PyString_AsString(v);
    self->data[i] = buf[0];
    return 0;
}

static int
mmap_ass_subscript(mmap_object *self, PyObject *item, PyObject *value)
{
    CHECK_VALID(-1);

    if (PyIndex_Check(item)) {
        Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        const char *buf;

        if (i == -1 && PyErr_Occurred())
            return -1;
        if (i < 0)
            i += self->size;
        if (i < 0 || (size_t)i >= self->size) {
            PyErr_SetString(PyExc_IndexError,
                "mmap index out of range");
            return -1;
        }
        if (value == NULL) {
            PyErr_SetString(PyExc_TypeError,
                "mmap object doesn't support item deletion");
            return -1;
        }
        if (!PyString_Check(value) || PyString_Size(value) != 1) {
            PyErr_SetString(PyExc_IndexError,
              "mmap assignment must be single-character string");
            return -1;
        }
        if (!is_writeable(self))
            return -1;
        buf = PyString_AsString(value);
        self->data[i] = buf[0];
        return 0;
    }
    else if (PySlice_Check(item)) {
        Py_ssize_t start, stop, step, slicelen;

        if (PySlice_GetIndicesEx((PySliceObject *)item,
                                 self->size, &start, &stop,
                                 &step, &slicelen) < 0) {
            return -1;
        }
        if (value == NULL) {
            PyErr_SetString(PyExc_TypeError,
                "mmap object doesn't support slice deletion");
            return -1;
        }
        if (!PyString_Check(value)) {
            PyErr_SetString(PyExc_IndexError,
                "mmap slice assignment must be a string");
            return -1;
        }
        if (PyString_Size(value) != slicelen) {
            PyErr_SetString(PyExc_IndexError,
                "mmap slice assignment is wrong size");
            return -1;
        }
        if (!is_writeable(self))
            return -1;

        if (slicelen == 0)
            return 0;
        else if (step == 1) {
            const char *buf = PyString_AsString(value);

            if (buf == NULL)
                return -1;
            memcpy(self->data + start, buf, slicelen);
            return 0;
        }
        else {
            Py_ssize_t cur, i;
            const char *buf = PyString_AsString(value);

            if (buf == NULL)
                return -1;
            for (cur = start, i = 0; i < slicelen;
                 cur += step, i++) {
                self->data[cur] = buf[i];
            }
            return 0;
        }
    }
    else {
        PyErr_SetString(PyExc_TypeError,
                        "mmap indices must be integer");
        return -1;
    }
}

static PySequenceMethods mmap_as_sequence = {
    (lenfunc)mmap_length,
    (binaryfunc)mmap_concat,
    (ssizeargfunc)mmap_repeat,
    (ssizeargfunc)mmap_item,
    (ssizessizeargfunc)mmap_slice,
    (ssizeobjargproc)mmap_ass_item,
    (ssizessizeobjargproc)mmap_ass_slice,
};

static PyMappingMethods mmap_as_mapping = {
    (lenfunc)mmap_length,
    (binaryfunc)mmap_subscript,
    (objobjargproc)mmap_ass_subscript,
};

static PyBufferProcs mmap_as_buffer = {
    (readbufferproc)mmap_buffer_getreadbuf,
    (writebufferproc)mmap_buffer_getwritebuf,
    (segcountproc)mmap_buffer_getsegcount,
    (charbufferproc)mmap_buffer_getcharbuffer,
};

static PyObject *
new_mmap_object(PyTypeObject *type, PyObject *args, PyObject *kwdict);

PyDoc_STRVAR(mmap_doc,
"Windows: mmap(fileno, length[, tagname[, access[, offset]]])\n\
\n\
Maps length bytes from the file specified by the file handle fileno,\n\
and returns a mmap object.  If length is larger than the current size\n\
of the file, the file is extended to contain length bytes.  If length\n\
is 0, the maximum length of the map is the current size of the file,\n\
except that if the file is empty Windows raises an exception (you cannot\n\
create an empty mapping on Windows).\n\
\n\
Unix: mmap(fileno, length[, flags[, prot[, access[, offset]]]])\n\
\n\
Maps length bytes from the file specified by the file descriptor fileno,\n\
and returns a mmap object.  If length is 0, the maximum length of the map\n\
will be the current size of the file when mmap is called.\n\
flags specifies the nature of the mapping. MAP_PRIVATE creates a\n\
private copy-on-write mapping, so changes to the contents of the mmap\n\
object will be private to this process, and MAP_SHARED creates a mapping\n\
that's shared with all other processes mapping the same areas of the file.\n\
The default value is MAP_SHARED.\n\
\n\
To map anonymous memory, pass -1 as the fileno (both versions).");

static PyTypeObject mmap_object_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "mmap.mmap",
    sizeof(mmap_object),
    0,

    (destructor) mmap_object_dealloc,
    0,
    0,
    0,
    0,
    0,
    0,
    &mmap_as_sequence,
    &mmap_as_mapping,
    0,
    0,
    0,
    PyObject_GenericGetAttr,
    0,
    &mmap_as_buffer,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GETCHARBUFFER,
    mmap_doc,
    0,
    0,
    0,
    0,
    0,
    0,
    mmap_object_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    PyType_GenericAlloc,
    new_mmap_object,
    PyObject_Del,
};

static Py_ssize_t
_GetMapSize(PyObject *o, const char* param)
{
    if (o == NULL)
        return 0;
    if (PyIndex_Check(o)) {
        Py_ssize_t i = PyNumber_AsSsize_t(o, PyExc_OverflowError);
        if (i==-1 && PyErr_Occurred())
            return -1;
        if (i < 0) {
            PyErr_Format(PyExc_OverflowError,
                            "memory mapped %s must be positive",
                            param);
            return -1;
        }
        return i;
    }

    PyErr_SetString(PyExc_TypeError, "map size must be an integral value");
    return -1;
}

#ifdef UNIX
#ifdef HAVE_LARGEFILE_SUPPORT
#define _Py_PARSE_OFF_T "L"
#else
#define _Py_PARSE_OFF_T "l"
#endif

static PyObject *
new_mmap_object(PyTypeObject *type, PyObject *args, PyObject *kwdict)
{
#ifdef HAVE_FSTAT
    struct stat st;
#endif
    mmap_object *m_obj;
    PyObject *map_size_obj = NULL;
    Py_ssize_t map_size;
    off_t offset = 0;
    int fd, flags = MAP_SHARED, prot = PROT_WRITE | PROT_READ;
    int devzero = -1;
    int access = (int)ACCESS_DEFAULT;
    static char *keywords[] = {"fileno", "length",
                                     "flags", "prot",
                                     "access", "offset", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwdict, "iO|iii" _Py_PARSE_OFF_T, keywords,
                                     &fd, &map_size_obj, &flags, &prot,
                                     &access, &offset))
        return NULL;
    map_size = _GetMapSize(map_size_obj, "size");
    if (map_size < 0)
        return NULL;
    if (offset < 0) {
        PyErr_SetString(PyExc_OverflowError,
            "memory mapped offset must be positive");
        return NULL;
    }

    if ((access != (int)ACCESS_DEFAULT) &&
        ((flags != MAP_SHARED) || (prot != (PROT_WRITE | PROT_READ))))
        return PyErr_Format(PyExc_ValueError,
                            "mmap can't specify both access and flags, prot.");
    switch ((access_mode)access) {
    case ACCESS_READ:
        flags = MAP_SHARED;
        prot = PROT_READ;
        break;
    case ACCESS_WRITE:
        flags = MAP_SHARED;
        prot = PROT_READ | PROT_WRITE;
        break;
    case ACCESS_COPY:
        flags = MAP_PRIVATE;
        prot = PROT_READ | PROT_WRITE;
        break;
    case ACCESS_DEFAULT:

        if ((prot & PROT_READ) && (prot & PROT_WRITE)) {

        }
        else if (prot & PROT_WRITE) {
            access = ACCESS_WRITE;
        }
        else {
            access = ACCESS_READ;
        }
        break;
    default:
        return PyErr_Format(PyExc_ValueError,
                            "mmap invalid access parameter.");
    }

#ifdef __APPLE__

    if (fd != -1)
        (void)fcntl(fd, F_FULLFSYNC);
#endif
#ifdef HAVE_FSTAT
#  ifdef __VMS

    if (fd != -1) {
        fsync(fd);
    }
#  endif
    if (fd != -1 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        if (map_size == 0) {
            if (st.st_size == 0) {
                PyErr_SetString(PyExc_ValueError,
                                "cannot mmap an empty file");
                return NULL;
            }
            if (offset >= st.st_size) {
                PyErr_SetString(PyExc_ValueError,
                                "mmap offset is greater than file size");
                return NULL;
            }
            if (st.st_size - offset > PY_SSIZE_T_MAX) {
                PyErr_SetString(PyExc_ValueError,
                                 "mmap length is too large");
                return NULL;
            }
            map_size = (Py_ssize_t) (st.st_size - offset);
        } else if (offset + (size_t)map_size > st.st_size) {
            PyErr_SetString(PyExc_ValueError,
                            "mmap length is greater than file size");
            return NULL;
        }
    }
#endif
    m_obj = (mmap_object *)type->tp_alloc(type, 0);
    if (m_obj == NULL) {return NULL;}
    m_obj->data = NULL;
    m_obj->size = (size_t) map_size;
    m_obj->pos = (size_t) 0;
    m_obj->offset = offset;
    if (fd == -1) {
        m_obj->fd = -1;

#ifdef MAP_ANONYMOUS

        flags |= MAP_ANONYMOUS;
#else

        fd = devzero = open("/dev/zero", O_RDWR);
        if (devzero == -1) {
            Py_DECREF(m_obj);
            PyErr_SetFromErrno(mmap_module_error);
            return NULL;
        }
#endif
    } else {
        m_obj->fd = dup(fd);
        if (m_obj->fd == -1) {
            Py_DECREF(m_obj);
            PyErr_SetFromErrno(mmap_module_error);
            return NULL;
        }
    }

    m_obj->data = mmap(NULL, map_size,
                       prot, flags,
                       fd, offset);

    if (devzero != -1) {
        close(devzero);
    }

    if (m_obj->data == (char *)-1) {
        m_obj->data = NULL;
        Py_DECREF(m_obj);
        PyErr_SetFromErrno(mmap_module_error);
        return NULL;
    }
    m_obj->access = (access_mode)access;
    return (PyObject *)m_obj;
}
#endif

#ifdef MS_WINDOWS

static PyObject *
new_mmap_object(PyTypeObject *type, PyObject *args, PyObject *kwdict)
{
    mmap_object *m_obj;
    PyObject *map_size_obj = NULL;
    Py_ssize_t map_size;
    PY_LONG_LONG offset = 0, size;
    DWORD off_hi;
    DWORD off_lo;
    DWORD size_hi;
    DWORD size_lo;
    char *tagname = "";
    DWORD dwErr = 0;
    int fileno;
    HANDLE fh = 0;
    int access = (access_mode)ACCESS_DEFAULT;
    DWORD flProtect, dwDesiredAccess;
    static char *keywords[] = { "fileno", "length",
                                      "tagname",
                                      "access", "offset", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwdict, "iO|ziL", keywords,
                                     &fileno, &map_size_obj,
                                     &tagname, &access, &offset)) {
        return NULL;
    }

    switch((access_mode)access) {
    case ACCESS_READ:
        flProtect = PAGE_READONLY;
        dwDesiredAccess = FILE_MAP_READ;
        break;
    case ACCESS_DEFAULT:  case ACCESS_WRITE:
        flProtect = PAGE_READWRITE;
        dwDesiredAccess = FILE_MAP_WRITE;
        break;
    case ACCESS_COPY:
        flProtect = PAGE_WRITECOPY;
        dwDesiredAccess = FILE_MAP_COPY;
        break;
    default:
        return PyErr_Format(PyExc_ValueError,
                            "mmap invalid access parameter.");
    }

    map_size = _GetMapSize(map_size_obj, "size");
    if (map_size < 0)
        return NULL;
    if (offset < 0) {
        PyErr_SetString(PyExc_OverflowError,
            "memory mapped offset must be positive");
        return NULL;
    }

    if (fileno != -1 && fileno != 0) {

        if (_PyVerify_fd(fileno) == 0) {
            PyErr_SetFromErrno(mmap_module_error);
            return NULL;
        }
        fh = (HANDLE)_get_osfhandle(fileno);
        if (fh==(HANDLE)-1) {
            PyErr_SetFromErrno(mmap_module_error);
            return NULL;
        }

        lseek(fileno, 0, SEEK_SET);
    }

    m_obj = (mmap_object *)type->tp_alloc(type, 0);
    if (m_obj == NULL)
        return NULL;

    m_obj->data = NULL;
    m_obj->file_handle = INVALID_HANDLE_VALUE;
    m_obj->map_handle = NULL;
    m_obj->tagname = NULL;
    m_obj->offset = offset;

    if (fh) {

        if (!DuplicateHandle(
            GetCurrentProcess(),
            fh,
            GetCurrentProcess(),
            (LPHANDLE)&m_obj->file_handle,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS)) {
            dwErr = GetLastError();
            Py_DECREF(m_obj);
            PyErr_SetFromWindowsErr(dwErr);
            return NULL;
        }
        if (!map_size) {
            DWORD low,high;
            low = GetFileSize(fh, &high);

            if (low == INVALID_FILE_SIZE &&
                (dwErr = GetLastError()) != NO_ERROR) {
                Py_DECREF(m_obj);
                return PyErr_SetFromWindowsErr(dwErr);
            }

            size = (((PY_LONG_LONG) high) << 32) + low;
            if (size == 0) {
                PyErr_SetString(PyExc_ValueError,
                                "cannot mmap an empty file");
                Py_DECREF(m_obj);
                return NULL;
            }
            if (offset >= size) {
                PyErr_SetString(PyExc_ValueError,
                                "mmap offset is greater than file size");
                Py_DECREF(m_obj);
                return NULL;
            }
            if (size - offset > PY_SSIZE_T_MAX) {
                PyErr_SetString(PyExc_ValueError,
                                "mmap length is too large");
                Py_DECREF(m_obj);
                return NULL;
            }
            m_obj->size = (Py_ssize_t) (size - offset);
        } else {
            m_obj->size = map_size;
            size = offset + map_size;
        }
    }
    else {
        m_obj->size = map_size;
        size = offset + map_size;
    }

    m_obj->pos = (size_t) 0;

    if (tagname != NULL && *tagname != '\0') {
        m_obj->tagname = PyMem_Malloc(strlen(tagname)+1);
        if (m_obj->tagname == NULL) {
            PyErr_NoMemory();
            Py_DECREF(m_obj);
            return NULL;
        }
        strcpy(m_obj->tagname, tagname);
    }
    else
        m_obj->tagname = NULL;

    m_obj->access = (access_mode)access;
    size_hi = (DWORD)(size >> 32);
    size_lo = (DWORD)(size & 0xFFFFFFFF);
    off_hi = (DWORD)(offset >> 32);
    off_lo = (DWORD)(offset & 0xFFFFFFFF);

    m_obj->map_handle = CreateFileMapping(m_obj->file_handle,
                                          NULL,
                                          flProtect,
                                          size_hi,
                                          size_lo,
                                          m_obj->tagname);
    if (m_obj->map_handle != NULL) {
        m_obj->data = (char *) MapViewOfFile(m_obj->map_handle,
                                             dwDesiredAccess,
                                             off_hi,
                                             off_lo,
                                             m_obj->size);
        if (m_obj->data != NULL)
            return (PyObject *)m_obj;
        else {
            dwErr = GetLastError();
            CloseHandle(m_obj->map_handle);
            m_obj->map_handle = NULL;
        }
    } else
        dwErr = GetLastError();
    Py_DECREF(m_obj);
    PyErr_SetFromWindowsErr(dwErr);
    return NULL;
}
#endif

static void
setint(PyObject *d, const char *name, long value)
{
    PyObject *o = PyInt_FromLong(value);
    if (o && PyDict_SetItemString(d, name, o) == 0) {
        Py_DECREF(o);
    }
}

PyMODINIT_FUNC
initmmap(void)
{
    PyObject *dict, *module;

    if (PyType_Ready(&mmap_object_type) < 0)
        return;

    module = Py_InitModule("mmap", NULL);
    if (module == NULL)
        return;
    dict = PyModule_GetDict(module);
    if (!dict)
        return;
    mmap_module_error = PyErr_NewException("mmap.error",
        PyExc_EnvironmentError , NULL);
    if (mmap_module_error == NULL)
        return;
    PyDict_SetItemString(dict, "error", mmap_module_error);
    PyDict_SetItemString(dict, "mmap", (PyObject*) &mmap_object_type);
#ifdef PROT_EXEC
    setint(dict, "PROT_EXEC", PROT_EXEC);
#endif
#ifdef PROT_READ
    setint(dict, "PROT_READ", PROT_READ);
#endif
#ifdef PROT_WRITE
    setint(dict, "PROT_WRITE", PROT_WRITE);
#endif

#ifdef MAP_SHARED
    setint(dict, "MAP_SHARED", MAP_SHARED);
#endif
#ifdef MAP_PRIVATE
    setint(dict, "MAP_PRIVATE", MAP_PRIVATE);
#endif
#ifdef MAP_DENYWRITE
    setint(dict, "MAP_DENYWRITE", MAP_DENYWRITE);
#endif
#ifdef MAP_EXECUTABLE
    setint(dict, "MAP_EXECUTABLE", MAP_EXECUTABLE);
#endif
#ifdef MAP_ANONYMOUS
    setint(dict, "MAP_ANON", MAP_ANONYMOUS);
    setint(dict, "MAP_ANONYMOUS", MAP_ANONYMOUS);
#endif

    setint(dict, "PAGESIZE", (long)my_getpagesize());

    setint(dict, "ALLOCATIONGRANULARITY", (long)my_getallocationgranularity());

    setint(dict, "ACCESS_READ", ACCESS_READ);
    setint(dict, "ACCESS_WRITE", ACCESS_WRITE);
    setint(dict, "ACCESS_COPY", ACCESS_COPY);
}
