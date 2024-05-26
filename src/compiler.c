#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "object.h"
#include "value.h"
#include "memory.h"
#include "vm.h"

#ifdef CLOX_PRINT_CODE
#include "debug.h"
#endif

#include "compiler.h"

typedef struct Parser {
  Token current;
  Token previous;
  bool had_error;
  bool panic_mode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // ==
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! - +
  PREC_CALL,        // . ()
  PREC_PRIMARY,
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct ParseRule {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct Local {
  Token name;
  int depth;
  bool is_captured;
} Local;

typedef struct Upvalue {
  uint8_t index;
  bool is_local;
} Upvalue;

typedef enum {
  TYPE_FUNCTION,
  TYPE_INITIALIZER,
  TYPE_METHOD,
  TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
  struct Compiler* enclosing;
  LoxFunction* function;
  FunctionType type;

  Local locals[UINT8_MAX];
  int local_count;

  Upvalue upvalues[UINT8_MAX];
  int scope_depth;
} Compiler;

typedef struct ClassCompiler {
  struct ClassCompiler* enclosing;
  bool has_superclass;
} ClassCompiler;

Parser parser;
Compiler* current = NULL;
ClassCompiler* current_class = NULL;

static Chunk* current_chunk () { return &current->function->chunk; }

// some forward declarations
static void expression (void);
static void statement (void);
static void declaration (void);
static ParseRule* get_rule (TokenType type);
static void parse_precedence (Precedence precedence);

static void error_at (Token* token, const char* message) {
  // don't keep reporting meaningless errors if we're panicking.
  if (parser.panic_mode) return;

  // Set panic mode!
  parser.panic_mode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // nothing
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.had_error = true;
}

static void error (const char* message) { error_at(&parser.previous, message); }

static void error_at_current (const char* message) { error_at(&parser.current, message); }

static void advance () {
  parser.previous = parser.current;

  for (;;) {
    parser.current = lex_token();
    if (parser.current.type != TOKEN_ERROR) break;

    error_at_current(parser.current.start);
  }
}

static void consume (TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  error_at_current(message);
}

static bool check (TokenType type) { return parser.current.type == type; }

static bool match (TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

static void patch_jump (int offset) {
  // use -2 to adjust for the bytecode for the jump offset literals
  int jump = (int)current_chunk()->count - offset - 2;

  if (jump > UINT16_MAX) { error("Too much code to jump over."); }

  // write actual offset
  current_chunk()->code[offset] = (jump >> 8) & 0xFF;
  current_chunk()->code[offset + 1] = jump & 0xFF;
}

static void init_compiler (Compiler* compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->local_count = 0;
  compiler->scope_depth = 0;
  compiler->function = new_function();
  current = compiler;

  if (type != TYPE_SCRIPT) {
    current->function->name = copy_string(parser.previous.start, (size_t)parser.previous.length);
  }

  // Locals reserved for VM internal use
  Local* local = &current->locals[current->local_count++];
  local->depth = 0;
  local->is_captured = false;
  if (type != TYPE_FUNCTION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static void emit_byte (uint8_t byte) { write_chunk(current_chunk(), byte, parser.previous.line); }

static void emit_bytes (uint8_t byte_1, uint8_t byte_2) {
  emit_byte(byte_1);
  emit_byte(byte_2);
}

static void emit_loop (int loop_start) {
  emit_byte(OP_LOOP);

  int offset = (int)(current_chunk()->count) - loop_start + 2;
  if (offset > UINT16_MAX) error("Loop body too large.");

  emit_byte((offset >> 8) & 0xff);
  emit_byte(offset & 0xff);
}

static int emit_jump (uint8_t instruction) {
  emit_byte(instruction);
  // two byte operand -> can jump over 65,535 B of code
  emit_byte(0xff);
  emit_byte(0xff);
  return (int)current_chunk()->count - 2;
}

static void emit_return () {
  if (current->type == TYPE_INITIALIZER) {
    emit_bytes(OP_GET_LOCAL, 0);
  } else {
    emit_byte(OP_NIL);
  } 
  emit_byte(OP_RETURN);
}

static uint8_t make_constant (Value value) {
  size_t constant = add_constant(current_chunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

static void emit_constant (Value value) { emit_bytes(OP_CONSTANT, make_constant(value)); }

static uint8_t identifier_constant (Token* name) {
  return make_constant(OBJ_VAL(copy_string(name->start, (size_t)name->length)));
}

static bool identifiers_equal (Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, (size_t)(a->length)) == 0;
}

static int resolve_local (Compiler* compiler, Token* name) {
  // try and find the current variable in the compiler's local variable list.
  for (int i = compiler->local_count - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifiers_equal(name, &local->name)) {
      if (local->depth == -1) { error("Can't read local variable in its own initializer."); }
      return i;
    }
  }

  return -1;
}

static int add_upvalue (Compiler* compiler, uint8_t index, bool is_local) {
  int upvalue_count = compiler->function->upvalue_count;

  for (int i = 0; i < upvalue_count; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->is_local == is_local) { return i; }
  }

  if (upvalue_count >= UINT8_MAX) {
    error("Too many closed variables in function.");
    return 0;
  }

  compiler->upvalues[upvalue_count].is_local = is_local;
  compiler->upvalues[upvalue_count].index = index;
  return compiler->function->upvalue_count++;
}

static int resolve_upvalue (Compiler* compiler, Token* name) {
  if (compiler->enclosing == NULL) return -1;

  int local = resolve_local(compiler->enclosing, name);
  if (local != -1) {
    // close this local
    compiler->enclosing->locals[local].is_captured = true;
    return add_upvalue(compiler, (uint8_t)local, true);
  }

  // recursively handle cases where the variable has already left the stack
  // by seeing if the enclosing scope has an upvalue that we can use
  int upvalue = resolve_upvalue(compiler->enclosing, name);
  if (upvalue != -1) { return add_upvalue(compiler, (uint8_t)upvalue, false); }

  return -1;
}

static void add_local (Token name) {
  // Check that we have enough room for local variables.
  if (current->local_count == UINT8_MAX) {
    error("Too many local variables in scope.");
    return;
  }

  // add the local variable to the compiler's array.
  Local* local = &current->locals[current->local_count++];
  local->name = name;
  // locals start uninitialized, which we indicate with a depth of -1.
  local->depth = -1;
  local->is_captured = false;
}

static void declare_variable () {
  // nothing to do for globals.
  if (current->scope_depth == 0) return;

  Token* name = &parser.previous;

  // check to make sure we're not re-declaring a variable that exists in the same scope.
  for (int i = current->local_count - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scope_depth) { break; }

    if (identifiers_equal(name, &local->name)) { error("Already a variable with this name in this scope."); }
  }

  add_local(*name);
}

static uint8_t parse_variable (const char* error_message) {
  consume(TOKEN_IDENTIFIER, error_message);

  // try to declare a local variable.
  declare_variable();

  // if the variable was local, we just return instead of adding the variable to the constant table.
  if (current->scope_depth > 0) return 0;

  return identifier_constant(&parser.previous);
}

static void mark_initialized () {
  if (current->scope_depth == 0) return;
  // Initialized locals have a depth equal to the current scope depth;
  current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable (uint8_t global) {
  if (current->scope_depth > 0) {
    // Locals have already been added to the stack. Mark initialized and return.
    mark_initialized();
    return;
  }
  emit_bytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argument_list () {
  uint8_t arg_count = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (arg_count == 255) { error("Can't have more than 255 arguments."); }
      arg_count++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments");
  return arg_count;
}

static void and_ (bool can_assign) {
  (void)can_assign;
  int end_jump = emit_jump(OP_JUMP_IF_FALSE);
  // if the condition is false, then we evaluate the next condition in sequence, or finish if there are no more.
  emit_byte(OP_POP);
  parse_precedence(PREC_AND);
  patch_jump(end_jump);
}

static LoxFunction* end_compiler () {
  emit_return();
  LoxFunction* function = current->function;

#ifdef CLOX_PRINT_CODE
  if (!parser.had_error) {
    disassemble_chunk(current_chunk(), function->name != NULL ? function->name->chars : "<script>");
  }
#endif

  current = current->enclosing;
  return function;
}

static void begin_scope () { current->scope_depth++; }

static void end_scope () {
  current->scope_depth--;

  // Pop local variables from the VM stack.
  while (current->local_count > 0 && current->locals[current->local_count - 1].depth > current->scope_depth) {
    if (current->locals[current->local_count - 1].is_captured) {
      // close upvalue before popping
      emit_byte(OP_CLOSE_UPVALUE);
    } else {
      // TODO: add OP_POPN instruction to pop many locals
      emit_byte(OP_POP);
    }
    current->local_count--;
  }
}

static void binary (bool can_assign) {
  (void)can_assign;
  TokenType operator_type = parser.previous.type;
  ParseRule* rule = get_rule(operator_type);
  parse_precedence((Precedence)(rule->precedence + 1));
  switch (operator_type) {
    case TOKEN_PLUS: emit_byte(OP_ADD); break;
    case TOKEN_MINUS: emit_byte(OP_SUB); break;
    case TOKEN_STAR: emit_byte(OP_MUL); break;
    case TOKEN_SLASH: emit_byte(OP_DIV); break;
    case TOKEN_EQUAL_EQUAL: emit_byte(OP_EQUAL); break;
    case TOKEN_BANG_EQUAL: emit_bytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_GREATER: emit_byte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emit_bytes(OP_LESS, OP_NOT); break;
    case TOKEN_LESS: emit_byte(OP_LESS); break;
    case TOKEN_LESS_EQUAL: emit_bytes(OP_GREATER, OP_NOT); break;
    default: return;
  }
}

static void call (bool can_assign) {
  (void)can_assign;
  uint8_t arg_count = argument_list();
  emit_bytes(OP_CALL, arg_count);
}

static void dot (bool can_assign) {
  consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint8_t name = identifier_constant(&parser.previous);

  if (can_assign && match(TOKEN_EQUAL)) {
    expression();
    emit_bytes(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    // invoke method directly
    uint8_t arg_count = argument_list();
    emit_bytes(OP_INVOKE, name);
    emit_byte(arg_count);
  } else {
    emit_bytes(OP_GET_PROPERTY, name);
  }
}

static void literal (bool can_assign) {
  (void)can_assign;
  switch (parser.previous.type) {
    case TOKEN_FALSE: emit_byte(OP_FALSE); break;
    case TOKEN_TRUE: emit_byte(OP_TRUE); break;
    case TOKEN_NIL: emit_byte(OP_NIL); break;
    default: return;  // unreachable
  }
}

static void grouping (bool can_assign) {
  (void)can_assign;
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static double str_to_d(Token token) {
  char buf[100] = {'\0'};

  // strip underscores
  int ind = 0;
  for (int i = 0; i < token.length; i++) {
    char c = token.start[i];
    if (c != '_') {
      buf[ind] = c;
      ind++;
    }
  }

  return strtod(buf, NULL);
}

static void number (bool can_assign) {
  (void)can_assign;
  Value value = NUMBER_VAL(str_to_d(parser.previous));
  emit_constant(value);
}

static void or_ (bool can_assign) {
  (void)can_assign;
  int else_jump = emit_jump(OP_JUMP_IF_FALSE);
  int end_jump = emit_jump(OP_JUMP);
  // if this condition is true, we continue
  patch_jump(else_jump);
  emit_byte(OP_POP);
  // otherwise, we evaluate the remaining conditions
  parse_precedence(PREC_OR);
  patch_jump(end_jump);
}

static void string (bool can_assign) {
  (void)can_assign;
  Value value = OBJ_VAL(copy_string(parser.previous.start + 1, (size_t)parser.previous.length - 2));
  make_constant(value);
  emit_constant(value);
}

static void named_variable (Token name, bool can_assign) {
  uint8_t get_op;
  uint8_t set_op;

  // determine which instruction set to use
  int arg = resolve_local(current, &name);

  if (arg != -1) {
    // use local variable ops
    set_op = OP_SET_LOCAL;
    get_op = OP_GET_LOCAL;
  } else if ((arg = resolve_upvalue(current, &name)) != -1) {
    // use upvalue ops
    set_op = OP_SET_UPVALUE;
    get_op = OP_GET_UPVALUE;
  } else {
    arg = identifier_constant(&name);
    // use local variable ops
    set_op = OP_SET_GLOBAL;
    get_op = OP_GET_GLOBAL;
  }

  if (can_assign && match(TOKEN_EQUAL)) {
    expression();
    emit_bytes(set_op, (uint8_t)arg);
  } else {
    emit_bytes(get_op, (uint8_t)arg);
  }
}

static void variable (bool can_assign) { named_variable(parser.previous, can_assign); }

static Token synthetic_token(const char* text) {
  Token token;
  token.start = text;
  token.length = (int)strlen(text);
  return token;
}

static void super_ (bool can_assign) {
  (void)can_assign;

  if (current_class == NULL) {
    error("Can't use 'super' outside of a class.");
  } else if (!current_class->has_superclass) {
    error("Can't use 'super' in a class that has no superclass.");
  }
  consume(TOKEN_DOT, "Expect '.' after 'super'.");
  consume(TOKEN_IDENTIFIER, "Expect superclass method name");
  uint8_t name = identifier_constant(&parser.previous);

  named_variable(synthetic_token("this"), false);
  if (match(TOKEN_LEFT_PAREN)) {
    uint8_t arg_count = argument_list();
    named_variable(synthetic_token("super"), false);
    emit_bytes(OP_INVOKE_SUPER, name);
    emit_byte(arg_count);
  } else {
    named_variable(synthetic_token("super"), false);
    emit_bytes(OP_GET_SUPER, name);
  }
}

static void this_ (bool can_assign) {
  (void)can_assign;
  if (current_class == NULL) {
    error("Can't use 'this' outside of a class.");
    return;
  }
  variable(false);
}

static void unary (bool can_assign) {
  (void)can_assign;
  TokenType operator_type = parser.previous.type;

  // compile the operand
  parse_precedence(PREC_UNARY);

  // Emit the operator instruction
  switch (operator_type) {
    case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
    case TOKEN_BANG: emit_byte(OP_NOT); break;
    default: return;  // unreachable
  }
}

static void parse_precedence (Precedence precedence) {
  advance();
  ParseFn prefix_rule = get_rule(parser.previous.type)->prefix;
  if (prefix_rule == NULL) {
    error("Expect expression.");
    return;
  }

  bool can_assign = precedence <= PREC_ASSIGNMENT;
  prefix_rule(can_assign);

  while (precedence <= get_rule(parser.current.type)->precedence) {
    advance();
    ParseFn infix_rule = get_rule(parser.previous.type)->infix;
    infix_rule(can_assign);
  }

  if (can_assign && match(TOKEN_EQUAL)) { error("Invalid assignment target."); }
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN] = { .prefix = grouping, .infix = call, .precedence = PREC_CALL },
  [TOKEN_RIGHT_PAREN] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_LEFT_BRACE] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_RIGHT_BRACE] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_COMMA] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_DOT] = { .prefix = NULL, .infix = dot, .precedence = PREC_CALL },
  [TOKEN_MINUS] = { .prefix = unary, .infix = binary, .precedence = PREC_TERM },
  [TOKEN_PLUS] = { .prefix = unary, .infix = binary, .precedence = PREC_TERM },
  [TOKEN_SEMICOLON] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_STAR] = { .prefix = NULL, .infix = binary, .precedence = PREC_FACTOR },
  [TOKEN_SLASH] = { .prefix = NULL, .infix = binary, .precedence = PREC_FACTOR },
  [TOKEN_BANG] = { .prefix = unary, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_EQUAL] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_BANG_EQUAL] = { .prefix = NULL, .infix = binary, .precedence = PREC_EQUALITY },
  [TOKEN_EQUAL_EQUAL] = { .prefix = NULL, .infix = binary, .precedence = PREC_EQUALITY },
  [TOKEN_GREATER] = { .prefix = NULL, .infix = binary, .precedence = PREC_COMPARISON },
  [TOKEN_GREATER_EQUAL] = { .prefix = NULL, .infix = binary, .precedence = PREC_COMPARISON },
  [TOKEN_LESS] = { .prefix = NULL, .infix = binary, .precedence = PREC_COMPARISON },
  [TOKEN_LESS_EQUAL] = { .prefix = NULL, .infix = binary, .precedence = PREC_COMPARISON },
  [TOKEN_IDENTIFIER] = { .prefix = variable, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_STRING] = { .prefix = string, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_FLOAT64] = { .prefix = number, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_INT] = { .prefix = number, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_AND] = { .prefix = NULL, .infix = and_, .precedence = PREC_AND },
  [TOKEN_BREAK] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_CLASS] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_ELSE] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_FALSE] = { .prefix = literal, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_FOR] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_FUN] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_IF] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_NIL] = { .prefix = literal, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_OR] = { .prefix = NULL, .infix = or_, .precedence = PREC_OR },
  [TOKEN_RETURN] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_SUPER] = { .prefix = super_, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_THIS] = { .prefix = this_, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_TRUE] = { .prefix = literal, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_VAR] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_WHILE] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_ERROR] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
  [TOKEN_EOF] = { .prefix = NULL, .infix = NULL, .precedence = PREC_NONE },
};

