#include <stdio.h>

#include <strlib.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "config.h"
#include "test.h"
#include "debug.h"

#include "chunk.h"
#include "vm.h"

#include "repl.h"

static void print_usage () {
  printf("Usage: clox\n");
  printf("   or: clox [OPTIONS] SOURCE\n");
  printf("   or: clox -t\n");
  printf("   or: clox --test\n");
  printf("Run clox interpreter in interactive mode (no args) on provided SOURCE file.\n\n");
  printf("Command line arguments:\n");
  printf("-t, --test\trun tests\n");
  printf("    --help\tdisplay this help and exit\n");
}

static void print_help_message () { printf("Try 'clox --help' for more information.\n"); }

static CloxConfig handle_args (int argc, char* argv[]) {
  CloxConfig config = { 0 };
  if (argc == 1) {
    return config;
  }

  bool input_file_set = false;

  // Check if we're in help mode
  char* first_arg = argv[1];
  if (strcmp(first_arg, "--help") == 0) {
    print_usage();
    exit(LOX_EXIT_SUCCESS);
  }

  for (int i = 1; i < argc; i++) {
    char* arg = argv[i];

    // Check if we're running tests
    if (strcmp(arg, "-t") == 0 || strcmp(arg, "--test") == 0) {
      config.test = true;
      continue;
    }

    // Look for input file name
    if (!input_file_set) {
      config.input_file = arg;
      input_file_set = true;
    } else {
      fprintf(stderr, "Unrecognized argument %s.\n", arg);
      print_help_message();
      exit(LOX_EXIT_IMPROPER_USAGE);
    }
  }

  // Check that we found an input file if we're not running tests
  if (!config.test && !input_file_set) {
    fprintf(stderr, "Missing input file.\n");
    print_help_message();
    exit(LOX_EXIT_IMPROPER_USAGE);
  }

  return config;
}

static char* read_file (const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(LOX_EXIT_FILE_ERROR);
  }
  fseek(file, 0L, SEEK_END);
  size_t file_size = (size_t)ftell(file);
  rewind(file);

  char* buffer = (char*)malloc(file_size + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(LOX_EXIT_FILE_ERROR);
  }
  size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
  if (bytes_read < file_size) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(LOX_EXIT_FILE_ERROR);
  }

  buffer[bytes_read] = '\0';
  fclose(file);
  return buffer;
}

static void run_file (const char* path) {
  char* source = read_file(path);
  InterpretResult result = interpret(source);
  free(source);

  if (result == INTERPRET_COMPILE_ERROR) exit(LOX_EXIT_COMPILE_ERROR);
  if (result == INTERPRET_RUNTIME_ERROR) exit(LOX_EXIT_RUNTIME_ERROR);
}

static void run_program (CloxConfig config) {
  init_vm();
  if (config.input_file == NULL) {
    repl();
  } else {
    run_file(config.input_file);
  }
  free_vm();
}

int main (int argc, char* argv[]) {
  CloxConfig config = handle_args(argc, argv);

  if (config.test) {
    run_tests();
  }

  run_program(config);

  return LOX_EXIT_SUCCESS;
}
