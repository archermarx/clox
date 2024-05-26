#ifndef clox_value_h
#define clox_value_h

#include <string.h>

#include "common.h"

typedef double LoxDouble;
typedef bool LoxBool;
typedef struct LoxObj LoxObj;
typedef struct LoxString LoxString;

#ifdef CLOX_ENABLE_NAN_BOXING

typedef uint64_t Value;

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN ((uint64_t)0x7ffc000000000000)

#define TAG_NIL 1    // 01
#define TAG_FALSE 2  // 10
#define TAG_TRUE 3   // 11

#define IS_BOOL(value) (((value) | 1) == TRUE_VAL)
#define IS_NIL(value) ((value == NIL_VAL))
#define IS_NUMBER(value) (((value) & QNAN) != QNAN)
#define IS_OBJ(value) (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(value) ((value) == TRUE_VAL)
#define AS_NUMBER(value) value_to_num(value)
#define AS_OBJ(value) ((LoxObj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(b) ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL ((Value)(uint64_t)(QNAN | TAG_NIL))
#define NUMBER_VAL(num) num_to_value(num)
#define OBJ_VAL(obj) \
  (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

static inline Value num_to_value (double num) {
  Value value;
  memcpy(&value, &num, sizeof(double));
  return value;
}

static inline double value_to_num (Value value) {
  double num;
  memcpy(&num, &value, sizeof(Value));
  return num;
}

#else

typedef enum { NUMBER_VAL_T, BOOL_VAL_T, NIL_VAL_T, LOX_OBJ_T } LOX_TYPE;

typedef struct {
  LOX_TYPE type;
  union {
    LoxDouble number;
    LoxBool boolean;
    LoxObj* obj;
  };
} Value;

#define BOOL_VAL(value) ((Value){ .type = BOOL_VAL_T, .boolean = value })
#define NIL_VAL ((Value){ .type = NIL_VAL_T, .number = 0 })
#define NUMBER_VAL(value) ((Value){ .type = NUMBER_VAL_T, .number = value })
#define OBJ_VAL(object) ((Value){ .type = LOX_OBJ_T, .obj = (LoxObj*)object })

#define AS_NUMBER(value) ((value).number)
#define AS_BOOL(value) ((value).boolean)
#define AS_OBJ(value) ((value).obj)

#define IS_NUMBER(value) (value.type == NUMBER_VAL_T)
#define IS_BOOL(value) (value.type == BOOL_VAL_T)
#define IS_NIL(value) (value.type == NIL_VAL_T)
#define IS_OBJ(value) (value.type == LOX_OBJ_T)

#endif

bool values_equal (Value a, Value b);

typedef struct ValueArray {
  size_t capacity;
  size_t count;
  Value* values;
} ValueArray;

ValueArray new_value_array ();
void init_value_array (ValueArray* array);
void write_value_array (ValueArray* array, Value value);
void free_value_array (ValueArray* array);

void print_value (Value value);

#endif