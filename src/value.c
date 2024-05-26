// System headers
#include <stdio.h>
#include <string.h>

// My headers
#include "memory.h"
#include "value.h"
#include "object.h"

ValueArray new_value_array () { return (ValueArray){ 0 }; }

void init_value_array (ValueArray* array) {
  array->capacity = 0;
  array->count = 0;
  array->values = NULL;
}

void write_value_array (ValueArray* array, Value value) {
  // Check if we have sufficient memory allocated for this array
  if (array->count >= array->capacity) {
    // We do not have enough memory ( array->count = array->capacity ). Allocate new memory.
    size_t current_capacity = array->capacity;
    array->capacity = GROW_CAPACITY(array->capacity);
    array->values = GROW_ARRAY(Value, array->values, current_capacity, array->capacity);
  }

  // we have enough memory. Add this byte to the end and increment the count
  array->values[array->count++] = value;
  return;
}

void free_value_array (ValueArray* array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  init_value_array(array);
}

bool values_equal (Value a, Value b) {
#ifdef CLOX_ENABLE_NAN_BOXING
  if (IS_NUMBER(a) && IS_NUMBER(b)) {
    return AS_NUMBER(a) == AS_NUMBER(b);
  }
  return a == b;
#else
  if (a.type != b.type) return false;
  switch (a.type) {
    case BOOL_VAL_T: return AS_BOOL(a) == AS_BOOL(b);
    case NIL_VAL_T: return true;
    case NUMBER_VAL_T: return AS_NUMBER(a) == AS_NUMBER(b);
    case LOX_OBJ_T: {
      return AS_OBJ(a) == AS_OBJ(b);
    }
    default: return false;  // unreachable
  }
#endif
}

void print_value (Value value) {
#ifdef CLOX_ENABLE_NAN_BOXING
  if (IS_BOOL(value)) {
    printf(AS_BOOL(value) ? "true" : "false");
    return;
  } else if (IS_NIL(value)) {
    printf("nil");
    return;
  } else if (IS_NUMBER(value)) {
    printf("%.15g", AS_NUMBER(value));
    return;
  } else if (IS_OBJ(value)) {
    print_object(value);
    return;
  }
#else
  switch (value.type) {
    case NUMBER_VAL_T: printf("%.15g", value.number); return;
    case BOOL_VAL_T: printf("%s", value.boolean ? "true" : "false"); return;
    case NIL_VAL_T: printf("nil"); return;
    case LOX_OBJ_T: print_object(value); return;
  }
#endif
}
