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



/*
 * align_up
 *
 * Rounds a size up to the nearest multiple of ALIGNMENT.
 * Ensures all returned pointers meet minimum alignment guarantees.
 */
static size_t align_up(size_t size) {
    return (size + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);
}


/*
 * my_init
 *
 * Initializes allocator state.
 *
 * This allocator does not require explicit initialization
 * because all state is statically allocated.
 *
 * Returns:
 *   0 on success.
 */

static int my_init(void) { return 0; }

/*
 * my_teardown
 *
 * Releases all operating system resources acquired by the allocator.
 *
 * In default mode:
 *   No action is required because each allocation is unmapped
 *   immediately in my_free().
 */

static void my_teardown(void) {}

#ifdef GAMBLE_MODE

/*
 * block_t
 *
 * Represents a memory block inside a span.
 *
 * size      - Size of the usable memory region.
 * magic     - Constant used to validate integrity.
 * is_free   - Indicates whether the block is available.
 *
 * Blocks are laid out sequentially within a span.
 */
typedef struct block {
    size_t size;              // size of this block
    uint64_t magic;           // validation
    int is_free;              // 1 if free, 0 if allocated
    int padding;
    uint64_t more_padding;
} block_t;

/*
 * span_t
 *
 * Represents a contiguous region of memory obtained
 * from the OS via mmap.
 *
 * start - Beginning address of the mapped region.
 * size  - Total size of the mapped region.
 *
 * Each span initially contains a single large free block.
 */
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

#define NUM_ARENAS 6 //used to be 4

static arena_t arenas[NUM_ARENAS];



#define DEFAULT_SPAN_SIZE (3072 * 1024)   // 3072 KB

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


/*
 * my_malloc
 *
 * Allocates a block of memory of at least `size` bytes.
 *
 * Behavior:
 * - Returns NULL if size is 0 or if allocation fails.
 * - Returned memory is aligned to ALIGNMENT bytes.
 * - In default mode, memory is obtained directly via mmap.
 * - In GAMBLE_MODE, memory is allocated from an arena span.
 *
 * The returned pointer refers to usable memory and does not
 * include allocator metadata.
 */
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


/*
 * my_free
 *
 * Frees a previously allocated block.
 *
 * Behavior:
 * - If ptr is NULL, no action is taken.
 * - If the pointer is invalid (magic mismatch), the request
 *   is ignored to prevent undefined behavior.
 *
 * In default mode:
 * - The underlying memory is released using munmap.
 *
 * In GAMBLE_MODE:
 * - The block is marked as free and may be reused.
 */
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


/*
 * my_realloc
 *
 * Resizes a previously allocated block.
 *
 * Behavior:
 * - If ptr is NULL, behaves like malloc(size).
 * - If size is 0, behaves like free(ptr) and returns NULL.
 * - Otherwise, allocates a new block, copies the minimum
 *   of old and new sizes, and frees the old block.
 *
 * Returns:
 * - Pointer to resized memory, or NULL on failure.
 */
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


/*
 * my_calloc
 *
 * Allocates memory for an array of nmemb elements of size bytes each.
 *
 * Behavior:
 * - Returns NULL if overflow would occur.
 * - Memory is zero-initialized.
 * - Internally calls my_malloc and then memset to zero.
 */
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


/*
 * my_malloc
 *
 * Allocates a block of memory of at least `size` bytes.
 *
 * Behavior:
 * - Returns NULL if size is 0 or if allocation fails.
 * - Returned memory is aligned to ALIGNMENT bytes.
 * - In default mode, memory is obtained directly via mmap.
 * - In GAMBLE_MODE, memory is allocated from an arena span.
 *
 * The returned pointer refers to usable memory and does not
 * include allocator metadata.
 */
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


/*
 * my_free
 *
 * Frees a previously allocated block.
 *
 * Behavior:
 * - If ptr is NULL, no action is taken.
 * - If the pointer is invalid (magic mismatch), the request
 *   is ignored to prevent undefined behavior.
 *
 * In default mode:
 * - The underlying memory is released using munmap.
 *
 * In GAMBLE_MODE:
 * - The block is marked as free and may be reused.
 */
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


/*
 * my_realloc
 *
 * Resizes a previously allocated block.
 *
 * Behavior:
 * - If ptr is NULL, behaves like malloc(size).
 * - If size is 0, behaves like free(ptr) and returns NULL.
 * - Otherwise, allocates a new block, copies the minimum
 *   of old and new sizes, and frees the old block.
 *
 * Returns:
 * - Pointer to resized memory, or NULL on failure.
 */
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


/*
 * my_calloc
 *
 * Allocates memory for an array of nmemb elements of size bytes each.
 *
 * Behavior:
 * - Returns NULL if overflow would occur.
 * - Memory is zero-initialized.
 * - Internally calls my_malloc and then memset to zero.
 */
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
                         .version = "1.2.0",
                         .description = "Dual-mode mmap-based allocator with arena/span support.",
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


