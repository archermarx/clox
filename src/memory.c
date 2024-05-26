#include <stdlib.h>

#include "common.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"
#include "compiler.h"

#ifdef CLOX_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

void* reallocate (void* ptr, size_t old_size, size_t new_size) {
  // increment the number of bytes we have allocated
  vm.bytes_allocated += new_size - old_size;

#ifdef CLOX_STRESS_GC
  if (new_size > old_size) { collect_garbage(); }
#endif

  // GC triggered when we exceed our chosen threshold
  if (vm.bytes_allocated > vm.next_gc) { collect_garbage(); }

  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  void* result = realloc(ptr, new_size);
  if (result == NULL) exit(1);
  return result;
}

void mark_object (LoxObj* object) {
  if (object == NULL) return;
  if (object->is_marked) return;
  object->is_marked = true;

  // add object to gray stack
  if (vm.gray_capacity < vm.gray_count + 1) {
    vm.gray_capacity = GROW_CAPACITY(vm.gray_capacity);
    vm.gray_stack = (LoxObj**)realloc(vm.gray_stack, sizeof(LoxObj*) * vm.gray_capacity);
    if (vm.gray_stack == NULL) exit(1);
  }
  vm.gray_stack[vm.gray_count] = object;
  vm.gray_count += 1;

#ifdef CLOX_LOG_GC
  printf("%p mark ", (void*)object);
  print_value(OBJ_VAL(object));
  printf("\n");
#endif
}

void mark_value (Value value) {
  if (IS_OBJ(value)) mark_object(AS_OBJ(value));
}

static void mark_array (ValueArray* array) {
  for (size_t i = 0; i < array->count; i++) { mark_value(array->values[i]); }
}

static void blacken_object (LoxObj* object) {
#ifdef CLOX_LOG_GC
  printf("%p blacken ", (void*)object);
  print_value(OBJ_VAL(object));
  printf("\n");
#endif
  switch (object->type) {
    case LOX_BOUND_METHOD_T: {
      LoxBoundMethod* bound = (LoxBoundMethod*)object;
      mark_value(bound->receiver);
      mark_object((LoxObj*)bound->method);
      break;
    }
    case LOX_CLASS_T: {
      LoxClass* cl = (LoxClass*)object;
      mark_object((LoxObj*)cl->name);
      mark_table(&cl->methods);
      break;
    }
    case LOX_CLOSURE_T: {
      LoxClosure* closure = (LoxClosure*)object;
      mark_object((LoxObj*)closure->function);
      if (closure->upvalues == NULL) break;
      for (int i = 0; i < closure->upvalue_count; i++) { mark_object((LoxObj*)closure->upvalues[i]); }
      break;
    }
    case LOX_FUNCTION_T: {
      LoxFunction* function = (LoxFunction*)object;
      mark_object((LoxObj*)function->name);
      mark_array(&function->chunk.constants);
      break;
    }
    case LOX_INSTANCE_T:{
      LoxInstance* instance = (LoxInstance*)object;
      mark_object((LoxObj*)instance->klass);
      mark_table(&instance->fields);
      break;
    }
    case LOX_UPVALUE_T: mark_value(((LoxUpvalue*)object)->closed); break;
    case LOX_NATIVE_T:
    case LOX_STRING_T: break;
  }
}

void free_object (LoxObj* object) {
#ifdef CLOX_LOG_GC
  printf("%p free type ", (void*)object);
  print_object_type(object->type);
  printf("\n");
#endif
  switch (object->type) {
    case LOX_BOUND_METHOD_T: {
      FREE(LoxBoundMethod, object);
      break;
    }
    case LOX_CLASS_T: {
      LoxClass* klass = (LoxClass*)object;
      free_table(&klass->methods);
      FREE(LoxClass, object);
      break;
    }
    case LOX_CLOSURE_T: {
      LoxClosure* closure = (LoxClosure*)object;
#ifdef CLOX_LOG_GC
      printf("freeing closure ");
      print_value(OBJ_VAL(closure));
      printf("\n");
#endif
      FREE_ARRAY(LoxUpvalue*, closure->upvalues, (size_t)closure->upvalue_count);
      FREE(LoxClosure, object);
      break;
    }
    case LOX_FUNCTION_T: {
      LoxFunction* function = (LoxFunction*)object;
      free_chunk(&function->chunk);
      FREE(LoxFunction, function);
      break;
    }
    case LOX_INSTANCE_T: {
      LoxInstance *instance = (LoxInstance*)object;
      free_table(&instance->fields);
      FREE(LOX_INSTANCE_T, object);
      break;
    }
    case LOX_STRING_T: {
      LoxString* string = (LoxString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(LoxString, object);
      break;
    }
    case LOX_NATIVE_T: {
      FREE(LOX_NATIVE_T, object);
      break;
    }
    case LOX_UPVALUE_T: {
      FREE(LoxUpvalue, object);
      break;
    }
  }
}

static void mark_roots () {
  // Mark values on the stack
  for (Value* slot = vm.stack; slot < vm.stack_top; slot++) { mark_value(*slot); }

  // Mark closures in call frames
  for (int i = 0; i < vm.frame_count; i++) { mark_object((LoxObj*)vm.frames[i].closure); }

  // Mark open upvalues
  for (LoxUpvalue* upvalue = vm.open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
    mark_object((LoxObj*)upvalue);
  }

  // Mark global variables
  mark_table(&vm.globals);

  // Mark things accessible from the compiler
  mark_compiler_roots();

  // Mark init_string
  mark_object((LoxObj*)vm.init_string);
}

static void trace_references () {
  while (vm.gray_count > 0) {
    vm.gray_count -= 1;
    LoxObj* object = vm.gray_stack[vm.gray_count];
    blacken_object(object);
  }
}

static void sweep () {
  LoxObj* previous = NULL;
  LoxObj* object = vm.objects;
  while (object != NULL) {
    if (object->is_marked) {
      object->is_marked = false;
      previous = object;
      object = object->next;
    } else {
      LoxObj* unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }
      free_object(unreached);
    }
  }
}

void collect_garbage () {
#ifdef CLOX_LOG_GC
  printf("--gc begin\n");
  size_t before = vm.bytes_allocated();
#endif

  mark_roots();
  trace_references();
  table_remove_white(&vm.strings);
  sweep();

  vm.next_gc = vm.bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef CLOX_LOG_GC
  printf("--gc end\n");
  printf("  collected %zu bytes (from %zu to %zu), next at %zu B\n", before - vm.bytes_allocated, before,
         vm.bytes_allocated, v.next_gc);
#endif
}

void free_objects () {
  LoxObj* object = vm.objects;
  while (object != NULL) {
    LoxObj* next = object->next;
    free_object(object);
    object = next;
  }
  free(vm.gray_stack);
}
