#ifndef clox_memory_h
#define clox_memory_h
#include <stddef.h>

#include "value.h"
#define LOX_INITIAL_ALLOC_SIZE 8
#define LOX_ALLOC_GROW_FACTOR 3/2
#define GC_HEAP_GROW_FACTOR 3/2

#define GROW_CAPACITY(capacity) \
  ((capacity) < LOX_INITIAL_ALLOC_SIZE ? LOX_INITIAL_ALLOC_SIZE : (capacity * LOX_ALLOC_GROW_FACTOR))

// for faster hashing, tables use a grow factor of two
// this lets use bitmasking instead of the modulo operator
#define LOX_TABLE_GROW_FACTOR 2
#define GROW_TABLE_CAPACITY(capacity) \
  ((capacity) < LOX_INITIAL_ALLOC_SIZE ? LOX_INITIAL_ALLOC_SIZE : (capacity * LOX_TABLE_GROW_FACTOR))

void* reallocate (void* ptr, size_t old_size, size_t new_size);
void mark_object (LoxObj* obj);
void mark_value (Value value);
void collect_garbage ();
void free_object (LoxObj* object);
void free_objects ();

#define GROW_ARRAY(type, pointer, old_count, new_count) \
  (type*)reallocate(pointer, sizeof(type) * (old_count), sizeof(type) * (new_count))

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

#define FREE_ARRAY(type, pointer, old_count) reallocate(pointer, sizeof(type) * (old_count), 0)

#define ALLOCATE(type, count) (type*)reallocate(NULL, 0, sizeof(type) * count);

#endif
