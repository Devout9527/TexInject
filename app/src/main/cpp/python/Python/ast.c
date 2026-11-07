#include "Python.h"
#include "Python-ast.h"
#include "grammar.h"
#include "node.h"
#include "pyarena.h"
#include "ast.h"
#include "token.h"
#include "parsetok.h"
#include "graminit.h"

#include <assert.h>

struct compiling {
    char *c_encoding;
    int c_future_unicode;
    PyArena *c_arena;
    const char *c_filename;
};

static asdl_seq *seq_for_testlist(struct compiling *, const node *);
static expr_ty ast_for_expr(struct compiling *, const node *);
static stmt_ty ast_for_stmt(struct compiling *, const node *);
static asdl_seq *ast_for_suite(struct compiling *, const node *);
static asdl_seq *ast_for_exprlist(struct compiling *, const node *,
                                  expr_context_ty);
static expr_ty ast_for_testlist(struct compiling *, const node *);
static stmt_ty ast_for_classdef(struct compiling *, const node *, asdl_seq *);
static expr_ty ast_for_testlist_comp(struct compiling *, const node *);

static expr_ty ast_for_call(struct compiling *, const node *, expr_ty);

static PyObject *parsenumber(struct compiling *, const char *);
static PyObject *parsestr(struct compiling *, const node *n, const char *);
static PyObject *parsestrplus(struct compiling *, const node *n);

#ifndef LINENO
#define LINENO(n)       ((n)->n_lineno)
#endif

#define COMP_GENEXP 0
#define COMP_SETCOMP  1

static identifier
new_identifier(const char* n, PyArena *arena) {
    PyObject* id = PyString_InternFromString(n);
    if (id != NULL)
        PyArena_AddPyObject(arena, id);
    return id;
}

#define NEW_IDENTIFIER(n) new_identifier(STR(n), c->c_arena)

static int
ast_error(const node *n, const char *errstr)
{
    PyObject *u = Py_BuildValue("zi", errstr, LINENO(n));
    if (!u)
        return 0;
    PyErr_SetObject(PyExc_SyntaxError, u);
    Py_DECREF(u);
    return 0;
}

static void
ast_error_finish(const char *filename)
{
    PyObject *type, *value, *tback, *errstr, *loc, *tmp;
    long lineno;

    assert(PyErr_Occurred());
    if (!PyErr_ExceptionMatches(PyExc_SyntaxError))
        return;

    PyErr_Fetch(&type, &value, &tback);
    errstr = PyTuple_GetItem(value, 0);
    if (!errstr)
        return;
    Py_INCREF(errstr);
    lineno = PyInt_AsLong(PyTuple_GetItem(value, 1));
    if (lineno == -1) {
        Py_DECREF(errstr);
        return;
    }
    Py_DECREF(value);

    loc = PyErr_ProgramText(filename, lineno);
    if (!loc) {
        Py_INCREF(Py_None);
        loc = Py_None;
    }
    tmp = Py_BuildValue("(zlOO)", filename, lineno, Py_None, loc);
    Py_DECREF(loc);
    if (!tmp) {
        Py_DECREF(errstr);
        return;
    }
    value = PyTuple_Pack(2, errstr, tmp);
    Py_DECREF(errstr);
    Py_DECREF(tmp);
    if (!value)
        return;
    PyErr_Restore(type, value, tback);
}

static int
ast_warn(struct compiling *c, const node *n, char *msg)
{
    if (PyErr_WarnExplicit(PyExc_SyntaxWarning, msg, c->c_filename, LINENO(n),
                           NULL, NULL) < 0) {

        if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_SyntaxWarning))
            ast_error(n, msg);
        return 0;
    }
    return 1;
}

static int
forbidden_check(struct compiling *c, const node *n, const char *x)
{
    if (!strcmp(x, "None"))
        return ast_error(n, "cannot assign to None");
    if (!strcmp(x, "__debug__"))
        return ast_error(n, "cannot assign to __debug__");
    if (Py_Py3kWarningFlag) {
        if (!(strcmp(x, "True") && strcmp(x, "False")) &&
            !ast_warn(c, n, "assignment to True or False is forbidden in 3.x"))
            return 0;
        if (!strcmp(x, "nonlocal") &&
            !ast_warn(c, n, "nonlocal is a keyword in 3.x"))
            return 0;
    }
    return 1;
}

static int
num_stmts(const node *n)
{
    int i, l;
    node *ch;

    switch (TYPE(n)) {
        case single_input:
            if (TYPE(CHILD(n, 0)) == NEWLINE)
                return 0;
            else
                return num_stmts(CHILD(n, 0));
        case file_input:
            l = 0;
            for (i = 0; i < NCH(n); i++) {
                ch = CHILD(n, i);
                if (TYPE(ch) == stmt)
                    l += num_stmts(ch);
            }
            return l;
        case stmt:
            return num_stmts(CHILD(n, 0));
        case compound_stmt:
            return 1;
        case simple_stmt:
            return NCH(n) / 2;
        case suite:
            if (NCH(n) == 1)
                return num_stmts(CHILD(n, 0));
            else {
                l = 0;
                for (i = 2; i < (NCH(n) - 1); i++)
                    l += num_stmts(CHILD(n, i));
                return l;
            }
        default: {
            char buf[128];

            sprintf(buf, "Non-statement found: %d %d",
                    TYPE(n), NCH(n));
            Py_FatalError(buf);
        }
    }
    assert(0);
    return 0;
}

mod_ty
PyAST_FromNode(const node *n, PyCompilerFlags *flags, const char *filename,
               PyArena *arena)
{
    int i, j, k, num;
    asdl_seq *stmts = NULL;
    stmt_ty s;
    node *ch;
    struct compiling c;

    if (flags && flags->cf_flags & PyCF_SOURCE_IS_UTF8) {
        c.c_encoding = "utf-8";
        if (TYPE(n) == encoding_decl) {
            ast_error(n, "encoding declaration in Unicode string");
            goto error;
        }
    } else if (TYPE(n) == encoding_decl) {
        c.c_encoding = STR(n);
        n = CHILD(n, 0);
    } else {
        c.c_encoding = NULL;
    }
    c.c_future_unicode = flags && flags->cf_flags & CO_FUTURE_UNICODE_LITERALS;
    c.c_arena = arena;
    c.c_filename = filename;

    k = 0;
    switch (TYPE(n)) {
        case file_input:
            stmts = asdl_seq_new(num_stmts(n), arena);
            if (!stmts)
                return NULL;
            for (i = 0; i < NCH(n) - 1; i++) {
                ch = CHILD(n, i);
                if (TYPE(ch) == NEWLINE)
                    continue;
                REQ(ch, stmt);
                num = num_stmts(ch);
                if (num == 1) {
                    s = ast_for_stmt(&c, ch);
                    if (!s)
                        goto error;
                    asdl_seq_SET(stmts, k++, s);
                }
                else {
                    ch = CHILD(ch, 0);
                    REQ(ch, simple_stmt);
                    for (j = 0; j < num; j++) {
                        s = ast_for_stmt(&c, CHILD(ch, j * 2));
                        if (!s)
                            goto error;
                        asdl_seq_SET(stmts, k++, s);
                    }
                }
            }
            return Module(stmts, arena);
        case eval_input: {
            expr_ty testlist_ast;

            testlist_ast = ast_for_testlist(&c, CHILD(n, 0));
            if (!testlist_ast)
                goto error;
            return Expression(testlist_ast, arena);
        }
        case single_input:
            if (TYPE(CHILD(n, 0)) == NEWLINE) {
                stmts = asdl_seq_new(1, arena);
                if (!stmts)
                    goto error;
                asdl_seq_SET(stmts, 0, Pass(n->n_lineno, n->n_col_offset,
                                            arena));
                if (!asdl_seq_GET(stmts, 0))
                    goto error;
                return Interactive(stmts, arena);
            }
            else {
                n = CHILD(n, 0);
                num = num_stmts(n);
                stmts = asdl_seq_new(num, arena);
                if (!stmts)
                    goto error;
                if (num == 1) {
                    s = ast_for_stmt(&c, n);
                    if (!s)
                        goto error;
                    asdl_seq_SET(stmts, 0, s);
                }
                else {

                    REQ(n, simple_stmt);
                    for (i = 0; i < NCH(n); i += 2) {
                        if (TYPE(CHILD(n, i)) == NEWLINE)
                            break;
                        s = ast_for_stmt(&c, CHILD(n, i));
                        if (!s)
                            goto error;
                        asdl_seq_SET(stmts, i / 2, s);
                    }
                }

                return Interactive(stmts, arena);
            }
        default:
            PyErr_Format(PyExc_SystemError,
                         "invalid node %d for PyAST_FromNode", TYPE(n));
            goto error;
    }
 error:
    ast_error_finish(filename);
    return NULL;
}

static operator_ty
get_operator(const node *n)
{
    switch (TYPE(n)) {
        case VBAR:
            return BitOr;
        case CIRCUMFLEX:
            return BitXor;
        case AMPER:
            return BitAnd;
        case LEFTSHIFT:
            return LShift;
        case RIGHTSHIFT:
            return RShift;
        case PLUS:
            return Add;
        case MINUS:
            return Sub;
        case STAR:
            return Mult;
        case SLASH:
            return Div;
        case DOUBLESLASH:
            return FloorDiv;
        case PERCENT:
            return Mod;
        default:
            return (operator_ty)0;
    }
}

static int
set_context(struct compiling *c, expr_ty e, expr_context_ty ctx, const node *n)
{
    asdl_seq *s = NULL;

    const char* expr_name = NULL;

    assert(ctx != AugStore && ctx != AugLoad);

    switch (e->kind) {
        case Attribute_kind:
            if (ctx == Store && !forbidden_check(c, n,
                                PyBytes_AS_STRING(e->v.Attribute.attr)))
                    return 0;
            e->v.Attribute.ctx = ctx;
            break;
        case Subscript_kind:
            e->v.Subscript.ctx = ctx;
            break;
        case Name_kind:
            if (ctx == Store && !forbidden_check(c, n,
                                PyBytes_AS_STRING(e->v.Name.id)))
                    return 0;
            e->v.Name.ctx = ctx;
            break;
        case List_kind:
            e->v.List.ctx = ctx;
            s = e->v.List.elts;
            break;
        case Tuple_kind:
            if (asdl_seq_LEN(e->v.Tuple.elts))  {
                e->v.Tuple.ctx = ctx;
                s = e->v.Tuple.elts;
            }
            else {
                expr_name = "()";
            }
            break;
        case Lambda_kind:
            expr_name = "lambda";
            break;
        case Call_kind:
            expr_name = "function call";
            break;
        case BoolOp_kind:
        case BinOp_kind:
        case UnaryOp_kind:
            expr_name = "operator";
            break;
        case GeneratorExp_kind:
            expr_name = "generator expression";
            break;
        case Yield_kind:
            expr_name = "yield expression";
            break;
        case ListComp_kind:
            expr_name = "list comprehension";
            break;
        case SetComp_kind:
            expr_name = "set comprehension";
            break;
        case DictComp_kind:
            expr_name = "dict comprehension";
            break;
        case Dict_kind:
        case Set_kind:
        case Num_kind:
        case Str_kind:
            expr_name = "literal";
            break;
        case Compare_kind:
            expr_name = "comparison";
            break;
        case Repr_kind:
            expr_name = "repr";
            break;
        case IfExp_kind:
            expr_name = "conditional expression";
            break;
        default:
            PyErr_Format(PyExc_SystemError,
                         "unexpected expression in assignment %d (line %d)",
                         e->kind, e->lineno);
            return 0;
    }

    if (expr_name) {
        char buf[300];
        PyOS_snprintf(buf, sizeof(buf),
                      "can't %s %s",
                      ctx == Store ? "assign to" : "delete",
                      expr_name);
        return ast_error(n, buf);
    }

    if (s) {
        int i;

        for (i = 0; i < asdl_seq_LEN(s); i++) {
            if (!set_context(c, (expr_ty)asdl_seq_GET(s, i), ctx, n))
                return 0;
        }
    }
    return 1;
}

