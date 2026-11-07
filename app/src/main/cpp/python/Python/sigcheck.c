#include "Python.h"

int
PyErr_CheckSignals(void)
{
	if (!PyOS_InterruptOccurred())
		return 0;
	PyErr_SetNone(PyExc_KeyboardInterrupt);
	return -1;
}
