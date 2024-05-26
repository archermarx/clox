#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#include "repl.h"
#include "linenoise.h"
#include "vm.h"

static char* keywords[] = {
  "and", "or", "else",
  "fun", "for", "if", "var", "while"
};

static size_t num_keywords = sizeof(keywords) / sizeof(keywords[1]);

static void match_keywords (const char* buf, linenoiseCompletions* lc) {
  // simple linear search through keywords. not most efficient but fast enough
  size_t buf_len = strlen(buf);
  for (size_t i = 0; i < num_keywords; i++) {
    if (memcmp(buf, keywords[i], buf_len) == 0) {
      linenoiseAddCompletion(lc, keywords[i]);
    }
  }
}

// line completions
void completion (const char* buf, linenoiseCompletions* lc) {
  match_keywords(buf, lc);
}

void repl () {
  char* history_file = "build/history.txt";

  // allow muliline editing
  linenoiseSetMultiLine(1);
  // register line completions
  linenoiseSetCompletionCallback(completion);
  // load history from file. the history is just a plaintext file where entries are separated by newlines
  linenoiseHistoryLoad(history_file);

  char* line;

  // main loop
  while (1) {
    line = linenoise("clox> ");
    if (line == NULL) {
      break;
    }
    if (line[0] != '\0' && line[0] != '/') {
      interpret(line);
      linenoiseHistoryAdd(line);
      linenoiseHistorySave(history_file);
    } else if (line[0] == '/') {
      printf("Unrecognized command: %s\n", line);
    }
    free(line);
  }
}