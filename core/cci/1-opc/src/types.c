#include "types.h"

#include <stdlib.h>
#include <string.h>

#include "type.h"
#include "record.h"

#define TAG_TYPEDEF 1
#define TAG_STRUCT 3
#define TAG_UNION 2

/*
 * Types are store in a simple hashtable with open addressing. A bucket is
 * empty if its name is NULL.
 *
 * The name and tag together form a key. The same name can be used for
 * different tags (because, for example, a typedef of `foo` is different from
 * `struct foo` and `union foo`.)
 */
static char** types_names;
static int* types_tags; // typedef, struct or union
static void** types_objects; // type_t* or record_t*
static size_t types_buckets;

void types_init(void) {
    // TODO we could make this growable later. For now we don't bother.
    types_buckets = 256;
    types_names = calloc(types_buckets, sizeof(char*));
    types_tags = malloc(types_buckets * sizeof(int));
    types_objects = malloc(types_buckets * sizeof(void*));

    if (types_names == NULL) {fatal("Out of memory.");}
    if (types_tags == NULL) {fatal("Out of memory.");}
    if (types_objects == NULL) {fatal("Out of memory.");}
}

void types_destroy(void) {
    size_t i = 0;
    while (i < types_buckets) {
        char* name = *(types_names + i);
        if (name != NULL) {
            free(name);
            int tag = *(types_tags + i);
            if (tag == TAG_TYPEDEF) {
                type_delete(*(types_objects + i));
            }
            if (tag != TAG_TYPEDEF) {
                record_delete(*(types_objects + i));
            }
        }
        i = (i + 1);
    }
}

/**
 * Finds the bucket for a type or record with the given name, or the empty
 * bucket where it should be inserted if it doesn't exist.
 */
int types_find_bucket(const char* name, int tag) {
    int hash = (fnv1a_cstr(name) + (tag * 31));
    int mask = (types_buckets - 1);
    int index = (hash & mask);
    while (1) {
        char* bucket_name = *(types_names + index);
        int bucket_tag = *(types_tags + index);
        if (bucket_name != NULL) {
            if (tag == bucket_tag) {
                if (0 == strcmp(name, bucket_name)) {
                    return index;
                }
            }
        }
        if (bucket_name == NULL) {
            return index;
        }
        index = ((index + 1) & mask);
    }
}

type_t* types_add_typedef(char* name, type_t* type) {
    int index = types_find_bucket(name, TAG_TYPEDEF);
    if (*(types_names + index) != NULL) {
        // The typedef already exists. Return the existing type.
        free(name);
        type_delete(type);
        return *(types_objects + index);
    }

    // The typedef does not exist. Add it.
    *(types_names + index) = name;
    *(types_tags + index) = TAG_TYPEDEF;
    *(types_objects + index) = type;
    return type;
}

static void types_add_record(int tag, record_t* record) {
    const char* name = record_name(record);
    int index = types_find_bucket(name, tag);
    if (*(types_names + index) != NULL) {
        // If this happens there's a bug; the parser should be checking for
        // pre-existing struct/union declarations first.
        fatal("Internal error: record already exists.");
    }

    // Add the record
    *(types_names + index) = strdup_checked(name);
    *(types_tags + index) = tag;
    *(types_objects + index) = record;
}

void types_add_struct(record_t* record) {
    types_add_record(TAG_STRUCT, record);
}

void types_add_union(record_t* record) {
    types_add_record(TAG_UNION, record);
}

void* types_find_object(const char* name, int tag) {
    int index = types_find_bucket(name, tag);
    if (*(types_names + index) != NULL) {
        return *(types_objects + index);
    }
    return NULL;
}

const type_t* types_find_typedef(const char* name) {
    return types_find_object(name, TAG_TYPEDEF);
}

record_t* types_find_struct(const char* name) {
    return types_find_object(name, TAG_STRUCT);
}

record_t* types_find_union(const char* name) {
    return types_find_object(name, TAG_UNION);
}

