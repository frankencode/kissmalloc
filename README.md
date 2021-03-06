# kissmalloc

*kissmalloc* is a simple and safe, yet extraordinary fast memory allocator. It utilizes purely mmap(2) sytem calls to manage memory. It single-threaded applications it has shown to out-perform glibc's memory allocator by a factor of at least 2. In embedded multi-core systems it truely shines (see performance stats below).

## Design

*kissmalloc* is a truly forward-only memory allocator, which never reuses dirty memory pages. It only passes on allocations on clean zero initialized pages, as generated by the operating system kernel. It utilizes page caching only to reduce lock contentions in the global page table of the kernel. It tries to minimize user-level memory management meta data, e.g. it doesn't have any free lists. But it does keep separate allocation zones for individual threads.

Each thread's allocation is guaranteed to be placed on distinct memory pages and any thread's consecutive allocations are guaranteed to be packed as tightly as possible. While most of these design decisions were motivated by safety concerns, it also lead to a very performant allocator, which places allocated data in a cache-friendly manner effectively preventing false sharing. The only thing that *kissmalloc* does not support effectively are long-running single-threaded batch processing programs.

## Dependencies

*kissmalloc* is build to run on any modern POSIX compatible operating system. For performance reasons it utilizes some gcc compiler intrinsics (e.g. `__builtin_ctz`). Building the library in debug mode in addition requires valgrind headers.

*Let me know if you run into problems!*

## How to use

The memory allocator comes in a single translation unit. For C programs simply copy `src/kissmalloc.c` and `src/kissmalloc.h` into your project. Comment out in  `kissmalloc.h` the following line if you want to overload the standard libc malloc/free functions:
```C
// #define KISSMALLOC_OVERLOAD_LIBC
```
Otherwise you'll get all memory management functions prefixed by "kiss":
```
void *kissmalloc(size_t size);
void kissfree(void *ptr);
void *kisscalloc(size_t number, size_t size);
void *kissrealloc(void *ptr, size_t size);
int kissposix_memalign(void **ptr, size_t alignment, size_t size);

void *kissaligned_alloc(size_t alignment, size_t size);
void *kissmemalign(size_t alignment, size_t size);
void *kissvalloc(size_t size);
void *kisspvalloc(size_t size);
```
These functions behave semantically exactly the same as the standard libc library functions without the "kiss" in their name:
 * https://en.cppreference.com/w/c/memory/malloc
 * https://en.cppreference.com/w/c/memory/free
 * https://en.cppreference.com/w/c/memory/calloc
 * https://en.cppreference.com/w/c/memory/realloc
 * https://linux.die.net/man/3/memalign

## How to use in C++

*kissmalloc* also contains overloads for the C++ **new** and **delete** operators. You do not need to modify the kissmalloc.h! Just drop the files `src/kissmalloc.c`, `src/kissmalloc.h` and `src/kissmalloc_new.cc` into your C++ project. Alternatively you can also build and link kissmalloc as a library.

## Building the library

Open a terminal and issue the following commands:

```
git clone https://gitlab.com/frankencode/kissmalloc.git
cd kissmalloc/
./build.sh
```

You can build out-of-source, too. The target directory will contain the library and a few benchmarking programs.

## Benchmark Example: Quad Core Allwinner A64 64-bit Cortex-A53

 * glibc 2.23 and musl 1.1.9 compared to kissmalloc 0.1
 * test system: Teres-I with Ubuntu 16.04
 * test programs: kissbench, kissbench_libc, etc.

#### Single-threaded malloc, free

 * allocation object count: 1e7
 * minimum object size: 12 bytes
 * maximum object size: 130 bytes

| glibc malloc | glibc free | musl malloc | musl free | kissmalloc malloc | kissmalloc free |
|--------------|------------|-------------|-----------|-------------------|-----------------|
| 406 ns       | 214 ns     | 488 ns      | 370 ns    | 141 ns            | 67 ns           |

 * *kissmalloc* is on average **2.98** times faster than glibc.
 * *kissmalloc* is on average **4.13** times faster than musl.

#### Multi-threaded malloc, free

  * allocation object count: 1e6
  * minimum object size: 12 bytes
  * maximum object size: 130 bytes
  * number of threads: 4

| glibc malloc | glibc free | musl malloc | musl free | kissmalloc malloc | kissmalloc free |
|--------------|------------|-------------|-----------|-------------------|-----------------|
| 2129 ns      | 501 ns     | 1369 ns     | 986 ns    | 250 ns            | 157 ns          |

 * *kissmalloc* is on average **6.46** times faster that glibc.
 * *kissmalloc* is on average **5.79** times faster than musl.

#### Single-threaded C++ std::list&lt;int&gt;

 * allocation object count: 1e6

| glibcxx push_back | glibcxx pop_back | kissmalloc push_back | kissmalloc pop_back |
|-------------------|------------------|----------------------|---------------------|
| 292 ns            | 122 ns           | 167 ns               | 88 ns               |

 * std::list&lt;int&gt; is on average **1.62** times faster when linking against *kissmalloc*.
