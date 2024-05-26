#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "test.h"

#include "lexer.h"
#include "memory.h"
#include "chunk.h"
#include "value.h"
#include "object.h"
#include "table.h"
#include "vm.h"


// Colors
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

typedef struct {
    char* name;
    int num_pass;
    int num_fail;
} TestSet;

void check_test(TestSet *set, char* file, int line, char* cond, bool passed) {
    if (passed) {
        set->num_pass++;
    } else {
        fprintf(stderr, "%s:%i: ", file, line);
        fprintf(stderr, ANSI_COLOR_RED "Test failed (testset \"%s\"):" ANSI_COLOR_RESET, set->name);
        fprintf(stderr, "\n    %s\n" , cond);
        set->num_fail++;
    }
}

void check_testset(TestSet *sets, TestSet *result, char *file, int line) {
    if (result->num_fail > 0) {
        fprintf(stderr, "\n%s:%i: ", file, line);
        fprintf(stderr, ANSI_COLOR_RED "Testset '%s' failed."  ANSI_COLOR_RESET "\n", result->name); 
    }
    sets->num_pass += result->num_pass;
    sets->num_fail += result->num_fail;
}

#define CHECK(set, cond) check_test(&set, __FILE__, __LINE__, #cond, (cond))
#define CHECK_TESTSET(sets, result) check_testset(&sets, &result, __FILE__, __LINE__)

