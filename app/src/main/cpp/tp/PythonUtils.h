#ifndef BUILD_TOOL_PYTHON_UTILS_H
#define BUILD_TOOL_PYTHON_UTILS_H

#include "string"
#include "FunctionUtils.h"
#include "FunctionsAddress.h"
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <android/log.h>

#define PY_TRACE(...)                                                            \
    do {                                                                         \
        if (FunctionsAddress::pyTrace())                                         \
            __android_log_print(ANDROID_LOG_INFO, "TexInjectNative", __VA_ARGS__);\
    } while (0)

typedef
enum { PyGILState_LOCKED1, PyGILState_UNLOCKED1 }
        PyGILState_STATE1;
class PyObject1;
class PyCompilerFlags1;
constexpr auto Py_file_input1 = 257;

class PythonUtils{
public:

static bool PyEvalUtf8(const std::string& statements, const std::string& expression,
                       std::string* output) {
    if (!output) return false;
    output->clear();
    FunctionsAddress::detect();
    if (Main::getBaseAddress() == 0 ||
        FunctionsAddress::PythonUtils_PyGILState_EnsureMC == 0 ||
        FunctionsAddress::PythonUtils_PyImport_AddModuleMC == 0 ||
        FunctionsAddress::PythonUtils_PyModule_GetDictMC == 0 ||
        FunctionsAddress::PythonUtils_PyRun_StringFlagsMC == 0 ||
        FunctionsAddress::PythonUtils_PyGILState_ReleaseMC == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(executionMutex());
    constexpr const char* kStatus = "INFINITECZ_PYEVAL_STATUS";
    constexpr const char* kValue = "INFINITECZ_PYEVAL_VALUE";
    setenv(kStatus, "0", 1);
    setenv(kValue, "", 1);
    setExecLocked(false);
    PY_TRACE("py[eval]: gil enter");
    const PyGILState_STATE1 state = PyGILState_EnsureMC();
    PY_TRACE("py[eval]: gil ok state=%d", (int)state);
    PyObject1* module = PyImport_AddModuleMC("__main__");
    PY_TRACE("py[eval]: addmodule=%p", (void*)module);
    PyObject1* dict = module ? PyModule_GetDictMC(module) : nullptr;
    PY_TRACE("py[eval]: dict=%p", (void*)dict);
    if (!dict) {
        reportPythonError();
        PyGILState_ReleaseMC(state);
        return false;
    }

    std::string bootstrap =
        "def __infinitecz_checked_eval():\n"
        "  import os, sys, traceback, binascii\n"
        "  __ok = '0'\n"
        "  __value = ''\n"
        "  try:\n"
        "    exec compile(" + pythonBytesLiteral(statements) +
            ", '<infinitecz-eval-setup>', 'exec') in globals(), globals()\n"
        "    __result = eval(compile(" + pythonBytesLiteral(expression) +
            ", '<infinitecz-eval>', 'eval'), globals(), globals())\n"
        "    try:\n"
        "      if isinstance(__result, unicode):\n"
        "        __result = __result.encode('utf-8')\n"
        "      else:\n"
        "        __result = str(__result)\n"
        "    except NameError:\n"
        "      __result = str(__result)\n"
        "    __value = binascii.hexlify(__result)\n"
        "    __ok = '1'\n"
        "  except BaseException:\n"
        "    try:\n"
        "      traceback.print_exc()\n"
        "    except BaseException:\n"
        "      pass\n"
        "  finally:\n"
        "    try:\n"
        "      sys.exc_clear()\n"
        "    except BaseException:\n"
        "      pass\n"
        "    try:\n"
        "      os.environ['INFINITECZ_PYEVAL_VALUE'] = __value\n"
        "      os.environ['INFINITECZ_PYEVAL_STATUS'] = __ok\n"
        "    except BaseException:\n"
        "      pass\n"
        "__infinitecz_checked_eval()\n"
        "try:\n"
        "  del __infinitecz_checked_eval\n"
        "except BaseException:\n"
        "  pass\n";
    PyObject1* result = PyRun_StringFlagsMC(
            bootstrap.c_str(), Py_file_input1, dict, dict, nullptr);
    if (!result) {
        reportPythonError();
    }
    releaseObject(result);
    const char* status = std::getenv(kStatus);
    const char* encoded = std::getenv(kValue);
    const bool success = result != nullptr && status && status[0] == '1' &&
        status[1] == '\0' && encoded && decodeHex(encoded, output);
    PyGILState_ReleaseMC(state);
    return success;
}

static bool PyExecChecked(std::string& str, bool noMessage = false) {
    (void)noMessage;
    FunctionsAddress::detect();
    if (Main::getBaseAddress() == 0 ||
        FunctionsAddress::PythonUtils_PyGILState_EnsureMC == 0 ||
        FunctionsAddress::PythonUtils_PyImport_AddModuleMC == 0 ||
        FunctionsAddress::PythonUtils_PyModule_GetDictMC == 0 ||
        FunctionsAddress::PythonUtils_PyRun_StringFlagsMC == 0 ||
        FunctionsAddress::PythonUtils_PyGILState_ReleaseMC == 0) {
        __android_log_print(ANDROID_LOG_ERROR, "TexInjectNative",
            "PyExecChecked: addr missing base=0x%lx gil=0x%lx addmod=0x%lx dict=0x%lx run=0x%lx rel=0x%lx reason=%s",
            (unsigned long) Main::getBaseAddress(),
            (unsigned long) FunctionsAddress::PythonUtils_PyGILState_EnsureMC,
            (unsigned long) FunctionsAddress::PythonUtils_PyImport_AddModuleMC,
            (unsigned long) FunctionsAddress::PythonUtils_PyModule_GetDictMC,
            (unsigned long) FunctionsAddress::PythonUtils_PyRun_StringFlagsMC,
            (unsigned long) FunctionsAddress::PythonUtils_PyGILState_ReleaseMC,
            FunctionsAddress::detectReason());
        return false;
    }
    std::lock_guard<std::mutex> lock(executionMutex());
    constexpr const char* kStatus = "INFINITECZ_PYEXEC_STATUS";
    setenv(kStatus, "0", 1);
    setenv("INFINITECZ_PYEXEC_ERR", "", 1);
    setExecLocked();
    PY_TRACE("py[exec]: gil enter");
    PyGILState_STATE1 State = PyGILState_EnsureMC();
    PY_TRACE("py[exec]: gil ok state=%d", (int)State);

    PyObject1* Module = PyImport_AddModuleMC("__main__");
    PY_TRACE("py[exec]: addmodule=%p", (void*)Module);
    if (!Module) {
        reportPythonError();
        PyGILState_ReleaseMC(State);
        return false;
    }
    PyObject1* Dict = PyModule_GetDictMC(Module);
    PY_TRACE("py[exec]: dict=%p", (void*)Dict);
    if (!Dict) {
        reportPythonError();
        PyGILState_ReleaseMC(State);
        return false;
    }
    std::string bootstrap =
        "def __infinitecz_checked_exec():\n"
        "  import os, sys, traceback\n"
        "  __ok = '0'\n"
        "  try:\n"
        "    exec compile(" + pythonBytesLiteral(str) +
            ", '<infinitecz-exec>', 'exec') in globals(), globals()\n"
        "    __ok = '1'\n"
        "  except BaseException:\n"
        "    try:\n"
        "      traceback.print_exc()\n"
        "    except BaseException:\n"
        "      pass\n"
        "    try:\n"
        "      os.environ['INFINITECZ_PYEXEC_ERR'] = traceback.format_exc()[:1500]\n"
        "    except BaseException:\n"
        "      pass\n"
        "  finally:\n"
        "    try:\n"
        "      sys.exc_clear()\n"
        "    except BaseException:\n"
        "      pass\n"
        "    try:\n"
        "      os.environ['INFINITECZ_PYEXEC_STATUS'] = __ok\n"
        "    except BaseException:\n"
        "      pass\n"
        "__infinitecz_checked_exec()\n"
        "try:\n"
        "  del __infinitecz_checked_exec\n"
        "except BaseException:\n"
        "  pass\n";
    PY_TRACE("py[exec]: run source (%zu bytes)", bootstrap.size());
    PyObject1* result = PyRun_StringFlagsMC(
            bootstrap.c_str(), Py_file_input1, Dict, Dict, 0);
    PY_TRACE("py[exec]: result=%p", (void*)result);
    if (!result) {
        reportPythonError();
    }
    releaseObject(result);
    const char* status = std::getenv(kStatus);
    const bool success = result != nullptr && status && status[0] == '1' && status[1] == '\0';
    if (!success) {
        const char* err = std::getenv("INFINITECZ_PYEXEC_ERR");
        __android_log_print(ANDROID_LOG_ERROR, "TexInjectNative", "py exec failed: %s",
                            (err && err[0]) ? err : "(no traceback)");
    }
    PyGILState_ReleaseMC(State);
    return success;
}

static bool PyExecDirectChecked(const std::string& str) {
    FunctionsAddress::detect();
    if (Main::getBaseAddress() == 0 ||
        FunctionsAddress::PythonUtils_PyGILState_EnsureMC == 0 ||
        FunctionsAddress::PythonUtils_PyImport_AddModuleMC == 0 ||
        FunctionsAddress::PythonUtils_PyModule_GetDictMC == 0 ||
        FunctionsAddress::PythonUtils_PyRun_StringFlagsMC == 0 ||
        FunctionsAddress::PythonUtils_PyGILState_ReleaseMC == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(executionMutex());
    setExecLocked();
    const PyGILState_STATE1 state = PyGILState_EnsureMC();
    PyObject1* module = PyImport_AddModuleMC("__main__");
    PyObject1* dict = module ? PyModule_GetDictMC(module) : nullptr;
    if (!dict) {
        reportPythonError();
        PyGILState_ReleaseMC(state);
        return false;
    }
    PyObject1* result = PyRun_StringFlagsMC(
        str.c_str(), Py_file_input1, dict, dict, nullptr);
    if (!result) reportPythonError();
    releaseObject(result);
    PyGILState_ReleaseMC(state);
    return result != nullptr;
}

static void setExecLocked(bool isLock = false) {
    if (FunctionsAddress::PythonUtils_setExecLocked == 0) return;
    const uintptr_t base_address = Main::getBaseAddress();
    const uintptr_t boolPtr = base_address + FunctionsAddress::PythonUtils_setExecLocked;
    bool* isExecLocked = (bool*)boolPtr;
    *isExecLocked = isLock;
}

static PyGILState_STATE1 PyGILState_EnsureMC() {
    const uintptr_t base_address = Main::getBaseAddress();
    return FunctionUtils::callFunc<PyGILState_STATE1>(base_address + FunctionsAddress::PythonUtils_PyGILState_EnsureMC);
}

static PyObject1* PyImport_AddModuleMC(const char* str) {
    const uintptr_t base_address = Main::getBaseAddress();
    return FunctionUtils::callFunc<PyObject1*, const char*>(base_address + FunctionsAddress::PythonUtils_PyImport_AddModuleMC, str);
}

static PyObject1* PyModule_GetDictMC(PyObject1* obj) {
    const uintptr_t base_address = Main::getBaseAddress();
    return FunctionUtils::callFunc<PyObject1*, PyObject1*>(base_address + FunctionsAddress::PythonUtils_PyModule_GetDictMC, obj);
}

static PyObject1* PyRun_StringFlagsMC(const char* str, int start, PyObject1* globals, PyObject1* locals, PyCompilerFlags1* flags) {
    const uintptr_t base_address = Main::getBaseAddress();
    return FunctionUtils::callFunc<PyObject1*, const char*, int, PyObject1*, PyObject1*, PyCompilerFlags1*>(base_address + FunctionsAddress::PythonUtils_PyRun_StringFlagsMC, str, start, globals, locals, flags);
}

static void PyGILState_ReleaseMC(PyGILState_STATE1 oldstate) {
    const uintptr_t base_address = Main::getBaseAddress();
    return FunctionUtils::callFunc<void, PyGILState_STATE1>(base_address + FunctionsAddress::PythonUtils_PyGILState_ReleaseMC, oldstate);
}

private:
static std::mutex& executionMutex() {
    static std::mutex mutex;
    return mutex;
}

static std::string pythonBytesLiteral(const std::string& value) {
    static const char hex[] = "0123456789abcdef";
    std::string literal;
    literal.reserve(value.size() * 2 + 2);
    literal.push_back('\'');
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': literal += "\\\\"; break;
            case '\'': literal += "\\\'"; break;
            case '\n': literal += "\\n"; break;
            case '\r': literal += "\\r"; break;
            case '\t': literal += "\\t"; break;
            default:
                if (ch >= 0x20 && ch <= 0x7e) {
                    literal.push_back(static_cast<char>(ch));
                } else {
                    literal += "\\x";
                    literal.push_back(hex[(ch >> 4) & 0x0f]);
                    literal.push_back(hex[ch & 0x0f]);
                }
        }
    }
    literal.push_back('\'');
    return literal;
}

static int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool decodeHex(const char* encoded, std::string* output) {
    if (!encoded || !output) return false;
    const std::string value(encoded);
    if ((value.size() & 1U) != 0) return false;
    std::string decoded;
    decoded.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int high = hexValue(value[i]);
        const int low = hexValue(value[i + 1]);
        if (high < 0 || low < 0) return false;
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    *output = std::move(decoded);
    return true;
}

static void releaseObject(PyObject1* object) {
    if (!object) return;
    using PyDecRef = void (*)(PyObject1*);
    static PyDecRef decref = reinterpret_cast<PyDecRef>(dlsym(RTLD_DEFAULT, "Py_DecRef"));
    if (decref) decref(object);
}

static void reportPythonError() {
    using PyErrPrint = void (*)();
    using PyErrClear = void (*)();
    static PyErrPrint print_error =
        reinterpret_cast<PyErrPrint>(dlsym(RTLD_DEFAULT, "PyErr_Print"));
    static PyErrClear clear_error =
        reinterpret_cast<PyErrClear>(dlsym(RTLD_DEFAULT, "PyErr_Clear"));
    if (print_error) print_error();
    else if (clear_error) clear_error();
}

};

#endif