static operator_ty
ast_for_augassign(struct compiling *c, const node *n)
{
    REQ(n, augassign);
    n = CHILD(n, 0);
    switch (STR(n)[0]) {
        case '+':
            return Add;
        case '-':
            return Sub;
        case '/':
            if (STR(n)[1] == '/')
                return FloorDiv;
            else
                return Div;
        case '%':
            return Mod;
        case '<':
            return LShift;
        case '>':
            return RShift;
        case '&':
            return BitAnd;
        case '^':
            return BitXor;
        case '|':
            return BitOr;
        case '*':
            if (STR(n)[1] == '*')
                return Pow;
            else
                return Mult;
        default:
            PyErr_Format(PyExc_SystemError, "invalid augassign: %s", STR(n));
            return (operator_ty)0;
    }
}

static cmpop_ty
ast_for_comp_op(struct compiling *c, const node *n)
{

    REQ(n, comp_op);
    if (NCH(n) == 1) {
        n = CHILD(n, 0);
        switch (TYPE(n)) {
            case LESS:
                return Lt;
            case GREATER:
                return Gt;
            case EQEQUAL:
                return Eq;
            case LESSEQUAL:
                return LtE;
            case GREATEREQUAL:
                return GtE;
            case NOTEQUAL:
                return NotEq;
            case NAME:
                if (strcmp(STR(n), "in") == 0)
                    return In;
                if (strcmp(STR(n), "is") == 0)
                    return Is;
            default:
                PyErr_Format(PyExc_SystemError, "invalid comp_op: %s",
                             STR(n));
                return (cmpop_ty)0;
        }
    }
    else if (NCH(n) == 2) {

        switch (TYPE(CHILD(n, 0))) {
            case NAME:
                if (strcmp(STR(CHILD(n, 1)), "in") == 0)
                    return NotIn;
                if (strcmp(STR(CHILD(n, 0)), "is") == 0)
                    return IsNot;
            default:
                PyErr_Format(PyExc_SystemError, "invalid comp_op: %s %s",
                             STR(CHILD(n, 0)), STR(CHILD(n, 1)));
                return (cmpop_ty)0;
        }
    }
    PyErr_Format(PyExc_SystemError, "invalid comp_op: has %d children",
                 NCH(n));
    return (cmpop_ty)0;
}

static asdl_seq *
seq_for_testlist(struct compiling *c, const node *n)
{

    asdl_seq *seq;
    expr_ty expression;
    int i;
    assert(TYPE(n) == testlist ||
           TYPE(n) == listmaker ||
           TYPE(n) == testlist_comp ||
           TYPE(n) == testlist_safe ||
           TYPE(n) == testlist1);

    seq = asdl_seq_new((NCH(n) + 1) / 2, c->c_arena);
    if (!seq)
        return NULL;

    for (i = 0; i < NCH(n); i += 2) {
        assert(TYPE(CHILD(n, i)) == test || TYPE(CHILD(n, i)) == old_test);

        expression = ast_for_expr(c, CHILD(n, i));
        if (!expression)
            return NULL;

        assert(i / 2 < seq->size);
        asdl_seq_SET(seq, i / 2, expression);
    }
    return seq;
}

static expr_ty
compiler_complex_args(struct compiling *c, const node *n)
{
    int i, len = (NCH(n) + 1) / 2;
    expr_ty result;
    asdl_seq *args = asdl_seq_new(len, c->c_arena);
    if (!args)
        return NULL;

    REQ(n, fplist);
    for (i = 0; i < len; i++) {
        PyObject *arg_id;
        const node *fpdef_node = CHILD(n, 2*i);
        const node *child;
        expr_ty arg;
set_name:

        child = CHILD(fpdef_node, 0);
        if (TYPE(child) == NAME) {
            if (!forbidden_check(c, n, STR(child)))
                return NULL;
            arg_id = NEW_IDENTIFIER(child);
            if (!arg_id)
                return NULL;
            arg = Name(arg_id, Store, LINENO(child), child->n_col_offset,
                       c->c_arena);
        }
        else {
            assert(TYPE(fpdef_node) == fpdef);

            child = CHILD(fpdef_node, 1);
            assert(TYPE(child) == fplist);

            if (NCH(child) == 1) {
                fpdef_node = CHILD(child, 0);
                assert(TYPE(fpdef_node) == fpdef);
                goto set_name;
            }
            arg = compiler_complex_args(c, child);
        }
        asdl_seq_SET(args, i, arg);
    }

    result = Tuple(args, Store, LINENO(n), n->n_col_offset, c->c_arena);
    if (!set_context(c, result, Store, n))
        return NULL;
    return result;
}

static arguments_ty
ast_for_arguments(struct compiling *c, const node *n)
{

    int i, j, k, n_args = 0, n_defaults = 0, found_default = 0;
    asdl_seq *args, *defaults;
    identifier vararg = NULL, kwarg = NULL;
    node *ch;

    if (TYPE(n) == parameters) {
        if (NCH(n) == 2)
            return arguments(NULL, NULL, NULL, NULL, c->c_arena);
        n = CHILD(n, 1);
    }
    REQ(n, varargslist);

    for (i = 0; i < NCH(n); i++) {
        ch = CHILD(n, i);
        if (TYPE(ch) == fpdef)
            n_args++;
        if (TYPE(ch) == EQUAL)
            n_defaults++;
    }
    args = (n_args ? asdl_seq_new(n_args, c->c_arena) : NULL);
    if (!args && n_args)
        return NULL;
    defaults = (n_defaults ? asdl_seq_new(n_defaults, c->c_arena) : NULL);
    if (!defaults && n_defaults)
        return NULL;

    i = 0;
    j = 0;
    k = 0;
    while (i < NCH(n)) {
        ch = CHILD(n, i);
        switch (TYPE(ch)) {
            case fpdef: {
                int complex_args = 0, parenthesized = 0;
            handle_fpdef:

                if (i + 1 < NCH(n) && TYPE(CHILD(n, i + 1)) == EQUAL) {
                    expr_ty expression = ast_for_expr(c, CHILD(n, i + 2));
                    if (!expression)
                        return NULL;
                    assert(defaults != NULL);
                    asdl_seq_SET(defaults, j++, expression);
                    i += 2;
                    found_default = 1;
                }
                else if (found_default) {

                    if (parenthesized && !complex_args) {
                        ast_error(n, "parenthesized arg with default");
                        return NULL;
                    }
                    ast_error(n,
                             "non-default argument follows default argument");
                    return NULL;
                }
                if (NCH(ch) == 3) {
                    ch = CHILD(ch, 1);

                    if (NCH(ch) != 1) {

                        if (Py_Py3kWarningFlag && !ast_warn(c, ch,
                            "tuple parameter unpacking has been removed in 3.x"))
                            return NULL;
                        complex_args = 1;
                        asdl_seq_SET(args, k++, compiler_complex_args(c, ch));
                        if (!asdl_seq_GET(args, k-1))
                                return NULL;
                    } else {

                        parenthesized = 1;
                        ch = CHILD(ch, 0);
                        assert(TYPE(ch) == fpdef);
                        goto handle_fpdef;
                    }
                }
                if (TYPE(CHILD(ch, 0)) == NAME) {
                    PyObject *id;
                    expr_ty name;
                    if (!forbidden_check(c, n, STR(CHILD(ch, 0))))
                        return NULL;
                    id = NEW_IDENTIFIER(CHILD(ch, 0));
                    if (!id)
                        return NULL;
                    name = Name(id, Param, LINENO(ch), ch->n_col_offset,
                                c->c_arena);
                    if (!name)
                        return NULL;
                    asdl_seq_SET(args, k++, name);

                }
                i += 2;
                if (parenthesized && Py_Py3kWarningFlag &&
                    !ast_warn(c, ch, "parenthesized argument names "
                              "are invalid in 3.x"))
                    return NULL;

                break;
            }
            case STAR:
                if (!forbidden_check(c, CHILD(n, i+1), STR(CHILD(n, i+1))))
                    return NULL;
                vararg = NEW_IDENTIFIER(CHILD(n, i+1));
                if (!vararg)
                    return NULL;
                i += 3;
                break;
            case DOUBLESTAR:
                if (!forbidden_check(c, CHILD(n, i+1), STR(CHILD(n, i+1))))
                    return NULL;
                kwarg = NEW_IDENTIFIER(CHILD(n, i+1));
                if (!kwarg)
                    return NULL;
                i += 3;
                break;
            default:
                PyErr_Format(PyExc_SystemError,
                             "unexpected node in varargslist: %d @ %d",
                             TYPE(ch), i);
                return NULL;
        }
    }

    return arguments(args, vararg, kwarg, defaults, c->c_arena);
}

static expr_ty
ast_for_dotted_name(struct compiling *c, const node *n)
{
    expr_ty e;
    identifier id;
    int lineno, col_offset;
    int i;

    REQ(n, dotted_name);

    lineno = LINENO(n);
    col_offset = n->n_col_offset;

    id = NEW_IDENTIFIER(CHILD(n, 0));
    if (!id)
        return NULL;
    e = Name(id, Load, lineno, col_offset, c->c_arena);
    if (!e)
        return NULL;

    for (i = 2; i < NCH(n); i+=2) {
        id = NEW_IDENTIFIER(CHILD(n, i));
        if (!id)
            return NULL;
        e = Attribute(e, id, Load, lineno, col_offset, c->c_arena);
        if (!e)
            return NULL;
    }

    return e;
}

static expr_ty
ast_for_decorator(struct compiling *c, const node *n)
{

    expr_ty d = NULL;
    expr_ty name_expr;

    REQ(n, decorator);
    REQ(CHILD(n, 0), AT);
    REQ(RCHILD(n, -1), NEWLINE);

    name_expr = ast_for_dotted_name(c, CHILD(n, 1));
    if (!name_expr)
        return NULL;

    if (NCH(n) == 3) {
        d = name_expr;
        name_expr = NULL;
    }
    else if (NCH(n) == 5) {
        d = Call(name_expr, NULL, NULL, NULL, NULL, LINENO(n),
                 n->n_col_offset, c->c_arena);
        if (!d)
            return NULL;
        name_expr = NULL;
    }
    else {
        d = ast_for_call(c, CHILD(n, 3), name_expr);
        if (!d)
            return NULL;
        name_expr = NULL;
    }

    return d;
}

static asdl_seq*
ast_for_decorators(struct compiling *c, const node *n)
{
    asdl_seq* decorator_seq;
    expr_ty d;
    int i;

    REQ(n, decorators);
    decorator_seq = asdl_seq_new(NCH(n), c->c_arena);
    if (!decorator_seq)
        return NULL;

    for (i = 0; i < NCH(n); i++) {
        d = ast_for_decorator(c, CHILD(n, i));
        if (!d)
            return NULL;
        asdl_seq_SET(decorator_seq, i, d);
    }
    return decorator_seq;
}

static stmt_ty
ast_for_funcdef(struct compiling *c, const node *n, asdl_seq *decorator_seq)
{

    identifier name;
    arguments_ty args;
    asdl_seq *body;
    int name_i = 1;

    REQ(n, funcdef);

    name = NEW_IDENTIFIER(CHILD(n, name_i));
    if (!name)
        return NULL;
    else if (!forbidden_check(c, CHILD(n, name_i), STR(CHILD(n, name_i))))
        return NULL;
    args = ast_for_arguments(c, CHILD(n, name_i + 1));
    if (!args)
        return NULL;
    body = ast_for_suite(c, CHILD(n, name_i + 3));
    if (!body)
        return NULL;

    return FunctionDef(name, args, body, decorator_seq, LINENO(n),
                       n->n_col_offset, c->c_arena);
}

static stmt_ty
ast_for_decorated(struct compiling *c, const node *n)
{

    stmt_ty thing = NULL;
    asdl_seq *decorator_seq = NULL;

    REQ(n, decorated);

    decorator_seq = ast_for_decorators(c, CHILD(n, 0));
    if (!decorator_seq)
      return NULL;

    assert(TYPE(CHILD(n, 1)) == funcdef ||
           TYPE(CHILD(n, 1)) == classdef);

    if (TYPE(CHILD(n, 1)) == funcdef) {
      thing = ast_for_funcdef(c, CHILD(n, 1), decorator_seq);
    } else if (TYPE(CHILD(n, 1)) == classdef) {
      thing = ast_for_classdef(c, CHILD(n, 1), decorator_seq);
    }

    if (thing) {
        thing->lineno = LINENO(n);
        thing->col_offset = n->n_col_offset;
    }
    return thing;
}

