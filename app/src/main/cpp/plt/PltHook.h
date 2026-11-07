#ifndef TEXINJECT_PLT_HOOK_H
#define TEXINJECT_PLT_HOOK_H

#include <cstdint>
#include <string>
#include <vector>

namespace Plt {

struct Entry {
    std::string name;
    void** slot;
    void* orig;
};

bool init();

bool ready();

int importCount();

void** find(const char* symbol);

void* replace(const char* symbol, void* replacement);

bool restore(const char* symbol);

const char* lastError();

std::vector<std::string> allNames();

}

#endif
