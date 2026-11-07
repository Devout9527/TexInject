#include "Python.h"

#include "Python-ast.h"
#include "node.h"
#include "pyarena.h"
#include "ast.h"
#include "code.h"
#include "compile.h"
#include "symtable.h"
#include "opcode.h"

#define GETARG(arr, i) ((int)((arr[i+2]<<8) + arr[i+1]))
#define UNCONDITIONAL_JUMP(op)  (op==JUMP_ABSOLUTE || op==JUMP_FORWARD)
#define CONDITIONAL_JUMP(op) (op==POP_JUMP_IF_FALSE || op==POP_JUMP_IF_TRUE \
    || op==JUMP_IF_FALSE_OR_POP || op==JUMP_IF_TRUE_OR_POP)
#define ABSOLUTE_JUMP(op) (op==JUMP_ABSOLUTE || op==CONTINUE_LOOP \
    || op==POP_JUMP_IF_FALSE || op==POP_JUMP_IF_TRUE \
    || op==JUMP_IF_FALSE_OR_POP || op==JUMP_IF_TRUE_OR_POP)
#define JUMPS_ON_TRUE(op) (op==POP_JUMP_IF_TRUE || op==JUMP_IF_TRUE_OR_POP)
#define GETJUMPTGT(arr, i) (GETARG(arr,i) + (ABSOLUTE_JUMP(arr[i]) ? 0 : i+3))
#define SETARG(arr, i, val) arr[i+2] = val>>8; arr[i+1] = val & 255
#define CODESIZE(op)  (HAS_ARG(op) ? 3 : 1)
#define ISBASICBLOCK(blocks, start, bytes) \
    (blocks[start]==blocks[start+bytes-1])

static int
tuple_of_constants(unsigned char *codestr, Py_ssize_t n, PyObject *consts)
{
    PyObject *newconst, *constant;
    Py_ssize_t i, arg, len_consts;

    assert(PyList_CheckExact(consts));
    assert(codestr[n*3] == BUILD_TUPLE || codestr[n*3] == BUILD_LIST);
    assert(GETARG(codestr, (n*3)) == n);
    for (i=0 ; i<n ; i++)
        assert(codestr[i*3] == LOAD_CONST);

    newconst = PyTuple_New(n);
    if (newconst == NULL)
        return 0;
    len_consts = PyList_GET_SIZE(consts);
    for (i=0 ; i<n ; i++) {
        arg = GETARG(codestr, (i*3));
        assert(arg < len_consts);
        constant = PyList_GET_ITEM(consts, arg);
        Py_INCREF(constant);
        PyTuple_SET_ITEM(newconst, i, constant);
    }

    if (PyList_Append(consts, newconst)) {
        Py_DECREF(newconst);
        return 0;
    }
    Py_DECREF(newconst);

    memset(codestr, NOP, n*3);
    codestr[n*3] = LOAD_CONST;
    SETARG(codestr, (n*3), len_consts);
    return 1;
}

static int
fold_binops_on_constants(unsigned char *codestr, PyObject *consts)
{
    PyObject *newconst, *v, *w;
    Py_ssize_t len_consts, size;
    int opcode;

    assert(PyList_CheckExact(consts));
    assert(codestr[0] == LOAD_CONST);
    assert(codestr[3] == LOAD_CONST);

    v = PyList_GET_ITEM(consts, GETARG(codestr, 0));
    w = PyList_GET_ITEM(consts, GETARG(codestr, 3));
    opcode = codestr[6];
    switch (opcode) {
        case BINARY_POWER:
            newconst = PyNumber_Power(v, w, Py_None);
            break;
        case BINARY_MULTIPLY:
            newconst = PyNumber_Multiply(v, w);
            break;
        case BINARY_DIVIDE:

            return 0;
        case BINARY_TRUE_DIVIDE:
            newconst = PyNumber_TrueDivide(v, w);
            break;
        case BINARY_FLOOR_DIVIDE:
            newconst = PyNumber_FloorDivide(v, w);
            break;
        case BINARY_MODULO:
            newconst = PyNumber_Remainder(v, w);
            break;
        case BINARY_ADD:
            newconst = PyNumber_Add(v, w);
            break;
        case BINARY_SUBTRACT:
            newconst = PyNumber_Subtract(v, w);
            break;
        case BINARY_SUBSCR:

            if (PyUnicode_Check(v))
                return 0;
            newconst = PyObject_GetItem(v, w);
            break;
        case BINARY_LSHIFT:
            newconst = PyNumber_Lshift(v, w);
            break;
        case BINARY_RSHIFT:
            newconst = PyNumber_Rshift(v, w);
            break;
        case BINARY_AND:
            newconst = PyNumber_And(v, w);
            break;
        case BINARY_XOR:
            newconst = PyNumber_Xor(v, w);
            break;
        case BINARY_OR:
            newconst = PyNumber_Or(v, w);
            break;
        default:

            PyErr_Format(PyExc_SystemError,
                 "unexpected binary operation %d on a constant",
                     opcode);
            return 0;
    }
    if (newconst == NULL) {
        PyErr_Clear();
        return 0;
    }
    size = PyObject_Size(newconst);
    if (size == -1)
        PyErr_Clear();
    else if (size > 20) {
        Py_DECREF(newconst);
        return 0;
    }

    len_consts = PyList_GET_SIZE(consts);
    if (PyList_Append(consts, newconst)) {
        Py_DECREF(newconst);
        return 0;
    }
    Py_DECREF(newconst);

    memset(codestr, NOP, 4);
    codestr[4] = LOAD_CONST;
    SETARG(codestr, 4, len_consts);
    return 1;
}

