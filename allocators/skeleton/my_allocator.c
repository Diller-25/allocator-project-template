#include "allocator.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGNMENT 16

#define MAGIC 0xC0FFEE1234ABCDEF //fight against THE POISONNN

#ifdef GAMBLE_MODE

static size_t total_allocs = 0;
static size_t total_frees = 0;
static size_t total_bytes_requested = 0;
static size_t largest_allocation = 0;
static size_t current_live_blocks = 0;
static size_t peak_live_blocks = 0;

#endif


typedef struct {
    size_t size;   // the user requested size (aligned)
    uint64_t magic;  //header 16 byte sized
} header_t;



static size_t align_up(size_t size) {
    return (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}


/*
 * Replace these stubs with your own logic.
 */

static int my_init(void) { return 0; }

static void my_teardown(void) {}

static void *my_malloc(size_t size) {
  if (size == 0 || size > SIZE_MAX - sizeof(header_t)){
    return NULL;
  }

  size_t aligned = align_up(size);
  size_t total = sizeof(header_t) + aligned;

  void *block =mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(block == MAP_FAILED){
    return NULL;
  }

  header_t *header = (header_t *)block;
  header->size = aligned;
  header->magic = MAGIC;
  return (void *)(header+1);
}

static void my_free(void *ptr) {
  if(ptr == NULL){
    return;
  }


  
  header_t *header = ((header_t *)ptr) -1;
  size_t total = sizeof(header_t) + header->size;

  if (header->magic != MAGIC)
    return;  // reject invalid free, ts IS NOT magical

  header->magic = 0; // keel da poison
  munmap((void *)header, total);
}

static void *my_realloc(void *ptr, size_t size) {

  if (ptr == NULL) {
        return my_malloc(size);
    }

    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    header_t *old_header = ((header_t *)ptr) - 1;
    size_t old_size = old_header->size;

    void *new_ptr = my_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    size_t copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, ptr, copy_size);

    my_free(ptr);

    return new_ptr;
}

static void *my_calloc(size_t nmemb, size_t size) {
  if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        return NULL;  // overflow
    }

    size_t total = nmemb * size;

    void *ptr = my_malloc(total);
    if (ptr == NULL) {
        return NULL;
    }

    memset(ptr, 0, total);
    return ptr;
}

allocator_t allocator = {.malloc = my_malloc,
                         .free = my_free,
                         .realloc = my_realloc,
                         .calloc = my_calloc,
                         .init = my_init,
                         .teardown = my_teardown,
                         .name = "normalloc",
                         .author = "Dylan Pachan",
                         .version = "0.7.0",
                         .description = "Gamble allocator.",
                         .memory_backend = "mmap",
                         .features = {.thread_safe = false,
                                      .per_thread_cache = false,
                                      .huge_page_support = false,
                                      .guard_pages = false,
                                      .guard_location = GUARD_NONE,
                                      .min_alignment = ALIGNMENT,
                                      .max_alignment = 4096}};

allocator_t *get_test_allocator(void) { return &allocator; }

allocator_t *get_bench_allocator(void) { return &allocator; }