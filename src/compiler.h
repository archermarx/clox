#ifndef clox_compiler_h
#define clox_compiler_h

#include <stdbool.h>
#include "chunk.h"
#include "object.h"

LoxFunction* compile (const char* source);
void mark_compiler_roots ();

#endif