static int
fold_unaryops_on_constants(unsigned char *codestr, PyObject *consts)
{
    PyObject *newconst=NULL, *v;
    Py_ssize_t len_consts;
    int opcode;

    assert(PyList_CheckExact(consts));
    assert(codestr[0] == LOAD_CONST);

    v = PyList_GET_ITEM(consts, GETARG(codestr, 0));
    opcode = codestr[3];
    switch (opcode) {
        case UNARY_NEGATIVE:

            if (PyObject_IsTrue(v) == 1)
                newconst = PyNumber_Negative(v);
            break;
        case UNARY_CONVERT:
            newconst = PyObject_Repr(v);
            break;
        case UNARY_INVERT:
            newconst = PyNumber_Invert(v);
            break;
        default:

            PyErr_Format(PyExc_SystemError,
                 "unexpected unary operation %d on a constant",
                     opcode);
            return 0;
    }
    if (newconst == NULL) {
        PyErr_Clear();
        return 0;
    }

    len_consts = PyList_GET_SIZE(consts);
    if (PyList_Append(consts, newconst)) {
        Py_DECREF(newconst);
        return 0;
    }
    Py_DECREF(newconst);

    codestr[0] = NOP;
    codestr[1] = LOAD_CONST;
    SETARG(codestr, 1, len_consts);
    return 1;
}

