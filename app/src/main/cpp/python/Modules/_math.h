double _Py_acosh(double x);
double _Py_asinh(double x);
double _Py_atanh(double x);
double _Py_expm1(double x);
double _Py_log1p(double x);

#ifdef HAVE_ACOSH
#define m_acosh acosh
#else

#define m_acosh _Py_acosh
#endif

#ifdef HAVE_ASINH
#define m_asinh asinh
#else

#define m_asinh _Py_asinh
#endif

#ifdef HAVE_ATANH
#define m_atanh atanh
#else

#define m_atanh _Py_atanh
#endif

#ifdef HAVE_EXPM1
#define m_expm1 expm1
#else

#define m_expm1 _Py_expm1
#endif

#define m_log1p _Py_log1p
