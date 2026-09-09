#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

struct free_area {
  uint8_t marker;
  struct free_area *prev;
  bool in_use;
  uint32_t length;
  struct free_area *next;
};

struct stats {
  int magical_bytes;
  bool my_simple_lock;
  uint32_t amount_of_blocks;
  uint16_t amount_of_pages;
};
typedef struct stats my_stats;
typedef struct free_area area;