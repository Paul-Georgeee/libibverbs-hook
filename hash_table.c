#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash_table.h"

// create a new hash table
HashTable* createTable(void) {
    HashTable *ht = (HashTable *)malloc(sizeof(HashTable));
    for (int i = 0; i < TABLE_SIZE; i++) {
        ht->table[i] = NULL;
    }
    return ht;
}

static unsigned int hash(uint64_t key) {
    return key % TABLE_SIZE;
}

// hash insert
void insert(HashTable *ht, uint64_t key, WRInfo *value) {
    unsigned int index = hash(key);
    Item *newItem = (Item *)malloc(sizeof(Item));
    newItem->key = key;
    memcpy(&newItem->value, value, sizeof(WRInfo));
    newItem->next = ht->table[index];
    ht->table[index] = newItem;
}

// hash table search
WRInfo* search(HashTable *ht, uint64_t key) {
    unsigned int index = hash(key);
    Item *current = ht->table[index];
    while (current) {
        if (current->key == key) {
            return &current->value;
        }
        current = current->next;
    }
    return NULL; // 如果没有找到，返回-1
}

void delete(HashTable *ht, uint64_t key) {
    unsigned int index = hash(key);
    Item *current = ht->table[index];
    Item *prev = NULL;
    
    while (current) {
        if (current->key == key) {
            if (prev) {
                prev->next = current->next;
            } else {
                ht->table[index] = current->next;
            }
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
    printf("[WARN]: Key %ld not found in delete function\n", key);
}


