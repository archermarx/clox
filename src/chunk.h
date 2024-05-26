#ifndef clox_chunk_h
#define clox_chunk_h

// System includes
#include <stdint.h>
#include <stddef.h>

// My includes
#include "value.h"

typedef int LineNumber;

typedef enum {
  OP_RETURN,         // return from the program.
  OP_TRUE,           // push "true" onto the stack.
  OP_FALSE,          // push "false" onto the stack.
  OP_NIL,            // push "nil" onto the stack.
  OP_CONSTANT,       // load a constant with a 1-byte index.
  OP_DEFINE_GLOBAL,  // Define a global with a 1-byte index.
  OP_GET_GLOBAL,     // Retrieve a global with a 1-byte index.
  OP_SET_GLOBAL,     // Set a global with a 1-byte index.
  OP_GET_LOCAL,      // Retrieve a local with a 1-byte index.
  OP_SET_LOCAL,      // Set a local with a 1-byte index.
  OP_GET_UPVALUE,    // Retrieve a upvalue with a 1-byte index.
  OP_SET_UPVALUE,    // Set a upvalue with a 1-byte index.
  OP_GET_PROPERTY,   // Get a class property
  OP_SET_PROPERTY,   // Set a class property
  OP_CLOSE_UPVALUE,  // close over an upvalue, popping it from the stack and moving it to the heap
  OP_NOT,            // pop the top value from the stack, and push `true` if the value is truthy, `false` otherwise.
  OP_NEGATE,         // negate the top values on the stack.
  OP_ADD,            // pop the top two values from the stack, add them, and push the result to the stack.
  OP_SUB,  // pop the top two values from the stack, subtract the first from the second, and push the result to the stack.
  OP_MUL,  // pop the top two values from the stack, multiply them, and push the result to the stack.
  OP_DIV,  // pop the top two values from the stack, divide the second by the first, and push the result to the stack.
  OP_GREATER,  // pop the top two values from the stack, check if the second is greater than the first, and push the
               // result to the stack.
  OP_LESS,  // pop the top two values from the stack, check if the second is less than the first, and push the result to
            // the stack.
  OP_EQUAL,          // pop the top two values from the stack, check if they're equal, and push the result to the stack
  OP_POP,            // pop the top value off the stack and forget it
  OP_JUMP,           // jump to the operand (2 byte)
  OP_JUMP_IF_FALSE,  // jump to the operand (2 byte) if false
  OP_LOOP,           // unconditionally jump backward by a given offset
  OP_CALL,           // call a function (opcode is number of arguments)
  OP_CLOSURE,        // create a new closure
  OP_CLASS,          // create a new class
  OP_METHOD,         // create a new method
  OP_INVOKE,         // Invoke a method
  OP_INHERIT,        // Inherit from a class
  OP_GET_SUPER,      // Get a superclass's method
  OP_INVOKE_SUPER,   // Invoke a superclass's method
} OpCode;

/*
 *  Type storing a chunk of opcodes to be evaluated.
 */
typedef struct {
  size_t count;
  size_t capacity;
  uint8_t* code;
  // TODO: better encoding for line numbers
  LineNumber* lines;
  ValueArray constants;
} Chunk;

/**
 *  Return an (uninitialized) chunk of opcodes.
 */
Chunk new_chunk (void);

/**
 *  Initialize a chunk with zero size, zero capacity, a null data pointer and an empty values array.
 *  @param chunk a pointer to a Chunk
 */
void init_chunk (Chunk* chunk);

/**
 *  Append a byte of data to the chunk.
 *  @param chunk a pointer to the chunk we're appending to
 *  @param byte a byte of data to append
 */
void write_chunk (Chunk* chunk, uint8_t byte, LineNumber line);

/**
 *  Add a constant to the chunk's value array.
 *  @param chunk a pointer to a chunk
 *  @param value a Value to append
 *  @return index where constant was appended
 */
size_t add_constant (Chunk* chunk, Value value);

/**
 * Write an opcode with variable width.
 * @param chunk a pointer to the chunk.
 * @param index the index at which the value is written.
 * @param line  the line of source code this originated from.
 * @param OP_1  the 1-byte version of the opcode.
 * @param OP_2  the 2-byte version of the opcode.
 * @param OP_4  the 4-byte version of the opcode.
 */
void emit_variable_width_op (Chunk* chunk, int constant, LineNumber line, OpCode OP_1, OpCode OP_2, OpCode OP_4);

/**
 *  Free memory allocated for chunk.
 *  @param chunk a pointer to the chunk.
 */
void free_chunk (Chunk* chunk);

#endif