static TestSet run_chunk_tests() {
    // Zero-initialize testset
    TestSet result = {0};
#define TEST(cond) CHECK(result, cond)
    result.name = "Chunks";

    Chunk chunk = new_chunk();
    TEST(chunk.count == 0);
    TEST(chunk.capacity == 0);
    TEST(chunk.code == NULL);

    // writing individual bytes
    uint8_t byte_1 = 1;
    write_chunk(&chunk, byte_1, 1);
    TEST(chunk.count == 1);
    TEST(chunk.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(chunk.code[0] == byte_1);
    TEST(chunk.lines[0] == 1);

    uint8_t byte_2 = 2;
    write_chunk(&chunk, byte_2, 2);
    TEST(chunk.count == 2);
    TEST(chunk.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(chunk.code[0] == byte_1);
    TEST(chunk.code[1] == byte_2);
    TEST(chunk.lines[0] == 1);
    TEST(chunk.lines[1] == 2);

    // filling up current allocation
    for (int i = 3; i <= LOX_INITIAL_ALLOC_SIZE; i++) {
        write_chunk(&chunk, (uint8_t) i, i);
    }
    TEST(chunk.count == LOX_INITIAL_ALLOC_SIZE);
    TEST(chunk.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(chunk.code[LOX_INITIAL_ALLOC_SIZE - 1] == LOX_INITIAL_ALLOC_SIZE);
    TEST(chunk.lines[LOX_INITIAL_ALLOC_SIZE - 1] == LOX_INITIAL_ALLOC_SIZE);

    // adding another byte to force expansion
    size_t expected_capacity = 3 * LOX_INITIAL_ALLOC_SIZE / 2;
    uint8_t byte_3 =  255;
    write_chunk(&chunk, byte_3, byte_3);
    TEST(chunk.count == LOX_INITIAL_ALLOC_SIZE + 1);
    TEST(chunk.capacity == expected_capacity);
    TEST(chunk.code[LOX_INITIAL_ALLOC_SIZE] == byte_3);
    TEST(chunk.lines[LOX_INITIAL_ALLOC_SIZE] == byte_3);

    // adding constants
    // first check that our constant array has been properly initialized
    TEST(chunk.constants.count == 0);
    TEST(chunk.constants.capacity == 0);
    TEST(chunk.constants.values == NULL);

    // now try adding a constant
    Value value = NUMBER_VAL(2.0);
    size_t ret = add_constant(&chunk, value);
    TEST(ret == 0);
    TEST(AS_NUMBER(chunk.constants.values[0]) == AS_NUMBER(value));
    TEST(chunk.constants.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(chunk.constants.count == 1);

    // freeing
    free_chunk(&chunk);
    TEST(chunk.count == 0);
    TEST(chunk.capacity == 0);
    TEST(chunk.code == NULL);
    TEST(chunk.constants.count == 0);
    TEST(chunk.constants.capacity == 0);
    TEST(chunk.constants.values == 0);

#undef TEST
    return result;
}

static TestSet run_value_array_tests() {
    // Zero-initialize testset
    TestSet result = {0};
#define TEST(cond) CHECK(result, cond)
    result.name = "Value arrays";

    ValueArray value_array = new_value_array();
    TEST(value_array.count == 0);
    TEST(value_array.capacity == 0);
    TEST(value_array.values == NULL);

    // writing individual bytes
    Value val_1 = NUMBER_VAL(1);
    write_value_array(&value_array, val_1);
    TEST(value_array.count == 1);
    TEST(value_array.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(AS_NUMBER(value_array.values[0]) == AS_NUMBER(val_1));

    Value val_2 = NUMBER_VAL(2);
    write_value_array(&value_array, val_2);
    TEST(value_array.count == 2);
    TEST(value_array.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(AS_NUMBER(value_array.values[0]) == AS_NUMBER(val_1));
    TEST(AS_NUMBER(value_array.values[1]) == AS_NUMBER(val_2));

    // filling up current allocation
    for (int i = 3; i <= LOX_INITIAL_ALLOC_SIZE; i++) {
        write_value_array(&value_array, NUMBER_VAL(i));
    }
    TEST(value_array.count == LOX_INITIAL_ALLOC_SIZE);
    TEST(value_array.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(AS_NUMBER(value_array.values[LOX_INITIAL_ALLOC_SIZE - 1]) == LOX_INITIAL_ALLOC_SIZE);

    // adding another byte
    size_t expected_capacity = 3 * LOX_INITIAL_ALLOC_SIZE / 2;
    Value val_3 = NUMBER_VAL(255);
    write_value_array(&value_array, val_3);
    TEST(value_array.count == LOX_INITIAL_ALLOC_SIZE + 1);
    TEST(value_array.capacity == expected_capacity);
    TEST(AS_NUMBER(value_array.values[LOX_INITIAL_ALLOC_SIZE]) == AS_NUMBER(val_3));

    // freeing
    free_value_array(&value_array);
    TEST(value_array.count == 0);
    TEST(value_array.capacity == 0);
    TEST(value_array.values == NULL);

#undef TEST
    return result;
}

static TestSet run_lexer_tests() {
    // Zero-initialize testset
    TestSet result = {0};
    result.name = "Lexer";

    // Test simple string
    #define CHECK_TOKEN(token) CHECK(result, lex_token().type == token)

    init_lexer("print(1 + _a, \"Hello\");");
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_LEFT_PAREN);
    CHECK_TOKEN(TOKEN_INT);
    CHECK_TOKEN(TOKEN_PLUS);
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_COMMA);
    CHECK_TOKEN(TOKEN_STRING);
    CHECK_TOKEN(TOKEN_RIGHT_PAREN);
    CHECK_TOKEN(TOKEN_SEMICOLON);
    CHECK_TOKEN(TOKEN_EOF);

    // Test punctuation
    init_lexer("(){}+-*/.");
    CHECK_TOKEN(TOKEN_LEFT_PAREN);
    CHECK_TOKEN(TOKEN_RIGHT_PAREN);
    CHECK_TOKEN(TOKEN_LEFT_BRACE);
    CHECK_TOKEN(TOKEN_RIGHT_BRACE);
    CHECK_TOKEN(TOKEN_PLUS);
    CHECK_TOKEN(TOKEN_MINUS);
    CHECK_TOKEN(TOKEN_STAR);
    CHECK_TOKEN(TOKEN_SLASH);
    CHECK_TOKEN(TOKEN_DOT);
    CHECK_TOKEN(TOKEN_EOF);

    // Test comments
    init_lexer(
        "if (true) { \n"
        "    // this doesn't do anything interesting\n"
        "    return false;\n"
        "}"
    );
    CHECK_TOKEN(TOKEN_IF);
    CHECK_TOKEN(TOKEN_LEFT_PAREN);
    CHECK_TOKEN(TOKEN_TRUE);
    CHECK_TOKEN(TOKEN_RIGHT_PAREN);
    CHECK_TOKEN(TOKEN_LEFT_BRACE);
    CHECK_TOKEN(TOKEN_RETURN);
    CHECK_TOKEN(TOKEN_FALSE);
    CHECK_TOKEN(TOKEN_SEMICOLON);
    CHECK_TOKEN(TOKEN_RIGHT_BRACE);
    CHECK_TOKEN(TOKEN_EOF);

    // some bigraphs and more keywords
    init_lexer( 
        "while (x <= 2_000) {           "
        "   if (y >= 4 or x == 3) {     "
        "       break;                  "
        "   } else {                    "
        "       if (y > 1 and y < 2) {  "
        "           var str = \"test\"; "
        "           return str;         "
        "       }                       "
        "   }                           "
        "}                              "
    );

    CHECK_TOKEN(TOKEN_WHILE);
    CHECK_TOKEN(TOKEN_LEFT_PAREN);
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_LESS_EQUAL);
    CHECK_TOKEN(TOKEN_INT);
    CHECK_TOKEN(TOKEN_RIGHT_PAREN);
    CHECK_TOKEN(TOKEN_LEFT_BRACE);
    CHECK_TOKEN(TOKEN_IF);
    CHECK_TOKEN(TOKEN_LEFT_PAREN);
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_GREATER_EQUAL);
    CHECK_TOKEN(TOKEN_INT);
    CHECK_TOKEN(TOKEN_OR);
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_EQUAL_EQUAL);
    CHECK_TOKEN(TOKEN_INT);
    CHECK_TOKEN(TOKEN_RIGHT_PAREN);
    CHECK_TOKEN(TOKEN_LEFT_BRACE);
    CHECK_TOKEN(TOKEN_BREAK);
    CHECK_TOKEN(TOKEN_SEMICOLON);
    CHECK_TOKEN(TOKEN_RIGHT_BRACE);
    CHECK_TOKEN(TOKEN_ELSE);
    CHECK_TOKEN(TOKEN_LEFT_BRACE);
    CHECK_TOKEN(TOKEN_IF);
    CHECK_TOKEN(TOKEN_LEFT_PAREN);
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_GREATER);
    CHECK_TOKEN(TOKEN_INT);
    CHECK_TOKEN(TOKEN_AND);
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_LESS);
    CHECK_TOKEN(TOKEN_INT);
    CHECK_TOKEN(TOKEN_RIGHT_PAREN);
    CHECK_TOKEN(TOKEN_LEFT_BRACE);
    CHECK_TOKEN(TOKEN_VAR);
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_EQUAL);
    CHECK_TOKEN(TOKEN_STRING);
    CHECK_TOKEN(TOKEN_SEMICOLON);
    CHECK_TOKEN(TOKEN_RETURN);
    CHECK_TOKEN(TOKEN_IDENTIFIER);
    CHECK_TOKEN(TOKEN_SEMICOLON);
    CHECK_TOKEN(TOKEN_RIGHT_BRACE);
    CHECK_TOKEN(TOKEN_RIGHT_BRACE);
    CHECK_TOKEN(TOKEN_RIGHT_BRACE);
    CHECK_TOKEN(TOKEN_EOF);

    // some extra keywords
    init_lexer("class fun !super, nil != 2.0 ");
    CHECK_TOKEN(TOKEN_CLASS);
    CHECK_TOKEN(TOKEN_FUN);
    CHECK_TOKEN(TOKEN_BANG);
    CHECK_TOKEN(TOKEN_SUPER);
    CHECK_TOKEN(TOKEN_COMMA);
    CHECK_TOKEN(TOKEN_NIL);
    CHECK_TOKEN(TOKEN_BANG_EQUAL);
    CHECK_TOKEN(TOKEN_FLOAT64);
    CHECK_TOKEN(TOKEN_EOF);

    // some numbers
    init_lexer("1e-2 1e+2 1e+ 1e- 1.0E-30, 3.14159_265359 4.5E-20_000, 4.5E*2 1. 1.. 2.2");
    CHECK_TOKEN(TOKEN_FLOAT64);
    CHECK_TOKEN(TOKEN_FLOAT64);
    CHECK_TOKEN(TOKEN_ERROR);
    CHECK_TOKEN(TOKEN_ERROR);
    CHECK_TOKEN(TOKEN_FLOAT64);
    CHECK_TOKEN(TOKEN_COMMA);
    CHECK_TOKEN(TOKEN_FLOAT64);
    CHECK_TOKEN(TOKEN_FLOAT64);
    CHECK_TOKEN(TOKEN_COMMA);
    CHECK_TOKEN(TOKEN_ERROR);
    CHECK_TOKEN(TOKEN_STAR);
    CHECK_TOKEN(TOKEN_INT);
    CHECK_TOKEN(TOKEN_ERROR);
    CHECK_TOKEN(TOKEN_ERROR);
    CHECK_TOKEN(TOKEN_DOT);
    CHECK_TOKEN(TOKEN_FLOAT64);
    CHECK_TOKEN(TOKEN_EOF);

    #undef CHECK_TOKEN

    return result;
}