static expr_ty
ast_for_lambdef(struct compiling *c, const node *n)
{

    arguments_ty args;
    expr_ty expression;

    if (NCH(n) == 3) {
        args = arguments(NULL, NULL, NULL, NULL, c->c_arena);
        if (!args)
            return NULL;
        expression = ast_for_expr(c, CHILD(n, 2));
        if (!expression)
            return NULL;
    }
    else {
        args = ast_for_arguments(c, CHILD(n, 1));
        if (!args)
            return NULL;
        expression = ast_for_expr(c, CHILD(n, 3));
        if (!expression)
            return NULL;
    }

    return Lambda(args, expression, LINENO(n), n->n_col_offset, c->c_arena);
}

static expr_ty
ast_for_ifexpr(struct compiling *c, const node *n)
{

    expr_ty expression, body, orelse;

    assert(NCH(n) == 5);
    body = ast_for_expr(c, CHILD(n, 0));
    if (!body)
        return NULL;
    expression = ast_for_expr(c, CHILD(n, 2));
    if (!expression)
        return NULL;
    orelse = ast_for_expr(c, CHILD(n, 4));
    if (!orelse)
        return NULL;
    return IfExp(expression, body, orelse, LINENO(n), n->n_col_offset,
                 c->c_arena);
}

static int
count_list_fors(struct compiling *c, const node *n)
{
    int n_fors = 0;
    node *ch = CHILD(n, 1);

 count_list_for:
    n_fors++;
    REQ(ch, list_for);
    if (NCH(ch) == 5)
        ch = CHILD(ch, 4);
    else
        return n_fors;
 count_list_iter:
    REQ(ch, list_iter);
    ch = CHILD(ch, 0);
    if (TYPE(ch) == list_for)
        goto count_list_for;
    else if (TYPE(ch) == list_if) {
        if (NCH(ch) == 3) {
            ch = CHILD(ch, 2);
            goto count_list_iter;
        }
        else
            return n_fors;
    }

    PyErr_SetString(PyExc_SystemError, "logic error in count_list_fors");
    return -1;
}

static int
count_list_ifs(struct compiling *c, const node *n)
{
    int n_ifs = 0;

 count_list_iter:
    REQ(n, list_iter);
    if (TYPE(CHILD(n, 0)) == list_for)
        return n_ifs;
    n = CHILD(n, 0);
    REQ(n, list_if);
    n_ifs++;
    if (NCH(n) == 2)
        return n_ifs;
    n = CHILD(n, 2);
    goto count_list_iter;
}

static expr_ty
ast_for_listcomp(struct compiling *c, const node *n)
{

    expr_ty elt, first;
    asdl_seq *listcomps;
    int i, n_fors;
    node *ch;

    REQ(n, listmaker);
    assert(NCH(n) > 1);

    elt = ast_for_expr(c, CHILD(n, 0));
    if (!elt)
        return NULL;

    n_fors = count_list_fors(c, n);
    if (n_fors == -1)
        return NULL;

    listcomps = asdl_seq_new(n_fors, c->c_arena);
    if (!listcomps)
        return NULL;

    ch = CHILD(n, 1);
    for (i = 0; i < n_fors; i++) {
        comprehension_ty lc;
        asdl_seq *t;
        expr_ty expression;
        node *for_ch;

        REQ(ch, list_for);

        for_ch = CHILD(ch, 1);
        t = ast_for_exprlist(c, for_ch, Store);
        if (!t)
            return NULL;
        expression = ast_for_testlist(c, CHILD(ch, 3));
        if (!expression)
            return NULL;

        first = (expr_ty)asdl_seq_GET(t, 0);
        if (NCH(for_ch) == 1)
            lc = comprehension(first, expression, NULL, c->c_arena);
        else
            lc = comprehension(Tuple(t, Store, first->lineno, first->col_offset,
                                     c->c_arena),
                               expression, NULL, c->c_arena);
        if (!lc)
            return NULL;

        if (NCH(ch) == 5) {
            int j, n_ifs;
            asdl_seq *ifs;
            expr_ty list_for_expr;

            ch = CHILD(ch, 4);
            n_ifs = count_list_ifs(c, ch);
            if (n_ifs == -1)
                return NULL;

            ifs = asdl_seq_new(n_ifs, c->c_arena);
            if (!ifs)
                return NULL;

            for (j = 0; j < n_ifs; j++) {
                REQ(ch, list_iter);
                ch = CHILD(ch, 0);
                REQ(ch, list_if);

                list_for_expr = ast_for_expr(c, CHILD(ch, 1));
                if (!list_for_expr)
                    return NULL;

                asdl_seq_SET(ifs, j, list_for_expr);
                if (NCH(ch) == 3)
                    ch = CHILD(ch, 2);
            }

            if (TYPE(ch) == list_iter)
                ch = CHILD(ch, 0);
            lc->ifs = ifs;
        }
        asdl_seq_SET(listcomps, i, lc);
    }

    return ListComp(elt, listcomps, LINENO(n), n->n_col_offset, c->c_arena);
}

static int
count_comp_fors(struct compiling *c, const node *n)
{
    int n_fors = 0;

  count_comp_for:
    n_fors++;
    REQ(n, comp_for);
    if (NCH(n) == 5)
        n = CHILD(n, 4);
    else
        return n_fors;
  count_comp_iter:
    REQ(n, comp_iter);
    n = CHILD(n, 0);
    if (TYPE(n) == comp_for)
        goto count_comp_for;
    else if (TYPE(n) == comp_if) {
        if (NCH(n) == 3) {
            n = CHILD(n, 2);
            goto count_comp_iter;
        }
        else
            return n_fors;
    }

    PyErr_SetString(PyExc_SystemError,
                    "logic error in count_comp_fors");
    return -1;
}

static int
count_comp_ifs(struct compiling *c, const node *n)
{
    int n_ifs = 0;

    while (1) {
        REQ(n, comp_iter);
        if (TYPE(CHILD(n, 0)) == comp_for)
            return n_ifs;
        n = CHILD(n, 0);
        REQ(n, comp_if);
        n_ifs++;
        if (NCH(n) == 2)
            return n_ifs;
        n = CHILD(n, 2);
    }
}

static asdl_seq *
ast_for_comprehension(struct compiling *c, const node *n)
{
    int i, n_fors;
    asdl_seq *comps;

    n_fors = count_comp_fors(c, n);
    if (n_fors == -1)
        return NULL;

    comps = asdl_seq_new(n_fors, c->c_arena);
    if (!comps)
        return NULL;

    for (i = 0; i < n_fors; i++) {
        comprehension_ty comp;
        asdl_seq *t;
        expr_ty expression, first;
        node *for_ch;

        REQ(n, comp_for);

        for_ch = CHILD(n, 1);
        t = ast_for_exprlist(c, for_ch, Store);
        if (!t)
            return NULL;
        expression = ast_for_expr(c, CHILD(n, 3));
        if (!expression)
            return NULL;

        first = (expr_ty)asdl_seq_GET(t, 0);
        if (NCH(for_ch) == 1)
            comp = comprehension(first, expression, NULL, c->c_arena);
        else
            comp = comprehension(Tuple(t, Store, first->lineno, first->col_offset,
                                     c->c_arena),
                               expression, NULL, c->c_arena);
        if (!comp)
            return NULL;

        if (NCH(n) == 5) {
            int j, n_ifs;
            asdl_seq *ifs;

            n = CHILD(n, 4);
            n_ifs = count_comp_ifs(c, n);
            if (n_ifs == -1)
                return NULL;

            ifs = asdl_seq_new(n_ifs, c->c_arena);
            if (!ifs)
                return NULL;

            for (j = 0; j < n_ifs; j++) {
                REQ(n, comp_iter);
                n = CHILD(n, 0);
                REQ(n, comp_if);

                expression = ast_for_expr(c, CHILD(n, 1));
                if (!expression)
                    return NULL;
                asdl_seq_SET(ifs, j, expression);
                if (NCH(n) == 3)
                    n = CHILD(n, 2);
            }

            if (TYPE(n) == comp_iter)
                n = CHILD(n, 0);
            comp->ifs = ifs;
        }
        asdl_seq_SET(comps, i, comp);
    }
    return comps;
}

static expr_ty
ast_for_itercomp(struct compiling *c, const node *n, int type)
{
    expr_ty elt;
    asdl_seq *comps;

    assert(NCH(n) > 1);

    elt = ast_for_expr(c, CHILD(n, 0));
    if (!elt)
        return NULL;

    comps = ast_for_comprehension(c, CHILD(n, 1));
    if (!comps)
        return NULL;

    if (type == COMP_GENEXP)
        return GeneratorExp(elt, comps, LINENO(n), n->n_col_offset, c->c_arena);
    else if (type == COMP_SETCOMP)
        return SetComp(elt, comps, LINENO(n), n->n_col_offset, c->c_arena);
    else

        return NULL;
}

static expr_ty
ast_for_dictcomp(struct compiling *c, const node *n)
{
    expr_ty key, value;
    asdl_seq *comps;

    assert(NCH(n) > 3);
    REQ(CHILD(n, 1), COLON);

    key = ast_for_expr(c, CHILD(n, 0));
    if (!key)
        return NULL;

    value = ast_for_expr(c, CHILD(n, 2));
    if (!value)
        return NULL;

    comps = ast_for_comprehension(c, CHILD(n, 3));
    if (!comps)
        return NULL;

    return DictComp(key, value, comps, LINENO(n), n->n_col_offset, c->c_arena);
}

static expr_ty
ast_for_genexp(struct compiling *c, const node *n)
{
    assert(TYPE(n) == (testlist_comp) || TYPE(n) == (argument));
    return ast_for_itercomp(c, n, COMP_GENEXP);
}

static expr_ty
ast_for_setcomp(struct compiling *c, const node *n)
{
    assert(TYPE(n) == (dictorsetmaker));
    return ast_for_itercomp(c, n, COMP_SETCOMP);
}

