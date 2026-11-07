#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

int PyLoaderCompilePython27(const char *source, const char *filename,
                             unsigned char **output, size_t *length,
                             char **error);
int PyLoaderCompilePython3105(const char *source, const char *filename,
                               unsigned char **output, size_t *length,
                               char **error);
void PyLoaderFinalizePythonCompiler(void);
#ifdef __cplusplus
}
#endif