static TestSet run_table_tests() {
    TestSet result = {.name = "Hash tables", 0};
#define TEST(cond) CHECK(result, cond)

    // initialize the VM
    init_vm();

    // create a new empty table
    Table table = new_table();
    TEST(table.capacity == 0);
    TEST(table.capacity == 0);
    TEST(table.entries == NULL);

    // create a key
    LoxString *key = new_string(strdup("key"));
    TEST(key->hash != 0);
    TEST(key->length = 4);

    // check that we don't get a value when there is an empty table
    Value val = NIL_VAL;
    TEST(!table_get(&table, key, &val));
    TEST(IS_NIL(val));

    // insert a key into the array
    val = NUMBER_VAL(2.0);
    TEST(table_set(&table, key, val));

    // check that we can find the key
    Value dum = NIL_VAL;
    TEST(table_get(&table, key, &dum));
    TEST(values_equal(val, dum));

    // check what the new table size is
    TEST(table.count == 1);
    TEST(table.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(table.entries != NULL);

    // Copy entries to new table
    Table dest = new_table();
    table_add_all(&table, &dest);
    
    // Check that new entries are present
    dum = NIL_VAL;
    TEST(table_get(&dest, key, &dum));
    TEST(values_equal(val, dum));

    // check what the new table size is
    TEST(dest.count == 1);
    TEST(dest.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(dest.entries != NULL);
    // free table
    free_table(&dest);

    // Another key
    LoxString *key_2 = new_string(strdup("key_2"));
    Value next_val = NUMBER_VAL(3.14);
    TEST(table_set(&table, key_2, next_val));
    TEST(table_get(&table, key_2, &dum));
    TEST(values_equal(next_val, dum));
    TEST(table.count == 2);
    TEST(table.capacity == LOX_INITIAL_ALLOC_SIZE);

    // Look for the keys in the table
    TEST(table_find_string(&table, key->chars, key->length, key->hash) == key);
    TEST(table_find_string(&table, key_2->chars, key_2->length, key_2->hash) == key_2);
    
    // Deletion
    TEST(table_delete(&table, key));
    TEST(!table_get(&table, key, &dum));
    TEST(table.count == 2);
    TEST(table.capacity == LOX_INITIAL_ALLOC_SIZE);

    // check that key is no longer in array
    TEST(table_find_string(&table, key->chars, key->length, key->hash) == NULL);

    // Copying the keys to a new table should not keep the tombstones
    init_table(&dest);
    table_add_all(&table, &dest);
    TEST(dest.count == 1);
    TEST(dest.capacity == LOX_INITIAL_ALLOC_SIZE);
    TEST(table_get(&table, key_2, &dum));
    TEST(values_equal(next_val, dum));
    TEST(!table_get(&table, key, &dum));
    free_table(&dest);

    // Re-inserting the original key should overwrite the tombstone instead of allocating a new spot
    TEST(table_set(&table, key, val));
    TEST(table_get(&table, key, &dum));
    TEST(values_equal(val, dum));
    TEST(table.count == 2);
    TEST(table.capacity = LOX_INITIAL_ALLOC_SIZE);

    // check that key is now back in the array
    TEST(table_find_string(&table, key->chars, key->length, key->hash) == key);

    // Freeing
    free_table(&table);
    TEST(table.count == 0);
    TEST(table.capacity == 0);
    TEST(table.entries == NULL);

    // free any memory allocated on the vm
    free_vm();

#undef TEST
    return result;
}

void run_tests() {

    init_vm();

    TestSet results = {0};

    TestSet chunk_result = run_chunk_tests();
    CHECK_TESTSET(results, chunk_result);

    TestSet value_array_result = run_value_array_tests();
    CHECK_TESTSET(results, value_array_result);

    TestSet lexer_result = run_lexer_tests();
    CHECK_TESTSET(results, lexer_result);

    TestSet table_result = run_table_tests();
    CHECK_TESTSET(results, table_result);

    printf(ANSI_COLOR_GREEN "\n%3i tests passed." ANSI_COLOR_RESET "\n", results.num_pass);
    int num_fail = results.num_fail;
    if (num_fail == 0) {
        exit(LOX_EXIT_SUCCESS);
    } else {
        printf(ANSI_COLOR_RED "%3i test%s failed." ANSI_COLOR_RESET "\n", num_fail, num_fail == 1 ? " " : "s");
        exit(LOX_EXIT_FAILURE);
    }
}