static expr_ty
ast_for_atom(struct compiling *c, const node *n)
{

    node *ch = CHILD(n, 0);

    switch (TYPE(ch)) {
    case NAME: {

        PyObject *name = NEW_IDENTIFIER(ch);
        if (!name)
            return NULL;
        return Name(name, Load, LINENO(n), n->n_col_offset, c->c_arena);
    }
    case STRING: {
        PyObject *str = parsestrplus(c, n);
        if (!str) {
#ifdef Py_USING_UNICODE
            if (PyErr_ExceptionMatches(PyExc_UnicodeError)){
                PyObject *type, *value, *tback, *errstr;
                PyErr_Fetch(&type, &value, &tback);
                errstr = PyObject_Str(value);
                if (errstr) {
                    char *s = "";
                    char buf[128];
                    s = PyString_AsString(errstr);
                    PyOS_snprintf(buf, sizeof(buf), "(unicode error) %s", s);
                    ast_error(n, buf);
                    Py_DECREF(errstr);
                } else {
                    ast_error(n, "(unicode error) unknown error");
                }
                Py_DECREF(type);
                Py_DECREF(value);
                Py_XDECREF(tback);
            }
#endif
            return NULL;
        }
        PyArena_AddPyObject(c->c_arena, str);
        return Str(str, LINENO(n), n->n_col_offset, c->c_arena);
    }
    case NUMBER: {
        PyObject *pynum = parsenumber(c, STR(ch));
        if (!pynum)
            return NULL;

        PyArena_AddPyObject(c->c_arena, pynum);
        return Num(pynum, LINENO(n), n->n_col_offset, c->c_arena);
    }
    case LPAR:
        ch = CHILD(n, 1);

        if (TYPE(ch) == RPAR)
            return Tuple(NULL, Load, LINENO(n), n->n_col_offset, c->c_arena);

        if (TYPE(ch) == yield_expr)
            return ast_for_expr(c, ch);

        return ast_for_testlist_comp(c, ch);
    case LSQB:
        ch = CHILD(n, 1);

        if (TYPE(ch) == RSQB)
            return List(NULL, Load, LINENO(n), n->n_col_offset, c->c_arena);

        REQ(ch, listmaker);
        if (NCH(ch) == 1 || TYPE(CHILD(ch, 1)) == COMMA) {
            asdl_seq *elts = seq_for_testlist(c, ch);
            if (!elts)
                return NULL;

            return List(elts, Load, LINENO(n), n->n_col_offset, c->c_arena);
        }
        else
            return ast_for_listcomp(c, ch);
    case LBRACE: {

        int i, size;
        asdl_seq *keys, *values;

        ch = CHILD(n, 1);
        if (TYPE(ch) == RBRACE) {

            return Dict(NULL, NULL, LINENO(n), n->n_col_offset, c->c_arena);
        } else if (NCH(ch) == 1 || TYPE(CHILD(ch, 1)) == COMMA) {

            asdl_seq *elts;
            size = (NCH(ch) + 1) / 2;
            elts = asdl_seq_new(size, c->c_arena);
            if (!elts)
                return NULL;
            for (i = 0; i < NCH(ch); i += 2) {
                expr_ty expression;
                expression = ast_for_expr(c, CHILD(ch, i));
                if (!expression)
                    return NULL;
                asdl_seq_SET(elts, i / 2, expression);
            }
            return Set(elts, LINENO(n), n->n_col_offset, c->c_arena);
        } else if (TYPE(CHILD(ch, 1)) == comp_for) {

            return ast_for_setcomp(c, ch);
        } else if (NCH(ch) > 3 && TYPE(CHILD(ch, 3)) == comp_for) {
            return ast_for_dictcomp(c, ch);
        } else {

            size = (NCH(ch) + 1) / 4;
            keys = asdl_seq_new(size, c->c_arena);
            if (!keys)
                return NULL;

            values = asdl_seq_new(size, c->c_arena);
            if (!values)
                return NULL;

            for (i = 0; i < NCH(ch); i += 4) {
                expr_ty expression;

                expression = ast_for_expr(c, CHILD(ch, i));
                if (!expression)
                    return NULL;

                asdl_seq_SET(keys, i / 4, expression);

                expression = ast_for_expr(c, CHILD(ch, i + 2));
                if (!expression)
                    return NULL;

                asdl_seq_SET(values, i / 4, expression);
            }
            return Dict(keys, values, LINENO(n), n->n_col_offset, c->c_arena);
        }
    }
    case BACKQUOTE: {
        expr_ty expression;
        if (Py_Py3kWarningFlag &&
            !ast_warn(c, n, "backquote not supported in 3.x; use repr()"))
            return NULL;
        expression = ast_for_testlist(c, CHILD(n, 1));
        if (!expression)
            return NULL;

        return Repr(expression, LINENO(n), n->n_col_offset, c->c_arena);
    }
    default:
        PyErr_Format(PyExc_SystemError, "unhandled atom %d", TYPE(ch));
        return NULL;
    }
}

static slice_ty
ast_for_slice(struct compiling *c, const node *n)
{
    node *ch;
    expr_ty lower = NULL, upper = NULL, step = NULL;

    REQ(n, subscript);

    ch = CHILD(n, 0);
    if (TYPE(ch) == DOT)
        return Ellipsis(c->c_arena);

    if (NCH(n) == 1 && TYPE(ch) == test) {

        step = ast_for_expr(c, ch);
        if (!step)
            return NULL;

        return Index(step, c->c_arena);
    }

    if (TYPE(ch) == test) {
        lower = ast_for_expr(c, ch);
        if (!lower)
            return NULL;
    }

    if (TYPE(ch) == COLON) {
        if (NCH(n) > 1) {
            node *n2 = CHILD(n, 1);

            if (TYPE(n2) == test) {
                upper = ast_for_expr(c, n2);
                if (!upper)
                    return NULL;
            }
        }
    } else if (NCH(n) > 2) {
        node *n2 = CHILD(n, 2);

        if (TYPE(n2) == test) {
            upper = ast_for_expr(c, n2);
            if (!upper)
                return NULL;
        }
    }

    ch = CHILD(n, NCH(n) - 1);
    if (TYPE(ch) == sliceop) {
        if (NCH(ch) == 1) {

            identifier none = new_identifier("None", c->c_arena);
            if (!none)
                return NULL;
            ch = CHILD(ch, 0);
            step = Name(none, Load, LINENO(ch), ch->n_col_offset, c->c_arena);
            if (!step)
                return NULL;
        } else {
            ch = CHILD(ch, 1);
            if (TYPE(ch) == test) {
                step = ast_for_expr(c, ch);
                if (!step)
                    return NULL;
            }
        }
    }

    return Slice(lower, upper, step, c->c_arena);
}

static expr_ty
ast_for_binop(struct compiling *c, const node *n)
{

        int i, nops;
        expr_ty expr1, expr2, result;
        operator_ty newoperator;

        expr1 = ast_for_expr(c, CHILD(n, 0));
        if (!expr1)
            return NULL;

        expr2 = ast_for_expr(c, CHILD(n, 2));
        if (!expr2)
            return NULL;

        newoperator = get_operator(CHILD(n, 1));
        if (!newoperator)
            return NULL;

        result = BinOp(expr1, newoperator, expr2, LINENO(n), n->n_col_offset,
                       c->c_arena);
        if (!result)
            return NULL;

        nops = (NCH(n) - 1) / 2;
        for (i = 1; i < nops; i++) {
                expr_ty tmp_result, tmp;
                const node* next_oper = CHILD(n, i * 2 + 1);

                newoperator = get_operator(next_oper);
                if (!newoperator)
                    return NULL;

                tmp = ast_for_expr(c, CHILD(n, i * 2 + 2));
                if (!tmp)
                    return NULL;

                tmp_result = BinOp(result, newoperator, tmp,
                                   LINENO(next_oper), next_oper->n_col_offset,
                                   c->c_arena);
                if (!tmp_result)
                        return NULL;
                result = tmp_result;
        }
        return result;
}

static expr_ty
ast_for_trailer(struct compiling *c, const node *n, expr_ty left_expr)
{

    REQ(n, trailer);
    if (TYPE(CHILD(n, 0)) == LPAR) {
        if (NCH(n) == 2)
            return Call(left_expr, NULL, NULL, NULL, NULL, LINENO(n),
                        n->n_col_offset, c->c_arena);
        else
            return ast_for_call(c, CHILD(n, 1), left_expr);
    }
    else if (TYPE(CHILD(n, 0)) == DOT ) {
        PyObject *attr_id = NEW_IDENTIFIER(CHILD(n, 1));
        if (!attr_id)
            return NULL;
        return Attribute(left_expr, attr_id, Load,
                         LINENO(n), n->n_col_offset, c->c_arena);
    }
    else {
        REQ(CHILD(n, 0), LSQB);
        REQ(CHILD(n, 2), RSQB);
        n = CHILD(n, 1);
        if (NCH(n) == 1) {
            slice_ty slc = ast_for_slice(c, CHILD(n, 0));
            if (!slc)
                return NULL;
            return Subscript(left_expr, slc, Load, LINENO(n), n->n_col_offset,
                             c->c_arena);
        }
        else {

            int j;
            slice_ty slc;
            expr_ty e;
            bool simple = true;
            asdl_seq *slices, *elts;
            slices = asdl_seq_new((NCH(n) + 1) / 2, c->c_arena);
            if (!slices)
                return NULL;
            for (j = 0; j < NCH(n); j += 2) {
                slc = ast_for_slice(c, CHILD(n, j));
                if (!slc)
                    return NULL;
                if (slc->kind != Index_kind)
                    simple = false;
                asdl_seq_SET(slices, j / 2, slc);
            }
            if (!simple) {
                return Subscript(left_expr, ExtSlice(slices, c->c_arena),
                                 Load, LINENO(n), n->n_col_offset, c->c_arena);
            }

            elts = asdl_seq_new(asdl_seq_LEN(slices), c->c_arena);
            if (!elts)
                return NULL;
            for (j = 0; j < asdl_seq_LEN(slices); ++j) {
                slc = (slice_ty)asdl_seq_GET(slices, j);
                assert(slc->kind == Index_kind  && slc->v.Index.value);
                asdl_seq_SET(elts, j, slc->v.Index.value);
            }
            e = Tuple(elts, Load, LINENO(n), n->n_col_offset, c->c_arena);
            if (!e)
                return NULL;
            return Subscript(left_expr, Index(e, c->c_arena),
                             Load, LINENO(n), n->n_col_offset, c->c_arena);
        }
    }
}

static expr_ty
ast_for_factor(struct compiling *c, const node *n)
{
    node *pfactor, *ppower, *patom, *pnum;
    expr_ty expression;

    if (TYPE(CHILD(n, 0)) == MINUS &&
        NCH(n) == 2 &&
        TYPE((pfactor = CHILD(n, 1))) == factor &&
        NCH(pfactor) == 1 &&
        TYPE((ppower = CHILD(pfactor, 0))) == power &&
        NCH(ppower) == 1 &&
        TYPE((patom = CHILD(ppower, 0))) == atom &&
        TYPE((pnum = CHILD(patom, 0))) == NUMBER) {
        PyObject *pynum;
        char *s = PyObject_MALLOC(strlen(STR(pnum)) + 2);
        if (s == NULL)
            return NULL;
        s[0] = '-';
        strcpy(s + 1, STR(pnum));
        pynum = parsenumber(c, s);
        PyObject_FREE(s);
        if (!pynum)
            return NULL;

        PyArena_AddPyObject(c->c_arena, pynum);
        return Num(pynum, LINENO(n), n->n_col_offset, c->c_arena);
    }

    expression = ast_for_expr(c, CHILD(n, 1));
    if (!expression)
        return NULL;

    switch (TYPE(CHILD(n, 0))) {
        case PLUS:
            return UnaryOp(UAdd, expression, LINENO(n), n->n_col_offset,
                           c->c_arena);
        case MINUS:
            return UnaryOp(USub, expression, LINENO(n), n->n_col_offset,
                           c->c_arena);
        case TILDE:
            return UnaryOp(Invert, expression, LINENO(n),
                           n->n_col_offset, c->c_arena);
    }
    PyErr_Format(PyExc_SystemError, "unhandled factor: %d",
                 TYPE(CHILD(n, 0)));
    return NULL;
}

static expr_ty
ast_for_power(struct compiling *c, const node *n)
{

    int i;
    expr_ty e, tmp;
    REQ(n, power);
    e = ast_for_atom(c, CHILD(n, 0));
    if (!e)
        return NULL;
    if (NCH(n) == 1)
        return e;
    for (i = 1; i < NCH(n); i++) {
        node *ch = CHILD(n, i);
        if (TYPE(ch) != trailer)
            break;
        tmp = ast_for_trailer(c, ch, e);
        if (!tmp)
            return NULL;
        tmp->lineno = e->lineno;
        tmp->col_offset = e->col_offset;
        e = tmp;
    }
    if (TYPE(CHILD(n, NCH(n) - 1)) == factor) {
        expr_ty f = ast_for_expr(c, CHILD(n, NCH(n) - 1));
        if (!f)
            return NULL;
        tmp = BinOp(e, Pow, f, LINENO(n), n->n_col_offset, c->c_arena);
        if (!tmp)
            return NULL;
        e = tmp;
    }
    return e;
}

