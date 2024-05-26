#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

// maximum allowable load factor for the hash table
#define TABLE_MAX_LOAD 0.75

typedef struct {
  LoxString* key;
  Value value;
} Entry;

typedef struct {
  size_t count;
  size_t capacity;
  Entry* entries;
} Table;

Table new_table (void);

void init_table (Table* table);
void free_table (Table* table);

/**
 *  Get the value in a table at the given key.
 *  @param table a pointer to the table.
 *  @param key a pointer to the key.
 *  @param[out] value a value pointer. Stores the value if the key is found.
 *  @return `true` if the key was found. `false` otherwise.
 */
bool table_get (Table* table, LoxString* key, Value* value);

/**
 *  Set a key-value pair in a table.
 *  @param table a pointer to the table.
 *  @param key a pointer to the key.
 *  @param value the value to insert.
 *  @return `true` if this was a new entry, `false` otherwise.
 */
bool table_set (Table* table, LoxString* key, Value value);

/**
 *  Delete a key from a table.
 *  @param table a pointer to the table.
 *  @param key the key to delete.
 *  @return `true` if the key was successfully deleted. `false` if the key was not found.
 */
bool table_delete (Table* table, LoxString* key);

/**
 *  Copy all keys from one table to another.
 *  @param from a pointer to the table we're copying from.
 *  @param to   a pointer to the table we're copying to.
 */
void table_add_all (Table* from, Table* to);

/**
 *  Check if we have previously stored this string in the table.
 *  @param table a pointer to the table to check.
 *  @param chars the string to look for.
 *  @param length the length of the string.
 *  @param hash the hash of the string.
 *  @return A pointer to the interned string, or `NULL` if the string was not found.
 */
LoxString* table_find_string (Table* table, const char* chars, size_t length, uint32_t hash);

void table_remove_white (Table* table);
void mark_table (Table* table);

#endif
