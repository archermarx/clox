#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "table.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, object_type) (type*)allocate_object(sizeof(type), object_type)

LoxObj* allocate_object (size_t size, ObjType type) {
  LoxObj* object = (LoxObj*)reallocate(NULL, 0, size);
  object->type = type;
  object->is_marked = false;

  // add object to linked list
  object->next = vm.objects;
  vm.objects = object;

#ifdef CLOX_LOG_GC
  printf("%p allocate %zu for ", (void*)object, size);
  print_object_type(object->type);
  printf("\n");
#endif
  return object;
}

LoxBoundMethod* new_bound_method (Value receiver, LoxClosure* method) {
  LoxBoundMethod *bound = ALLOCATE_OBJ(LoxBoundMethod, LOX_BOUND_METHOD_T);
  bound->receiver = receiver;
  bound->method = method;
  return bound;
}

LoxClass* new_class (LoxString* name) {
  LoxClass* klass = ALLOCATE_OBJ(LoxClass, LOX_CLASS_T); 
  klass->name = name;
  init_table(&klass->methods);
  return klass;
}

LoxClosure* new_closure (LoxFunction* function) {
  // this is kind of convoluted for reasons related to GC
  // We first allocate the closure and set its function
  LoxClosure* closure = ALLOCATE_OBJ(LoxClosure, LOX_CLOSURE_T);
  closure->function = function;
  closure->upvalue_count = function->upvalue_count;
  closure->upvalues = NULL;

  // We next need to create the closure's upvalues.
  // Allocations of any type can trigger GC, so we need to temporarily push
  // the closure onto the stack so the GC can find it and mark it as in use
  // otherwise, the GC will not be able to find this object and will collect it.
  push(OBJ_VAL(closure));

  // we now allocate the upvalues. this triggers GC, but since the closure is
  // on the stack, it can be found and will not be cleared
  LoxUpvalue** upvalues = ALLOCATE(LoxUpvalue*, (size_t)function->upvalue_count);
  closure->upvalues = upvalues;

  // Now that the closure is safe from collection, we pop it off the stack
  pop();

  // finally, set the upvalues to null. we fill fill them in later in the OP_CLOSURE
  for (int i = 0; i < function->upvalue_count; i++) { upvalues[i] = NULL; }

#ifdef CLOX_LOG_GC
  printf("allocated closure ");
  print_value(OBJ_VAL(closure));
  printf("\n");
#endif

  return closure;
}

LoxFunction* new_function () {
  LoxFunction* function = ALLOCATE_OBJ(LoxFunction, LOX_FUNCTION_T);
  function->arity = 0;
  function->upvalue_count = 0;
  function->name = NULL;
  init_chunk(&function->chunk);

#ifdef CLOX_LOG_GC
  printf("allocated function ");
  print_value(OBJ_VAL(function));
  printf("\n");
#endif

  return function;
}

LoxInstance* new_instance(LoxClass *klass) {
  LoxInstance *instance = ALLOCATE_OBJ(LoxInstance, LOX_INSTANCE_T);
  instance->klass = klass;
  init_table(&instance->fields);
  return instance;
}

LoxNative* new_native (NativeFn function) {
  LoxNative* native = ALLOCATE_OBJ(LoxNative, LOX_NATIVE_T);
  native->function = function;
  return native;
}

LoxString* allocate_string (char* chars, size_t length, uint32_t hash) {
  LoxString* string = ALLOCATE_OBJ(LoxString, LOX_STRING_T);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
#ifdef CLOX_LOG_GC
  printf("allocated string: %s\n", string->chars);
#endif

  // push onto stack so GC can find this string
  push(OBJ_VAL(string));
  // intern this string
  table_set(&vm.strings, string, NIL_VAL);
  // Pop the string from the stack
  pop();
  return string;
}

static uint32_t hash_string (const char* key, size_t length) {
  // FNV-1a hash
  uint32_t hash = 216613626u;
  for (size_t i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

LoxString* new_string (char* chars) { return take_string(chars, strlen(chars)); }

LoxString* take_string (char* chars, size_t length) {
  uint32_t hash = hash_string(chars, length);

  // check to see if we have already interned this string
  LoxString* interned = table_find_string(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    // Since this function takes ownership of the provided pointer, we are free to free it.
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }
  return allocate_string(chars, length, hash);
}

LoxString* copy_string (const char* chars, size_t length) {
  uint32_t hash = hash_string(chars, length);

  // check to see if we have already interned this string
  LoxString* interned = table_find_string(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;

  char* heap_chars = ALLOCATE(char, length + 1);
  memcpy(heap_chars, chars, length);
  heap_chars[length] = '\0';
  return allocate_string(heap_chars, length, hash);
}

LoxUpvalue* new_upvalue (Value* slot) {
  LoxUpvalue* upvalue = ALLOCATE_OBJ(LoxUpvalue, LOX_UPVALUE_T);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = NULL;
  return upvalue;
}

void print_function (LoxFunction* function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}

void print_object (Value value) {
  switch (OBJ_TYPE(value)) {
    case LOX_BOUND_METHOD_T: print_function(AS_BOUND_METHOD(value)->method->function); break;
    case LOX_CLASS_T: printf("%s", AS_CLASS(value)->name->chars);  break;
    case LOX_CLOSURE_T: print_function(AS_CLOSURE(value)->function); break;
    case LOX_FUNCTION_T: print_function(AS_FUNCTION(value)); break;
    case LOX_INSTANCE_T: printf("%s instance", AS_INSTANCE(value)->klass->name->chars);  break;
    case LOX_NATIVE_T: printf("<native fn>"); break;
    case LOX_STRING_T: printf("%s", AS_CSTRING(value)); break;
    case LOX_UPVALUE_T: printf("upvalue"); break;
    default: printf("unknown (type %d)", OBJ_TYPE(value)); break;
  }
}

void print_object_type (ObjType type) {
  switch (type) {
    case LOX_CLASS_T: printf("LOX_CLASS_T"); break;
    case LOX_CLOSURE_T: printf("LOX_CLOSURE_T"); break;
    case LOX_FUNCTION_T: printf("LOX_FUNCTION_T"); break;
    case LOX_INSTANCE_T: printf("LOX_INSTANCE_T"); break;
    case LOX_NATIVE_T:  printf("LOX_NATIVE_T"); break;
    case LOX_STRING_T:  printf("LOX_STRING_T"); break;
    case LOX_UPVALUE_T: printf("LOX_UPVALUE_T"); break;
    default: printf("UNKNOWN"); break;
  }
  printf(" (%d)", type);
}