static expr_ty
ast_for_expr(struct compiling *c, const node *n)
{

    asdl_seq *seq;
    int i;

 loop:
    switch (TYPE(n)) {
        case test:
        case old_test:
            if (TYPE(CHILD(n, 0)) == lambdef ||
                TYPE(CHILD(n, 0)) == old_lambdef)
                return ast_for_lambdef(c, CHILD(n, 0));
            else if (NCH(n) > 1)
                return ast_for_ifexpr(c, n);

        case or_test:
        case and_test:
            if (NCH(n) == 1) {
                n = CHILD(n, 0);
                goto loop;
            }
            seq = asdl_seq_new((NCH(n) + 1) / 2, c->c_arena);
            if (!seq)
                return NULL;
            for (i = 0; i < NCH(n); i += 2) {
                expr_ty e = ast_for_expr(c, CHILD(n, i));
                if (!e)
                    return NULL;
                asdl_seq_SET(seq, i / 2, e);
            }
            if (!strcmp(STR(CHILD(n, 1)), "and"))
                return BoolOp(And, seq, LINENO(n), n->n_col_offset,
                              c->c_arena);
            assert(!strcmp(STR(CHILD(n, 1)), "or"));
            return BoolOp(Or, seq, LINENO(n), n->n_col_offset, c->c_arena);
        case not_test:
            if (NCH(n) == 1) {
                n = CHILD(n, 0);
                goto loop;
            }
            else {
                expr_ty expression = ast_for_expr(c, CHILD(n, 1));
                if (!expression)
                    return NULL;

                return UnaryOp(Not, expression, LINENO(n), n->n_col_offset,
                               c->c_arena);
            }
        case comparison:
            if (NCH(n) == 1) {
                n = CHILD(n, 0);
                goto loop;
            }
            else {
                expr_ty expression;
                asdl_int_seq *ops;
                asdl_seq *cmps;
                ops = asdl_int_seq_new(NCH(n) / 2, c->c_arena);
                if (!ops)
                    return NULL;
                cmps = asdl_seq_new(NCH(n) / 2, c->c_arena);
                if (!cmps) {
                    return NULL;
                }
                for (i = 1; i < NCH(n); i += 2) {
                    cmpop_ty newoperator;

                    newoperator = ast_for_comp_op(c, CHILD(n, i));
                    if (!newoperator) {
                        return NULL;
                    }

                    expression = ast_for_expr(c, CHILD(n, i + 1));
                    if (!expression) {
                        return NULL;
                    }

                    asdl_seq_SET(ops, i / 2, newoperator);
                    asdl_seq_SET(cmps, i / 2, expression);
                }
                expression = ast_for_expr(c, CHILD(n, 0));
                if (!expression) {
                    return NULL;
                }

                return Compare(expression, ops, cmps, LINENO(n),
                               n->n_col_offset, c->c_arena);
            }
            break;

        case expr:
        case xor_expr:
        case and_expr:
        case shift_expr:
        case arith_expr:
        case term:
            if (NCH(n) == 1) {
                n = CHILD(n, 0);
                goto loop;
            }
            return ast_for_binop(c, n);
        case yield_expr: {
            expr_ty exp = NULL;
            if (NCH(n) == 2) {
                exp = ast_for_testlist(c, CHILD(n, 1));
                if (!exp)
                    return NULL;
            }
            return Yield(exp, LINENO(n), n->n_col_offset, c->c_arena);
        }
        case factor:
            if (NCH(n) == 1) {
                n = CHILD(n, 0);
                goto loop;
            }
            return ast_for_factor(c, n);
        case power:
            return ast_for_power(c, n);
        default:
            PyErr_Format(PyExc_SystemError, "unhandled expr: %d", TYPE(n));
            return NULL;
    }

    return NULL;
}

static expr_ty
ast_for_call(struct compiling *c, const node *n, expr_ty func)
{

    int i, nargs, nkeywords, ngens;
    asdl_seq *args;
    asdl_seq *keywords;
    expr_ty vararg = NULL, kwarg = NULL;

    REQ(n, arglist);

    nargs = 0;
    nkeywords = 0;
    ngens = 0;
    for (i = 0; i < NCH(n); i++) {
        node *ch = CHILD(n, i);
        if (TYPE(ch) == argument) {
            if (NCH(ch) == 1)
                nargs++;
            else if (TYPE(CHILD(ch, 1)) == comp_for)
                ngens++;
            else
                nkeywords++;
        }
    }
    if (ngens > 1 || (ngens && (nargs || nkeywords))) {
        ast_error(n, "Generator expression must be parenthesized "
                  "if not sole argument");
        return NULL;
    }

    if (nargs + nkeywords + ngens > 255) {
      ast_error(n, "more than 255 arguments");
      return NULL;
    }

    args = asdl_seq_new(nargs + ngens, c->c_arena);
    if (!args)
        return NULL;
    keywords = asdl_seq_new(nkeywords, c->c_arena);
    if (!keywords)
        return NULL;
    nargs = 0;
    nkeywords = 0;
    for (i = 0; i < NCH(n); i++) {
        node *ch = CHILD(n, i);
        if (TYPE(ch) == argument) {
            expr_ty e;
            if (NCH(ch) == 1) {
                if (nkeywords) {
                    ast_error(CHILD(ch, 0),
                              "non-keyword arg after keyword arg");
                    return NULL;
                }
                if (vararg) {
                    ast_error(CHILD(ch, 0),
                              "only named arguments may follow *expression");
                    return NULL;
                }
                e = ast_for_expr(c, CHILD(ch, 0));
                if (!e)
                    return NULL;
                asdl_seq_SET(args, nargs++, e);
            }
            else if (TYPE(CHILD(ch, 1)) == comp_for) {
                e = ast_for_genexp(c, ch);
                if (!e)
                    return NULL;
                asdl_seq_SET(args, nargs++, e);
            }
            else {
                keyword_ty kw;
                identifier key;
                int k;
                char *tmp;

                e = ast_for_expr(c, CHILD(ch, 0));
                if (!e)
                    return NULL;

                if (e->kind == Lambda_kind) {
                    ast_error(CHILD(ch, 0),
                              "lambda cannot contain assignment");
                    return NULL;
                } else if (e->kind != Name_kind) {
                    ast_error(CHILD(ch, 0), "keyword can't be an expression");
                    return NULL;
                }
                key = e->v.Name.id;
                if (!forbidden_check(c, CHILD(ch, 0), PyBytes_AS_STRING(key)))
                    return NULL;
                for (k = 0; k < nkeywords; k++) {
                    tmp = PyString_AS_STRING(
                        ((keyword_ty)asdl_seq_GET(keywords, k))->arg);
                    if (!strcmp(tmp, PyString_AS_STRING(key))) {
                        ast_error(CHILD(ch, 0), "keyword argument repeated");
                        return NULL;
                    }
                }
                e = ast_for_expr(c, CHILD(ch, 2));
                if (!e)
                    return NULL;
                kw = keyword(key, e, c->c_arena);
                if (!kw)
                    return NULL;
                asdl_seq_SET(keywords, nkeywords++, kw);
            }
        }
        else if (TYPE(ch) == STAR) {
            vararg = ast_for_expr(c, CHILD(n, i+1));
            if (!vararg)
                return NULL;
            i++;
        }
        else if (TYPE(ch) == DOUBLESTAR) {
            kwarg = ast_for_expr(c, CHILD(n, i+1));
            if (!kwarg)
                return NULL;
            i++;
        }
    }

    return Call(func, args, keywords, vararg, kwarg, func->lineno,
                func->col_offset, c->c_arena);
}

static expr_ty
ast_for_testlist(struct compiling *c, const node* n)
{

    assert(NCH(n) > 0);
    if (TYPE(n) == testlist_comp) {
        if (NCH(n) > 1)
            assert(TYPE(CHILD(n, 1)) != comp_for);
    }
    else {
        assert(TYPE(n) == testlist ||
               TYPE(n) == testlist_safe ||
               TYPE(n) == testlist1);
    }
    if (NCH(n) == 1)
        return ast_for_expr(c, CHILD(n, 0));
    else {
        asdl_seq *tmp = seq_for_testlist(c, n);
        if (!tmp)
            return NULL;
        return Tuple(tmp, Load, LINENO(n), n->n_col_offset, c->c_arena);
    }
}

static expr_ty
ast_for_testlist_comp(struct compiling *c, const node* n)
{

    assert(TYPE(n) == testlist_comp || TYPE(n) == argument);
    if (NCH(n) > 1 && TYPE(CHILD(n, 1)) == comp_for)
        return ast_for_genexp(c, n);
    return ast_for_testlist(c, n);
}

static asdl_seq*
ast_for_class_bases(struct compiling *c, const node* n)
{

    assert(NCH(n) > 0);
    REQ(n, testlist);
    if (NCH(n) == 1) {
        expr_ty base;
        asdl_seq *bases = asdl_seq_new(1, c->c_arena);
        if (!bases)
            return NULL;
        base = ast_for_expr(c, CHILD(n, 0));
        if (!base)
            return NULL;
        asdl_seq_SET(bases, 0, base);
        return bases;
    }

    return seq_for_testlist(c, n);
}

static stmt_ty
ast_for_expr_stmt(struct compiling *c, const node *n)
{
    REQ(n, expr_stmt);

    if (NCH(n) == 1) {
        expr_ty e = ast_for_testlist(c, CHILD(n, 0));
        if (!e)
            return NULL;

        return Expr(e, LINENO(n), n->n_col_offset, c->c_arena);
    }
    else if (TYPE(CHILD(n, 1)) == augassign) {
        expr_ty expr1, expr2;
        operator_ty newoperator;
        node *ch = CHILD(n, 0);

        expr1 = ast_for_testlist(c, ch);
        if (!expr1)
            return NULL;
        if(!set_context(c, expr1, Store, ch))
            return NULL;

        switch (expr1->kind) {
            case Name_kind:
            case Attribute_kind:
            case Subscript_kind:
                break;
            default:
                ast_error(ch, "illegal expression for augmented assignment");
                return NULL;
        }

        ch = CHILD(n, 2);
        if (TYPE(ch) == testlist)
            expr2 = ast_for_testlist(c, ch);
        else
            expr2 = ast_for_expr(c, ch);
        if (!expr2)
            return NULL;

        newoperator = ast_for_augassign(c, CHILD(n, 1));
        if (!newoperator)
            return NULL;

        return AugAssign(expr1, newoperator, expr2, LINENO(n), n->n_col_offset,
                         c->c_arena);
    }
    else {
        int i;
        asdl_seq *targets;
        node *value;
        expr_ty expression;

        REQ(CHILD(n, 1), EQUAL);
        targets = asdl_seq_new(NCH(n) / 2, c->c_arena);
        if (!targets)
            return NULL;
        for (i = 0; i < NCH(n) - 2; i += 2) {
            expr_ty e;
            node *ch = CHILD(n, i);
            if (TYPE(ch) == yield_expr) {
                ast_error(ch, "assignment to yield expression not possible");
                return NULL;
            }
            e = ast_for_testlist(c, ch);
            if (!e)
                return NULL;

            if (!set_context(c, e, Store, CHILD(n, i)))
                return NULL;

            asdl_seq_SET(targets, i / 2, e);
        }
        value = CHILD(n, NCH(n) - 1);
        if (TYPE(value) == testlist)
            expression = ast_for_testlist(c, value);
        else
            expression = ast_for_expr(c, value);
        if (!expression)
            return NULL;
        return Assign(targets, expression, LINENO(n), n->n_col_offset,
                      c->c_arena);
    }
}

static stmt_ty
ast_for_print_stmt(struct compiling *c, const node *n)
{

    expr_ty dest = NULL, expression;
    asdl_seq *seq = NULL;
    bool nl;
    int i, j, values_count, start = 1;

    REQ(n, print_stmt);
    if (NCH(n) >= 2 && TYPE(CHILD(n, 1)) == RIGHTSHIFT) {
        dest = ast_for_expr(c, CHILD(n, 2));
        if (!dest)
            return NULL;
        start = 4;
    }
    values_count = (NCH(n) + 1 - start) / 2;
    if (values_count) {
        seq = asdl_seq_new(values_count, c->c_arena);
        if (!seq)
            return NULL;
        for (i = start, j = 0; i < NCH(n); i += 2, ++j) {
            expression = ast_for_expr(c, CHILD(n, i));
            if (!expression)
                return NULL;
            asdl_seq_SET(seq, j, expression);
        }
    }
    nl = (TYPE(CHILD(n, NCH(n) - 1)) == COMMA) ? false : true;
    return Print(dest, seq, nl, LINENO(n), n->n_col_offset, c->c_arena);
}

