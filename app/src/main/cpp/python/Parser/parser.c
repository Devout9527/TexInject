#include "Python.h"
#include "pgenheaders.h"
#include "token.h"
#include "grammar.h"
#include "node.h"
#include "parser.h"
#include "errcode.h"

#ifdef Py_DEBUG
extern int Py_DebugFlag;
#define D(x) if (!Py_DebugFlag); else x
#else
#define D(x)
#endif

static void s_reset(stack *);

static void
s_reset(stack *s)
{
    s->s_top = &s->s_base[MAXSTACK];
}

#define s_empty(s) ((s)->s_top == &(s)->s_base[MAXSTACK])

static int
s_push(register stack *s, dfa *d, node *parent)
{
    register stackentry *top;
    if (s->s_top == s->s_base) {
        fprintf(stderr, "s_push: parser stack overflow\n");
        return E_NOMEM;
    }
    top = --s->s_top;
    top->s_dfa = d;
    top->s_parent = parent;
    top->s_state = 0;
    return 0;
}

#ifdef Py_DEBUG

static void
s_pop(register stack *s)
{
    if (s_empty(s))
        Py_FatalError("s_pop: parser stack underflow -- FATAL");
    s->s_top++;
}

#else

#define s_pop(s) (s)->s_top++

#endif

parser_state *
PyParser_New(grammar *g, int start)
{
    parser_state *ps;

    if (!g->g_accel)
        PyGrammar_AddAccelerators(g);
    ps = (parser_state *)PyMem_MALLOC(sizeof(parser_state));
    if (ps == NULL)
        return NULL;
    ps->p_grammar = g;
#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
    ps->p_flags = 0;
#endif
    ps->p_tree = PyNode_New(start);
    if (ps->p_tree == NULL) {
        PyMem_FREE(ps);
        return NULL;
    }
    s_reset(&ps->p_stack);
    (void) s_push(&ps->p_stack, PyGrammar_FindDFA(g, start), ps->p_tree);
    return ps;
}

void
PyParser_Delete(parser_state *ps)
{

    PyNode_Free(ps->p_tree);
    PyMem_FREE(ps);
}

static int
shift(register stack *s, int type, char *str, int newstate, int lineno, int col_offset)
{
    int err;
    assert(!s_empty(s));
    err = PyNode_AddChild(s->s_top->s_parent, type, str, lineno, col_offset);
    if (err)
        return err;
    s->s_top->s_state = newstate;
    return 0;
}

static int
push(register stack *s, int type, dfa *d, int newstate, int lineno, int col_offset)
{
    int err;
    register node *n;
    n = s->s_top->s_parent;
    assert(!s_empty(s));
    err = PyNode_AddChild(n, type, (char *)NULL, lineno, col_offset);
    if (err)
        return err;
    s->s_top->s_state = newstate;
    return s_push(s, d, CHILD(n, NCH(n)-1));
}

static int
classify(parser_state *ps, int type, char *str)
{
    grammar *g = ps->p_grammar;
    register int n = g->g_ll.ll_nlabels;

    if (type == NAME) {
        register char *s = str;
        register label *l = g->g_ll.ll_label;
        register int i;
        for (i = n; i > 0; i--, l++) {
            if (l->lb_type != NAME || l->lb_str == NULL ||
                l->lb_str[0] != s[0] ||
                strcmp(l->lb_str, s) != 0)
                continue;
#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
            if (ps->p_flags & CO_FUTURE_PRINT_FUNCTION &&
                s[0] == 'p' && strcmp(s, "print") == 0) {
                break;
            }
#endif
            D(printf("It's a keyword\n"));
            return n - i;
        }
    }

    {
        register label *l = g->g_ll.ll_label;
        register int i;
        for (i = n; i > 0; i--, l++) {
            if (l->lb_type == type && l->lb_str == NULL) {
                D(printf("It's a token we know\n"));
                return n - i;
            }
        }
    }

    D(printf("Illegal token\n"));
    return -1;
}

#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
static void
future_hack(parser_state *ps)
{
    node *n = ps->p_stack.s_top->s_parent;
    node *ch, *cch;
    int i;

    n = CHILD(n, 0);
    if (NCH(n) < 4)
        return;
    ch = CHILD(n, 0);
    if (STR(ch) == NULL || strcmp(STR(ch), "from") != 0)
        return;
    ch = CHILD(n, 1);
    if (NCH(ch) == 1 && STR(CHILD(ch, 0)) &&
        strcmp(STR(CHILD(ch, 0)), "__future__") != 0)
        return;
    ch = CHILD(n, 3);

    if (TYPE(ch) == STAR)
        return;
    if (TYPE(ch) == LPAR)
        ch = CHILD(n, 4);

    for (i = 0; i < NCH(ch); i += 2) {
        cch = CHILD(ch, i);
        if (NCH(cch) >= 1 && TYPE(CHILD(cch, 0)) == NAME) {
            char *str_ch = STR(CHILD(cch, 0));
            if (strcmp(str_ch, FUTURE_WITH_STATEMENT) == 0) {
                ps->p_flags |= CO_FUTURE_WITH_STATEMENT;
            } else if (strcmp(str_ch, FUTURE_PRINT_FUNCTION) == 0) {
                ps->p_flags |= CO_FUTURE_PRINT_FUNCTION;
            } else if (strcmp(str_ch, FUTURE_UNICODE_LITERALS) == 0) {
                ps->p_flags |= CO_FUTURE_UNICODE_LITERALS;
            }
        }
    }
}
#endif

