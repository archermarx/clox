// System includes
#include <stdlib.h>
#include <stdio.h>

// My includes
#include "memory.h"
#include "chunk.h"
#include "vm.h"

Chunk new_chunk (void) {
  Chunk chunk;
  init_chunk(&chunk);
  return chunk;
}

void init_chunk (Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  init_value_array(&chunk->constants);
}

void write_chunk (Chunk* chunk, uint8_t byte, LineNumber line) {
  // Check if we have sufficient memory allocated for this chunk
  if (chunk->count >= chunk->capacity) {
    // We do not have enough memory ( chunk->count = chunk->capacity ). Allocate new memory.
    size_t current_capacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(chunk->capacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code, current_capacity, chunk->capacity);
    chunk->lines = GROW_ARRAY(LineNumber, chunk->lines, current_capacity, chunk->capacity);
  }

  // we have enough memory. Add this byte to the end and increment the count
  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;

  return;
}

size_t add_constant (Chunk* chunk, Value value) {
  push(value);
  write_value_array(&chunk->constants, value);
  pop();
  return chunk->constants.count - 1;
}

void free_chunk (Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(LineNumber, chunk->lines, chunk->capacity);
  free_value_array(&chunk->constants);
  init_chunk(chunk);
}