static ParseRule* get_rule (TokenType type) { return &rules[type]; }

static void expression () { parse_precedence(PREC_ASSIGNMENT); }

static void block () {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) { declaration(); }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block");
}

static void function (FunctionType type) {
  Compiler compiler;
  init_compiler(&compiler, type);
  begin_scope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) { error_at_current("Can't have more than 255 parameters."); }
      uint8_t constant = parse_variable("Expect parameter name");
      define_variable(constant);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters.");
  consume(TOKEN_LEFT_BRACE, "Expect '}' before function body.");
  block();

  LoxFunction* function = end_compiler();

  emit_bytes(OP_CLOSURE, make_constant(OBJ_VAL(function)));

  for (int i = 0; i < function->upvalue_count; i++) {
    emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
    emit_byte(compiler.upvalues[i].index);
  }
}

static void method () {
  consume(TOKEN_IDENTIFIER, "Expect method name.");
  uint8_t constant = identifier_constant(&parser.previous);

  FunctionType type = TYPE_METHOD;
  if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
    type = TYPE_INITIALIZER;
  }
  function(type);

  emit_bytes(OP_METHOD, constant);
}

static void class_declaration () {
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  Token class_name = parser.previous;
  uint8_t name_constant = identifier_constant(&class_name);
  declare_variable();

  emit_bytes(OP_CLASS, name_constant);
  define_variable(name_constant);

  ClassCompiler class_compiler;
  class_compiler.enclosing = current_class;
  class_compiler.has_superclass = false;
  current_class = &class_compiler;

  // look for inheritance declarations
  if (match(TOKEN_LESS)) {
    consume(TOKEN_IDENTIFIER, "Expect superclass name");
    variable(false);

    if (identifiers_equal(&class_name, &parser.previous)) {
      error("A class can't inherit from itself.");
    }

    begin_scope();
    add_local(synthetic_token("super"));
    define_variable(0);

    named_variable(class_name, false);
    emit_byte(OP_INHERIT);
    class_compiler.has_superclass = true;
  }

  // for method definitions, we load the class onto the stack
  named_variable(class_name, false);
  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) { method(); }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
  // Pop the class off the stack
  emit_byte(OP_POP);

  if (class_compiler.has_superclass) {
    end_scope();
  }
  current_class = current_class->enclosing;
}

