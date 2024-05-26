#include <stdio.h>
#include <stdarg.h>
#include "vm.h"
#include "object.h"
#include "string.h"
#include "memory.h"
#include "time.h"
#include "compiler.h"

#ifdef CLOX_TRACE_EXECUTION
#include "debug.h"
#endif

// global VM variable
VM vm;

static Value clock_native (int arg_count, Value* args) {
  (void)arg_count, (void)args;
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value print_native(int arg_count, Value* args) {
  for (int i = 0; i < arg_count; i++) {
    print_value(args[i]);
  }
  fflush(stdout);
  return NIL_VAL;
}

static Value println_native(int arg_count, Value* args) {
   for (int i = 0; i < arg_count; i++) {
    print_value(args[i]);
  }
  printf("\n");
  return NIL_VAL;
}

static void reset_stack () {
  vm.stack_top = vm.stack;
  vm.frame_count = 0;
  vm.open_upvalues = NULL;
}

static void runtime_error (const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  // print stacktrace
  for (int i = vm.frame_count - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    LoxFunction* function = frame->closure->function;
    size_t instruction = (size_t)(frame->ip - function->chunk.code - 1);
    fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }
  reset_stack();
}

static void define_native (const char* name, NativeFn function) {
  push(OBJ_VAL(copy_string(name, strlen(name))));
  push(OBJ_VAL(new_native(function)));
  table_set(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

void init_vm () {
  reset_stack();
  vm.objects = NULL;
  vm.bytes_allocated = 0;
  vm.next_gc = 1024 * 1024;  // 1 MB

  // gray stack initializes to empty
  vm.gray_count = 0;
  vm.gray_capacity = 0;
  vm.gray_stack = NULL;

  init_table(&vm.strings);

  // need to initialize to NULL before setting it to init for GC reasons
  vm.init_string = NULL;
  vm.init_string = copy_string("init", 4);


  // Define native functions
  init_table(&vm.globals);

  // `clock()`
  define_native("clock", clock_native);

  // `print(args...)`
  define_native("print", print_native);

  // `println(args...)`
  define_native("println", println_native);

}

void free_vm () {
  free_table(&vm.strings);
  free_table(&vm.globals);
  vm.init_string = NULL;
  free_objects();
}

void push (Value value) {
  *vm.stack_top = value;
  vm.stack_top++;
}

Value pop (void) {
  vm.stack_top--;
  return *vm.stack_top;
}

Value peek (int distance) { return vm.stack_top[-1 - distance]; }

static bool call (LoxClosure* closure, int arg_count) {
  if (arg_count != closure->function->arity) {
    runtime_error("Expected %d arguments but got %d.", closure->function->arity, arg_count);
    return false;
  }

  if (vm.frame_count == FRAMES_MAX) {
    runtime_error("Stack overflow.");
    return false;
  }
  CallFrame* frame = &vm.frames[vm.frame_count++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stack_top - arg_count - 1;
  return true;
}

static bool call_value (Value callee, int arg_count) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case LOX_BOUND_METHOD_T: {
        LoxBoundMethod* bound = AS_BOUND_METHOD(callee);
        vm.stack_top[-arg_count - 1] = bound->receiver;
        return call(bound->method, arg_count);
      }
      case LOX_CLASS_T: {
        LoxClass* klass = AS_CLASS(callee);
        vm.stack_top[-arg_count - 1] = OBJ_VAL(new_instance(klass));
        Value initializer;
        if (table_get(&klass->methods, vm.init_string, &initializer)) {
          return call(AS_CLOSURE(initializer), arg_count);
        } else if (arg_count != 0) {
          runtime_error("Exected 0 arguments but got %d.", arg_count);
        }
        return true;
      }
      case LOX_CLOSURE_T: return call(AS_CLOSURE(callee), arg_count);
      case LOX_NATIVE_T: {
        NativeFn native = AS_NATIVE(callee);
        Value result = native(arg_count, vm.stack_top - arg_count);
        vm.stack_top -= arg_count + 1;
        push(result);
        return true;
      }
      default: break;
    }
  }
  runtime_error("Can only call functions and classes.");
  return false;
}

static bool invoke_from_class (LoxClass* klass, LoxString* name, int arg_count) {
  Value method;
  if (!table_get(&klass->methods, name, &method)) {
    runtime_error("Undefined property '%s'.", name->chars);
    return false;
  }
  return call(AS_CLOSURE(method), arg_count);
}

static bool invoke (LoxString* name, int arg_count) {
  Value receiver = peek(arg_count);

  if (!IS_INSTANCE(receiver)) {
    runtime_error("Only instances have methods.");
    return false;
  }
  LoxInstance* instance = AS_INSTANCE(receiver);

  Value value;
  if (table_get(&instance->fields, name, &value)) {
    vm.stack_top[-arg_count - 1] = value;
    return call_value(value, arg_count);
  }
  return invoke_from_class(instance->klass, name, arg_count);
}

static bool bind_method (LoxClass* klass, LoxString* name) {
  Value method;
  if (!table_get(&klass->methods, name, &method)) {
    runtime_error("Undefined property '%s.%s'.", klass->name->chars, name->chars);
    return false;
  }

  LoxBoundMethod* bound = new_bound_method(peek(0), AS_CLOSURE(method));
  pop();
  push(OBJ_VAL(bound));
  return true;
}

static LoxUpvalue* capture_upvalue (Value* local) {
  LoxUpvalue* prev_upvalue = NULL;
  LoxUpvalue* upvalue = vm.open_upvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prev_upvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) { return upvalue; }

  LoxUpvalue* created_upvalue = new_upvalue(local);
  created_upvalue->next = upvalue;

  if (prev_upvalue == NULL) {
    vm.open_upvalues = created_upvalue;
  } else {
    prev_upvalue->next = created_upvalue;
  }
  return created_upvalue;
}