static asdl_seq *
ast_for_exprlist(struct compiling *c, const node *n, expr_context_ty context)
{
    asdl_seq *seq;
    int i;
    expr_ty e;

    REQ(n, exprlist);

    seq = asdl_seq_new((NCH(n) + 1) / 2, c->c_arena);
    if (!seq)
        return NULL;
    for (i = 0; i < NCH(n); i += 2) {
        e = ast_for_expr(c, CHILD(n, i));
        if (!e)
            return NULL;
        asdl_seq_SET(seq, i / 2, e);
        if (context && !set_context(c, e, context, CHILD(n, i)))
            return NULL;
    }
    return seq;
}

static stmt_ty
ast_for_del_stmt(struct compiling *c, const node *n)
{
    asdl_seq *expr_list;

    REQ(n, del_stmt);

    expr_list = ast_for_exprlist(c, CHILD(n, 1), Del);
    if (!expr_list)
        return NULL;
    return Delete(expr_list, LINENO(n), n->n_col_offset, c->c_arena);
}

static stmt_ty
ast_for_flow_stmt(struct compiling *c, const node *n)
{

    node *ch;

    REQ(n, flow_stmt);
    ch = CHILD(n, 0);
    switch (TYPE(ch)) {
        case break_stmt:
            return Break(LINENO(n), n->n_col_offset, c->c_arena);
        case continue_stmt:
            return Continue(LINENO(n), n->n_col_offset, c->c_arena);
        case yield_stmt: {
            expr_ty exp = ast_for_expr(c, CHILD(ch, 0));
            if (!exp)
                return NULL;
            return Expr(exp, LINENO(n), n->n_col_offset, c->c_arena);
        }
        case return_stmt:
            if (NCH(ch) == 1)
                return Return(NULL, LINENO(n), n->n_col_offset, c->c_arena);
            else {
                expr_ty expression = ast_for_testlist(c, CHILD(ch, 1));
                if (!expression)
                    return NULL;
                return Return(expression, LINENO(n), n->n_col_offset,
                              c->c_arena);
            }
        case raise_stmt:
            if (NCH(ch) == 1)
                return Raise(NULL, NULL, NULL, LINENO(n), n->n_col_offset,
                             c->c_arena);
            else if (NCH(ch) == 2) {
                expr_ty expression = ast_for_expr(c, CHILD(ch, 1));
                if (!expression)
                    return NULL;
                return Raise(expression, NULL, NULL, LINENO(n),
                             n->n_col_offset, c->c_arena);
            }
            else if (NCH(ch) == 4) {
                expr_ty expr1, expr2;

                expr1 = ast_for_expr(c, CHILD(ch, 1));
                if (!expr1)
                    return NULL;
                expr2 = ast_for_expr(c, CHILD(ch, 3));
                if (!expr2)
                    return NULL;

                return Raise(expr1, expr2, NULL, LINENO(n), n->n_col_offset,
                             c->c_arena);
            }
            else if (NCH(ch) == 6) {
                expr_ty expr1, expr2, expr3;

                expr1 = ast_for_expr(c, CHILD(ch, 1));
                if (!expr1)
                    return NULL;
                expr2 = ast_for_expr(c, CHILD(ch, 3));
                if (!expr2)
                    return NULL;
                expr3 = ast_for_expr(c, CHILD(ch, 5));
                if (!expr3)
                    return NULL;

                return Raise(expr1, expr2, expr3, LINENO(n), n->n_col_offset,
                             c->c_arena);
            }
        default:
            PyErr_Format(PyExc_SystemError,
                         "unexpected flow_stmt: %d", TYPE(ch));
            return NULL;
    }

    PyErr_SetString(PyExc_SystemError, "unhandled flow statement");
    return NULL;
}

static alias_ty
alias_for_import_name(struct compiling *c, const node *n, int store)
{

    PyObject *str, *name;

 loop:
    switch (TYPE(n)) {
         case import_as_name: {
            node *name_node = CHILD(n, 0);
            str = NULL;
            if (NCH(n) == 3) {
                node *str_node = CHILD(n, 2);
                if (store && !forbidden_check(c, str_node, STR(str_node)))
                    return NULL;
                str = NEW_IDENTIFIER(str_node);
                if (!str)
                    return NULL;
            }
            else {
                if (!forbidden_check(c, name_node, STR(name_node)))
                    return NULL;
            }
            name = NEW_IDENTIFIER(name_node);
            if (!name)
                return NULL;
            return alias(name, str, c->c_arena);
        }
        case dotted_as_name:
            if (NCH(n) == 1) {
                n = CHILD(n, 0);
                goto loop;
            }
            else {
                node *asname_node = CHILD(n, 2);
                alias_ty a = alias_for_import_name(c, CHILD(n, 0), 0);
                if (!a)
                    return NULL;
                assert(!a->asname);
                if (!forbidden_check(c, asname_node, STR(asname_node)))
                    return NULL;
                a->asname = NEW_IDENTIFIER(asname_node);
                if (!a->asname)
                    return NULL;
                return a;
            }
            break;
        case dotted_name:
            if (NCH(n) == 1) {
                node *name_node = CHILD(n, 0);
                if (store && !forbidden_check(c, name_node, STR(name_node)))
                    return NULL;
                name = NEW_IDENTIFIER(name_node);
                if (!name)
                    return NULL;
                return alias(name, NULL, c->c_arena);
            }
            else {

                int i;
                size_t len;
                char *s;

                len = 0;
                for (i = 0; i < NCH(n); i += 2)

                    len += strlen(STR(CHILD(n, i))) + 1;
                len--;
                str = PyString_FromStringAndSize(NULL, len);
                if (!str)
                    return NULL;
                s = PyString_AS_STRING(str);
                if (!s)
                    return NULL;
                for (i = 0; i < NCH(n); i += 2) {
                    char *sch = STR(CHILD(n, i));
                    strcpy(s, STR(CHILD(n, i)));
                    s += strlen(sch);
                    *s++ = '.';
                }
                --s;
                *s = '\0';
                PyString_InternInPlace(&str);
                PyArena_AddPyObject(c->c_arena, str);
                return alias(str, NULL, c->c_arena);
            }
            break;
        case STAR:
            str = PyString_InternFromString("*");
            PyArena_AddPyObject(c->c_arena, str);
            return alias(str, NULL, c->c_arena);
        default:
            PyErr_Format(PyExc_SystemError,
                         "unexpected import name: %d", TYPE(n));
            return NULL;
    }

    PyErr_SetString(PyExc_SystemError, "unhandled import name condition");
    return NULL;
}

static stmt_ty
ast_for_import_stmt(struct compiling *c, const node *n)
{

    int lineno;
    int col_offset;
    int i;
    asdl_seq *aliases;

    REQ(n, import_stmt);
    lineno = LINENO(n);
    col_offset = n->n_col_offset;
    n = CHILD(n, 0);
    if (TYPE(n) == import_name) {
        n = CHILD(n, 1);
        REQ(n, dotted_as_names);
        aliases = asdl_seq_new((NCH(n) + 1) / 2, c->c_arena);
        if (!aliases)
            return NULL;
        for (i = 0; i < NCH(n); i += 2) {
            alias_ty import_alias = alias_for_import_name(c, CHILD(n, i), 1);
            if (!import_alias)
                return NULL;
            asdl_seq_SET(aliases, i / 2, import_alias);
        }
        return Import(aliases, lineno, col_offset, c->c_arena);
    }
    else if (TYPE(n) == import_from) {
        int n_children;
        int idx, ndots = 0;
        alias_ty mod = NULL;
        identifier modname = NULL;

        for (idx = 1; idx < NCH(n); idx++) {
            if (TYPE(CHILD(n, idx)) == dotted_name) {
                mod = alias_for_import_name(c, CHILD(n, idx), 0);
                if (!mod)
                    return NULL;
                idx++;
                break;
            } else if (TYPE(CHILD(n, idx)) != DOT) {
                break;
            }
            ndots++;
        }
        idx++;
        switch (TYPE(CHILD(n, idx))) {
        case STAR:

            n = CHILD(n, idx);
            n_children = 1;
            break;
        case LPAR:

            n = CHILD(n, idx + 1);
            n_children = NCH(n);
            break;
        case import_as_names:

            n = CHILD(n, idx);
            n_children = NCH(n);
            if (n_children % 2 == 0) {
                ast_error(n, "trailing comma not allowed without"
                             " surrounding parentheses");
                return NULL;
            }
            break;
        default:
            ast_error(n, "Unexpected node-type in from-import");
            return NULL;
        }

        aliases = asdl_seq_new((n_children + 1) / 2, c->c_arena);
        if (!aliases)
            return NULL;

        if (TYPE(n) == STAR) {
            alias_ty import_alias = alias_for_import_name(c, n, 1);
            if (!import_alias)
                return NULL;
                asdl_seq_SET(aliases, 0, import_alias);
        }
        else {
            for (i = 0; i < NCH(n); i += 2) {
                alias_ty import_alias = alias_for_import_name(c, CHILD(n, i), 1);
                if (!import_alias)
                    return NULL;
                    asdl_seq_SET(aliases, i / 2, import_alias);
            }
        }
        if (mod != NULL)
            modname = mod->name;
        return ImportFrom(modname, aliases, ndots, lineno, col_offset,
                          c->c_arena);
    }
    PyErr_Format(PyExc_SystemError,
                 "unknown import statement: starts with command '%s'",
                 STR(CHILD(n, 0)));
    return NULL;
}

static stmt_ty
ast_for_global_stmt(struct compiling *c, const node *n)
{

    identifier name;
    asdl_seq *s;
    int i;

    REQ(n, global_stmt);
    s = asdl_seq_new(NCH(n) / 2, c->c_arena);
    if (!s)
        return NULL;
    for (i = 1; i < NCH(n); i += 2) {
        name = NEW_IDENTIFIER(CHILD(n, i));
        if (!name)
            return NULL;
        asdl_seq_SET(s, i / 2, name);
    }
    return Global(s, LINENO(n), n->n_col_offset, c->c_arena);
}

static stmt_ty
ast_for_exec_stmt(struct compiling *c, const node *n)
{
    expr_ty expr1, globals = NULL, locals = NULL;
    int n_children = NCH(n);
    if (n_children != 2 && n_children != 4 && n_children != 6) {
        PyErr_Format(PyExc_SystemError,
                     "poorly formed 'exec' statement: %d parts to statement",
                     n_children);
        return NULL;
    }

    REQ(n, exec_stmt);
    expr1 = ast_for_expr(c, CHILD(n, 1));
    if (!expr1)
        return NULL;

    if (expr1->kind == Tuple_kind && n_children < 4 &&
        (asdl_seq_LEN(expr1->v.Tuple.elts) == 2 ||
         asdl_seq_LEN(expr1->v.Tuple.elts) == 3)) {

        globals = asdl_seq_GET(expr1->v.Tuple.elts, 1);
        if (asdl_seq_LEN(expr1->v.Tuple.elts) == 3) {
            locals = asdl_seq_GET(expr1->v.Tuple.elts, 2);
        }
        expr1 = asdl_seq_GET(expr1->v.Tuple.elts, 0);
    }

    if (n_children >= 4) {
        globals = ast_for_expr(c, CHILD(n, 3));
        if (!globals)
            return NULL;
    }
    if (n_children == 6) {
        locals = ast_for_expr(c, CHILD(n, 5));
        if (!locals)
            return NULL;
    }

    return Exec(expr1, globals, locals, LINENO(n), n->n_col_offset,
                c->c_arena);
}

static stmt_ty
ast_for_assert_stmt(struct compiling *c, const node *n)
{

    REQ(n, assert_stmt);
    if (NCH(n) == 2) {
        expr_ty expression = ast_for_expr(c, CHILD(n, 1));
        if (!expression)
            return NULL;
        return Assert(expression, NULL, LINENO(n), n->n_col_offset,
                      c->c_arena);
    }
    else if (NCH(n) == 4) {
        expr_ty expr1, expr2;

        expr1 = ast_for_expr(c, CHILD(n, 1));
        if (!expr1)
            return NULL;
        expr2 = ast_for_expr(c, CHILD(n, 3));
        if (!expr2)
            return NULL;

        return Assert(expr1, expr2, LINENO(n), n->n_col_offset, c->c_arena);
    }
    PyErr_Format(PyExc_SystemError,
                 "improper number of parts to 'assert' statement: %d",
                 NCH(n));
    return NULL;
}