static void fun_declaration () {
  uint8_t global = parse_variable("Expect function name.");
  mark_initialized();
  function(TYPE_FUNCTION);
  define_variable(global);
}

static void var_declaration () {
  uint8_t global = parse_variable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emit_byte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
  define_variable(global);
}

static void expression_statement () {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emit_byte(OP_POP);
}

static void for_statement () {
  // create a scope that initialized variables live within
  begin_scope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  // parse initializer clause
  if (match(TOKEN_SEMICOLON)) {
    // no initializer
  } else if (match(TOKEN_VAR)) {
    // a var declaration
    var_declaration();
  } else {
    // something else
    expression_statement();
  }

  // initialize the loop
  int loop_start = (int)current_chunk()->count;
  int exit_jump = -1;

  // parse the condition
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    // jump out of the loop if the condition is false
    exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
  }

  // Increment clause
  // We first execute the body, then the increment, then jump back to the condition
  if (!match(TOKEN_RIGHT_PAREN)) {
    // log location of body jump
    int body_jump = emit_jump(OP_JUMP);
    int increment_start = (int)current_chunk()->count;
    // consume increment start
    expression();
    // pop increment expression
    emit_byte(OP_POP);

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emit_loop(loop_start);
    loop_start = increment_start;
    patch_jump(body_jump);
  }

  // parse loop body
  statement();
  emit_loop(loop_start);

  if (exit_jump != -1) {
    patch_jump(exit_jump);
    emit_byte(OP_POP);  // pop condition
  }

  end_scope();
}