static void close_upvalues (Value* last) {
  while (vm.open_upvalues != NULL && vm.open_upvalues->location >= last) {
    LoxUpvalue* upvalue = vm.open_upvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.open_upvalues = upvalue->next;
  }
}

static void define_method (LoxString* name) {
  Value method = peek(0);
  LoxClass* klass = AS_CLASS(peek(1));
  table_set(&klass->methods, name, method);
  pop();  // pop the method off the stack
}

static bool is_falsey (Value value) { return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value)); }

static void concatenate () {
  LoxString* b = AS_STRING(peek(0));
  LoxString* a = AS_STRING(peek(1));
  size_t length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';
  LoxString* result = take_string(chars, length);
  // only pop these after the new string has been allocated
  // otherwise, GC will collect them too eagerly
  pop();
  pop();
  push(OBJ_VAL(result));
}

static InterpretResult run () {
  CallFrame* frame = &vm.frames[vm.frame_count - 1];

#define READ_BYTE() (frame->ip += 1, (uint8_t)(frame->ip[-1]))
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define VALUES (frame->closure->function->chunk.constants.values)
#define READ_CONSTANT() VALUES[READ_BYTE()]
#define READ_STRING() (AS_STRING(READ_CONSTANT()))
#define BINARY_OP(value_type, op)                     \
  do {                                                \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
      runtime_error("Operands must be numbers.");     \
      return INTERPRET_RUNTIME_ERROR;                 \
    }                                                 \
    LoxDouble b = AS_NUMBER(pop());                   \
    LoxDouble a = AS_NUMBER(pop());                   \
    push(value_type((a)op(b)));                       \
  } while (0)

  for (;;) {
#ifdef CLOX_TRACE_EXECUTION
    printf("        ");
    for (Value* slot = vm.stack; slot < vm.stack_top; slot++) {
      printf("[");
      print_value(*slot);
      printf("]");
    }
    printf("\n");
    disassemble_instruction(&frame->closure->function->chunk,
                            (size_t)(frame->ip - frame->closure->function->chunk.code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_RETURN: {
        Value result = pop();
        close_upvalues(frame->slots);
        vm.frame_count--;
        if (vm.frame_count == 0) {
          // we're at the end of the program
          pop();
          return INTERPRET_OK;
        }

        vm.stack_top = frame->slots;
        push(result);
        frame = &vm.frames[vm.frame_count - 1];
        break;
      }
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_NIL: push(NIL_VAL); break;
      case OP_CONSTANT: {
        Value val = READ_CONSTANT();
        push(val);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        LoxString* name = READ_STRING();
        table_set(&vm.globals, name, peek(0));
        pop();
      } break;
      case OP_GET_GLOBAL: {
        LoxString* name = READ_STRING();
        Value value;
        if (!table_get(&vm.globals, name, &value)) {
          runtime_error("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        LoxString* name = READ_STRING();
        if (table_set(&vm.globals, name, peek(0))) {
          table_delete(&vm.globals, name);
          runtime_error("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
        break;
      }
      case OP_GET_PROPERTY: {
        if (!IS_INSTANCE(peek(0))) {
          runtime_error("Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }
        LoxInstance* instance = AS_INSTANCE(peek(0));
        LoxString* name = READ_STRING();

        Value value;
        if (table_get(&instance->fields, name, &value)) {
          pop();  // Instance
          push(value);
          break;
        }

        if (!bind_method(instance->klass, name)) { return INTERPRET_RUNTIME_ERROR; }
        break;
      }
      case OP_SET_PROPERTY: {
        // Top of stack is value to set
        // Below that is instance whose property is being set
        if (!IS_INSTANCE(peek(1))) {
          runtime_error("Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        LoxInstance* instance = AS_INSTANCE(peek(1));
        table_set(&instance->fields, READ_STRING(), peek(0));
        Value value = pop();  // Pop stored value off
        pop();                // Pop instance off
        push(value);          // Put value back on
        break;
      }
      case OP_CLOSE_UPVALUE: {
        close_upvalues(vm.stack_top - 1);
        pop();
        break;
      }
      case OP_NEGATE: {
        if (!IS_NUMBER(peek(0))) {
          runtime_error("Operand to negation must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }
      case OP_NOT: push(BOOL_VAL(is_falsey(pop()))); break;
      case OP_SUB: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MUL: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIV: BINARY_OP(NUMBER_VAL, /); break;
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          BINARY_OP(NUMBER_VAL, +);
        } else {
          runtime_error("Operands to '+' must be two strings or two numbers");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_EQUAL: {
        Value a = pop();
        Value b = pop();
        push(BOOL_VAL(values_equal(a, b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
      case OP_POP: pop(); break;
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (is_falsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_CALL: {
        int arg_count = READ_BYTE();
        if (!call_value(peek(arg_count), arg_count)) { return INTERPRET_RUNTIME_ERROR; }
        frame = &vm.frames[vm.frame_count - 1];
        break;
      }
      case OP_INVOKE: {
        LoxString* method = READ_STRING();
        int arg_count = READ_BYTE();
        if (!invoke(method, arg_count)) { return INTERPRET_RUNTIME_ERROR; }
        frame = &vm.frames[vm.frame_count - 1];
        break;
      }
      case OP_CLOSURE: {
        LoxFunction* function = AS_FUNCTION(READ_CONSTANT());
        LoxClosure* closure = new_closure(function);
        push(OBJ_VAL(closure));
        // fill upvalue array
        for (int i = 0; i < closure->upvalue_count; i++) {
          uint8_t is_local = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (is_local) {
            closure->upvalues[i] = capture_upvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_CLASS: push(OBJ_VAL(new_class(READ_STRING()))); break;
      case OP_GET_SUPER: {
        LoxString* name = READ_STRING();
        LoxClass* superclass = AS_CLASS(pop());
        if (!bind_method(superclass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_INVOKE_SUPER: {
        LoxString* method = READ_STRING();
        int arg_count = READ_BYTE();
        LoxClass* superclass = AS_CLASS(pop());
        if (!invoke_from_class(superclass, method, arg_count)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frame_count - 1];
        break;
      }
      case OP_INHERIT: {
        Value superclass = peek(1);
        if (!(IS_CLASS(superclass))) {
          runtime_error("Superclass must be a class.");
          return INTERPRET_RUNTIME_ERROR;
        }
        LoxClass* subclass = AS_CLASS(peek(0));
        table_add_all(&AS_CLASS(superclass)->methods, &subclass->methods);
        pop();  // Subclass
        break;
      }
      case OP_METHOD: define_method(READ_STRING()); break;
    }
  }
#undef VALUES
#undef READ_BYTE
#undef READ_SHORT
#undef READ_STRING
#undef READ_CONSTANT
#undef PUSH_CONSTANT
#undef DEFINE_GLOBAL
#undef GET_GLOBAL
#undef SET_GLOBAL
#undef GET_LOCAL
#undef SET_LOCAL
#undef NEGATE
#undef BINARY_OP
}

InterpretResult interpret (const char* source) {
  LoxFunction* function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));
  LoxClosure* closure = new_closure(function);
  pop();  // pop function from stack and push closure in its place
  push(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}
