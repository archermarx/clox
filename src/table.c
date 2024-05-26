#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

Table new_table () {
  Table table;
  init_table(&table);
  return table;
}

void init_table (Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void free_table (Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  init_table(table);
}

#if LOX_TABLE_GROW_FACTOR == 2
  // capacity is always a power of two, so we can use bit-masking to speed up modulo operator
  #define MOD_CAPACITY(val) ((val) & (capacity - 1))
#else
  #define MOD_CAPACITY(val) ((val) % capacity)
#endif


static Entry* find_entry (Entry* entries, size_t capacity, LoxString* key) {

  uint32_t index = MOD_CAPACITY(key->hash);
  Entry* tombstone = NULL;

  for (;;) {
    Entry* entry = &entries[index];
    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        // Empty entry
        // If we previously passed a tombstone, return the tombstone instead of this empty slot
        return tombstone != NULL ? tombstone : entry;
      } else {
        // we found a tombstone
        if (tombstone == NULL) tombstone = entry;
      }
    } else if (entry->key == key) {
      // we found the key
      return entry;
    }

    // collision: linear search for the key if we didn't hit it first try
    index = MOD_CAPACITY(index + 1);
  }
}

static void adjust_capacity (Table* table, size_t capacity) {
  Entry* entries = ALLOCATE(Entry, capacity);
  for (size_t i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  // reinsert existing keys in table. we do not copy tombstones, so we will need to recompute the count.
  table->count = 0;
  for (size_t i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key == NULL) continue;
    Entry* dest = find_entry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  // free old entries
  FREE_ARRAY(Entry, table->entries, table->capacity);

  // update our table pointers
  table->entries = entries;
  table->capacity = capacity;
}

bool table_get (Table* table, LoxString* key, Value* value) {
  if (table->count == 0) return false;

  Entry* entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

bool table_set (Table* table, LoxString* key, Value value) {
  if ((double)(table->count + 1) > (double)table->capacity * TABLE_MAX_LOAD) {
    size_t capacity = GROW_TABLE_CAPACITY(table->capacity);
    adjust_capacity(table, capacity);
  }
  // Try to find the key in the map
  Entry* entry = find_entry(table->entries, table->capacity, key);
  bool is_new_key = entry->key == NULL;

  // only increment the count if we're inserting into an empty spot, not a tombstone
  if (is_new_key && IS_NIL(entry->value)) table->count++;

  entry->key = key;
  entry->value = value;
  return is_new_key;
}

bool table_delete (Table* table, LoxString* key) {
  if (table->count == 0) return false;

  // Find the entry
  Entry* entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  // Place a tombstone in the entry
  entry->key = NULL;
  entry->value = BOOL_VAL(true);
  return true;
}

void table_add_all (Table* from, Table* to) {
  for (size_t i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if (entry->key != NULL) { table_set(to, entry->key, entry->value); }
  }
}

LoxString* table_find_string (Table* table, const char* chars, size_t length, uint32_t hash) {
  if (table->count == 0) return NULL;
  size_t capacity = table->capacity;
  uint32_t index = MOD_CAPACITY(hash);
  for (;;) {
    Entry* entry = &table->entries[index];
    if (entry->key == NULL) {
      // Stop if we find an empty non-tombstone array.
      if (IS_NIL(entry->value)) return NULL;
    } else if (entry->key->length == length && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, length) == 0) {
      // We found it.
      return entry->key;
    }
    // Name collision, start linear search
    index = MOD_CAPACITY(index + 1);
  }
}

void table_remove_white (Table* table) {
  for (size_t i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key != NULL && !entry->key->obj.is_marked) { table_delete(table, entry->key); }
  }
}

void mark_table (Table* table) {
  for (size_t i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    mark_object((LoxObj*)entry->key);
    mark_value(entry->value);
  }
}