static asdl_seq *
ast_for_suite(struct compiling *c, const node *n)
{

    asdl_seq *seq;
    stmt_ty s;
    int i, total, num, end, pos = 0;
    node *ch;

    REQ(n, suite);

    total = num_stmts(n);
    seq = asdl_seq_new(total, c->c_arena);
    if (!seq)
        return NULL;
    if (TYPE(CHILD(n, 0)) == simple_stmt) {
        n = CHILD(n, 0);

        end = NCH(n) - 1;
        if (TYPE(CHILD(n, end - 1)) == SEMI)
            end--;

        for (i = 0; i < end; i += 2) {
            ch = CHILD(n, i);
            s = ast_for_stmt(c, ch);
            if (!s)
                return NULL;
            asdl_seq_SET(seq, pos++, s);
        }
    }
    else {
        for (i = 2; i < (NCH(n) - 1); i++) {
            ch = CHILD(n, i);
            REQ(ch, stmt);
            num = num_stmts(ch);
            if (num == 1) {

                s = ast_for_stmt(c, ch);
                if (!s)
                    return NULL;
                asdl_seq_SET(seq, pos++, s);
            }
            else {
                int j;
                ch = CHILD(ch, 0);
                REQ(ch, simple_stmt);
                for (j = 0; j < NCH(ch); j += 2) {

                    if (NCH(CHILD(ch, j)) == 0) {
                        assert((j + 1) == NCH(ch));
                        break;
                    }
                    s = ast_for_stmt(c, CHILD(ch, j));
                    if (!s)
                        return NULL;
                    asdl_seq_SET(seq, pos++, s);
                }
            }
        }
    }
    assert(pos == seq->size);
    return seq;
}

static stmt_ty
ast_for_if_stmt(struct compiling *c, const node *n)
{

    char *s;

    REQ(n, if_stmt);

    if (NCH(n) == 4) {
        expr_ty expression;
        asdl_seq *suite_seq;

        expression = ast_for_expr(c, CHILD(n, 1));
        if (!expression)
            return NULL;
        suite_seq = ast_for_suite(c, CHILD(n, 3));
        if (!suite_seq)
            return NULL;

        return If(expression, suite_seq, NULL, LINENO(n), n->n_col_offset,
                  c->c_arena);
    }

    s = STR(CHILD(n, 4));

    if (s[2] == 's') {
        expr_ty expression;
        asdl_seq *seq1, *seq2;

        expression = ast_for_expr(c, CHILD(n, 1));
        if (!expression)
            return NULL;
        seq1 = ast_for_suite(c, CHILD(n, 3));
        if (!seq1)
            return NULL;
        seq2 = ast_for_suite(c, CHILD(n, 6));
        if (!seq2)
            return NULL;

        return If(expression, seq1, seq2, LINENO(n), n->n_col_offset,
                  c->c_arena);
    }
    else if (s[2] == 'i') {
        int i, n_elif, has_else = 0;
        expr_ty expression;
        asdl_seq *suite_seq;
        asdl_seq *orelse = NULL;
        n_elif = NCH(n) - 4;

        if (TYPE(CHILD(n, (n_elif + 1))) == NAME
            && STR(CHILD(n, (n_elif + 1)))[2] == 's') {
            has_else = 1;
            n_elif -= 3;
        }
        n_elif /= 4;

        if (has_else) {
            asdl_seq *suite_seq2;

            orelse = asdl_seq_new(1, c->c_arena);
            if (!orelse)
                return NULL;
            expression = ast_for_expr(c, CHILD(n, NCH(n) - 6));
            if (!expression)
                return NULL;
            suite_seq = ast_for_suite(c, CHILD(n, NCH(n) - 4));
            if (!suite_seq)
                return NULL;
            suite_seq2 = ast_for_suite(c, CHILD(n, NCH(n) - 1));
            if (!suite_seq2)
                return NULL;

            asdl_seq_SET(orelse, 0,
                         If(expression, suite_seq, suite_seq2,
                            LINENO(CHILD(n, NCH(n) - 6)),
                            CHILD(n, NCH(n) - 6)->n_col_offset,
                            c->c_arena));

            n_elif--;
        }

        for (i = 0; i < n_elif; i++) {
            int off = 5 + (n_elif - i - 1) * 4;
            asdl_seq *newobj = asdl_seq_new(1, c->c_arena);
            if (!newobj)
                return NULL;
            expression = ast_for_expr(c, CHILD(n, off));
            if (!expression)
                return NULL;
            suite_seq = ast_for_suite(c, CHILD(n, off + 2));
            if (!suite_seq)
                return NULL;

            asdl_seq_SET(newobj, 0,
                         If(expression, suite_seq, orelse,
                            LINENO(CHILD(n, off)),
                            CHILD(n, off)->n_col_offset, c->c_arena));
            orelse = newobj;
        }
        expression = ast_for_expr(c, CHILD(n, 1));
        if (!expression)
            return NULL;
        suite_seq = ast_for_suite(c, CHILD(n, 3));
        if (!suite_seq)
            return NULL;
        return If(expression, suite_seq, orelse,
                  LINENO(n), n->n_col_offset, c->c_arena);
    }

    PyErr_Format(PyExc_SystemError,
                 "unexpected token in 'if' statement: %s", s);
    return NULL;
}

static stmt_ty
ast_for_while_stmt(struct compiling *c, const node *n)
{

    REQ(n, while_stmt);

    if (NCH(n) == 4) {
        expr_ty expression;
        asdl_seq *suite_seq;

        expression = ast_for_expr(c, CHILD(n, 1));
        if (!expression)
            return NULL;
        suite_seq = ast_for_suite(c, CHILD(n, 3));
        if (!suite_seq)
            return NULL;
        return While(expression, suite_seq, NULL, LINENO(n), n->n_col_offset,
                     c->c_arena);
    }
    else if (NCH(n) == 7) {
        expr_ty expression;
        asdl_seq *seq1, *seq2;

        expression = ast_for_expr(c, CHILD(n, 1));
        if (!expression)
            return NULL;
        seq1 = ast_for_suite(c, CHILD(n, 3));
        if (!seq1)
            return NULL;
        seq2 = ast_for_suite(c, CHILD(n, 6));
        if (!seq2)
            return NULL;

        return While(expression, seq1, seq2, LINENO(n), n->n_col_offset,
                     c->c_arena);
    }

    PyErr_Format(PyExc_SystemError,
                 "wrong number of tokens for 'while' statement: %d",
                 NCH(n));
    return NULL;
}

static stmt_ty
ast_for_for_stmt(struct compiling *c, const node *n)
{
    asdl_seq *_target, *seq = NULL, *suite_seq;
    expr_ty expression;
    expr_ty target, first;
    const node *node_target;

    REQ(n, for_stmt);

    if (NCH(n) == 9) {
        seq = ast_for_suite(c, CHILD(n, 8));
        if (!seq)
            return NULL;
    }

    node_target = CHILD(n, 1);
    _target = ast_for_exprlist(c, node_target, Store);
    if (!_target)
        return NULL;

    first = (expr_ty)asdl_seq_GET(_target, 0);
    if (NCH(node_target) == 1)
        target = first;
    else
        target = Tuple(_target, Store, first->lineno, first->col_offset, c->c_arena);

    expression = ast_for_testlist(c, CHILD(n, 3));
    if (!expression)
        return NULL;
    suite_seq = ast_for_suite(c, CHILD(n, 5));
    if (!suite_seq)
        return NULL;

    return For(target, expression, suite_seq, seq, LINENO(n), n->n_col_offset,
               c->c_arena);
}

static excepthandler_ty
ast_for_except_clause(struct compiling *c, const node *exc, node *body)
{

    REQ(exc, except_clause);
    REQ(body, suite);

    if (NCH(exc) == 1) {
        asdl_seq *suite_seq = ast_for_suite(c, body);
        if (!suite_seq)
            return NULL;

        return ExceptHandler(NULL, NULL, suite_seq, LINENO(exc),
                             exc->n_col_offset, c->c_arena);
    }
    else if (NCH(exc) == 2) {
        expr_ty expression;
        asdl_seq *suite_seq;

        expression = ast_for_expr(c, CHILD(exc, 1));
        if (!expression)
            return NULL;
        suite_seq = ast_for_suite(c, body);
        if (!suite_seq)
            return NULL;

        return ExceptHandler(expression, NULL, suite_seq, LINENO(exc),
                             exc->n_col_offset, c->c_arena);
    }
    else if (NCH(exc) == 4) {
        asdl_seq *suite_seq;
        expr_ty expression;
        expr_ty e = ast_for_expr(c, CHILD(exc, 3));
        if (!e)
            return NULL;
        if (!set_context(c, e, Store, CHILD(exc, 3)))
            return NULL;
        expression = ast_for_expr(c, CHILD(exc, 1));
        if (!expression)
            return NULL;
        suite_seq = ast_for_suite(c, body);
        if (!suite_seq)
            return NULL;

        return ExceptHandler(expression, e, suite_seq, LINENO(exc),
                             exc->n_col_offset, c->c_arena);
    }

    PyErr_Format(PyExc_SystemError,
                 "wrong number of children for 'except' clause: %d",
                 NCH(exc));
    return NULL;
}

static stmt_ty
ast_for_try_stmt(struct compiling *c, const node *n)
{
    const int nch = NCH(n);
    int n_except = (nch - 3)/3;
    asdl_seq *body, *orelse = NULL, *finally = NULL;

    REQ(n, try_stmt);

    body = ast_for_suite(c, CHILD(n, 2));
    if (body == NULL)
        return NULL;

    if (TYPE(CHILD(n, nch - 3)) == NAME) {
        if (strcmp(STR(CHILD(n, nch - 3)), "finally") == 0) {
            if (nch >= 9 && TYPE(CHILD(n, nch - 6)) == NAME) {

                orelse = ast_for_suite(c, CHILD(n, nch - 4));
                if (orelse == NULL)
                    return NULL;
                n_except--;
            }

            finally = ast_for_suite(c, CHILD(n, nch - 1));
            if (finally == NULL)
                return NULL;
            n_except--;
        }
        else {

            orelse = ast_for_suite(c, CHILD(n, nch - 1));
            if (orelse == NULL)
                return NULL;
            n_except--;
        }
    }
    else if (TYPE(CHILD(n, nch - 3)) != except_clause) {
        ast_error(n, "malformed 'try' statement");
        return NULL;
    }

    if (n_except > 0) {
        int i;
        stmt_ty except_st;

        asdl_seq *handlers = asdl_seq_new(n_except, c->c_arena);
        if (handlers == NULL)
            return NULL;

        for (i = 0; i < n_except; i++) {
            excepthandler_ty e = ast_for_except_clause(c, CHILD(n, 3 + i * 3),
                                                       CHILD(n, 5 + i * 3));
            if (!e)
                return NULL;
            asdl_seq_SET(handlers, i, e);
        }

        except_st = TryExcept(body, handlers, orelse, LINENO(n),
                              n->n_col_offset, c->c_arena);
        if (!finally)
            return except_st;

        body = asdl_seq_new(1, c->c_arena);
        if (body == NULL)
            return NULL;
        asdl_seq_SET(body, 0, except_st);
    }

    assert(finally != NULL);
    return TryFinally(body, finally, LINENO(n), n->n_col_offset, c->c_arena);
}

static stmt_ty
ast_for_with_item(struct compiling *c, const node *n, asdl_seq *content)
{
    expr_ty context_expr, optional_vars = NULL;

    REQ(n, with_item);
    context_expr = ast_for_expr(c, CHILD(n, 0));
    if (!context_expr)
        return NULL;
    if (NCH(n) == 3) {
        optional_vars = ast_for_expr(c, CHILD(n, 2));

        if (!optional_vars) {
            return NULL;
        }
        if (!set_context(c, optional_vars, Store, n)) {
            return NULL;
        }
    }

    return With(context_expr, optional_vars, content, LINENO(n),
                n->n_col_offset, c->c_arena);
}

