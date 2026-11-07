#include "Python.h"

#include "Python-ast.h"
#undef Yield
#include "pyarena.h"
#include "pythonrun.h"
#include "errcode.h"
#include "marshal.h"
#include "code.h"
#include "compile.h"
#include "eval.h"
#include "osdefs.h"
#include "importdl.h"

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif

#ifdef MS_WINDOWS

typedef unsigned short mode_t;
#endif

#define MAGIC (62211 | ((long)'\r'<<16) | ((long)'\n'<<24))

static long pyc_magic = MAGIC;

static PyObject *extensions = NULL;

extern struct _inittab _PyImport_Inittab[];

struct _inittab *PyImport_Inittab = _PyImport_Inittab;

struct filedescr * _PyImport_Filetab = NULL;

#ifdef RISCOS
static const struct filedescr _PyImport_StandardFiletab[] = {
    {"/py", "U", PY_SOURCE},
    {"/pyc", "rb", PY_COMPILED},
    {0, 0}
};
#else
static const struct filedescr _PyImport_StandardFiletab[] = {
    {".py", "U", PY_SOURCE},
#ifdef MS_WINDOWS
    {".pyw", "U", PY_SOURCE},
#endif
    {".pyc", "rb", PY_COMPILED},
    {0, 0}
};
#endif

#ifdef MS_WINDOWS
static int isdir(char *path) {
    DWORD rv;

    rv = GetFileAttributesA(path);
    return rv != INVALID_FILE_ATTRIBUTES && rv & FILE_ATTRIBUTE_DIRECTORY;
}
#else
#ifdef HAVE_STAT
static int isdir(char *path) {
    struct stat statbuf;
    return stat(path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode);
}
#else
#ifdef RISCOS

#else
int isdir(char *path) {
    return 0;
}
#endif
#endif
#endif

void
_PyImport_Init(void)
{
    const struct filedescr *scan;
    struct filedescr *filetab;
    int countD = 0;
    int countS = 0;

#ifdef HAVE_DYNAMIC_LOADING
    for (scan = _PyImport_DynLoadFiletab; scan->suffix != NULL; ++scan)
        ++countD;
#endif
    for (scan = _PyImport_StandardFiletab; scan->suffix != NULL; ++scan)
        ++countS;
    filetab = PyMem_NEW(struct filedescr, countD + countS + 1);
    if (filetab == NULL)
        Py_FatalError("Can't initialize import file table.");
#ifdef HAVE_DYNAMIC_LOADING
    memcpy(filetab, _PyImport_DynLoadFiletab,
           countD * sizeof(struct filedescr));
#endif
    memcpy(filetab + countD, _PyImport_StandardFiletab,
           countS * sizeof(struct filedescr));
    filetab[countD + countS].suffix = NULL;

    _PyImport_Filetab = filetab;

    if (Py_OptimizeFlag) {

        for (; filetab->suffix != NULL; filetab++) {
#ifndef RISCOS
            if (strcmp(filetab->suffix, ".pyc") == 0)
                filetab->suffix = ".pyo";
#else
            if (strcmp(filetab->suffix, "/pyc") == 0)
                filetab->suffix = "/pyo";
#endif
        }
    }

    if (Py_UnicodeFlag) {

        pyc_magic = MAGIC + 1;
    }
}

void
_PyImportHooks_Init(void)
{
    PyObject *v, *path_hooks = NULL, *zimpimport;
    int err = 0;

    if (PyType_Ready(&PyNullImporter_Type) < 0)
        goto error;

    if (Py_VerboseFlag)
        PySys_WriteStderr("# installing zipimport hook\n");

    v = PyList_New(0);
    if (v == NULL)
        goto error;
    err = PySys_SetObject("meta_path", v);
    Py_DECREF(v);
    if (err)
        goto error;
    v = PyDict_New();
    if (v == NULL)
        goto error;
    err = PySys_SetObject("path_importer_cache", v);
    Py_DECREF(v);
    if (err)
        goto error;
    path_hooks = PyList_New(0);
    if (path_hooks == NULL)
        goto error;
    err = PySys_SetObject("path_hooks", path_hooks);
    if (err) {
  error:
        PyErr_Print();
        Py_FatalError("initializing sys.meta_path, sys.path_hooks, "
                      "path_importer_cache, or NullImporter failed"
                      );
    }

    zimpimport = PyImport_ImportModule("zipimport");
    if (zimpimport == NULL) {
        PyErr_Clear();
        if (Py_VerboseFlag)
            PySys_WriteStderr("# can't import zipimport\n");
    }
    else {
        PyObject *zipimporter = PyObject_GetAttrString(zimpimport,
                                                       "zipimporter");
        Py_DECREF(zimpimport);
        if (zipimporter == NULL) {
            PyErr_Clear();
            if (Py_VerboseFlag)
                PySys_WriteStderr(
                    "# can't import zipimport.zipimporter\n");
        }
        else {

            err = PyList_Append(path_hooks, zipimporter);
            Py_DECREF(zipimporter);
            if (err)
                goto error;
            if (Py_VerboseFlag)
                PySys_WriteStderr(
                    "# installed zipimport hook\n");
        }
    }
    Py_DECREF(path_hooks);
}

void
_PyImport_Fini(void)
{
    Py_XDECREF(extensions);
    extensions = NULL;
    PyMem_DEL(_PyImport_Filetab);
    _PyImport_Filetab = NULL;
}

#ifdef WITH_THREAD

#include "pythread.h"

static PyThread_type_lock import_lock = 0;
static long import_lock_thread = -1;
static int import_lock_level = 0;

void
_PyImport_AcquireLock(void)
{
    long me = PyThread_get_thread_ident();
    if (me == -1)
        return;
    if (import_lock == NULL) {
        import_lock = PyThread_allocate_lock();
        if (import_lock == NULL)
            return;
    }
    if (import_lock_thread == me) {
        import_lock_level++;
        return;
    }
    if (import_lock_thread != -1 || !PyThread_acquire_lock(import_lock, 0))
    {
        PyThreadState *tstate = PyEval_SaveThread();
        PyThread_acquire_lock(import_lock, 1);
        PyEval_RestoreThread(tstate);
    }
    import_lock_thread = me;
    import_lock_level = 1;
}

int
_PyImport_ReleaseLock(void)
{
    long me = PyThread_get_thread_ident();
    if (me == -1 || import_lock == NULL)
        return 0;
    if (import_lock_thread != me)
        return -1;
    import_lock_level--;
    if (import_lock_level == 0) {
        import_lock_thread = -1;
        PyThread_release_lock(import_lock);
    }
    return 1;
}

void
_PyImport_ReInitLock(void)
{
    if (import_lock != NULL) {
        import_lock = PyThread_allocate_lock();
        if (import_lock == NULL) {
            Py_FatalError("PyImport_ReInitLock failed to create a new lock");
        }
    }
    import_lock_thread = -1;
    import_lock_level = 0;
}

#endif

static PyObject *
imp_lock_held(PyObject *self, PyObject *noargs)
{
#ifdef WITH_THREAD
    return PyBool_FromLong(import_lock_thread != -1);
#else
    return PyBool_FromLong(0);
#endif
}