static unsigned int *
markblocks(unsigned char *code, Py_ssize_t len)
{
    unsigned int *blocks = PyMem_New(unsigned int, len);
    int i,j, opcode, blockcnt = 0;

    if (blocks == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    memset(blocks, 0, len*sizeof(int));

    for (i=0 ; i<len ; i+=CODESIZE(opcode)) {
        opcode = code[i];
        switch (opcode) {
            case FOR_ITER:
            case JUMP_FORWARD:
            case JUMP_IF_FALSE_OR_POP:
            case JUMP_IF_TRUE_OR_POP:
            case POP_JUMP_IF_FALSE:
            case POP_JUMP_IF_TRUE:
            case JUMP_ABSOLUTE:
            case CONTINUE_LOOP:
            case SETUP_LOOP:
            case SETUP_EXCEPT:
            case SETUP_FINALLY:
            case SETUP_WITH:
                j = GETJUMPTGT(code, i);
                blocks[j] = 1;
                break;
        }
    }

    for (i=0 ; i<len ; i++) {
        blockcnt += blocks[i];
        blocks[i] = blockcnt;
    }
    return blocks;
}

PyObject *
PyCode_Optimize(PyObject *code, PyObject* consts, PyObject *names,
                PyObject *lineno_obj)
{
    Py_ssize_t i, j, codelen;
    int nops, h, adj;
    int tgt, tgttgt, opcode;
    unsigned char *codestr = NULL;
    unsigned char *lineno;
    int *addrmap = NULL;
    int new_line, cum_orig_line, last_line, tabsiz;
    int cumlc=0, lastlc=0;
    unsigned int *blocks = NULL;
    char *name;

    if (PyErr_Occurred())
        goto exitError;

    assert(PyString_Check(lineno_obj));
    lineno = (unsigned char*)PyString_AS_STRING(lineno_obj);
    tabsiz = PyString_GET_SIZE(lineno_obj);
    if (memchr(lineno, 255, tabsiz) != NULL)
        goto exitUnchanged;

    assert(PyString_Check(code));
    codelen = PyString_GET_SIZE(code);
    if (codelen > 32700)
        goto exitUnchanged;

    codestr = (unsigned char *)PyMem_Malloc(codelen);
    if (codestr == NULL)
        goto exitError;
    codestr = (unsigned char *)memcpy(codestr,
                                      PyString_AS_STRING(code), codelen);

    if (codestr[codelen-1] != RETURN_VALUE)
        goto exitUnchanged;

    addrmap = PyMem_New(int, codelen);
    if (addrmap == NULL) {
        PyErr_NoMemory();
        goto exitError;
    }

    blocks = markblocks(codestr, codelen);
    if (blocks == NULL)
        goto exitError;
    assert(PyList_Check(consts));

    for (i=0 ; i<codelen ; i += CODESIZE(codestr[i])) {
      reoptimize_current:
        opcode = codestr[i];

        lastlc = cumlc;
        cumlc = 0;

        switch (opcode) {

            case UNARY_NOT:
                if (codestr[i+1] != POP_JUMP_IF_FALSE
                    || !ISBASICBLOCK(blocks,i,4))
                    continue;
                j = GETARG(codestr, i+1);
                codestr[i] = POP_JUMP_IF_TRUE;
                SETARG(codestr, i, j);
                codestr[i+3] = NOP;
                goto reoptimize_current;

            case COMPARE_OP:
                j = GETARG(codestr, i);
                if (j < 6  ||  j > 9  ||
                    codestr[i+3] != UNARY_NOT  ||
                    !ISBASICBLOCK(blocks,i,4))
                    continue;
                SETARG(codestr, i, (j^1));
                codestr[i+3] = NOP;
                break;

            case LOAD_NAME:
            case LOAD_GLOBAL:
                j = GETARG(codestr, i);
                name = PyString_AsString(PyTuple_GET_ITEM(names, j));
                if (name == NULL  ||  strcmp(name, "None") != 0)
                    continue;
                for (j=0 ; j < PyList_GET_SIZE(consts) ; j++) {
                    if (PyList_GET_ITEM(consts, j) == Py_None)
                        break;
                }
                if (j == PyList_GET_SIZE(consts)) {
                    if (PyList_Append(consts, Py_None) == -1)
                        goto exitError;
                }
                assert(PyList_GET_ITEM(consts, j) == Py_None);
                codestr[i] = LOAD_CONST;
                SETARG(codestr, i, j);
                cumlc = lastlc + 1;
                break;

            case LOAD_CONST:
                cumlc = lastlc + 1;
                j = GETARG(codestr, i);
                if (codestr[i+3] != POP_JUMP_IF_FALSE  ||
                    !ISBASICBLOCK(blocks,i,6)  ||
                    !PyObject_IsTrue(PyList_GET_ITEM(consts, j)))
                    continue;
                memset(codestr+i, NOP, 6);
                cumlc = 0;
                break;

            case BUILD_TUPLE:
            case BUILD_LIST:
                j = GETARG(codestr, i);
                h = i - 3 * j;
                if (h >= 0 &&
                    j <= lastlc &&
                    ((opcode == BUILD_TUPLE &&
                      ISBASICBLOCK(blocks, h, 3*(j+1))) ||
                     (opcode == BUILD_LIST &&
                      codestr[i+3]==COMPARE_OP &&
                      ISBASICBLOCK(blocks, h, 3*(j+2)) &&
                      (GETARG(codestr,i+3)==6 ||
                       GETARG(codestr,i+3)==7))) &&
                    tuple_of_constants(&codestr[h], j, consts)) {
                    assert(codestr[i] == LOAD_CONST);
                    cumlc = 1;
                    break;
                }
                if (codestr[i+3] != UNPACK_SEQUENCE  ||
                    !ISBASICBLOCK(blocks,i,6) ||
                    j != GETARG(codestr, i+3))
                    continue;
                if (j == 1) {
                    memset(codestr+i, NOP, 6);
                } else if (j == 2) {
                    codestr[i] = ROT_TWO;
                    memset(codestr+i+1, NOP, 5);
                } else if (j == 3) {
                    codestr[i] = ROT_THREE;
                    codestr[i+1] = ROT_TWO;
                    memset(codestr+i+2, NOP, 4);
                }
                break;

            case BINARY_POWER:
            case BINARY_MULTIPLY:
            case BINARY_TRUE_DIVIDE:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_MODULO:
            case BINARY_ADD:
            case BINARY_SUBTRACT:
            case BINARY_SUBSCR:
            case BINARY_LSHIFT:
            case BINARY_RSHIFT:
            case BINARY_AND:
            case BINARY_XOR:
            case BINARY_OR:
                if (lastlc >= 2 &&
                    ISBASICBLOCK(blocks, i-6, 7) &&
                    fold_binops_on_constants(&codestr[i-6], consts)) {
                    i -= 2;
                    assert(codestr[i] == LOAD_CONST);
                    cumlc = 1;
                }
                break;

            case UNARY_NEGATIVE:
            case UNARY_CONVERT:
            case UNARY_INVERT:
                if (lastlc >= 1 &&
                    ISBASICBLOCK(blocks, i-3, 4) &&
                    fold_unaryops_on_constants(&codestr[i-3], consts)) {
                    i -= 2;
                    assert(codestr[i] == LOAD_CONST);
                    cumlc = 1;
                }
                break;

            case JUMP_IF_FALSE_OR_POP:
            case JUMP_IF_TRUE_OR_POP:
                tgt = GETJUMPTGT(codestr, i);
                j = codestr[tgt];
                if (CONDITIONAL_JUMP(j)) {

                    if (JUMPS_ON_TRUE(j) == JUMPS_ON_TRUE(opcode)) {

                        tgttgt = GETJUMPTGT(codestr, tgt);

                        codestr[i] = j;
                        SETARG(codestr, i, tgttgt);
                        goto reoptimize_current;
                    } else {

                        if (JUMPS_ON_TRUE(opcode))
                            codestr[i] = POP_JUMP_IF_TRUE;
                        else
                            codestr[i] = POP_JUMP_IF_FALSE;
                        SETARG(codestr, i, (tgt + 3));
                        goto reoptimize_current;
                    }
                }

            case POP_JUMP_IF_FALSE:
            case POP_JUMP_IF_TRUE:
            case FOR_ITER:
            case JUMP_FORWARD:
            case JUMP_ABSOLUTE:
            case CONTINUE_LOOP:
            case SETUP_LOOP:
            case SETUP_EXCEPT:
            case SETUP_FINALLY:
            case SETUP_WITH:
                tgt = GETJUMPTGT(codestr, i);

                if (UNCONDITIONAL_JUMP(opcode) &&
                    codestr[tgt] == RETURN_VALUE) {
                    codestr[i] = RETURN_VALUE;
                    memset(codestr+i+1, NOP, 2);
                    continue;
                }
                if (!UNCONDITIONAL_JUMP(codestr[tgt]))
                    continue;
                tgttgt = GETJUMPTGT(codestr, tgt);
                if (opcode == JUMP_FORWARD)
                    opcode = JUMP_ABSOLUTE;
                if (!ABSOLUTE_JUMP(opcode))
                    tgttgt -= i + 3;
                if (tgttgt < 0)
                    continue;
                codestr[i] = opcode;
                SETARG(codestr, i, tgttgt);
                break;

            case EXTENDED_ARG:
                goto exitUnchanged;

            case RETURN_VALUE:
                if (i+4 >= codelen)
                    continue;
                if (codestr[i+4] == RETURN_VALUE &&
                    ISBASICBLOCK(blocks,i,5))
                    memset(codestr+i+1, NOP, 4);
                else if (UNCONDITIONAL_JUMP(codestr[i+1]) &&
                         ISBASICBLOCK(blocks,i,4))
                    memset(codestr+i+1, NOP, 3);
                break;
        }
    }

    for (i=0, nops=0 ; i<codelen ; i += CODESIZE(codestr[i])) {
        addrmap[i] = i - nops;
        if (codestr[i] == NOP)
            nops++;
    }
    cum_orig_line = 0;
    last_line = 0;
    for (i=0 ; i < tabsiz ; i+=2) {
        cum_orig_line += lineno[i];
        new_line = addrmap[cum_orig_line];
        assert (new_line - last_line < 255);
        lineno[i] =((unsigned char)(new_line - last_line));
        last_line = new_line;
    }

    for (i=0, h=0 ; i<codelen ; ) {
        opcode = codestr[i];
        switch (opcode) {
            case NOP:
                i++;
                continue;

            case JUMP_ABSOLUTE:
            case CONTINUE_LOOP:
            case POP_JUMP_IF_FALSE:
            case POP_JUMP_IF_TRUE:
            case JUMP_IF_FALSE_OR_POP:
            case JUMP_IF_TRUE_OR_POP:
                j = addrmap[GETARG(codestr, i)];
                SETARG(codestr, i, j);
                break;

            case FOR_ITER:
            case JUMP_FORWARD:
            case SETUP_LOOP:
            case SETUP_EXCEPT:
            case SETUP_FINALLY:
            case SETUP_WITH:
                j = addrmap[GETARG(codestr, i) + i + 3] - addrmap[i] - 3;
                SETARG(codestr, i, j);
                break;
        }
        adj = CODESIZE(opcode);
        while (adj--)
            codestr[h++] = codestr[i++];
    }
    assert(h + nops == codelen);

    code = PyString_FromStringAndSize((char *)codestr, h);
    PyMem_Free(addrmap);
    PyMem_Free(codestr);
    PyMem_Free(blocks);
    return code;

 exitError:
    code = NULL;

 exitUnchanged:
    if (blocks != NULL)
        PyMem_Free(blocks);
    if (addrmap != NULL)
        PyMem_Free(addrmap);
    if (codestr != NULL)
        PyMem_Free(codestr);
    Py_XINCREF(code);
    return code;
}
