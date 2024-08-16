/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023-2024 Fraser Heavy Software
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __ONRAMP_LIBC_STDLIB_H_INCLUDED
#define __ONRAMP_LIBC_STDLIB_H_INCLUDED

#ifndef __onramp_libc__
    #error "__onramp/__predef.h must be force-included by the preprocessor before any libc headers."
#endif

#include <__onramp/__size_t.h>
#include <__onramp/__wchar_t.h>
#include <__onramp/__null.h>



// TODO MB_CUR_MAX

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1



// exit functions

_Noreturn void abort(void);
_Noreturn void exit(int status);
_Noreturn void _Exit(int status);
char* getenv(const char* name);
_Noreturn void quick_exit(int status);
int system(const char* string);

#ifndef __onramp_cci_omc__
#ifndef __onramp_cci_opc__
int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
#endif
#endif



// number parsing functions

int atoi(const char* nptr);

#ifndef __onramp_cci_omc__
long int atol(const char* nptr);
long int strtol(const char* restrict nptr, char** restrict endptr, int base);
unsigned long int strtoul(const char* restrict nptr, char** restrict endptr, int base);
#endif

#ifndef __onramp_cci_omc__
#ifndef __onramp_cci_opc__

long long int atoll(const char* nptr);
double atof(const char* nptr);

double strtod(const char* restrict nptr, char** restrict endptr);
float strtof(const char* restrict nptr, char** restrict endptr);
long double strtold(const char* restrict nptr, char** restrict endptr);

long long int strtoll(const char* restrict nptr, char** restrict endptr, int base);
unsigned long long int strtoull(const char* restrict nptr, char** restrict endptr, int base);

#endif
#endif



// malloc functions

void* aligned_alloc(size_t __alignment, size_t __size);
void* calloc(size_t __count, size_t __element_size);
void free(void* __ptr);
void* malloc(size_t __size);
void* realloc(void* __ptr, size_t __size);
int posix_memalign(void** __ptr, size_t __alignment, size_t __size);
// TODO extensions
size_t malloc_size(void* ptr);



// rand

#ifndef __onramp_cci_omc__

#define RAND_MAX 65535
int rand(void);
void srand(unsigned int seed);
//#if 1 // TODO POSIX 199506L
int rand_r(unsigned* seed);
//#endif

#endif



// Integer arithmetic functions
int abs(int j);
#ifndef __onramp_cci_omc__
long int labs(long int j);
#ifndef __onramp_cci_opc__
long long int llabs(long long int j);
#endif
#endif
//div_t div(int numer, int denom);
//ldiv_t ldiv(long int numer, long int denom);
//lldiv_t lldiv(long long int numer, long long int denom


/* Multibyte/wide character conversion functions */
// TODO



// Searching and sorting utilities
#ifndef __onramp_cci_omc__
#ifndef __onramp_cci_opc__
void *bsearch(const void* __key, const void* __base,
        size_t __element_count, size_t __element_size,
        int (*__user_compare)(const void* __key, const void* __other));
void qsort(void* __first, size_t __element_count, size_t __element_size,
        int (*__user_compare)(const void* __left, const void* __right));
void qsort_r(void* __first, size_t __element_count, size_t __element_size,
        int (*__user_compare)(const void* __left, const void* __right, void* __user_context),
        void* __user_context);
#endif
#endif



/**
 * Gets the value of the environment variable with the given name.
 */
char* getenv(const char* key);

/**
 * Returns the bounds of the largest region of memory that is currently
 * unallocated.
 *
 * This can be used as the heap for child processes.
 *
 * The memory is not actually allocated. You must not free() this.
 *
 * TODO move this to an internal onramp header
 */
void* __malloc_largest_unused_region(size_t* out_size);

#endif