static PyObject *
imp_acquire_lock(PyObject *self, PyObject *noargs)
{
#ifdef WITH_THREAD
    _PyImport_AcquireLock();
#endif
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
imp_release_lock(PyObject *self, PyObject *noargs)
{
#ifdef WITH_THREAD
    if (_PyImport_ReleaseLock() < 0) {
        PyErr_SetString(PyExc_RuntimeError,
                        "not holding the import lock");
        return NULL;
    }
#endif
    Py_INCREF(Py_None);
    return Py_None;
}

static void
imp_modules_reloading_clear(void)
{
    PyInterpreterState *interp = PyThreadState_Get()->interp;
    if (interp->modules_reloading != NULL)
        PyDict_Clear(interp->modules_reloading);
}

PyObject *
PyImport_GetModuleDict(void)
{
    PyInterpreterState *interp = PyThreadState_GET()->interp;
    if (interp->modules == NULL)
        Py_FatalError("PyImport_GetModuleDict: no module dictionary!");
    return interp->modules;
}

static char* sys_deletes[] = {
    "path", "argv", "ps1", "ps2", "exitfunc",
    "exc_type", "exc_value", "exc_traceback",
    "last_type", "last_value", "last_traceback",
    "path_hooks", "path_importer_cache", "meta_path",

    "flags", "float_info",
    NULL
};

static char* sys_files[] = {
    "stdin", "__stdin__",
    "stdout", "__stdout__",
    "stderr", "__stderr__",
    NULL
};

void
PyImport_Cleanup(void)
{
    Py_ssize_t pos, ndone;
    char *name;
    PyObject *key, *value, *dict;
    PyInterpreterState *interp = PyThreadState_GET()->interp;
    PyObject *modules = interp->modules;

    if (modules == NULL)
        return;

    value = PyDict_GetItemString(modules, "__builtin__");
    if (value != NULL && PyModule_Check(value)) {
        dict = PyModule_GetDict(value);
        if (Py_VerboseFlag)
            PySys_WriteStderr("# clear __builtin__._\n");
        PyDict_SetItemString(dict, "_", Py_None);
    }
    value = PyDict_GetItemString(modules, "sys");
    if (value != NULL && PyModule_Check(value)) {
        char **p;
        PyObject *v;
        dict = PyModule_GetDict(value);
        for (p = sys_deletes; *p != NULL; p++) {
            if (Py_VerboseFlag)
                PySys_WriteStderr("# clear sys.%s\n", *p);
            PyDict_SetItemString(dict, *p, Py_None);
        }
        for (p = sys_files; *p != NULL; p+=2) {
            if (Py_VerboseFlag)
                PySys_WriteStderr("# restore sys.%s\n", *p);
            v = PyDict_GetItemString(dict, *(p+1));
            if (v == NULL)
                v = Py_None;
            PyDict_SetItemString(dict, *p, v);
        }
    }

    value = PyDict_GetItemString(modules, "__main__");
    if (value != NULL && PyModule_Check(value)) {
        if (Py_VerboseFlag)
            PySys_WriteStderr("# cleanup __main__\n");
        _PyModule_Clear(value);
        PyDict_SetItemString(modules, "__main__", Py_None);
    }

    do {
        ndone = 0;
        pos = 0;
        while (PyDict_Next(modules, &pos, &key, &value)) {
            if (value->ob_refcnt != 1)
                continue;
            if (PyString_Check(key) && PyModule_Check(value)) {
                name = PyString_AS_STRING(key);
                if (strcmp(name, "__builtin__") == 0)
                    continue;
                if (strcmp(name, "sys") == 0)
                    continue;
                if (Py_VerboseFlag)
                    PySys_WriteStderr(
                        "# cleanup[1] %s\n", name);
                _PyModule_Clear(value);
                PyDict_SetItem(modules, key, Py_None);
                ndone++;
            }
        }
    } while (ndone > 0);

    pos = 0;
    while (PyDict_Next(modules, &pos, &key, &value)) {
        if (PyString_Check(key) && PyModule_Check(value)) {
            name = PyString_AS_STRING(key);
            if (strcmp(name, "__builtin__") == 0)
                continue;
            if (strcmp(name, "sys") == 0)
                continue;
            if (Py_VerboseFlag)
                PySys_WriteStderr("# cleanup[2] %s\n", name);
            _PyModule_Clear(value);
            PyDict_SetItem(modules, key, Py_None);
        }
    }

    value = PyDict_GetItemString(modules, "sys");
    if (value != NULL && PyModule_Check(value)) {
        if (Py_VerboseFlag)
            PySys_WriteStderr("# cleanup sys\n");
        _PyModule_Clear(value);
        PyDict_SetItemString(modules, "sys", Py_None);
    }
    value = PyDict_GetItemString(modules, "__builtin__");
    if (value != NULL && PyModule_Check(value)) {
        if (Py_VerboseFlag)
            PySys_WriteStderr("# cleanup __builtin__\n");
        _PyModule_Clear(value);
        PyDict_SetItemString(modules, "__builtin__", Py_None);
    }

    PyDict_Clear(modules);
    interp->modules = NULL;
    Py_DECREF(modules);
    Py_CLEAR(interp->modules_reloading);
}

long
PyImport_GetMagicNumber(void)
{
    return pyc_magic;
}

PyObject *
_PyImport_FixupExtension(char *name, char *filename)
{
    PyObject *modules, *mod, *dict, *copy;
    if (extensions == NULL) {
        extensions = PyDict_New();
        if (extensions == NULL)
            return NULL;
    }
    modules = PyImport_GetModuleDict();
    mod = PyDict_GetItemString(modules, name);
    if (mod == NULL || !PyModule_Check(mod)) {
        PyErr_Format(PyExc_SystemError,
          "_PyImport_FixupExtension: module %.200s not loaded", name);
        return NULL;
    }
    dict = PyModule_GetDict(mod);
    if (dict == NULL)
        return NULL;
    copy = PyDict_Copy(dict);
    if (copy == NULL)
        return NULL;
    PyDict_SetItemString(extensions, filename, copy);
    Py_DECREF(copy);
    return copy;
}

PyObject *
_PyImport_FindExtension(char *name, char *filename)
{
    PyObject *dict, *mod, *mdict;
    if (extensions == NULL)
        return NULL;
    dict = PyDict_GetItemString(extensions, filename);
    if (dict == NULL)
        return NULL;
    mod = PyImport_AddModule(name);
    if (mod == NULL)
        return NULL;
    mdict = PyModule_GetDict(mod);
    if (mdict == NULL)
        return NULL;
    if (PyDict_Update(mdict, dict))
        return NULL;
    if (Py_VerboseFlag)
        PySys_WriteStderr("import %s # previously loaded (%s)\n",
            name, filename);
    return mod;
}

static PyObject *
_PyImport_AddModuleObject(PyObject *name)
{
    PyObject *modules = PyImport_GetModuleDict();
    PyObject *m;

    if ((m = _PyDict_GetItemWithError(modules, name)) != NULL &&
        PyModule_Check(m)) {
        return m;
    }
    if (PyErr_Occurred()) {
        return NULL;
    }
    m = PyModule_New(PyString_AS_STRING(name));
    if (m == NULL) {
        return NULL;
    }
    if (PyDict_SetItem(modules, name, m) != 0) {
        Py_DECREF(m);
        return NULL;
    }
    assert(Py_REFCNT(m) > 1);
    Py_DECREF(m);

    return m;
}

PyObject *
PyImport_AddModule(const char *name)
{
    PyObject *nameobj, *module;
    nameobj = PyString_FromString(name);
    if (nameobj == NULL)
        return NULL;
    module = _PyImport_AddModuleObject(nameobj);
    Py_DECREF(nameobj);
    return module;
}

static void
remove_module(const char *name)
{
    PyObject *modules = PyImport_GetModuleDict();
    if (PyDict_GetItemString(modules, name) == NULL)
        return;
    if (PyDict_DelItemString(modules, name) < 0)
        Py_FatalError("import:  deleting existing key in"
                      "sys.modules failed");
}

PyObject *
PyImport_ExecCodeModule(char *name, PyObject *co)
{
    return PyImport_ExecCodeModuleEx(name, co, (char *)NULL);
}

PyObject *
PyImport_ExecCodeModuleEx(char *name, PyObject *co, char *pathname)
{
    PyObject *modules = PyImport_GetModuleDict();
    PyObject *m, *d, *v;

    m = PyImport_AddModule(name);
    if (m == NULL)
        return NULL;

    d = PyModule_GetDict(m);
    if (PyDict_GetItemString(d, "__builtins__") == NULL) {
        if (PyDict_SetItemString(d, "__builtins__",
                                 PyEval_GetBuiltins()) != 0)
            goto error;
    }

    v = NULL;
    if (pathname != NULL) {
        v = PyString_FromString(pathname);
        if (v == NULL)
            PyErr_Clear();
    }
    if (v == NULL) {
        v = ((PyCodeObject *)co)->co_filename;
        Py_INCREF(v);
    }
    if (PyDict_SetItemString(d, "__file__", v) != 0)
        PyErr_Clear();
    Py_DECREF(v);

    v = PyEval_EvalCode((PyCodeObject *)co, d, d);
    if (v == NULL)
        goto error;
    Py_DECREF(v);

    if ((m = PyDict_GetItemString(modules, name)) == NULL) {
        PyErr_Format(PyExc_ImportError,
                     "Loaded module %.200s not found in sys.modules",
                     name);
        return NULL;
    }

    Py_INCREF(m);

    return m;

  error:
    remove_module(name);
    return NULL;
}

static char *
make_compiled_pathname(char *pathname, char *buf, size_t buflen)
{
    size_t len = strlen(pathname);
    if (len+2 > buflen)
        return NULL;

#ifdef MS_WINDOWS

    if (len >= 4 && strcmp(&pathname[len-4], ".pyw") == 0)
        --len;
#endif
    memcpy(buf, pathname, len);
    buf[len] = Py_OptimizeFlag ? 'o' : 'c';
    buf[len+1] = '\0';

    return buf;
}

static FILE *
check_compiled_module(char *pathname, time_t mtime, char *cpathname)
{
    FILE *fp;
    long magic;
    long pyc_mtime;

    fp = fopen(cpathname, "rb");
    if (fp == NULL)
        return NULL;
    magic = PyMarshal_ReadLongFromFile(fp);
    if (magic != pyc_magic) {
        if (Py_VerboseFlag)
            PySys_WriteStderr("# %s has bad magic\n", cpathname);
        fclose(fp);
        return NULL;
    }
    pyc_mtime = PyMarshal_ReadLongFromFile(fp);
    if (pyc_mtime != mtime) {
        if (Py_VerboseFlag)
            PySys_WriteStderr("# %s has bad mtime\n", cpathname);
        fclose(fp);
        return NULL;
    }
    if (Py_VerboseFlag)
        PySys_WriteStderr("# %s matches %s\n", cpathname, pathname);
    return fp;
}

static PyCodeObject *
read_compiled_module(char *cpathname, FILE *fp)
{
    PyObject *co;

    co = PyMarshal_ReadLastObjectFromFile(fp);
    if (co == NULL)
        return NULL;
    if (!PyCode_Check(co)) {
        PyErr_Format(PyExc_ImportError,
                     "Non-code object in %.200s", cpathname);
        Py_DECREF(co);
        return NULL;
    }
    return (PyCodeObject *)co;
}

static PyObject *
load_compiled_module(char *name, char *cpathname, FILE *fp)
{
    long magic;
    PyCodeObject *co;
    PyObject *m;

    magic = PyMarshal_ReadLongFromFile(fp);
    if (magic != pyc_magic) {
        PyErr_Format(PyExc_ImportError,
                     "Bad magic number in %.200s", cpathname);
        return NULL;
    }
    (void) PyMarshal_ReadLongFromFile(fp);
    co = read_compiled_module(cpathname, fp);
    if (co == NULL)
        return NULL;
    if (Py_VerboseFlag)
        PySys_WriteStderr("import %s # precompiled from %s\n",
            name, cpathname);
    m = PyImport_ExecCodeModuleEx(name, (PyObject *)co, cpathname);
    Py_DECREF(co);

    return m;
}

static PyCodeObject *
parse_source_module(const char *pathname, FILE *fp)
{
    PyCodeObject *co = NULL;
    mod_ty mod;
    PyCompilerFlags flags;
    PyArena *arena = PyArena_New();
    if (arena == NULL)
        return NULL;

    flags.cf_flags = 0;

    mod = PyParser_ASTFromFile(fp, pathname, Py_file_input, 0, 0, &flags,
                               NULL, arena);
    if (mod) {
        co = PyAST_Compile(mod, pathname, NULL, arena);
    }
    PyArena_Free(arena);
    return co;
}

static FILE *
open_exclusive(char *filename, mode_t mode)
{
#if defined(O_EXCL)&&defined(O_CREAT)&&defined(O_WRONLY)&&defined(O_TRUNC)

    int fd;
    (void) unlink(filename);
    fd = open(filename, O_EXCL|O_CREAT|O_WRONLY|O_TRUNC
#ifdef O_BINARY
                            |O_BINARY
#endif
#ifdef __VMS
            , mode, "ctxt=bin", "shr=nil"
#else
            , mode
#endif
          );
    if (fd < 0)
        return NULL;
    return fdopen(fd, "wb");
#else

    return fopen(filename, "wb");
#endif
}

static void
write_compiled_module(PyCodeObject *co, char *cpathname, struct stat *srcstat, time_t mtime)
{
    FILE *fp;
#ifdef MS_WINDOWS
    mode_t mode = srcstat->st_mode & ~S_IEXEC;

    mode |= _S_IWRITE;
#else
    mode_t mode = srcstat->st_mode & ~S_IXUSR & ~S_IXGRP & ~S_IXOTH;
#endif

    fp = open_exclusive(cpathname, mode);
    if (fp == NULL) {
        if (Py_VerboseFlag)
            PySys_WriteStderr(
                "# can't create %s\n", cpathname);
        return;
    }
    PyMarshal_WriteLongToFile(pyc_magic, fp, Py_MARSHAL_VERSION);

    PyMarshal_WriteLongToFile(0L, fp, Py_MARSHAL_VERSION);
    PyMarshal_WriteObjectToFile((PyObject *)co, fp, Py_MARSHAL_VERSION);
    if (fflush(fp) != 0 || ferror(fp)) {
        if (Py_VerboseFlag)
            PySys_WriteStderr("# can't write %s\n", cpathname);

        fclose(fp);
        (void) unlink(cpathname);
        return;
    }

    fseek(fp, 4L, 0);
    assert(mtime <= 0xFFFFFFFF);
    PyMarshal_WriteLongToFile((long)mtime, fp, Py_MARSHAL_VERSION);
    fflush(fp);
    fclose(fp);
    if (Py_VerboseFlag)
        PySys_WriteStderr("# wrote %s\n", cpathname);
}

static void
update_code_filenames(PyCodeObject *co, PyObject *oldname, PyObject *newname)
{
    PyObject *constants, *tmp;
    Py_ssize_t i, n;

    if (!_PyString_Eq(co->co_filename, oldname))
        return;

    tmp = co->co_filename;
    co->co_filename = newname;
    Py_INCREF(co->co_filename);
    Py_DECREF(tmp);

    constants = co->co_consts;
    n = PyTuple_GET_SIZE(constants);
    for (i = 0; i < n; i++) {
        tmp = PyTuple_GET_ITEM(constants, i);
        if (PyCode_Check(tmp))
            update_code_filenames((PyCodeObject *)tmp,
                                  oldname, newname);
    }
}

static int
update_compiled_module(PyCodeObject *co, char *pathname)
{
    PyObject *oldname, *newname;

    if (strcmp(PyString_AsString(co->co_filename), pathname) == 0)
        return 0;

    newname = PyString_FromString(pathname);
    if (newname == NULL)
        return -1;

    oldname = co->co_filename;
    Py_INCREF(oldname);
    update_code_filenames(co, oldname, newname);
    Py_DECREF(oldname);
    Py_DECREF(newname);
    return 1;
}

#ifdef MS_WINDOWS

static __int64 secs_between_epochs = 11644473600;

static time_t
win32_mtime(FILE *fp, char *pathname)
{
    __int64 filetime;
    HANDLE fh;
    BY_HANDLE_FILE_INFORMATION file_information;

    fh = (HANDLE)_get_osfhandle(fileno(fp));
    if (fh == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(fh, &file_information)) {
        PyErr_Format(PyExc_RuntimeError,
                     "unable to get file status from '%s'",
                     pathname);
        return -1;
    }

    filetime = (__int64)file_information.ftLastWriteTime.dwHighDateTime << 32 |
               file_information.ftLastWriteTime.dwLowDateTime;
    return filetime / 10000000 - secs_between_epochs;
}

#endif

static PyObject *
load_source_module(char *name, char *pathname, FILE *fp)
{
    struct stat st;
    FILE *fpc;
    char *buf;
    char *cpathname;
    PyCodeObject *co = NULL;
    PyObject *m;
    time_t mtime;

    if (fstat(fileno(fp), &st) != 0) {
        PyErr_Format(PyExc_RuntimeError,
                     "unable to get file status from '%s'",
                     pathname);
        return NULL;
    }

#ifdef MS_WINDOWS
    mtime = win32_mtime(fp, pathname);
    if (mtime == (time_t)-1 && PyErr_Occurred())
        return NULL;
#else
    mtime = st.st_mtime;
#endif
    if (sizeof mtime > 4) {

        mtime &= 0xFFFFFFFF;
    }
    buf = PyMem_MALLOC(MAXPATHLEN+1);
    if (buf == NULL) {
        return PyErr_NoMemory();
    }
    cpathname = make_compiled_pathname(pathname, buf,
                                       (size_t)MAXPATHLEN + 1);
    if (cpathname != NULL &&
        (fpc = check_compiled_module(pathname, mtime, cpathname))) {
        co = read_compiled_module(cpathname, fpc);
        fclose(fpc);
        if (co == NULL)
            goto error_exit;
        if (update_compiled_module(co, pathname) < 0)
            goto error_exit;
        if (Py_VerboseFlag)
            PySys_WriteStderr("import %s # precompiled from %s\n",
                name, cpathname);
        pathname = cpathname;
    }
    else {
        co = parse_source_module(pathname, fp);
        if (co == NULL)
            goto error_exit;
        if (Py_VerboseFlag)
            PySys_WriteStderr("import %s # from %s\n",
                name, pathname);
        if (cpathname) {
            PyObject *ro = PySys_GetObject("dont_write_bytecode");
            int b = (ro == NULL) ? 0 : PyObject_IsTrue(ro);
            if (b < 0)
                goto error_exit;
            if (!b)
                write_compiled_module(co, cpathname, &st, mtime);
        }
    }
    m = PyImport_ExecCodeModuleEx(name, (PyObject *)co, pathname);
    Py_DECREF(co);

    PyMem_FREE(buf);
    return m;

error_exit:
    Py_XDECREF(co);
    PyMem_FREE(buf);
    return NULL;
}

static PyObject *load_module(char *, FILE *, char *, int, PyObject *);
static struct filedescr *find_module(char *, char *, PyObject *,
                                     char *, size_t, FILE **, PyObject **);
static struct _frozen *find_frozen(char *name);

static PyObject *
load_package(char *name, char *pathname)
{
    PyObject *m, *d;
    PyObject *file = NULL;
    PyObject *path = NULL;
    int err;
    char *buf = NULL;
    FILE *fp = NULL;
    struct filedescr *fdp;

    m = PyImport_AddModule(name);
    if (m == NULL)
        return NULL;
    if (Py_VerboseFlag)
        PySys_WriteStderr("import %s # directory %s\n",
            name, pathname);
    d = PyModule_GetDict(m);
    file = PyString_FromString(pathname);
    if (file == NULL)
        goto error;
    path = Py_BuildValue("[O]", file);
    if (path == NULL)
        goto error;
    err = PyDict_SetItemString(d, "__file__", file);
    if (err == 0)
        err = PyDict_SetItemString(d, "__path__", path);
    if (err != 0)
        goto error;
    buf = PyMem_MALLOC(MAXPATHLEN+1);
    if (buf == NULL) {
        PyErr_NoMemory();
        goto error;
    }
    buf[0] = '\0';
    fdp = find_module(name, "__init__", path, buf, MAXPATHLEN+1, &fp, NULL);
    if (fdp == NULL) {
        if (PyErr_ExceptionMatches(PyExc_ImportError)) {
            PyErr_Clear();
            Py_INCREF(m);
        }
        else
            m = NULL;
        goto cleanup;
    }
    m = load_module(name, fp, buf, fdp->type, NULL);
    if (fp != NULL)
        fclose(fp);
    goto cleanup;

  error:
    m = NULL;
  cleanup:
    if (buf)
        PyMem_FREE(buf);
    Py_XDECREF(path);
    Py_XDECREF(file);
    return m;
}

static int
is_builtin(char *name)
{
    int i;
    for (i = 0; PyImport_Inittab[i].name != NULL; i++) {
        if (strcmp(name, PyImport_Inittab[i].name) == 0) {
            if (PyImport_Inittab[i].initfunc == NULL)
                return -1;
            else
                return 1;
        }
    }
    return 0;
}

static PyObject *
get_path_importer(PyObject *path_importer_cache, PyObject *path_hooks,
                  PyObject *p)
{
    PyObject *importer;
    Py_ssize_t j, nhooks;

    assert(PyList_Check(path_hooks));
    assert(PyDict_Check(path_importer_cache));

    nhooks = PyList_Size(path_hooks);
    if (nhooks < 0)
        return NULL;

    importer = PyDict_GetItem(path_importer_cache, p);
    if (importer != NULL)
        return importer;

    if (PyDict_SetItem(path_importer_cache, p, Py_None) != 0)
        return NULL;

    for (j = 0; j < nhooks; j++) {
        PyObject *hook = PyList_GetItem(path_hooks, j);
        if (hook == NULL)
            return NULL;
        importer = PyObject_CallFunctionObjArgs(hook, p, NULL);
        if (importer != NULL)
            break;

        if (!PyErr_ExceptionMatches(PyExc_ImportError)) {
            return NULL;
        }
        PyErr_Clear();
    }
    if (importer == NULL) {
        importer = PyObject_CallFunctionObjArgs(
            (PyObject *)&PyNullImporter_Type, p, NULL
        );
        if (importer == NULL) {
            if (PyErr_ExceptionMatches(PyExc_ImportError)) {
                PyErr_Clear();
                return Py_None;
            }
        }
    }
    if (importer != NULL) {
        int err = PyDict_SetItem(path_importer_cache, p, importer);
        Py_DECREF(importer);
        if (err != 0)
            return NULL;
    }
    return importer;
}

PyAPI_FUNC(PyObject *)
PyImport_GetImporter(PyObject *path) {
    PyObject *importer=NULL, *path_importer_cache=NULL, *path_hooks=NULL;

    if ((path_importer_cache = PySys_GetObject("path_importer_cache"))) {
        if ((path_hooks = PySys_GetObject("path_hooks"))) {
            importer = get_path_importer(path_importer_cache,
                                         path_hooks, path);
        }
    }
    Py_XINCREF(importer);
    return importer;
}

#ifdef MS_COREDLL
extern FILE *PyWin_FindRegisteredModule(const char *, struct filedescr **,
                                        char *, Py_ssize_t);
#endif

static int case_ok(char *, Py_ssize_t, Py_ssize_t, char *);
static int find_init_module(char *);
static struct filedescr importhookdescr = {"", "", IMP_HOOK};

static struct filedescr *
find_module(char *fullname, char *subname, PyObject *path, char *buf,
            size_t buflen, FILE **p_fp, PyObject **p_loader)
{
    Py_ssize_t i, npath;
    size_t len, namelen;
    struct filedescr *fdp = NULL;
    char *filemode;
    FILE *fp = NULL;
    PyObject *path_hooks, *path_importer_cache;
    static struct filedescr fd_frozen = {"", "", PY_FROZEN};
    static struct filedescr fd_builtin = {"", "", C_BUILTIN};
    static struct filedescr fd_package = {"", "", PKG_DIRECTORY};
    char *name;
#if defined(PYOS_OS2)
    size_t saved_len;
    size_t saved_namelen;
    char *saved_buf = NULL;
#endif
    if (p_loader != NULL)
        *p_loader = NULL;

    if (strlen(subname) > MAXPATHLEN) {
        PyErr_SetString(PyExc_OverflowError,
                        "module name is too long");
        return NULL;
    }
    name = PyMem_MALLOC(MAXPATHLEN+1);
    if (name == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    strcpy(name, subname);

    if (p_loader != NULL) {
        PyObject *meta_path;

        meta_path = PySys_GetObject("meta_path");
        if (meta_path == NULL || !PyList_Check(meta_path)) {
            PyErr_SetString(PyExc_RuntimeError,
                            "sys.meta_path must be a list of "
                            "import hooks");
            goto error_exit;
        }
        Py_INCREF(meta_path);
        npath = PyList_Size(meta_path);
        for (i = 0; i < npath; i++) {
            PyObject *loader;
            PyObject *hook = PyList_GetItem(meta_path, i);
            loader = PyObject_CallMethod(hook, "find_module",
                                         "sO", fullname,
                                         path != NULL ?
                                         path : Py_None);
            if (loader == NULL) {
                Py_DECREF(meta_path);
                goto error_exit;
            }
            if (loader != Py_None) {

                *p_loader = loader;
                Py_DECREF(meta_path);
                PyMem_FREE(name);
                return &importhookdescr;
            }
            Py_DECREF(loader);
        }
        Py_DECREF(meta_path);
    }

    if (path != NULL && PyString_Check(path)) {

        if (PyString_Size(path) + 1 + strlen(name) >= (size_t)buflen) {
            PyErr_SetString(PyExc_ImportError,
                            "full frozen module name too long");
            goto error_exit;
        }
        strcpy(buf, PyString_AsString(path));
        strcat(buf, ".");
        strcat(buf, name);
        strcpy(name, buf);
        if (find_frozen(name) != NULL) {
            strcpy(buf, name);
            PyMem_FREE(name);
            return &fd_frozen;
        }
        PyErr_Format(PyExc_ImportError,
                     "No frozen submodule named %.200s", name);
        goto error_exit;
    }
    if (path == NULL) {
        if (is_builtin(name)) {
            strcpy(buf, name);
            PyMem_FREE(name);
            return &fd_builtin;
        }
        if ((find_frozen(name)) != NULL) {
            strcpy(buf, name);
            PyMem_FREE(name);
            return &fd_frozen;
        }

#ifdef MS_COREDLL
        fp = PyWin_FindRegisteredModule(name, &fdp, buf, buflen);
        if (fp != NULL) {
            *p_fp = fp;
            PyMem_FREE(name);
            return fdp;
        }
#endif
        path = PySys_GetObject("path");
    }
    if (path == NULL || !PyList_Check(path)) {
        PyErr_SetString(PyExc_RuntimeError,
                        "sys.path must be a list of directory names");
        goto error_exit;
    }

    path_hooks = PySys_GetObject("path_hooks");
    if (path_hooks == NULL || !PyList_Check(path_hooks)) {
        PyErr_SetString(PyExc_RuntimeError,
                        "sys.path_hooks must be a list of "
                        "import hooks");
        goto error_exit;
    }
    path_importer_cache = PySys_GetObject("path_importer_cache");
    if (path_importer_cache == NULL ||
        !PyDict_Check(path_importer_cache)) {
        PyErr_SetString(PyExc_RuntimeError,
                        "sys.path_importer_cache must be a dict");
        goto error_exit;
    }

    npath = PyList_Size(path);
    namelen = strlen(name);
    for (i = 0; i < npath; i++) {
        PyObject *copy = NULL;
        PyObject *v = PyList_GetItem(path, i);
        if (!v)
            goto error_exit;
#ifdef Py_USING_UNICODE
        if (PyUnicode_Check(v)) {
            copy = PyUnicode_Encode(PyUnicode_AS_UNICODE(v),
                PyUnicode_GET_SIZE(v), Py_FileSystemDefaultEncoding, NULL);
            if (copy == NULL)
                goto error_exit;
            v = copy;
        }
        else
#endif
        if (!PyString_Check(v))
            continue;
        len = PyString_GET_SIZE(v);
        if (len + 2 + namelen + MAXSUFFIXSIZE >= buflen) {
            Py_XDECREF(copy);
            continue;
        }
        strcpy(buf, PyString_AS_STRING(v));
        if (strlen(buf) != len) {
            Py_XDECREF(copy);
            continue;
        }

        if (p_loader != NULL) {
            PyObject *importer;

            importer = get_path_importer(path_importer_cache,
                                         path_hooks, v);
            if (importer == NULL) {
                Py_XDECREF(copy);
                goto error_exit;
            }

            if (importer != Py_None) {
                PyObject *loader;
                loader = PyObject_CallMethod(importer,
                                             "find_module",
                                             "s", fullname);
                Py_XDECREF(copy);
                if (loader == NULL)
                    goto error_exit;
                if (loader != Py_None) {

                    *p_loader = loader;
                    PyMem_FREE(name);
                    return &importhookdescr;
                }
                Py_DECREF(loader);
                continue;
            }
        }

        if (len > 0 && buf[len-1] != SEP
#ifdef ALTSEP
            && buf[len-1] != ALTSEP
#endif
            )
            buf[len++] = SEP;
        strcpy(buf+len, name);
        len += namelen;

        if (isdir(buf) &&
            case_ok(buf, len, namelen, name)) {
            if (find_init_module(buf)) {
                Py_XDECREF(copy);
                PyMem_FREE(name);
                return &fd_package;
            }
            else {
                char warnstr[MAXPATHLEN+80];
                sprintf(warnstr, "Not importing directory "
                    "'%.*s': missing __init__.py",
                    MAXPATHLEN, buf);
                if (PyErr_Warn(PyExc_ImportWarning,
                               warnstr)) {
                    Py_XDECREF(copy);
                    goto error_exit;
                }
            }
        }
#if defined(PYOS_OS2)

        saved_buf = strdup(buf);
        saved_len = len;
        saved_namelen = namelen;
#endif
        for (fdp = _PyImport_Filetab; fdp->suffix != NULL; fdp++) {
#if defined(PYOS_OS2) && defined(HAVE_DYNAMIC_LOADING)

            if (strlen(subname) > 8) {

                const struct filedescr *scan;
                scan = _PyImport_DynLoadFiletab;
                while (scan->suffix != NULL) {
                    if (!strcmp(scan->suffix, fdp->suffix))
                        break;
                    else
                        scan++;
                }
                if (scan->suffix != NULL) {

                    namelen = 8;
                    len -= strlen(subname) - namelen;
                    buf[len] = '\0';
                }
            }
#endif
            strcpy(buf+len, fdp->suffix);
            if (Py_VerboseFlag > 1)
                PySys_WriteStderr("# trying %s\n", buf);
            filemode = fdp->mode;
            if (filemode[0] == 'U')
                filemode = "r" PY_STDIOTEXTMODE;
            fp = fopen(buf, filemode);
            if (fp != NULL) {
                if (case_ok(buf, len, namelen, name))
                    break;
                else {
                    fclose(fp);
                    fp = NULL;
                }
            }
#if defined(PYOS_OS2)

            strcpy(buf, saved_buf);
            len = saved_len;
            namelen = saved_namelen;
#endif
        }
#if defined(PYOS_OS2)

        if (saved_buf)
        {
            free(saved_buf);
            saved_buf = NULL;
        }
#endif
        Py_XDECREF(copy);
        if (fp != NULL)
            break;
    }
    if (fp == NULL) {
        PyErr_Format(PyExc_ImportError,
                     "No module named %.200s", name);
        goto error_exit;
    }
    *p_fp = fp;
    PyMem_FREE(name);
    return fdp;

error_exit:
    PyMem_FREE(name);
    return NULL;
}

struct filedescr *
_PyImport_FindModule(const char *name, PyObject *path, char *buf,
            size_t buflen, FILE **p_fp, PyObject **p_loader)
{
    return find_module((char *) name, (char *) name, path,
                       buf, buflen, p_fp, p_loader);
}

PyAPI_FUNC(int) _PyImport_IsScript(struct filedescr * fd)
{
    return fd->type == PY_SOURCE || fd->type == PY_COMPILED;
}

#if defined(MS_WINDOWS)
#include <windows.h>

#elif defined(DJGPP)
#include <dir.h>

#elif (defined(__MACH__) && defined(__APPLE__) || defined(__CYGWIN__)) && defined(HAVE_DIRENT_H)
#include <sys/types.h>
#include <dirent.h>

#elif defined(PYOS_OS2)
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_NOPMAPI
#include <os2.h>

#elif defined(RISCOS)
#include "oslib/osfscontrol.h"
#endif

static int
case_ok(char *buf, Py_ssize_t len, Py_ssize_t namelen, char *name)
{

#if defined(MS_WINDOWS)
    WIN32_FIND_DATA data;
    HANDLE h;

    if (Py_GETENV("PYTHONCASEOK") != NULL)
        return 1;

    h = FindFirstFile(buf, &data);
    if (h == INVALID_HANDLE_VALUE) {
        PyErr_Format(PyExc_NameError,
          "Can't find file for module %.100s\n(filename %.300s)",
          name, buf);
        return 0;
    }
    FindClose(h);
    return strncmp(data.cFileName, name, namelen) == 0;

#elif defined(DJGPP)
    struct ffblk ffblk;
    int done;

    if (Py_GETENV("PYTHONCASEOK") != NULL)
        return 1;

    done = findfirst(buf, &ffblk, FA_ARCH|FA_RDONLY|FA_HIDDEN|FA_DIREC);
    if (done) {
        PyErr_Format(PyExc_NameError,
          "Can't find file for module %.100s\n(filename %.300s)",
          name, buf);
        return 0;
    }
    return strncmp(ffblk.ff_name, name, namelen) == 0;

#elif (defined(__MACH__) && defined(__APPLE__) || defined(__CYGWIN__)) && defined(HAVE_DIRENT_H)
    DIR *dirp;
    struct dirent *dp;
    char dirname[MAXPATHLEN + 1];
    const int dirlen = len - namelen - 1;

    if (Py_GETENV("PYTHONCASEOK") != NULL)
        return 1;

    if (dirlen <= 0) {
        dirname[0] = '.';
        dirname[1] = '\0';
    }
    else {
        assert(dirlen <= MAXPATHLEN);
        memcpy(dirname, buf, dirlen);
        dirname[dirlen] = '\0';
    }

    dirp = opendir(dirname);
    if (dirp) {
        char *nameWithExt = buf + len - namelen;
        while ((dp = readdir(dirp)) != NULL) {
            const int thislen =
#ifdef _DIRENT_HAVE_D_NAMELEN
                                    dp->d_namlen;
#else
                                    strlen(dp->d_name);
#endif
            if (thislen >= namelen &&
                strcmp(dp->d_name, nameWithExt) == 0) {
                (void)closedir(dirp);
                return 1;
            }
        }
        (void)closedir(dirp);
    }
    return 0 ;

#elif defined(RISCOS)
    char canon[MAXPATHLEN+1];
    char buf2[MAXPATHLEN+2];
    char *nameWithExt = buf+len-namelen;
    int canonlen;
    os_error *e;

    if (Py_GETENV("PYTHONCASEOK") != NULL)
        return 1;

    strcpy(buf2, buf);
    strcat(buf2, "*");

    e = xosfscontrol_canonicalise_path(buf2,canon,0,0,MAXPATHLEN+1,&canonlen);
    canonlen = MAXPATHLEN+1-canonlen;
    if (e || canonlen<=0 || canonlen>(MAXPATHLEN+1) )
        return 0;
    if (strcmp(nameWithExt, canon+canonlen-strlen(nameWithExt))==0)
        return 1;

    return 0;

#elif defined(PYOS_OS2)
    HDIR hdir = 1;
    ULONG srchcnt = 1;
    FILEFINDBUF3 ffbuf;
    APIRET rc;

    if (Py_GETENV("PYTHONCASEOK") != NULL)
        return 1;

    rc = DosFindFirst(buf,
                      &hdir,
                      FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_DIRECTORY,
                      &ffbuf, sizeof(ffbuf),
                      &srchcnt,
                      FIL_STANDARD);
    if (rc != NO_ERROR)
        return 0;
    return strncmp(ffbuf.achName, name, namelen) == 0;

#else
    return 1;

#endif
}

#ifdef HAVE_STAT

static int
find_init_module(char *buf)
{
    const size_t save_len = strlen(buf);
    size_t i = save_len;
    char *pname;
    struct stat statbuf;

    if (save_len + 13 >= MAXPATHLEN)
        return 0;
    buf[i++] = SEP;
    pname = buf + i;
    strcpy(pname, "__init__.py");
    if (stat(buf, &statbuf) == 0) {
        if (case_ok(buf,
                    save_len + 9,
                8,
                pname)) {
            buf[save_len] = '\0';
            return 1;
        }
    }
    i += strlen(pname);
    strcpy(buf+i, Py_OptimizeFlag ? "o" : "c");
    if (stat(buf, &statbuf) == 0) {
        if (case_ok(buf,
                    save_len + 9,
                8,
                pname)) {
            buf[save_len] = '\0';
            return 1;
        }
    }
    buf[save_len] = '\0';
    return 0;
}

#else

#ifdef RISCOS
static int
find_init_module(buf)
    char *buf;
{
    int save_len = strlen(buf);
    int i = save_len;

    if (save_len + 13 >= MAXPATHLEN)
        return 0;
    buf[i++] = SEP;
    strcpy(buf+i, "__init__/py");
    if (isfile(buf)) {
        buf[save_len] = '\0';
        return 1;
    }

    if (Py_OptimizeFlag)
        strcpy(buf+i, "o");
    else
        strcpy(buf+i, "c");
    if (isfile(buf)) {
        buf[save_len] = '\0';
        return 1;
    }
    buf[save_len] = '\0';
    return 0;
}
#endif

#endif

static int init_builtin(char *);

static PyObject *
load_module(char *name, FILE *fp, char *pathname, int type, PyObject *loader)
{
    PyObject *modules;
    PyObject *m;
    int err;

    switch (type) {
    case PY_SOURCE:
    case PY_COMPILED:
        if (fp == NULL) {
            PyErr_Format(PyExc_ValueError,
               "file object required for import (type code %d)",
                         type);
            return NULL;
        }
    }

    switch (type) {

    case PY_SOURCE:
        m = load_source_module(name, pathname, fp);
        break;

    case PY_COMPILED:
        m = load_compiled_module(name, pathname, fp);
        break;

#ifdef HAVE_DYNAMIC_LOADING
    case C_EXTENSION:
        m = _PyImport_LoadDynamicModule(name, pathname, fp);
        break;
#endif

    case PKG_DIRECTORY:
        m = load_package(name, pathname);
        break;

    case C_BUILTIN:
    case PY_FROZEN:
        if (pathname != NULL && pathname[0] != '\0')
            name = pathname;
        if (type == C_BUILTIN)
            err = init_builtin(name);
        else
            err = PyImport_ImportFrozenModule(name);
        if (err < 0)
            return NULL;
        if (err == 0) {
            PyErr_Format(PyExc_ImportError,
                         "Purported %s module %.200s not found",
                         type == C_BUILTIN ?
                                    "builtin" : "frozen",
                         name);
            return NULL;
        }
        modules = PyImport_GetModuleDict();
        m = PyDict_GetItemString(modules, name);
        if (m == NULL) {
            PyErr_Format(
                PyExc_ImportError,
                "%s module %.200s not properly initialized",
                type == C_BUILTIN ?
                    "builtin" : "frozen",
                name);
            return NULL;
        }
        Py_INCREF(m);
        break;

    case IMP_HOOK: {
        if (loader == NULL) {
            PyErr_SetString(PyExc_ImportError,
                            "import hook without loader");
            return NULL;
        }
        m = PyObject_CallMethod(loader, "load_module", "s", name);
        break;
    }

    default:
        PyErr_Format(PyExc_ImportError,
                     "Don't know how to import %.200s (type code %d)",
                      name, type);
        m = NULL;

    }

    return m;
}

static int
init_builtin(char *name)
{
    struct _inittab *p;

    if (_PyImport_FindExtension(name, name) != NULL)
        return 1;

    for (p = PyImport_Inittab; p->name != NULL; p++) {
        if (strcmp(name, p->name) == 0) {
            if (p->initfunc == NULL) {
                PyErr_Format(PyExc_ImportError,
                    "Cannot re-init internal module %.200s",
                    name);
                return -1;
            }
            if (Py_VerboseFlag)
                PySys_WriteStderr("import %s # builtin\n", name);
            (*p->initfunc)();
            if (PyErr_Occurred())
                return -1;
            if (_PyImport_FixupExtension(name, name) == NULL)
                return -1;
            return 1;
        }
    }
    return 0;
}

static struct _frozen *
find_frozen(char *name)
{
    struct _frozen *p;

    for (p = PyImport_FrozenModules; ; p++) {
        if (p->name == NULL)
            return NULL;
        if (strcmp(p->name, name) == 0)
            break;
    }
    return p;
}

static PyObject *
get_frozen_object(char *name)
{
    struct _frozen *p = find_frozen(name);
    int size;

    if (p == NULL) {
        PyErr_Format(PyExc_ImportError,
                     "No such frozen object named %.200s",
                     name);
        return NULL;
    }
    if (p->code == NULL) {
        PyErr_Format(PyExc_ImportError,
                     "Excluded frozen object named %.200s",
                     name);
        return NULL;
    }
    size = p->size;
    if (size < 0)
        size = -size;
    return PyMarshal_ReadObjectFromString((char *)p->code, size);
}

int
PyImport_ImportFrozenModule(char *name)
{
    struct _frozen *p = find_frozen(name);
    PyObject *co;
    PyObject *m;
    int ispackage;
    int size;

    if (p == NULL)
        return 0;
    if (p->code == NULL) {
        PyErr_Format(PyExc_ImportError,
                     "Excluded frozen object named %.200s",
                     name);
        return -1;
    }
    size = p->size;
    ispackage = (size < 0);
    if (ispackage)
        size = -size;
    if (Py_VerboseFlag)
        PySys_WriteStderr("import %s # frozen%s\n",
            name, ispackage ? " package" : "");
    co = PyMarshal_ReadObjectFromString((char *)p->code, size);
    if (co == NULL)
        return -1;
    if (!PyCode_Check(co)) {
        PyErr_Format(PyExc_TypeError,
                     "frozen object %.200s is not a code object",
                     name);
        goto err_return;
    }
    if (ispackage) {

        PyObject *d, *s;
        int err;
        m = PyImport_AddModule(name);
        if (m == NULL)
            goto err_return;
        d = PyModule_GetDict(m);
        s = PyString_InternFromString(name);
        if (s == NULL)
            goto err_return;
        err = PyDict_SetItemString(d, "__path__", s);
        Py_DECREF(s);
        if (err != 0)
            goto err_return;
    }
    m = PyImport_ExecCodeModuleEx(name, co, "<frozen>");
    if (m == NULL)
        goto err_return;
    Py_DECREF(co);
    Py_DECREF(m);
    return 1;
err_return:
    Py_DECREF(co);
    return -1;
}

PyObject *
PyImport_ImportModule(const char *name)
{
    PyObject *pname;
    PyObject *result;

    pname = PyString_FromString(name);
    if (pname == NULL)
        return NULL;
    result = PyImport_Import(pname);
    Py_DECREF(pname);
    return result;
}

PyObject *
PyImport_ImportModuleNoBlock(const char *name)
{
    PyObject *result;
    PyObject *modules;
#ifdef WITH_THREAD
    long me;
#endif

    modules = PyImport_GetModuleDict();
    if (modules == NULL)
        return NULL;

    result = PyDict_GetItemString(modules, name);
    if (result != NULL) {
        Py_INCREF(result);
        return result;
    }
    else {
        PyErr_Clear();
    }
#ifdef WITH_THREAD

    me = PyThread_get_thread_ident();
    if (import_lock_thread == -1 || import_lock_thread == me) {

        return PyImport_ImportModule(name);
    }
    else {
        PyErr_Format(PyExc_ImportError,
                     "Failed to import %.200s because the import lock"
                     "is held by another thread.",
                     name);
        return NULL;
    }
#else
    return PyImport_ImportModule(name);
#endif
}

static PyObject *get_parent(PyObject *globals, char *buf,
                            Py_ssize_t *p_buflen, int level);
static PyObject *load_next(PyObject *mod, PyObject *altmod,
                           char **p_name, char *buf, Py_ssize_t *p_buflen);
static int mark_miss(char *name);
static int ensure_fromlist(PyObject *mod, PyObject *fromlist,
                           char *buf, Py_ssize_t buflen, int recursive);
static PyObject * import_submodule(PyObject *mod, char *name, char *fullname);

static PyObject *
import_module_level(char *name, PyObject *globals, PyObject *locals,
                    PyObject *fromlist, int level)
{
    char *buf;
    Py_ssize_t buflen = 0;
    PyObject *parent, *head, *next, *tail;

    if (strchr(name, '/') != NULL
#ifdef MS_WINDOWS
        || strchr(name, '\\') != NULL
#endif
        ) {
        PyErr_SetString(PyExc_ImportError,
                        "Import by filename is not supported.");
        return NULL;
    }

    buf = PyMem_MALLOC(MAXPATHLEN+1);
    if (buf == NULL) {
        return PyErr_NoMemory();
    }
    parent = get_parent(globals, buf, &buflen, level);
    if (parent == NULL)
        goto error_exit;

    Py_INCREF(parent);
    head = load_next(parent, level < 0 ? Py_None : parent, &name, buf,
                        &buflen);
    Py_DECREF(parent);
    if (head == NULL)
        goto error_exit;

    tail = head;
    Py_INCREF(tail);
    while (name) {
        next = load_next(tail, tail, &name, buf, &buflen);
        Py_DECREF(tail);
        if (next == NULL) {
            Py_DECREF(head);
            goto error_exit;
        }
        tail = next;
    }
    if (tail == Py_None) {

        Py_DECREF(tail);
        Py_DECREF(head);
        PyErr_SetString(PyExc_ValueError,
                        "Empty module name");
        goto error_exit;
    }

    if (fromlist != NULL) {
        int b = (fromlist == Py_None) ? 0 : PyObject_IsTrue(fromlist);
        if (b < 0) {
            Py_DECREF(tail);
            Py_DECREF(head);
            goto error_exit;
        }
        if (!b)
            fromlist = NULL;
    }

    if (fromlist == NULL) {
        Py_DECREF(tail);
        PyMem_FREE(buf);
        return head;
    }

    Py_DECREF(head);
    if (!ensure_fromlist(tail, fromlist, buf, buflen, 0)) {
        Py_DECREF(tail);
        goto error_exit;
    }

    PyMem_FREE(buf);
    return tail;

error_exit:
    PyMem_FREE(buf);
    return NULL;
}

PyObject *
PyImport_ImportModuleLevel(char *name, PyObject *globals, PyObject *locals,
                         PyObject *fromlist, int level)
{
    PyObject *result;
    _PyImport_AcquireLock();
    result = import_module_level(name, globals, locals, fromlist, level);
    if (_PyImport_ReleaseLock() < 0) {
        Py_XDECREF(result);
        PyErr_SetString(PyExc_RuntimeError,
                        "not holding the import lock");
        return NULL;
    }
    return result;
}

static PyObject *
get_parent(PyObject *globals, char *buf, Py_ssize_t *p_buflen, int level)
{
    static PyObject *namestr = NULL;
    static PyObject *pathstr = NULL;
    static PyObject *pkgstr = NULL;
    PyObject *pkgname, *modname, *modpath, *modules, *parent;
    int orig_level = level;

    if (globals == NULL || !PyDict_Check(globals) || !level)
        return Py_None;

    if (namestr == NULL) {
        namestr = PyString_InternFromString("__name__");
        if (namestr == NULL)
            return NULL;
    }
    if (pathstr == NULL) {
        pathstr = PyString_InternFromString("__path__");
        if (pathstr == NULL)
            return NULL;
    }
    if (pkgstr == NULL) {
        pkgstr = PyString_InternFromString("__package__");
        if (pkgstr == NULL)
            return NULL;
    }

    *buf = '\0';
    *p_buflen = 0;
    pkgname = PyDict_GetItem(globals, pkgstr);

    if ((pkgname != NULL) && (pkgname != Py_None)) {

        Py_ssize_t len;
        if (!PyString_Check(pkgname)) {
            PyErr_SetString(PyExc_ValueError,
                            "__package__ set to non-string");
            return NULL;
        }
        len = PyString_GET_SIZE(pkgname);
        if (len == 0) {
            if (level > 0) {
                PyErr_SetString(PyExc_ValueError,
                    "Attempted relative import in non-package");
                return NULL;
            }
            return Py_None;
        }
        if (len > MAXPATHLEN) {
            PyErr_SetString(PyExc_ValueError,
                            "Package name too long");
            return NULL;
        }
        strcpy(buf, PyString_AS_STRING(pkgname));
    } else {

        modname = PyDict_GetItem(globals, namestr);
        if (modname == NULL || !PyString_Check(modname))
            return Py_None;

        modpath = PyDict_GetItem(globals, pathstr);
        if (modpath != NULL) {

            Py_ssize_t len = PyString_GET_SIZE(modname);
            int error;
            if (len > MAXPATHLEN) {
                PyErr_SetString(PyExc_ValueError,
                                "Module name too long");
                return NULL;
            }
            strcpy(buf, PyString_AS_STRING(modname));
            error = PyDict_SetItem(globals, pkgstr, modname);
            if (error) {
                PyErr_SetString(PyExc_ValueError,
                                "Could not set __package__");
                return NULL;
            }
        } else {

            char *start = PyString_AS_STRING(modname);
            char *lastdot = strrchr(start, '.');
            size_t len;
            int error;
            if (lastdot == NULL && level > 0) {
                PyErr_SetString(PyExc_ValueError,
                    "Attempted relative import in non-package");
                return NULL;
            }
            if (lastdot == NULL) {
                error = PyDict_SetItem(globals, pkgstr, Py_None);
                if (error) {
                    PyErr_SetString(PyExc_ValueError,
                        "Could not set __package__");
                    return NULL;
                }
                return Py_None;
            }
            len = lastdot - start;
            if (len >= MAXPATHLEN) {
                PyErr_SetString(PyExc_ValueError,
                                "Module name too long");
                return NULL;
            }
            strncpy(buf, start, len);
            buf[len] = '\0';
            pkgname = PyString_FromString(buf);
            if (pkgname == NULL) {
                return NULL;
            }
            error = PyDict_SetItem(globals, pkgstr, pkgname);
            Py_DECREF(pkgname);
            if (error) {
                PyErr_SetString(PyExc_ValueError,
                                "Could not set __package__");
                return NULL;
            }
        }
    }
    while (--level > 0) {
        char *dot = strrchr(buf, '.');
        if (dot == NULL) {
            PyErr_SetString(PyExc_ValueError,
                "Attempted relative import beyond "
                "toplevel package");
            return NULL;
        }
        *dot = '\0';
    }
    *p_buflen = strlen(buf);

    modules = PyImport_GetModuleDict();
    parent = PyDict_GetItemString(modules, buf);
    if (parent == NULL) {
        if (orig_level < 1) {
            PyObject *err_msg = PyString_FromFormat(
                "Parent module '%.200s' not found "
                "while handling absolute import", buf);
            if (err_msg == NULL) {
                return NULL;
            }
            if (!PyErr_WarnEx(PyExc_RuntimeWarning,
                            PyString_AsString(err_msg), 1)) {
                *buf = '\0';
                *p_buflen = 0;
                parent = Py_None;
            }
            Py_DECREF(err_msg);
        } else {
            PyErr_Format(PyExc_SystemError,
                "Parent module '%.200s' not loaded, "
                "cannot perform relative import", buf);
        }
    }
    return parent;

}

static PyObject *
load_next(PyObject *mod, PyObject *altmod, char **p_name, char *buf,
          Py_ssize_t *p_buflen)
{
    char *name = *p_name;
    char *dot = strchr(name, '.');
    size_t len;
    char *p;
    PyObject *result;

    if (strlen(name) == 0) {

        Py_INCREF(mod);
        *p_name = NULL;
        return mod;
    }

    if (dot == NULL) {
        *p_name = NULL;
        len = strlen(name);
    }
    else {
        *p_name = dot+1;
        len = dot-name;
    }
    if (len == 0) {
        PyErr_SetString(PyExc_ValueError,
                        "Empty module name");
        return NULL;
    }

    p = buf + *p_buflen;
    if (p != buf)
        *p++ = '.';
    if (p+len-buf >= MAXPATHLEN) {
        PyErr_SetString(PyExc_ValueError,
                        "Module name too long");
        return NULL;
    }
    strncpy(p, name, len);
    p[len] = '\0';
    *p_buflen = p+len-buf;

    result = import_submodule(mod, p, buf);
    if (result == Py_None && altmod != mod) {
        Py_DECREF(result);

        result = import_submodule(altmod, p, p);
        if (result != NULL && result != Py_None) {
            if (mark_miss(buf) != 0) {
                Py_DECREF(result);
                return NULL;
            }
            strncpy(buf, name, len);
            buf[len] = '\0';
            *p_buflen = len;
        }
    }
    if (result == NULL)
        return NULL;

    if (result == Py_None) {
        Py_DECREF(result);
        PyErr_Format(PyExc_ImportError,
                     "No module named %.200s", name);
        return NULL;
    }

    return result;
}

static int
mark_miss(char *name)
{
    PyObject *modules = PyImport_GetModuleDict();
    return PyDict_SetItemString(modules, name, Py_None);
}

static int
ensure_fromlist(PyObject *mod, PyObject *fromlist, char *buf, Py_ssize_t buflen,
                int recursive)
{
    int i;

    if (!PyObject_HasAttrString(mod, "__path__"))
        return 1;

    for (i = 0; ; i++) {
        PyObject *item = PySequence_GetItem(fromlist, i);
        int hasit;
        if (item == NULL) {
            if (PyErr_ExceptionMatches(PyExc_IndexError)) {
                PyErr_Clear();
                return 1;
            }
            return 0;
        }
        if (!PyString_Check(item)) {
            PyErr_Format(PyExc_TypeError,
                         "Item in ``from list'' must be str, not %.200s",
                         Py_TYPE(item)->tp_name);
            Py_DECREF(item);
            return 0;
        }
        if (PyString_AS_STRING(item)[0] == '*') {
            PyObject *all;
            Py_DECREF(item);

            if (recursive)
                continue;
            all = PyObject_GetAttrString(mod, "__all__");
            if (all == NULL)
                PyErr_Clear();
            else {
                int ret = ensure_fromlist(mod, all, buf, buflen, 1);
                Py_DECREF(all);
                if (!ret)
                    return 0;
            }
            continue;
        }
        hasit = PyObject_HasAttr(mod, item);
        if (!hasit) {
            char *subname = PyString_AS_STRING(item);
            PyObject *submod;
            char *p;
            if (buflen + strlen(subname) >= MAXPATHLEN) {
                PyErr_SetString(PyExc_ValueError,
                                "Module name too long");
                Py_DECREF(item);
                return 0;
            }
            p = buf + buflen;
            *p++ = '.';
            strcpy(p, subname);
            submod = import_submodule(mod, subname, buf);
            Py_XDECREF(submod);
            if (submod == NULL) {
                Py_DECREF(item);
                return 0;
            }
        }
        Py_DECREF(item);
    }

}

static int
add_submodule(PyObject *mod, PyObject *submod, char *fullname, char *subname,
              PyObject *modules)
{
    if (mod == Py_None)
        return 1;

    if (submod == NULL) {
        submod = PyDict_GetItemString(modules, fullname);
        if (submod == NULL)
            return 1;
    }
    if (PyModule_Check(mod)) {

        PyObject *dict = PyModule_GetDict(mod);
        if (!dict)
            return 0;
        if (PyDict_SetItemString(dict, subname, submod) < 0)
            return 0;
    }
    else {
        if (PyObject_SetAttrString(mod, subname, submod) < 0)
            return 0;
    }
    return 1;
}

static PyObject *
import_submodule(PyObject *mod, char *subname, char *fullname)
{
    PyObject *modules = PyImport_GetModuleDict();
    PyObject *m = NULL;

    if ((m = PyDict_GetItemString(modules, fullname)) != NULL) {
        Py_INCREF(m);
    }
    else {
        PyObject *path, *loader = NULL;
        char *buf;
        struct filedescr *fdp;
        FILE *fp = NULL;

        if (mod == Py_None)
            path = NULL;
        else {
            path = PyObject_GetAttrString(mod, "__path__");
            if (path == NULL) {
                PyErr_Clear();
                Py_INCREF(Py_None);
                return Py_None;
            }
        }

        buf = PyMem_MALLOC(MAXPATHLEN+1);
        if (buf == NULL) {
            return PyErr_NoMemory();
        }
        buf[0] = '\0';
        fdp = find_module(fullname, subname, path, buf, MAXPATHLEN+1,
                          &fp, &loader);
        Py_XDECREF(path);
        if (fdp == NULL) {
            PyMem_FREE(buf);
            if (!PyErr_ExceptionMatches(PyExc_ImportError))
                return NULL;
            PyErr_Clear();
            Py_INCREF(Py_None);
            return Py_None;
        }
        m = load_module(fullname, fp, buf, fdp->type, loader);
        Py_XDECREF(loader);
        if (fp)
            fclose(fp);
        if (!add_submodule(mod, m, fullname, subname, modules)) {
            Py_XDECREF(m);
            m = NULL;
        }
        PyMem_FREE(buf);
    }

    return m;
}

PyObject *
PyImport_ReloadModule(PyObject *m)
{
    PyInterpreterState *interp = PyThreadState_Get()->interp;
    PyObject *modules_reloading = interp->modules_reloading;
    PyObject *modules = PyImport_GetModuleDict();
    PyObject *path = NULL, *loader = NULL, *existing_m = NULL;
    char *name, *subname;
    char *buf;
    struct filedescr *fdp;
    FILE *fp = NULL;
    PyObject *newm;

    if (modules_reloading == NULL) {
        Py_FatalError("PyImport_ReloadModule: "
                      "no modules_reloading dictionary!");
        return NULL;
    }

    if (m == NULL || !PyModule_Check(m)) {
        PyErr_SetString(PyExc_TypeError,
                        "reload() argument must be module");
        return NULL;
    }
    name = PyModule_GetName(m);
    if (name == NULL)
        return NULL;
    if (m != PyDict_GetItemString(modules, name)) {
        PyErr_Format(PyExc_ImportError,
                     "reload(): module %.200s not in sys.modules",
                     name);
        return NULL;
    }
    existing_m = PyDict_GetItemString(modules_reloading, name);
    if (existing_m != NULL) {

        Py_INCREF(existing_m);
        return existing_m;
    }
    if (PyDict_SetItemString(modules_reloading, name, m) < 0)
        return NULL;

    subname = strrchr(name, '.');
    if (subname == NULL)
        subname = name;
    else {
        PyObject *parentname, *parent;
        parentname = PyString_FromStringAndSize(name, (subname-name));
        if (parentname == NULL) {
            imp_modules_reloading_clear();
            return NULL;
        }
        parent = PyDict_GetItem(modules, parentname);
        if (parent == NULL) {
            PyErr_Format(PyExc_ImportError,
                "reload(): parent %.200s not in sys.modules",
                PyString_AS_STRING(parentname));
            Py_DECREF(parentname);
            imp_modules_reloading_clear();
            return NULL;
        }
        Py_DECREF(parentname);
        subname++;
        path = PyObject_GetAttrString(parent, "__path__");
        if (path == NULL)
            PyErr_Clear();
    }
    buf = PyMem_MALLOC(MAXPATHLEN+1);
    if (buf == NULL) {
        Py_XDECREF(path);
        return PyErr_NoMemory();
    }
    buf[0] = '\0';
    fdp = find_module(name, subname, path, buf, MAXPATHLEN+1, &fp, &loader);
    Py_XDECREF(path);

    if (fdp == NULL) {
        Py_XDECREF(loader);
        imp_modules_reloading_clear();
        PyMem_FREE(buf);
        return NULL;
    }

    newm = load_module(name, fp, buf, fdp->type, loader);
    Py_XDECREF(loader);

    if (fp)
        fclose(fp);
    if (newm == NULL) {

        PyDict_SetItemString(modules, name, m);
    }
    imp_modules_reloading_clear();
    PyMem_FREE(buf);
    return newm;
}

PyObject *
PyImport_Import(PyObject *module_name)
{
    static PyObject *silly_list = NULL;
    static PyObject *builtins_str = NULL;
    static PyObject *import_str = NULL;
    PyObject *globals = NULL;
    PyObject *import = NULL;
    PyObject *builtins = NULL;
    PyObject *r = NULL;

    if (silly_list == NULL) {
        import_str = PyString_InternFromString("__import__");
        if (import_str == NULL)
            return NULL;
        builtins_str = PyString_InternFromString("__builtins__");
        if (builtins_str == NULL)
            return NULL;
        silly_list = Py_BuildValue("[s]", "__doc__");
        if (silly_list == NULL)
            return NULL;
    }

    globals = PyEval_GetGlobals();
    if (globals != NULL) {
        Py_INCREF(globals);
        builtins = PyObject_GetItem(globals, builtins_str);
        if (builtins == NULL)
            goto err;
    }
    else {

        builtins = PyImport_ImportModuleLevel("__builtin__",
                                              NULL, NULL, NULL, 0);
        if (builtins == NULL)
            return NULL;
        globals = Py_BuildValue("{OO}", builtins_str, builtins);
        if (globals == NULL)
            goto err;
    }

    if (PyDict_Check(builtins)) {
        import = PyObject_GetItem(builtins, import_str);
        if (import == NULL)
            PyErr_SetObject(PyExc_KeyError, import_str);
    }
    else
        import = PyObject_GetAttr(builtins, import_str);
    if (import == NULL)
        goto err;

    r = PyObject_CallFunction(import, "OOOOi", module_name, globals,
                              globals, silly_list, 0, NULL);

  err:
    Py_XDECREF(globals);
    Py_XDECREF(builtins);
    Py_XDECREF(import);

    return r;
}

static PyObject *
imp_get_magic(PyObject *self, PyObject *noargs)
{
    char buf[4];

    buf[0] = (char) ((pyc_magic >>  0) & 0xff);
    buf[1] = (char) ((pyc_magic >>  8) & 0xff);
    buf[2] = (char) ((pyc_magic >> 16) & 0xff);
    buf[3] = (char) ((pyc_magic >> 24) & 0xff);

    return PyString_FromStringAndSize(buf, 4);
}

static PyObject *
imp_get_suffixes(PyObject *self, PyObject *noargs)
{
    PyObject *list;
    struct filedescr *fdp;

    list = PyList_New(0);
    if (list == NULL)
        return NULL;
    for (fdp = _PyImport_Filetab; fdp->suffix != NULL; fdp++) {
        PyObject *item = Py_BuildValue("ssi",
                               fdp->suffix, fdp->mode, fdp->type);
        if (item == NULL) {
            Py_DECREF(list);
            return NULL;
        }
        if (PyList_Append(list, item) < 0) {
            Py_DECREF(list);
            Py_DECREF(item);
            return NULL;
        }
        Py_DECREF(item);
    }
    return list;
}

static PyObject *
call_find_module(char *name, PyObject *path)
{
    extern int fclose(FILE *);
    PyObject *fob, *ret;
    struct filedescr *fdp;
    char *pathname;
    FILE *fp = NULL;

    pathname = PyMem_MALLOC(MAXPATHLEN+1);
    if (pathname == NULL) {
        return PyErr_NoMemory();
    }
    pathname[0] = '\0';
    if (path == Py_None)
        path = NULL;
    fdp = find_module(NULL, name, path, pathname, MAXPATHLEN+1, &fp, NULL);
    if (fdp == NULL) {
        PyMem_FREE(pathname);
        return NULL;
    }
    if (fp != NULL) {
        fob = PyFile_FromFile(fp, pathname, fdp->mode, fclose);
        if (fob == NULL) {
            PyMem_FREE(pathname);
            return NULL;
        }
    }
    else {
        fob = Py_None;
        Py_INCREF(fob);
    }
    ret = Py_BuildValue("Os(ssi)",
                  fob, pathname, fdp->suffix, fdp->mode, fdp->type);
    Py_DECREF(fob);
    PyMem_FREE(pathname);
    return ret;
}

static PyObject *
imp_find_module(PyObject *self, PyObject *args)
{
    char *name;
    PyObject *path = NULL;
    if (!PyArg_ParseTuple(args, "s|O:find_module", &name, &path))
        return NULL;
    return call_find_module(name, path);
}

static PyObject *
imp_init_builtin(PyObject *self, PyObject *args)
{
    char *name;
    int ret;
    PyObject *m;
    if (!PyArg_ParseTuple(args, "s:init_builtin", &name))
        return NULL;
    ret = init_builtin(name);
    if (ret < 0)
        return NULL;
    if (ret == 0) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    m = PyImport_AddModule(name);
    Py_XINCREF(m);
    return m;
}

static PyObject *
imp_init_frozen(PyObject *self, PyObject *args)
{
    char *name;
    int ret;
    PyObject *m;
    if (!PyArg_ParseTuple(args, "s:init_frozen", &name))
        return NULL;
    ret = PyImport_ImportFrozenModule(name);
    if (ret < 0)
        return NULL;
    if (ret == 0) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    m = PyImport_AddModule(name);
    Py_XINCREF(m);
    return m;
}

static PyObject *
imp_get_frozen_object(PyObject *self, PyObject *args)
{
    char *name;

    if (!PyArg_ParseTuple(args, "s:get_frozen_object", &name))
        return NULL;
    return get_frozen_object(name);
}

static PyObject *
imp_is_builtin(PyObject *self, PyObject *args)
{
    char *name;
    if (!PyArg_ParseTuple(args, "s:is_builtin", &name))
        return NULL;
    return PyInt_FromLong(is_builtin(name));
}

static PyObject *
imp_is_frozen(PyObject *self, PyObject *args)
{
    char *name;
    struct _frozen *p;
    if (!PyArg_ParseTuple(args, "s:is_frozen", &name))
        return NULL;
    p = find_frozen(name);
    return PyBool_FromLong((long) (p == NULL ? 0 : p->size));
}

static FILE *
get_file(char *pathname, PyObject *fob, char *mode)
{
    FILE *fp;
    if (fob == NULL) {
        if (mode[0] == 'U')
            mode = "r" PY_STDIOTEXTMODE;
        fp = fopen(pathname, mode);
        if (fp == NULL)
            PyErr_SetFromErrno(PyExc_IOError);
    }
    else {
        fp = PyFile_AsFile(fob);
        if (fp == NULL)
            PyErr_SetString(PyExc_ValueError,
                            "bad/closed file object");
    }
    return fp;
}

static PyObject *
imp_load_compiled(PyObject *self, PyObject *args)
{
    char *name;
    char *pathname;
    PyObject *fob = NULL;
    PyObject *m;
    FILE *fp;
    if (!PyArg_ParseTuple(args, "ss|O!:load_compiled", &name, &pathname,
                          &PyFile_Type, &fob))
        return NULL;
    fp = get_file(pathname, fob, "rb");
    if (fp == NULL)
        return NULL;
    m = load_compiled_module(name, pathname, fp);
    if (fob == NULL)
        fclose(fp);
    return m;
}

#ifdef HAVE_DYNAMIC_LOADING

static PyObject *
imp_load_dynamic(PyObject *self, PyObject *args)
{
    char *name;
    char *pathname;
    PyObject *fob = NULL;
    PyObject *m;
    FILE *fp = NULL;
    if (!PyArg_ParseTuple(args, "ss|O!:load_dynamic", &name, &pathname,
                          &PyFile_Type, &fob))
        return NULL;
    if (fob) {
        fp = get_file(pathname, fob, "r");
        if (fp == NULL)
            return NULL;
    }
    m = _PyImport_LoadDynamicModule(name, pathname, fp);
    return m;
}

#endif

static PyObject *
imp_load_source(PyObject *self, PyObject *args)
{
    char *name;
    char *pathname;
    PyObject *fob = NULL;
    PyObject *m;
    FILE *fp;
    if (!PyArg_ParseTuple(args, "ss|O!:load_source", &name, &pathname,
                          &PyFile_Type, &fob))
        return NULL;
    fp = get_file(pathname, fob, "r");
    if (fp == NULL)
        return NULL;
    m = load_source_module(name, pathname, fp);
    if (fob == NULL)
        fclose(fp);
    return m;
}

static PyObject *
imp_load_module(PyObject *self, PyObject *args)
{
    char *name;
    PyObject *fob;
    char *pathname;
    char *suffix;
    char *mode;
    int type;
    FILE *fp;

    if (!PyArg_ParseTuple(args, "sOs(ssi):load_module",
                          &name, &fob, &pathname,
                          &suffix, &mode, &type))
        return NULL;
    if (*mode) {

        if (!(*mode == 'r' || *mode == 'U') || strchr(mode, '+')) {
            PyErr_Format(PyExc_ValueError,
                         "invalid file open mode %.200s", mode);
            return NULL;
        }
    }
    if (fob == Py_None)
        fp = NULL;
    else {
        if (!PyFile_Check(fob)) {
            PyErr_SetString(PyExc_ValueError,
                "load_module arg#2 should be a file or None");
            return NULL;
        }
        fp = get_file(pathname, fob, mode);
        if (fp == NULL)
            return NULL;
    }
    return load_module(name, fp, pathname, type, NULL);
}

static PyObject *
imp_load_package(PyObject *self, PyObject *args)
{
    char *name;
    char *pathname;
    if (!PyArg_ParseTuple(args, "ss:load_package", &name, &pathname))
        return NULL;
    return load_package(name, pathname);
}

static PyObject *
imp_new_module(PyObject *self, PyObject *args)
{
    char *name;
    if (!PyArg_ParseTuple(args, "s:new_module", &name))
        return NULL;
    return PyModule_New(name);
}

static PyObject *
imp_reload(PyObject *self, PyObject *v)
{
    return PyImport_ReloadModule(v);
}

PyDoc_STRVAR(doc_imp,
"This module provides the components needed to build your own\n\
__import__ function.  Undocumented functions are obsolete.");

PyDoc_STRVAR(doc_reload,
"reload(module) -> module\n\
\n\
Reload the module.  The module must have been successfully imported before.");

PyDoc_STRVAR(doc_find_module,
"find_module(name, [path]) -> (file, filename, (suffix, mode, type))\n\
Search for a module.  If path is omitted or None, search for a\n\
built-in, frozen or special module and continue search in sys.path.\n\
The module name cannot contain '.'; to search for a submodule of a\n\
package, pass the submodule name and the package's __path__.");

PyDoc_STRVAR(doc_load_module,
"load_module(name, file, filename, (suffix, mode, type)) -> module\n\
Load a module, given information returned by find_module().\n\
The module name must include the full package name, if any.");

PyDoc_STRVAR(doc_get_magic,
"get_magic() -> string\n\
Return the magic number for .pyc or .pyo files.");

PyDoc_STRVAR(doc_get_suffixes,
"get_suffixes() -> [(suffix, mode, type), ...]\n\
Return a list of (suffix, mode, type) tuples describing the files\n\
that find_module() looks for.");

PyDoc_STRVAR(doc_new_module,
"new_module(name) -> module\n\
Create a new module.  Do not enter it in sys.modules.\n\
The module name must include the full package name, if any.");

PyDoc_STRVAR(doc_lock_held,
"lock_held() -> boolean\n\
Return True if the import lock is currently held, else False.\n\
On platforms without threads, return False.");

PyDoc_STRVAR(doc_acquire_lock,
"acquire_lock() -> None\n\
Acquires the interpreter's import lock for the current thread.\n\
This lock should be used by import hooks to ensure thread-safety\n\
when importing modules.\n\
On platforms without threads, this function does nothing.");

PyDoc_STRVAR(doc_release_lock,
"release_lock() -> None\n\
Release the interpreter's import lock.\n\
On platforms without threads, this function does nothing.");

static PyMethodDef imp_methods[] = {
    {"reload",           imp_reload,       METH_O,       doc_reload},
    {"find_module",      imp_find_module,  METH_VARARGS, doc_find_module},
    {"get_magic",        imp_get_magic,    METH_NOARGS,  doc_get_magic},
    {"get_suffixes", imp_get_suffixes, METH_NOARGS,  doc_get_suffixes},
    {"load_module",      imp_load_module,  METH_VARARGS, doc_load_module},
    {"new_module",       imp_new_module,   METH_VARARGS, doc_new_module},
    {"lock_held",        imp_lock_held,    METH_NOARGS,  doc_lock_held},
    {"acquire_lock", imp_acquire_lock, METH_NOARGS,  doc_acquire_lock},
    {"release_lock", imp_release_lock, METH_NOARGS,  doc_release_lock},

    {"get_frozen_object",       imp_get_frozen_object,  METH_VARARGS},
    {"init_builtin",            imp_init_builtin,       METH_VARARGS},
    {"init_frozen",             imp_init_frozen,        METH_VARARGS},
    {"is_builtin",              imp_is_builtin,         METH_VARARGS},
    {"is_frozen",               imp_is_frozen,          METH_VARARGS},
    {"load_compiled",           imp_load_compiled,      METH_VARARGS},
#ifdef HAVE_DYNAMIC_LOADING
    {"load_dynamic",            imp_load_dynamic,       METH_VARARGS},
#endif
    {"load_package",            imp_load_package,       METH_VARARGS},
    {"load_source",             imp_load_source,        METH_VARARGS},
    {NULL,                      NULL}
};

static int
setint(PyObject *d, char *name, int value)
{
    PyObject *v;
    int err;

    v = PyInt_FromLong((long)value);
    err = PyDict_SetItemString(d, name, v);
    Py_XDECREF(v);
    return err;
}

typedef struct {
    PyObject_HEAD
} NullImporter;

static int
NullImporter_init(NullImporter *self, PyObject *args, PyObject *kwds)
{
    char *path;
    Py_ssize_t pathlen;

    if (!_PyArg_NoKeywords("NullImporter()", kwds))
        return -1;

    if (!PyArg_ParseTuple(args, "s:NullImporter",
                          &path))
        return -1;

    pathlen = strlen(path);
    if (pathlen == 0) {
        PyErr_SetString(PyExc_ImportError, "empty pathname");
        return -1;
    } else {
        if(isdir(path)) {
            PyErr_SetString(PyExc_ImportError,
                            "existing directory");
            return -1;
        }
    }
    return 0;
}

static PyObject *
NullImporter_find_module(NullImporter *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static PyMethodDef NullImporter_methods[] = {
    {"find_module", (PyCFunction)NullImporter_find_module, METH_VARARGS,
     "Always return None"
    },
    {NULL}
};

PyTypeObject PyNullImporter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "imp.NullImporter",
    sizeof(NullImporter),
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
    Py_TPFLAGS_DEFAULT,
    "Null importer object",
    0,
    0,
    0,
    0,
    0,
    0,
    NullImporter_methods,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    (initproc)NullImporter_init,
    0,
    PyType_GenericNew
};

PyMODINIT_FUNC
initimp(void)
{
    PyObject *m, *d;

    if (PyType_Ready(&PyNullImporter_Type) < 0)
        goto failure;

    m = Py_InitModule4("imp", imp_methods, doc_imp,
                       NULL, PYTHON_API_VERSION);
    if (m == NULL)
        goto failure;
    d = PyModule_GetDict(m);
    if (d == NULL)
        goto failure;

    if (setint(d, "SEARCH_ERROR", SEARCH_ERROR) < 0) goto failure;
    if (setint(d, "PY_SOURCE", PY_SOURCE) < 0) goto failure;
    if (setint(d, "PY_COMPILED", PY_COMPILED) < 0) goto failure;
    if (setint(d, "C_EXTENSION", C_EXTENSION) < 0) goto failure;
    if (setint(d, "PY_RESOURCE", PY_RESOURCE) < 0) goto failure;
    if (setint(d, "PKG_DIRECTORY", PKG_DIRECTORY) < 0) goto failure;
    if (setint(d, "C_BUILTIN", C_BUILTIN) < 0) goto failure;
    if (setint(d, "PY_FROZEN", PY_FROZEN) < 0) goto failure;
    if (setint(d, "PY_CODERESOURCE", PY_CODERESOURCE) < 0) goto failure;
    if (setint(d, "IMP_HOOK", IMP_HOOK) < 0) goto failure;

    Py_INCREF(&PyNullImporter_Type);
    PyModule_AddObject(m, "NullImporter", (PyObject *)&PyNullImporter_Type);
  failure:
    ;
}

int
PyImport_ExtendInittab(struct _inittab *newtab)
{
    static struct _inittab *our_copy = NULL;
    struct _inittab *p;
    int i, n;

    for (n = 0; newtab[n].name != NULL; n++)
        ;
    if (n == 0)
        return 0;
    for (i = 0; PyImport_Inittab[i].name != NULL; i++)
        ;

    p = our_copy;
    PyMem_RESIZE(p, struct _inittab, i+n+1);
    if (p == NULL)
        return -1;

    if (our_copy != PyImport_Inittab)
        memcpy(p, PyImport_Inittab, (i+1) * sizeof(struct _inittab));
    PyImport_Inittab = our_copy = p;
    memcpy(p+i, newtab, (n+1) * sizeof(struct _inittab));

    return 0;
}

int
PyImport_AppendInittab(const char *name, void (*initfunc)(void))
{
    struct _inittab newtab[2];

    memset(newtab, '\0', sizeof newtab);

    newtab[0].name = (char *)name;
    newtab[0].initfunc = initfunc;

    return PyImport_ExtendInittab(newtab);
}

#ifdef __cplusplus
}
#endif
