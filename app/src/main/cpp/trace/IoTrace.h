#ifndef TEXINJECT_IO_TRACE_H
#define TEXINJECT_IO_TRACE_H

#include <cstddef>

namespace IoTrace {

bool start();

bool installRedirect(const char* toPath);

bool installMirror(const char* root);

bool mirrorOn();

void stop();

bool running();

size_t count();

bool dump(const char* path);

const char* lastError();

}

#endif