static void if_statement () {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  // if the condition is false, we jump to the end of the statement and begin executing the else branch.
  int then_jump = emit_jump(OP_JUMP_IF_FALSE);
  // pop the condition
  emit_byte(OP_POP);
  statement();

  // we emit the else jump condition to make sure that if the condition is true, we don't fall through to the else branch.
  int else_jump = emit_jump(OP_JUMP);

  // insert the jump to start executing the else branch.
  patch_jump(then_jump);
  // pop the po the condition
  emit_byte(OP_POP);

  // read the else jump.
  if (match(TOKEN_ELSE)) statement();

  // patch the jump point from the end of the true branch
  patch_jump(else_jump);
}

static void return_statement () {
  if (current->type == TYPE_SCRIPT) { error("Can't return from top-level code."); }
  if (match(TOKEN_SEMICOLON)) {
    emit_return();
  } else {
    if (current->type == TYPE_INITIALIZER) {
      error("Can't return a value from an initializer.");
    }
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value");
    emit_byte(OP_RETURN);
  }
}

static void while_statement () {
  int loop_start = (int)current_chunk()->count;
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'");
  expression();  // parse condition
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'condition'");

  // set up jump
  int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
  // pop condition if true
  emit_byte(OP_POP);

  // parse body
  statement();
  // return to loop begin;
  emit_loop(loop_start);

  // jump location for exiting loop
  patch_jump(exit_jump);

  // pop condition
  emit_byte(OP_POP);
}

