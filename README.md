# dualalloc

## Author

Dylan Pachan

## Version

1.0.0

------------------------------------------------------------------------

## Overview

`dualalloc` is a custom memory allocator that uses `mmap` as its memory
backend.\
It supports two operating modes:

1.  **Default Mode**
2.  **GAMBLE_MODE (Arena-Based Mode)**

Both modes implement the standard allocation interface:

-   `malloc`
-   `free`
-   `realloc`
-   `calloc`

All returned memory is aligned to 16 bytes.

------------------------------------------------------------------------

## Memory Backend

All memory is obtained directly from the operating system using `mmap`.

The allocator does not rely on the system `malloc`. Instead, it manages
memory explicitly with:

-   `mmap` for acquiring memory
-   `munmap` for releasing memory (in default mode)

The allocator metadata declares `"mmap"` as the memory backend.

------------------------------------------------------------------------

## Default Mode Design

In default mode:

-   Each call to `malloc` directly invokes `mmap`
-   Each call to `free` directly invokes `munmap`
-   No persistent allocator state is maintained

This design prioritizes simplicity and correctness.\
Each allocation corresponds to a dedicated memory mapping.

------------------------------------------------------------------------

## GAMBLE_MODE Design

When compiled with `GAMBLE_MODE`, the allocator switches to an
arena-based design.

### Architecture

-   The allocator maintains multiple arenas.
-   Each arena manages multiple spans.
-   Each span is a large contiguous region obtained via `mmap`.
-   Spans are subdivided into blocks.
-   Each block contains metadata including:
    -   Block size
    -   Allocation state
    -   Validation magic value

### Allocation Strategy

-   Allocation searches for a free block within a randomly selected
    arena.
-   If a suitable block is found, it may be split.
-   If no suitable block exists, a new span is allocated using `mmap`.

### Freeing

-   Freed blocks are marked as free.
-   Freed memory remains within the span for reuse.
-   Memory is not immediately returned to the OS in this mode.

------------------------------------------------------------------------

## Alignment Guarantees

All allocations are aligned to 16 bytes.

Alignment is enforced by rounding requested sizes up to the nearest
multiple of 16 before allocation.

------------------------------------------------------------------------

## Safety Features

-   Magic value validation helps detect invalid frees.
-   `calloc` includes overflow protection before multiplication.
-   `realloc` correctly handles:
    -   `realloc(NULL, size)`
    -   `realloc(ptr, 0)`
    -   resizing with data preservation

------------------------------------------------------------------------

## Limitations

-   No coalescing of adjacent free blocks.
-   Not thread-safe.
-   No guard pages.
-   No per-thread caching.
-   In arena mode, spans remain mapped for the lifetime of the process.

------------------------------------------------------------------------

## Summary

`dualalloc` demonstrates two allocation strategies built directly on top
of `mmap`:

-   A simple per-allocation mapping approach
-   An arena-based span allocator
