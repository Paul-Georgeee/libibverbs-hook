#include <sys/time.h>
#include <stdint.h>

#define TABLE_SIZE 65536  // 定义哈希表大小

typedef struct WRInfo{
    uint64_t original_wr_id;
    struct timeval post_timestamp;
    int data_size;
} WRInfo;

typedef struct Item {
    uint64_t key;
    WRInfo value;
    struct Item *next;
} Item;

typedef struct HashTable {
    Item *table[TABLE_SIZE];
} HashTable;

HashTable* createTable(void);
void insert(HashTable *ht, uint64_t key, WRInfo *value);
WRInfo* search(HashTable *ht, uint64_t key);
void delete(HashTable *ht, uint64_t key);