static stmt_ty
ast_for_with_stmt(struct compiling *c, const node *n)
{
    int i;
    stmt_ty ret;
    asdl_seq *inner;

    REQ(n, with_stmt);

    i = NCH(n) - 1;

    inner = ast_for_suite(c, CHILD(n, i));
    if (!inner)
        return NULL;

    for (;;) {
        i -= 2;
        ret = ast_for_with_item(c, CHILD(n, i), inner);
        if (!ret)
            return NULL;

        if (i == 1)
            break;

        inner = asdl_seq_new(1, c->c_arena);
        if (!inner)
            return NULL;
        asdl_seq_SET(inner, 0, ret);
    }

    return ret;
}

static stmt_ty
ast_for_classdef(struct compiling *c, const node *n, asdl_seq *decorator_seq)
{

    PyObject *classname;
    asdl_seq *bases, *s;

    REQ(n, classdef);

    if (!forbidden_check(c, n, STR(CHILD(n, 1))))
            return NULL;

    if (NCH(n) == 4) {
        s = ast_for_suite(c, CHILD(n, 3));
        if (!s)
            return NULL;
        classname = NEW_IDENTIFIER(CHILD(n, 1));
        if (!classname)
            return NULL;
        return ClassDef(classname, NULL, s, decorator_seq, LINENO(n),
                        n->n_col_offset, c->c_arena);
    }

    if (TYPE(CHILD(n,3)) == RPAR) {
        s = ast_for_suite(c, CHILD(n,5));
        if (!s)
            return NULL;
        classname = NEW_IDENTIFIER(CHILD(n, 1));
        if (!classname)
            return NULL;
        return ClassDef(classname, NULL, s, decorator_seq, LINENO(n),
                        n->n_col_offset, c->c_arena);
    }

    bases = ast_for_class_bases(c, CHILD(n, 3));
    if (!bases)
        return NULL;

    s = ast_for_suite(c, CHILD(n, 6));
    if (!s)
        return NULL;
    classname = NEW_IDENTIFIER(CHILD(n, 1));
    if (!classname)
        return NULL;
    return ClassDef(classname, bases, s, decorator_seq,
                    LINENO(n), n->n_col_offset, c->c_arena);
}

static stmt_ty
ast_for_stmt(struct compiling *c, const node *n)
{
    if (TYPE(n) == stmt) {
        assert(NCH(n) == 1);
        n = CHILD(n, 0);
    }
    if (TYPE(n) == simple_stmt) {
        assert(num_stmts(n) == 1);
        n = CHILD(n, 0);
    }
    if (TYPE(n) == small_stmt) {
        n = CHILD(n, 0);

        switch (TYPE(n)) {
            case expr_stmt:
                return ast_for_expr_stmt(c, n);
            case print_stmt:
                return ast_for_print_stmt(c, n);
            case del_stmt:
                return ast_for_del_stmt(c, n);
            case pass_stmt:
                return Pass(LINENO(n), n->n_col_offset, c->c_arena);
            case flow_stmt:
                return ast_for_flow_stmt(c, n);
            case import_stmt:
                return ast_for_import_stmt(c, n);
            case global_stmt:
                return ast_for_global_stmt(c, n);
            case exec_stmt:
                return ast_for_exec_stmt(c, n);
            case assert_stmt:
                return ast_for_assert_stmt(c, n);
            default:
                PyErr_Format(PyExc_SystemError,
                             "unhandled small_stmt: TYPE=%d NCH=%d\n",
                             TYPE(n), NCH(n));
                return NULL;
        }
    }
    else {

        node *ch = CHILD(n, 0);
        REQ(n, compound_stmt);
        switch (TYPE(ch)) {
            case if_stmt:
                return ast_for_if_stmt(c, ch);
            case while_stmt:
                return ast_for_while_stmt(c, ch);
            case for_stmt:
                return ast_for_for_stmt(c, ch);
            case try_stmt:
                return ast_for_try_stmt(c, ch);
            case with_stmt:
                return ast_for_with_stmt(c, ch);
            case funcdef:
                return ast_for_funcdef(c, ch, NULL);
            case classdef:
                return ast_for_classdef(c, ch, NULL);
            case decorated:
                return ast_for_decorated(c, ch);
            default:
                PyErr_Format(PyExc_SystemError,
                             "unhandled small_stmt: TYPE=%d NCH=%d\n",
                             TYPE(n), NCH(n));
                return NULL;
        }
    }
}

static PyObject *
parsenumber(struct compiling *c, const char *s)
{
        const char *end;
        long x;
        double dx;
#ifndef WITHOUT_COMPLEX
        Py_complex complex;
        int imflag;
#endif

        assert(s != NULL);
        errno = 0;
        end = s + strlen(s) - 1;
#ifndef WITHOUT_COMPLEX
        imflag = *end == 'j' || *end == 'J';
#endif
        if (*end == 'l' || *end == 'L')
                return PyLong_FromString((char *)s, (char **)0, 0);
        x = PyOS_strtol((char *)s, (char **)&end, 0);
        if (*end == '\0') {
                if (errno != 0)
                        return PyLong_FromString((char *)s, (char **)0, 0);
                return PyInt_FromLong(x);
        }

#ifndef WITHOUT_COMPLEX
        if (imflag) {
                complex.real = 0.;
                complex.imag = PyOS_string_to_double(s, (char **)&end, NULL);
                if (complex.imag == -1.0 && PyErr_Occurred())
                        return NULL;
                return PyComplex_FromCComplex(complex);
        }
        else
#endif
        {
                dx = PyOS_string_to_double(s, NULL, NULL);
                if (dx == -1.0 && PyErr_Occurred())
                        return NULL;
                return PyFloat_FromDouble(dx);
        }
}

static PyObject *
decode_utf8(struct compiling *c, const char **sPtr, const char *end, char* encoding)
{
#ifndef Py_USING_UNICODE
        Py_FatalError("decode_utf8 should not be called in this build.");
        return NULL;
#else
        PyObject *u, *v;
        char *s, *t;
        t = s = (char *)*sPtr;

        while (s < end && (*s & 0x80)) s++;
        *sPtr = s;
        u = PyUnicode_DecodeUTF8(t, s - t, NULL);
        if (u == NULL)
                return NULL;
        v = PyUnicode_AsEncodedString(u, encoding, NULL);
        Py_DECREF(u);
        return v;
#endif
}

#ifdef Py_USING_UNICODE
static PyObject *
decode_unicode(struct compiling *c, const char *s, size_t len, int rawmode, const char *encoding)
{
        PyObject *v;
        PyObject *u = NULL;
        char *buf;
        char *p;
        const char *end;
        if (encoding != NULL && strcmp(encoding, "iso-8859-1")) {

                if (len > PY_SIZE_MAX / 6)
                        return NULL;

                u = PyString_FromStringAndSize((char *)NULL, len * 6);
                if (u == NULL)
                        return NULL;
                p = buf = PyString_AsString(u);
                end = s + len;
                while (s < end) {
                        if (*s == '\\') {
                                *p++ = *s++;
                                if (*s & 0x80) {
                                        strcpy(p, "u005c");
                                        p += 5;
                                }
                        }
                        if (*s & 0x80) {
                                PyObject *w;
                                char *r;
                                Py_ssize_t rn, i;
                                w = decode_utf8(c, &s, end, "utf-32-be");
                                if (w == NULL) {
                                        Py_DECREF(u);
                                        return NULL;
                                }
                                r = PyString_AsString(w);
                                rn = PyString_Size(w);
                                assert(rn % 4 == 0);
                                for (i = 0; i < rn; i += 4) {
                                        sprintf(p, "\\U%02x%02x%02x%02x",
                                                r[i + 0] & 0xFF,
                                                r[i + 1] & 0xFF,
                                                r[i + 2] & 0xFF,
                                                r[i + 3] & 0xFF);
                                        p += 10;
                                }
                                Py_DECREF(w);
                        } else {
                                *p++ = *s++;
                        }
                }
                len = p - buf;
                s = buf;
        }
        if (rawmode)
                v = PyUnicode_DecodeRawUnicodeEscape(s, len, NULL);
        else
                v = PyUnicode_DecodeUnicodeEscape(s, len, NULL);
        Py_XDECREF(u);
        return v;
}
#endif

static PyObject *
parsestr(struct compiling *c, const node *n, const char *s)
{
        size_t len, i;
        int quote = Py_CHARMASK(*s);
        int rawmode = 0;
        int need_encoding;
        int unicode = c->c_future_unicode;
        int bytes = 0;

        if (isalpha(quote) || quote == '_') {
                if (quote == 'u' || quote == 'U') {
                        quote = *++s;
                        unicode = 1;
                }
                if (quote == 'b' || quote == 'B') {
                        quote = *++s;
                        unicode = 0;
                        bytes = 1;
                }
                if (quote == 'r' || quote == 'R') {
                        quote = *++s;
                        rawmode = 1;
                }
        }
        if (quote != '\'' && quote != '\"') {
                PyErr_BadInternalCall();
                return NULL;
        }
        s++;
        len = strlen(s);
        if (len > INT_MAX) {
                PyErr_SetString(PyExc_OverflowError,
                                "string to parse is too long");
                return NULL;
        }
        if (s[--len] != quote) {
                PyErr_BadInternalCall();
                return NULL;
        }
        if (len >= 4 && s[0] == quote && s[1] == quote) {
                s += 2;
                len -= 2;
                if (s[--len] != quote || s[--len] != quote) {
                        PyErr_BadInternalCall();
                        return NULL;
                }
        }
        if (Py_Py3kWarningFlag && bytes) {
            for (i = 0; i < len; i++) {
                if ((unsigned char)s[i] > 127) {
                    if (!ast_warn(c, n,
                        "non-ascii bytes literals not supported in 3.x"))
                        return NULL;
                    break;
                }
            }
        }
#ifdef Py_USING_UNICODE
        if (unicode || Py_UnicodeFlag) {
                return decode_unicode(c, s, len, rawmode, c->c_encoding);
        }
#endif
        need_encoding = (c->c_encoding != NULL &&
                         strcmp(c->c_encoding, "utf-8") != 0 &&
                         strcmp(c->c_encoding, "iso-8859-1") != 0);
        if (rawmode || strchr(s, '\\') == NULL) {
                if (need_encoding) {
#ifndef Py_USING_UNICODE

                        Py_FatalError(
                            "cannot deal with encodings in this build.");
#else
                        PyObject *v, *u = PyUnicode_DecodeUTF8(s, len, NULL);
                        if (u == NULL)
                                return NULL;
                        v = PyUnicode_AsEncodedString(u, c->c_encoding, NULL);
                        Py_DECREF(u);
                        return v;
#endif
                } else {
                        return PyString_FromStringAndSize(s, len);
                }
        }

        return PyString_DecodeEscape(s, len, NULL, unicode,
                                     need_encoding ? c->c_encoding : NULL);
}

static PyObject *
parsestrplus(struct compiling *c, const node *n)
{
        PyObject *v;
        int i;
        REQ(CHILD(n, 0), STRING);
        if ((v = parsestr(c, n, STR(CHILD(n, 0)))) != NULL) {

                for (i = 1; i < NCH(n); i++) {
                        PyObject *s;
                        s = parsestr(c, n, STR(CHILD(n, i)));
                        if (s == NULL)
                                goto onError;
                        if (PyString_Check(v) && PyString_Check(s)) {
                                PyString_ConcatAndDel(&v, s);
                                if (v == NULL)
                                    goto onError;
                        }
#ifdef Py_USING_UNICODE
                        else {
                                PyObject *temp = PyUnicode_Concat(v, s);
                                Py_DECREF(s);
                                Py_DECREF(v);
                                v = temp;
                                if (v == NULL)
                                    goto onError;
                        }
#endif
                }
        }
        return v;

 onError:
        Py_XDECREF(v);
        return NULL;
}