int
PyParser_AddToken(register parser_state *ps, register int type, char *str,
                  int lineno, int col_offset, int *expected_ret)
{
    register int ilabel;
    int err;

    D(printf("Token %s/'%s' ... ", _PyParser_TokenNames[type], str));

    ilabel = classify(ps, type, str);
    if (ilabel < 0)
        return E_SYNTAX;

    for (;;) {

        register dfa *d = ps->p_stack.s_top->s_dfa;
        register state *s = &d->d_state[ps->p_stack.s_top->s_state];

        D(printf(" DFA '%s', state %d:",
            d->d_name, ps->p_stack.s_top->s_state));

        if (s->s_lower <= ilabel && ilabel < s->s_upper) {
            register int x = s->s_accel[ilabel - s->s_lower];
            if (x != -1) {
                if (x & (1<<7)) {

                    int nt = (x >> 8) + NT_OFFSET;
                    int arrow = x & ((1<<7)-1);
                    dfa *d1 = PyGrammar_FindDFA(
                        ps->p_grammar, nt);
                    if ((err = push(&ps->p_stack, nt, d1,
                        arrow, lineno, col_offset)) > 0) {
                        D(printf(" MemError: push\n"));
                        return err;
                    }
                    D(printf(" Push ...\n"));
                    continue;
                }

                if ((err = shift(&ps->p_stack, type, str,
                                x, lineno, col_offset)) > 0) {
                    D(printf(" MemError: shift.\n"));
                    return err;
                }
                D(printf(" Shift.\n"));

                while (s = &d->d_state
                                [ps->p_stack.s_top->s_state],
                    s->s_accept && s->s_narcs == 1) {
                    D(printf("  DFA '%s', state %d: "
                             "Direct pop.\n",
                             d->d_name,
                             ps->p_stack.s_top->s_state));
#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
                    if (d->d_name[0] == 'i' &&
                        strcmp(d->d_name,
                           "import_stmt") == 0)
                        future_hack(ps);
#endif
                    s_pop(&ps->p_stack);
                    if (s_empty(&ps->p_stack)) {
                        D(printf("  ACCEPT.\n"));
                        return E_DONE;
                    }
                    d = ps->p_stack.s_top->s_dfa;
                }
                return E_OK;
            }
        }

        if (s->s_accept) {
#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
            if (d->d_name[0] == 'i' &&
                strcmp(d->d_name, "import_stmt") == 0)
                future_hack(ps);
#endif

            s_pop(&ps->p_stack);
            D(printf(" Pop ...\n"));
            if (s_empty(&ps->p_stack)) {
                D(printf(" Error: bottom of stack.\n"));
                return E_SYNTAX;
            }
            continue;
        }

        D(printf(" Error.\n"));
        if (expected_ret) {
            if (s->s_lower == s->s_upper - 1) {

                *expected_ret = ps->p_grammar->
                    g_ll.ll_label[s->s_lower].lb_type;
            }
            else
                *expected_ret = -1;
        }
        return E_SYNTAX;
    }
}

#ifdef Py_DEBUG

void
dumptree(grammar *g, node *n)
{
    int i;

    if (n == NULL)
        printf("NIL");
    else {
        label l;
        l.lb_type = TYPE(n);
        l.lb_str = STR(n);
        printf("%s", PyGrammar_LabelRepr(&l));
        if (ISNONTERMINAL(TYPE(n))) {
            printf("(");
            for (i = 0; i < NCH(n); i++) {
                if (i > 0)
                    printf(",");
                dumptree(g, CHILD(n, i));
            }
            printf(")");
        }
    }
}

void
showtree(grammar *g, node *n)
{
    int i;

    if (n == NULL)
        return;
    if (ISNONTERMINAL(TYPE(n))) {
        for (i = 0; i < NCH(n); i++)
            showtree(g, CHILD(n, i));
    }
    else if (ISTERMINAL(TYPE(n))) {
        printf("%s", _PyParser_TokenNames[TYPE(n)]);
        if (TYPE(n) == NUMBER || TYPE(n) == NAME)
            printf("(%s)", STR(n));
        printf(" ");
    }
    else
        printf("? ");
}

void
printtree(parser_state *ps)
{
    if (Py_DebugFlag) {
        printf("Parse tree:\n");
        dumptree(ps->p_grammar, ps->p_tree);
        printf("\n");
        printf("Tokens:\n");
        showtree(ps->p_grammar, ps->p_tree);
        printf("\n");
    }
    printf("Listing:\n");
    PyNode_ListTree(ps->p_tree);
    printf("\n");
}

#endif
