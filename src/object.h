#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) (is_obj_type(value, LOX_BOUND_METHOD_T))
#define AS_BOUND_METHOD(value) ((LoxBoundMethod*)AS_OBJ(value))

#define IS_CLASS(value) (is_obj_type(value, LOX_CLASS_T))
#define AS_CLASS(value) ((LoxClass*)AS_OBJ(value))

#define IS_CLOSURE(value) (is_obj_type(value, LOX_CLOSURE_T))
#define AS_CLOSURE(value) ((LoxClosure*)AS_OBJ(value))

#define IS_FUNCTION(value) (is_obj_type(value, LOX_FUNCTION_T))
#define AS_FUNCTION(value) ((LoxFunction*)AS_OBJ(value))

#define IS_INSTANCE(value) (is_obj_type(value, LOX_INSTANCE_T))
#define AS_INSTANCE(value) ((LoxInstance*)AS_OBJ(value))

#define IS_NATIVE(value) (is_obj_type(value, LOX_NATIVE_T))
#define AS_NATIVE(value) (((LoxNative*)AS_OBJ(value))->function)

#define IS_STRING(value) (is_obj_type(value, LOX_STRING_T))
#define AS_STRING(value) ((LoxString*)AS_OBJ(value))
#define AS_CSTRING(value) (((LoxString*)AS_OBJ(value))->chars)

typedef enum {
  LOX_BOUND_METHOD_T,
  LOX_CLASS_T,
  LOX_CLOSURE_T,
  LOX_FUNCTION_T,
  LOX_INSTANCE_T,
  LOX_STRING_T,
  LOX_UPVALUE_T,
  LOX_NATIVE_T,
} ObjType;

struct LoxObj {
  ObjType type;
  bool is_marked;
  struct LoxObj* next;
};

typedef struct LoxFunction {
  LoxObj obj;
  int arity;
  int upvalue_count;
  Chunk chunk;
  LoxString* name;
} LoxFunction;

typedef struct LoxUpvalue {
  LoxObj obj;
  Value* location;
  Value closed;
  struct LoxUpvalue* next;
} LoxUpvalue;

typedef Value (*NativeFn)(int arg_count, Value* args);

typedef struct LoxNative {
  LoxObj obj;
  NativeFn function;
} LoxNative;

struct LoxString {
  LoxObj obj;
  size_t length;
  char* chars;
  uint32_t hash;
};

typedef struct LoxClosure {
  LoxObj obj;
  LoxFunction* function;
  LoxUpvalue** upvalues;
  int upvalue_count;
} LoxClosure;

typedef struct LoxClass {
  LoxObj obj;
  LoxString* name;
  Table methods;
} LoxClass;

typedef struct LoxInstance {
  LoxObj obj;
  LoxClass* klass;
  Table fields;
} LoxInstance;

typedef struct LoxBoundMethod {
  LoxObj obj;
  Value receiver;
  LoxClosure* method;
} LoxBoundMethod;

LoxBoundMethod* new_bound_method(Value receiver, LoxClosure* method);
LoxClass* new_class (LoxString* name);
LoxClosure* new_closure (LoxFunction* function);
LoxFunction* new_function (void);
LoxInstance* new_instance (LoxClass* klass);
LoxNative* new_native (NativeFn function);
LoxString* new_string (char* chars);
LoxString* take_string (char* chars, size_t length);
LoxString* copy_string (const char* chars, size_t length);
LoxUpvalue* new_upvalue (Value* slot);

void print_object (Value value);
void print_object_type (ObjType type);

static inline bool is_obj_type (Value value, ObjType type) { return IS_OBJ(value) && AS_OBJ(value)->type == type; }

#endif
