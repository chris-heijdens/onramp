#include "global.h"

#include <stdlib.h>
#include <string.h>

/**
 * A global looks like this:
 *
 * struct global_t {
 *     char* name;
 *     type_t* type; // return type for functions
 *     bool variadic;
 *     int arg_count; // -1 for variables
 *     type_t* arg_types[];
 * }
 *
 * As usual we're faking structs with wrapper functions and manual pointer
 * arithmetic. Each value is the size of a void* for simplicity.
 */

#define GLOBAL_NAME_OFFSET 0
#define GLOBAL_TYPE_OFFSET 1
#define GLOBAL_VARIADIC_OFFSET 2
#define GLOBAL_ARGS_COUNT_OFFSET 3
#define GLOBAL_ARG_TYPES_OFFSET 4
#define GLOBAL_FIELDS 4  // not including arg_types

static global_t** globals;
static size_t globals_count;
static size_t globals_buckets; // power of two

static void global_delete(global_t* global) {
    if (global_is_function(global)) {
        int arg_count = global_function_arg_count(global);
        while (arg_count > 0) {
            arg_count = (arg_count - 1);
            type_delete(*(type_t**)((void**)global + (GLOBAL_ARG_TYPES_OFFSET + arg_count)));
        }
    }

    free(*(char**)((void**)global + GLOBAL_NAME_OFFSET));
    type_delete(*(type_t**)((void**)global + GLOBAL_TYPE_OFFSET));

    free(global);
}

void globals_init(void) {
    // TODO start with a smaller table once growable
    globals_buckets = 512;
    globals = calloc(globals_buckets, sizeof(global_t*));
}

void globals_destroy(void) {
    size_t i = 0;
    while (i < globals_buckets) {
        global_t* global = *(globals + i);
        if (global) {
            global_delete(global);
        }
        i = (i + 1);
    }
}

static global_t** global_find_bucket(const char* name) {
    int hash = fnv1a_cstr(name);
    int mask = (globals_buckets - 1);
    int index = (hash & mask);
    while (1) {
        global_t** bucket = (globals + index);
        if (*bucket == NULL) {
            return bucket;
        }
        if (0 == strcmp(name, global_name(*bucket))) {
            return bucket;
        }
        index = ((index + 1) & mask);
    }
}

global_t* global_find(const char* name) {
    return *global_find_bucket(name);
}

/**
 * Ensures a re-declaration of a global matches the existing declaration.
 */
static void global_check_match(global_t* left, global_t* right) {
    if (global_is_variable(left) != global_is_variable(right)) {
        fatal_2("Global symbol re-declared as different kind: ", global_name(left));
    }

    if (!type_equal(global_type(left), global_type(right))) {
        if (global_is_variable(left)) {
            fatal_2("Global variable re-declared with a different type:", global_name(left));
        }
        fatal_2("Function re-declared with a different return type: ", global_name(left));
    }

    if (global_is_variable(left)) {
        return;
    }

    int arg_count = global_function_arg_count(left);
    if (arg_count != global_function_arg_count(right)) {
        fatal_2("Function re-declared with a different number of arguments:", global_name(left));
    }

    int i = 0;
    while (i < arg_count) {
        if (!type_equal(global_function_arg_type(left, i), global_function_arg_type(right, i))) {
            fatal_2("Function re-declared with different argument types:", global_name(left));
        }
        i = (i + 1);
    }

    if (global_function_is_variadic(left) != global_function_is_variadic(right)) {
        fatal_2("Function re-declared with a different variadic argument:", global_name(left));
    }
}

static global_t* global_add(global_t* global) {
    global_t** bucket = global_find_bucket(global_name(global));

    // check for duplicates
    if (*bucket != NULL) {
        global_check_match(global, *bucket);
        global_delete(global);
        return *bucket;
    }

    // check total
    if (globals_count == (globals_buckets >> 1)) {
        // It should be easy to make this grow. We just have to allocate a new
        // table and rehash. For now we don't bother. We'll just make it big
        // enough to bootstrap cci/2 and our other tools.
        fatal("Too many globals.");
    }

    // add it
    globals_count = (globals_count + 1);
    *bucket = global;
    return global;
}

const global_t* global_declare_variable(type_t* type, char* name) {
    global_t* global = malloc(sizeof(void*) * GLOBAL_FIELDS);
    *(char**)((void**)global + GLOBAL_NAME_OFFSET) = name;
    *(type_t**)((void**)global + GLOBAL_TYPE_OFFSET) = type;
    *(bool*)((void**)global + GLOBAL_VARIADIC_OFFSET) = false; // should never be accessed
    *(int*)((void**)global + GLOBAL_ARGS_COUNT_OFFSET) = -1;
    return global_add(global);
}

const global_t* global_declare_function(type_t* return_type, char* name,
        int arg_count, type_t** arg_types)
{
    global_t* global = malloc(sizeof(void*) * (GLOBAL_FIELDS + arg_count));
    *(char**)((void**)global + GLOBAL_NAME_OFFSET) = name;
    *(type_t**)((void**)global + GLOBAL_TYPE_OFFSET) = return_type;
    *(bool*)((void**)global + GLOBAL_VARIADIC_OFFSET) = false;
    *(int*)((void**)global + GLOBAL_ARGS_COUNT_OFFSET) = arg_count;
    memcpy((void**)global + GLOBAL_ARG_TYPES_OFFSET, arg_types, sizeof(type_t*) * arg_count);
    return global_add(global);
}

void global_set_variadic(global_t* global, bool variadic) {
    assert(global_is_function(global));
    *(bool*)((void**)global + GLOBAL_VARIADIC_OFFSET) = variadic;
}

const char* global_name(const global_t* global) {
    return *(char**)((void**)global + GLOBAL_NAME_OFFSET);
}

const type_t* global_type(const global_t* global) {
    return *(type_t**)((void**)global + GLOBAL_TYPE_OFFSET);
}

bool global_is_variable(const global_t* global) {
    int arg_count = *(int*)((void**)global + GLOBAL_ARGS_COUNT_OFFSET);
    return arg_count == -1;
}

bool global_is_function(const global_t* global) {
    int arg_count = *(int*)((void**)global + GLOBAL_ARGS_COUNT_OFFSET);
    return arg_count != -1;
}

bool global_function_is_variadic(const global_t* global) {
    assert(global_is_function(global));
    return *(bool*)((void**)global + GLOBAL_VARIADIC_OFFSET);
}

int global_function_arg_count(const global_t* global) {
    assert(global_is_function(global));
    return *(int*)((void**)global + GLOBAL_ARGS_COUNT_OFFSET);
}

type_t* global_function_arg_type(const global_t* global, int index) {
    assert(index < global_function_arg_count(global));
    return *(type_t**)((void**)global + (GLOBAL_ARG_TYPES_OFFSET + index));
}
