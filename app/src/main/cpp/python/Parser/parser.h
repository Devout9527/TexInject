#ifndef Py_PARSER_H
#define Py_PARSER_H
#ifdef __cplusplus
extern "C" {
#endif

#define MAXSTACK 1500

typedef struct {
	int		 s_state;
	dfa		*s_dfa;
	struct _node	*s_parent;
} stackentry;

typedef struct {
	stackentry	*s_top;
	stackentry	 s_base[MAXSTACK];

} stack;

typedef struct {
	stack	 	p_stack;
	grammar		*p_grammar;
	node		*p_tree;
#ifdef PY_PARSER_REQUIRES_FUTURE_KEYWORD
	unsigned long	p_flags;
#endif
} parser_state;

parser_state *PyParser_New(grammar *g, int start);
void PyParser_Delete(parser_state *ps);
int PyParser_AddToken(parser_state *ps, int type, char *str, int lineno, int col_offset,
                      int *expected_ret);
void PyGrammar_AddAccelerators(grammar *g);

#ifdef __cplusplus
}
#endif
#endif
