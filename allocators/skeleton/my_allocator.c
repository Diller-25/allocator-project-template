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




static size_t align_up(size_t size) {
    return (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}


/*
 * Replace these stubs with your own logic.
 */

static int my_init(void) { return 0; }

static void my_teardown(void) {}

#ifdef GAMBLE_MODE

//Allocator Statistics
// static size_t total_allocs = 0;
// static size_t total_frees = 0;
// static size_t total_bytes_requested = 0;
// static size_t largest_allocation = 0;
// static size_t current_live_blocks = 0;
// static size_t peak_live_blocks = 0;

typedef struct block {
    size_t size;              // size of this block
    uint64_t magic;           // validation
    int is_free;              // 1 if free, 0 if allocated
    int padding;
    uint64_t more_padding;
} block_t;

#define MAX_SPAN_SEARCH_ATTEMPTS 5
typedef struct span {
    void *start;              // beginning of mmap region
    size_t size;              // total size of span
} span_t;

#define MAX_SPANS 1024

typedef struct arena {
    span_t spans[MAX_SPANS];
    size_t span_count;
} arena_t;

#define NUM_ARENAS 4

static arena_t arenas[NUM_ARENAS];



#define DEFAULT_SPAN_SIZE (256 * 1024)   // 256 KB

static int init_span(span_t *span, size_t span_size) {
    if (span_size < sizeof(block_t))
        return -1;

    void *mem = mmap(NULL, span_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mem == MAP_FAILED)
        return -1;

    span->start = mem;
    span->size = span_size;

    //Create a big free block inside of span
    block_t *block = (block_t *)mem;
    block->size = span_size - sizeof(block_t);
    block->magic = MAGIC;
    block->is_free = 1;

    return 0;
}

static block_t *find_first_fit(span_t *span, size_t size) {
    char *cursor = (char *)span->start;
    char *end = cursor + span->size;

    while (cursor + sizeof(block_t) <= end) {
        block_t *block = (block_t *)cursor;

        if (block->magic != MAGIC)
            return NULL; // guard against evil

        if (block->is_free && block->size >= size) {
            return block;
        }

        cursor += sizeof(block_t) + block->size;
    }

    return NULL;
}

static void split_block(block_t *block, size_t size) {
    size_t remaining = block->size - size - sizeof(block_t);

    if (remaining < ALIGNMENT)
        return; // not worth splitting

    char *new_block_addr = (char *)(block + 1) + size;
    block_t *new_block = (block_t *)new_block_addr;

    new_block->size = remaining;
    new_block->magic = MAGIC;
    new_block->is_free = 1;

    block->size = size;
}


static arena_t *pick_random_arena(void) {
    return &arenas[rand() % NUM_ARENAS];
}


static span_t *pick_random_span(arena_t *arena) {
    if (arena->span_count == 0)
        return NULL;

    return &arena->spans[rand() % arena->span_count];
}

static void *my_malloc(size_t size) {
    if (size == 0 || size > SIZE_MAX - sizeof(block_t))
        return NULL;

    size_t aligned = align_up(size);

    arena_t *arena = pick_random_arena();

    for (int attempt = 0; attempt < MAX_SPAN_SEARCH_ATTEMPTS; attempt++) {

        span_t *span = pick_random_span(arena);
        if (!span)
            break;

        block_t *block = find_first_fit(span, aligned);

        if (block) {
            if (block->size >= aligned + sizeof(block_t) + ALIGNMENT)
                split_block(block, aligned);

            block->is_free = 0;
            block->magic = MAGIC;
            // total_allocs++;
            // total_bytes_requested += size;
            return (void *)(block + 1);
        }
    }

    // If search failed create new span
    if (arena->span_count < MAX_SPANS) {

        span_t *new_span = &arena->spans[arena->span_count++];

        if (init_span(new_span, DEFAULT_SPAN_SIZE) == 0) {

            block_t *block = find_first_fit(new_span, aligned);
            if (!block)
                return NULL;

            if (block->size >= aligned + sizeof(block_t) + ALIGNMENT)
                split_block(block, aligned);

            block->is_free = 0;

            // total_allocs++;
            // total_bytes_requested += size;

            return (void *)(block + 1);
        }
    }

    return NULL;
}

static void my_free(void *ptr) {
  if(ptr == NULL){
    return;
  }
  block_t *block = ((block_t *)ptr) - 1;

  if (block->magic != MAGIC)
      return;

  block->is_free = 1;
  block->magic = MAGIC; //dont turn to 0 here, messes up linear search
}

static void *my_realloc(void *ptr, size_t size) {

  if (ptr == NULL) {
        return my_malloc(size);
    }

    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    block_t *old_block = ((block_t *)ptr) - 1;

    if (old_block->magic != MAGIC)
        return NULL;  // invalid pointer

    size_t old_size = old_block->size;

    void *new_ptr = my_malloc(size);
    if (!new_ptr)
        return NULL;

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
    if (!ptr)
        return NULL;

    memset(ptr, 0, total);
    return ptr;
}

#else    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    size_t size;   // the user requested size (aligned)
    uint64_t magic;  //header 16 byte sized
} header_t;

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

#endif
allocator_t allocator = {.malloc = my_malloc,
                         .free = my_free,
                         .realloc = my_realloc,
                         .calloc = my_calloc,
                         .init = my_init,
                         .teardown = my_teardown,
                         .name = "dualalloc",
                         .author = "Dylan Pachan",
                         .version = "0.9.0",
                         .description = "Schizo allocator.",
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

//Gamble (:

// #ifdef GAMBLE_MODE

// __attribute__((destructor))
//   static void casino_report(void){
//     printf("\n======= MEMORY CASINO REPORT =======\n");
//     printf("Total bets placed:         %zu\n", total_allocs);
//     printf("Total cash-outs:           %zu\n", total_frees);
//     printf("Total bytes wagered:       %zu\n", total_bytes_requested);
//     printf("Largest bet placed:        %zu\n", largest_allocation);
//     printf("Most hands played at once: %zu\n", peak_live_blocks);
//     printf("Currently playing:         %zu\n", current_live_blocks);
//     printf("====================================\n");
//   }
// #endif

