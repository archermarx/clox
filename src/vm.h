#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_MAX)

typedef struct CallFrame {
  LoxClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct VM {
  CallFrame frames[FRAMES_MAX];
  int frame_count;
  Value stack[STACK_MAX];
  Value* stack_top;
  Table strings;
  LoxString* init_string;
  Table globals;
  LoxUpvalue* open_upvalues;
  size_t bytes_allocated;
  size_t next_gc;
  LoxObj* objects;
  size_t gray_count;
  size_t gray_capacity;
  LoxObj** gray_stack;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void init_vm ();
void free_vm ();
InterpretResult interpret (const char* source);
void push (Value value);
Value pop (void);

#endif