static void synchronize () {
  parser.panic_mode = false;

  while (parser.current.type != TOKEN_EOF) {
    // advance until we hit a statement boundary, i.e. a semicolon or
    // something that looks like it begins another statement, like
    // a declaration or keyword.
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_RETURN:
      case TOKEN_BREAK: return;
      default:;  // do nothing
    }
    advance();
  }
}

static void declaration () {
  if (match(TOKEN_CLASS)) {
    class_declaration();
  } else if (match(TOKEN_FUN)) {
    fun_declaration();
  } else if (match(TOKEN_VAR)) {
    var_declaration();
  } else {
    statement();
  }

  if (parser.panic_mode) synchronize();
}

static void statement () {
  if (match(TOKEN_IF)) {
    if_statement();
  } else if (match(TOKEN_RETURN)) {
    return_statement();
  } else if (match(TOKEN_WHILE)) {
    while_statement();
  } else if (match(TOKEN_FOR)) {
    for_statement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    begin_scope();
    block();
    end_scope();
  } else {
    expression_statement();
  }
}

LoxFunction* compile (const char* source) {
  // Initialize the lexer
  init_lexer(source);

  // Set up the compiler
  Compiler compiler;
  init_compiler(&compiler, TYPE_SCRIPT);

  // initialize error flags
  parser.had_error = false;
  parser.panic_mode = false;

  advance();

  while (!match(TOKEN_EOF)) { declaration(); }

  LoxFunction* function = end_compiler();
  return parser.had_error ? NULL : function;
}

void mark_compiler_roots () {
  Compiler* compiler = current;
  while (compiler != NULL) {
    mark_object((LoxObj*)compiler->function);
    compiler = compiler->enclosing;
  }
}
