/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Fraser Heavy Software
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

#include "parse_decl.h"

#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "libo-vector.h"
#include "record.h"
#include "enum.h"
#include "strings.h"
#include "type.h"
#include "scope.h"
#include "node.h"
#include "parse_stmt.h"
#include "parse_expr.h"
#include "parse_init.h"
#include "lexer.h"
#include "token.h"
#include "options.h"
#include "generate.h"
#include "function.h"
#include "emit.h"
#include "symbol.h"
#include "optimize_tree.h"
#include "optimize_asm.h"

extern struct function_t* current_function;

void parse_decl_init(void) {
}

void parse_decl_destroy(void) {
}



/*
 * Specifiers
 */

// storage specifiers, excluding _Thread_local
typedef enum storage_specifier_t {
    storage_specifier_none = 0,
    storage_specifier_typedef,
    storage_specifier_extern,
    storage_specifier_static,
    storage_specifier_auto,
    storage_specifier_register,
} storage_specifier_t;

/*
 * The specifier sequence of a declaration (the part that does not include the
 * declarators.)
 */
typedef struct specifiers_t {
    storage_specifier_t storage_specifier;

    int type_specifiers;
    int type_qualifiers;
    int function_specifiers;
    //int alignment_specifier; // value, not flags; 0 if not provided

    // We hold a strong reference to type because it might be an anonymous
    // record, in which case nothing else holds a reference to it until the
    // declarator is parsed.
    type_t* type;
    //record_t* record; // TODO probably don't need these, type_t* can cover it
    //enum_t* enum_;

    // this should have a strong reference
    //bool is_struct;
    //token_t* record_name;
} specifiers_t;

static void specifiers_init(specifiers_t* specifiers) {
    memset(specifiers, 0, sizeof(*specifiers));
}

static void specifiers_destroy(specifiers_t* specifiers) {
    if (specifiers->type)
        type_deref(specifiers->type);
}

#define TYPE_SPECIFIER_VOID         (1 << 0)
#define TYPE_SPECIFIER_CHAR         (1 << 1)
#define TYPE_SPECIFIER_SHORT        (1 << 2)
#define TYPE_SPECIFIER_INT          (1 << 3)
#define TYPE_SPECIFIER_LONG         (1 << 4)
#define TYPE_SPECIFIER_LONG_LONG    (1 << 5)
#define TYPE_SPECIFIER_SIGNED       (1 << 6)
#define TYPE_SPECIFIER_UNSIGNED     (1 << 7)
#define TYPE_SPECIFIER_RECORD       (1 << 8)
#define TYPE_SPECIFIER_ENUM         (1 << 9)
#define TYPE_SPECIFIER_TYPEDEF      (1 << 10)
#define TYPE_SPECIFIER_BOOL         (1 << 11)
#define TYPE_SPECIFIER_FLOAT        (1 << 12)
#define TYPE_SPECIFIER_DOUBLE       (1 << 13)

#define TYPE_QUALIFIER_CONST        (1 << 0)
#define TYPE_QUALIFIER_VOLATILE     (1 << 1)
#define TYPE_QUALIFIER_RESTRICT     (1 << 2)
//#define TYPE_QUALIFIER_ATOMIC       (1 << 3)

#define FUNCTION_SPECIFIER_INLINE     (1 << 0)
#define FUNCTION_SPECIFIER_NORETURN   (1 << 1)

/**
 * Converts a set of type specifiers to a base type.
 *
 * The full list of supported combinations is in 6.7.2.2 in the C17 spec. We
 * reproduce the primitive base types here as a switch.
 *
 * We exclude those that specify a user-defined type (struct, union, enum and
 * typedef name.) In those cases `specifiers->type` will be non-null.
 */
static base_t specifiers_convert(specifiers_t* specifiers) {
    assert(specifiers->type == NULL);

    switch (specifiers->type_specifiers) {
        case 0:
            warn(warning_implicit_int, lexer_token,
                    "Unrecognized type, or no type specifiers for this declaration."); // (Implicit int was removed in C99.)
            return BASE_SIGNED_INT;

        case TYPE_SPECIFIER_VOID:
            return BASE_VOID;

        case TYPE_SPECIFIER_CHAR:
            return BASE_CHAR;

        case TYPE_SPECIFIER_SIGNED | TYPE_SPECIFIER_CHAR:
            return BASE_SIGNED_CHAR;

        case TYPE_SPECIFIER_UNSIGNED | TYPE_SPECIFIER_CHAR:
            return BASE_UNSIGNED_CHAR;

        case TYPE_SPECIFIER_SHORT:
        case TYPE_SPECIFIER_SIGNED | TYPE_SPECIFIER_SHORT:
        case TYPE_SPECIFIER_SHORT | TYPE_SPECIFIER_INT:
        case TYPE_SPECIFIER_SIGNED | TYPE_SPECIFIER_SHORT | TYPE_SPECIFIER_INT:
            return BASE_SIGNED_SHORT;

        case TYPE_SPECIFIER_UNSIGNED | TYPE_SPECIFIER_SHORT:
        case TYPE_SPECIFIER_UNSIGNED | TYPE_SPECIFIER_SHORT | TYPE_SPECIFIER_INT:
            return BASE_UNSIGNED_SHORT;

        case TYPE_SPECIFIER_INT:
        case TYPE_SPECIFIER_SIGNED:
        case TYPE_SPECIFIER_SIGNED | TYPE_SPECIFIER_INT:
            return BASE_SIGNED_INT;

        case TYPE_SPECIFIER_UNSIGNED:
        case TYPE_SPECIFIER_UNSIGNED | TYPE_SPECIFIER_INT:
            return BASE_UNSIGNED_INT;

        case TYPE_SPECIFIER_LONG:
        case TYPE_SPECIFIER_SIGNED | TYPE_SPECIFIER_LONG:
        case TYPE_SPECIFIER_LONG | TYPE_SPECIFIER_INT:
        case TYPE_SPECIFIER_SIGNED | TYPE_SPECIFIER_LONG | TYPE_SPECIFIER_INT:
            //return BASE_SIGNED_LONG; // TODO implement this to make _Generic work properly
            return BASE_SIGNED_INT;

        case TYPE_SPECIFIER_UNSIGNED | TYPE_SPECIFIER_LONG:
        case TYPE_SPECIFIER_UNSIGNED | TYPE_SPECIFIER_LONG | TYPE_SPECIFIER_INT:
            //return BASE_UNSIGNED_LONG; // TODO implement this to make _Generic work properly
            return BASE_UNSIGNED_INT;

        case TYPE_SPECIFIER_LONG_LONG:
        case TYPE_SPECIFIER_SIGNED | TYPE_SPECIFIER_LONG_LONG:
        case TYPE_SPECIFIER_LONG_LONG | TYPE_SPECIFIER_INT:
        case TYPE_SPECIFIER_SIGNED | TYPE_SPECIFIER_LONG_LONG | TYPE_SPECIFIER_INT:
            return BASE_SIGNED_LONG_LONG;

        case TYPE_SPECIFIER_UNSIGNED | TYPE_SPECIFIER_LONG_LONG:
        case TYPE_SPECIFIER_UNSIGNED | TYPE_SPECIFIER_LONG_LONG | TYPE_SPECIFIER_INT:
            return BASE_UNSIGNED_LONG_LONG;

        case TYPE_SPECIFIER_FLOAT:
            return BASE_FLOAT;

        case TYPE_SPECIFIER_DOUBLE:
            return BASE_DOUBLE;

        case TYPE_SPECIFIER_LONG | TYPE_SPECIFIER_DOUBLE:
            //return BASE_LONG_DOUBLE; // TODO implement this to make _Generic work properly
            return BASE_DOUBLE;

        case TYPE_SPECIFIER_BOOL:
            return BASE_BOOL;

        // _Complex is not supported

        default:
            break;
    }

    fatal_token(lexer_token, "Invalid combination of type specifiers.");
}

static void specifiers_check_type(specifiers_t* specifiers) {

    // If we have a user-defined type, make sure we have no other type specifiers
    if (specifiers->type != NULL) {
        if (specifiers->type_specifiers != 0) {
            fatal_token(lexer_token, "Invalid combination of type specifiers.");
        }
        return;
    }

    // Otherwise, make sure the combination of type specifiers we've
    // accumulated so far is valid. This places the error message for an
    // invalid combination on the first incorrect token.
    //
    // (There is no valid combination of specifiers where a subset of those
    // specifiers would be invalid so this check is safe.)
    //
    // We ignore the resulting base for now. It will get converted to a type
    // later once the full specifier list is parsed.
    (void)specifiers_convert(specifiers);
}

static bool try_parse_declaration_specifiers(specifiers_t* specifiers);
static type_t* specifiers_make_type(specifiers_t* specifiers);
static bool try_parse_declarator(type_t** type, token_t** /*nullable*/ out_name);

static bool try_parse_specifier(int* flags, int flag, const string_t* keyword) {
    if (!lexer_accept(keyword)) {
        return false;
    }
    if (*flags & flag) {
        fatal("Redundant declaration specifier: %s", keyword->bytes);
    }
    *flags |= flag;
    return true;
}

static bool try_parse_type_qualifier(int* type_qualifiers) {
    if (try_parse_specifier(type_qualifiers, TYPE_QUALIFIER_CONST, STR_CONST)) return true;
    if (try_parse_specifier(type_qualifiers, TYPE_QUALIFIER_VOLATILE, STR_VOLATILE)) return true;
    if (try_parse_specifier(type_qualifiers, TYPE_QUALIFIER_RESTRICT, STR_RESTRICT)) return true;
    return false;
}

static bool try_parse_type_qualifiers(int* type_qualifiers) {
    if (!try_parse_type_qualifier(type_qualifiers))
        return false;
    while (try_parse_type_qualifiers(type_qualifiers)) {}
    return true;
}

static bool try_parse_storage_specifier(specifiers_t* specifiers, storage_specifier_t specifier, string_t* keyword) {
    if (!lexer_is(keyword))
        return false;
    if (specifiers->storage_specifier != storage_specifier_none)
        fatal_token(lexer_token, "At most one storage specifier (besides `_Thread_local`) can be provided for a declaration.");
    specifiers->storage_specifier = specifier;
    lexer_consume();
    return true;
}

static bool try_parse_type_specifier(specifiers_t* specifiers, int specifier, string_t* keyword) {
    if (!lexer_is(keyword))
        return false;
    if (specifiers->type_specifiers & specifier)
        fatal_token(lexer_token, "Redundant type specifier: %s", keyword->bytes);
    specifiers->type_specifiers |= specifier;
    specifiers_check_type(specifiers);
    lexer_consume();
    return true;
}

static bool try_parse_declaration_specifier_keywords(specifiers_t* specifiers) {

    // storage specifiers
    if (try_parse_storage_specifier(specifiers, storage_specifier_typedef, STR_TYPEDEF)) return true;
    if (try_parse_storage_specifier(specifiers, storage_specifier_extern, STR_EXTERN)) return true;
    if (try_parse_storage_specifier(specifiers, storage_specifier_static, STR_STATIC)) return true;
    if (try_parse_storage_specifier(specifiers, storage_specifier_auto, STR_AUTO)) return true;
    if (try_parse_storage_specifier(specifiers, storage_specifier_register, STR_REGISTER)) return true;
    if (lexer_is(STR_THREAD_LOCAL)) fatal_token(lexer_token, "_Thread_local is not supported.");

    // type specifiers (except `long` and user types)
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_VOID, STR_VOID)) return true;
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_CHAR, STR_CHAR)) return true;
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_SHORT, STR_SHORT)) return true;
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_INT, STR_INT)) return true;
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_SIGNED, STR_SIGNED)) return true;
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_UNSIGNED, STR_UNSIGNED)) return true;
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_FLOAT, STR_FLOAT)) return true;
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_DOUBLE, STR_DOUBLE)) return true;
    if (try_parse_type_specifier(specifiers, TYPE_SPECIFIER_BOOL, STR_BOOL_X)) return true;

    // type qualifiers
    if (try_parse_type_qualifier(&specifiers->type_qualifiers)) return true;

    // function specifiers
    if (try_parse_specifier(&specifiers->function_specifiers, FUNCTION_SPECIFIER_INLINE, STR_INLINE)) return true;
    if (try_parse_specifier(&specifiers->function_specifiers, FUNCTION_SPECIFIER_NORETURN, STR_NORETURN_X)) return true;

    // `long` can appear at most twice.
    if (lexer_is(STR_LONG)) {
        if (specifiers->type_specifiers & TYPE_SPECIFIER_LONG_LONG) {
            fatal("`long long long` is invalid.");
        }
        if (specifiers->type_specifiers & TYPE_SPECIFIER_LONG) {
            specifiers->type_specifiers &= ~TYPE_SPECIFIER_LONG;
            specifiers->type_specifiers |= TYPE_SPECIFIER_LONG_LONG;
        } else {
            specifiers->type_specifiers |= TYPE_SPECIFIER_LONG;
        }
        specifiers_check_type(specifiers);
        lexer_consume();
        return true;
    }

    // We don't support _Atomic yet because we have to parse _Atomic(type-specifiers)
    // and I haven't bothered yet. Same with _Alignas. We might never bother to
    // implement these since there's no point.
    if (lexer_accept(STR_ATOMIC)) {
        fatal("_Atomic is not supported.");
    }
    if (lexer_accept(STR_ALIGNAS) || lexer_accept(STR_ALIGNAS_X)) {
        fatal("_Alignas is not supported.");
    }

    return false;
}

static void parse_record_member(record_t* record) {

    // Keep the start of this declaration for error reporting
    token_t* error_token = token_ref(lexer_token);

    // Parse specifier sequence
    specifiers_t specifiers;
    specifiers_init(&specifiers);
    if (!try_parse_declaration_specifiers(&specifiers)) {
        fatal_token(lexer_token, "Expected a declaration.");
    }
    type_t* base_type = specifiers_make_type(&specifiers);

    // No storage or function specifiers are allowed
    if (specifiers.storage_specifier) {
        fatal("Storage specifiers are not allowed in a `struct` or `union` definition.");
    }
    if (specifiers.function_specifiers) {
        fatal("Function specifiers are not allowed in a `struct` or `union` definition.");
    }

    // Parse comma-separated list of declarators.
    for (;;) {
        type_t* type = type_ref(base_type);
        token_t* name = NULL;
        (void)try_parse_declarator(&type, &name);
        // fatal_token(lexer_token, "Expected a declarator for this `struct` or `union` member declaration.");

        // Parse bitfield
        bool has_bitfield = false;
        uint32_t bitfield_width;
        if (lexer_is(STR_COLON)) {
            if (!type_is_integer(type)) {
                fatal_token(lexer_token, "Only integer types are allowed to have a bitfield width.");
            }
            lexer_consume();

            has_bitfield = true;
            node_t* node = parse_constant_expression();
            bitfield_width = node_eval_32(node);

            if (bitfield_width == 0 && name != NULL) {
                fatal_token(error_token, "Only unnamed fields are allowed to have a bitfield width of zero.");
            }
            if (bitfield_width > 64) {
                fatal_token(node->token, "A bitfield specifier must be from 0 to 64 bits.");
            }

            node_delete(node);
        }

        // If the member is anonymous, make sure it's allowed
        if (name == NULL) {
            // There are a few cases where a struct member can be anonymous:
            // - a struct or union
            // - a bitfield
            if (type_matches_base(type, BASE_RECORD)) {
                // We have an anonymous record.
                record_t* record = type->record;
                assert(record != NULL);
                if (record->tag != NULL) {
                    // TODO It would be better if we could warn specifically on
                    // the tag name but we currently don't have a way to get
                    // it. (The record only stores the tag of its first
                    // declaration.) Probably specifiers_t could store it.
                    warn(warning_anonymous_tags, error_token,
                            "Anonymous struct/union members of struct/union type having tag names is a Microsoft/Plan9 extension.");
                }
            } else if (!has_bitfield) {
                fatal_token(error_token,
                        "This struct/union member needs a name. (Only struct/union types and zero-width bitfields are allowed to be anonymous.)");
            }
        }

        // TODO for now we ignore the bitfield width, and we ignore unnamed
        // bitfield members.
        if (name != NULL || !has_bitfield)
            record_add(record, name, type);

        if (name != NULL)
            token_deref(name);
        type_deref(type);

        if (lexer_is(STR_ASSIGN)) {
            fatal_token(lexer_token, "An initializer is not allowed in a `struct` or `union` member declaration.");
        }
        if (lexer_accept(STR_COMMA)) {
            continue;
        }
        lexer_expect(STR_SEMICOLON, "Expected `;` or `,` at end of declaration.");
        break;
    }

    type_deref(base_type);
    specifiers_destroy(&specifiers);
    token_deref(error_token);
}

static void parse_record(specifiers_t* specifiers) {

    // apply the struct/union keyword
    if (specifiers->type_specifiers & TYPE_SPECIFIER_RECORD) {
        fatal_token(lexer_token, "Redundant struct/union specifier");
    }
    specifiers->type_specifiers |= TYPE_SPECIFIER_RECORD;
    bool is_struct = lexer_is(STR_STRUCT);
    lexer_consume();

    // collect the optional tag
    token_t* tag = NULL;
    if (lexer_token->type == token_type_alphanumeric) {
        tag = lexer_take();
    } else if (!lexer_is(STR_BRACE_OPEN)) {
        fatal_token(lexer_token, "Expected tag or `{` after `%s`", is_struct ? "struct" : "union");
    }

    // Decide if we should search in parent scopes or only in the current scope
    // for this record definition. If this is a record definition, or if this
    // is a forward declaration, we only check the current scope (as both
    // declare a record in the current scope if one doesn't already exist.)
    //
    // A forward declaration of a struct occurs when the named struct is the
    // only thing in the declaration. Anything else, even a type qualifier
    // (e.g. const), makes it not a forward declaration.
    //
    // GCC has a warning about an incorrect forward declaration (e.g. `const
    // struct foo;`) that fails to shadow a declaration in an outer scope. We
    // could potentially implement the same.
    bool is_definition = lexer_is(STR_BRACE_OPEN);
    bool is_forward_declaration = lexer_is(STR_SEMICOLON)
            && specifiers->type_qualifiers == 0 && specifiers->storage_specifier == 0;
    bool find_recursive = !is_definition && !is_forward_declaration;

    // find the struct if it exists
    type_t* type = NULL;
    if (tag) {
        type = scope_find_type(scope_current,
                NAMESPACE_TAG,
                tag->value,
                find_recursive);
        if (type) {
            if (!type_matches_base(type, BASE_RECORD) || is_struct != type->record->is_struct) {
                if (is_struct) {
                    fatal_token(tag, "Non-struct tag cannot be referred to as `struct`");
                } else {
                    fatal_token(tag, "Non-union tag cannot be referred to as `union`");
                }
            }
            type_ref(type);
        }
    }

    // create it if it doesn't exist
    if (type == NULL) {
        record_t* record = record_new(tag, is_struct);
        scope_add_record(scope_current, record);
        type = type_new_record(record);
        if (tag) {
            scope_add_type(scope_current,
                    NAMESPACE_TAG,
                    tag,
                    type);
        }
    }

    specifiers->type = type;
    if (tag) {
        token_deref(tag);
    }

    // parse a definition if given
    if (lexer_is(STR_BRACE_OPEN)) {

        // make sure we don't already have a definition
        record_t* record = type->record;
        if (record->is_defined) {
            fatal_token(lexer_token, "Duplicate definition of struct/union");
        }
        record->is_defined = true;
        lexer_consume();

        // parse members
        while (!lexer_accept(STR_BRACE_CLOSE)) {
            parse_record_member(record);
        }

        if (vector_is_empty(&record->member_list)) {
            // TODO an empty struct is a GNU extension
            fatal("TODO empty struct not yet supported, GNU extension");
        }

        // Check if the last member is zero length
        // TODO we should probably just support zero-length arrays as an
        // extension everywhere and show this warning whenever we parse it. See
        // the TODO in record_add().
        member_t* last = vector_last(&record->member_list);
        if (type_matches_declarator(last->type, DECLARATOR_ARRAY) && last->type->count == 0) {
            warn(warning_zero_length_array, last->name,
                    "A zero-length array as a flexible array member is a GNU extension.");
        }

    }
}

static void parse_enum(specifiers_t* specifiers) {

    // Apply the enum keyword
    if (specifiers->type_specifiers & TYPE_SPECIFIER_ENUM) {
        fatal_token(lexer_token, "Redundant enum specifier. (Are you missing `;` between these enums?)");
    }
    specifiers->type_specifiers |= TYPE_SPECIFIER_ENUM;
    token_t* keyword = lexer_take();

    // Collect the optional tag
    token_t* tag = NULL;
    if (lexer_token->type == token_type_alphanumeric) {
        tag = lexer_take();
    }

    // If this is not a definition, the enum have a tag and must already exist.
    // (There are no forward declarations of enums.)
    if (!lexer_accept(STR_BRACE_OPEN)) {
        if (!tag) {
            fatal_token(keyword, "Expected `{` or a tag name after `enum`.");
        }
        type_t* type = scope_find_type(scope_current, NAMESPACE_TAG, tag->value, true);
        if (!type) {
            if (lexer_is(STR_SEMICOLON))
                fatal_token(tag, "Forward declarations of enums are not allowed.");
            fatal_token(tag, "An enum with this tag has not been defined.");
        }
        if (!type_matches_base(type, BASE_ENUM)) {
            fatal_token(tag, "This tag refers to a struct or union in this scope. It cannot be referred to as `enum`.");
        }
        specifiers->type = type_ref(type);
        goto done;
    }

    // This is a definition. Check in the current scope for a duplicate.
    if (tag) {
        type_t* type = scope_find_type(scope_current, NAMESPACE_TAG, tag->value, false);
        if (type) {
            fatal_token(tag, "A union, struct or enum with this tag is already defined in this scope.");
        }
    }

    // Create new type
    enum_t* enum_ = enum_new(tag);
    type_t* type = type_new_enum(enum_);
    specifiers->type = type;
    enum_deref(enum_);

    // Add to current scope
    // (If the enum is anonymous, we don't add it anywhere; the enum values
    // hold strong references to it.)
    if (tag) {
        scope_add_type(scope_current, NAMESPACE_TAG, tag, type);
    }

    // Parse values
    bool found = false;
    int value = 0;
    while (!lexer_is(STR_BRACE_CLOSE)) {
        if (lexer_token->type != token_type_alphanumeric) {
            fatal_token(lexer_token, "Expected an identifier for this enum value");
        }
        token_t* name = lexer_take();

        if (lexer_is(STR_ASSIGN)) {
            token_t* token = lexer_take();
            node_t* node = parse_constant_expression();
            // TODO should do a type conversion check and give a better error message
            node = node_cast_base(node, BASE_SIGNED_INT, token);
            value = (int)node_eval_32(node);
            node_delete(node);
            token_deref(token);
        }

        symbol_t* symbol = symbol_new(symbol_kind_constant, type, name, NULL);
        symbol->u32 = value++;
        scope_add_symbol(scope_current, symbol);
        symbol_deref(symbol);

        // TODO add the symbol to the enum (as non-retaining reference to prevent cycles)

        found = true;
        token_deref(name);
        if (lexer_is(STR_BRACE_CLOSE))
            break;
        // TODO trailing comma isn't allowed in pedantic C89 mode
        lexer_expect(STR_COMMA, "Expected `,` or `}` after this enum value.");
    }

    if (!found) {
        fatal_token(lexer_token, "Expected an enumerator in this enumeration. (An empty `enum` is not allowed.)");
    }
    lexer_consume();

done:
    if (tag)
        token_deref(tag);
    token_deref(keyword);
}

static bool try_parse_declaration_specifiers(specifiers_t* specifiers) {
    bool found = false;

    // All declaration specifiers are alphanumeric so we can short-circuit with
    // an alphanumeric test.
    while (lexer_token->type == token_type_alphanumeric) {
        if (try_parse_declaration_specifier_keywords(specifiers)) {
            found = true;
            continue;
        }

        // record (struct and enum)
        if (lexer_is(STR_STRUCT) || lexer_is(STR_UNION)) {
            found = true;
            parse_record(specifiers);
            continue;
        }

        // enum
        if (lexer_is(STR_ENUM)) {
            found = true;
            parse_enum(specifiers);
            continue;
        }

        // typedef (only if we don't already have a type specifier)
        if (specifiers->type_specifiers == 0) {
            type_t* type = scope_find_type(scope_current, NAMESPACE_TYPEDEF, lexer_token->value, true);
            if (type) {
                found = true;
                if (specifiers->type) {
                    fatal_token(lexer_token, "Redundant type name specifier");
                }
                lexer_consume();
                specifiers->type_specifiers |= TYPE_SPECIFIER_TYPEDEF;
                specifiers->type = type_ref(type);
                continue;
            }
        }

        break;
    }

    return found;
}

static type_t* specifiers_make_type(specifiers_t* specifiers) {
    type_t* type;

    if (specifiers->type) {
        switch (specifiers->type_specifiers) {
            case TYPE_SPECIFIER_TYPEDEF:
            case TYPE_SPECIFIER_ENUM:
            case TYPE_SPECIFIER_RECORD:
                type = type_ref(specifiers->type);
                break;
            default:
                fatal("Unsupported combination of type specifiers.");
        }
    } else {
        type = type_new_base(specifiers_convert(specifiers));
    }

    return type_qualify(type,
            specifiers->type_qualifiers & TYPE_QUALIFIER_CONST,
            specifiers->type_qualifiers & TYPE_QUALIFIER_VOLATILE);
}



/*
 * Declarators
 *
 * The declarator parse functions take a strong reference to a type and return
 * a strong reference to a potentially different type that includes any parsed
 * declarators.
 */

static void parse_function_arguments(type_t** type) {
    bool is_variadic = false;
    int arg_count = 0;
    int arg_capacity = 64;
    type_t** arg_types = malloc(arg_capacity * sizeof(type_t*));
    token_t** arg_names = malloc(arg_capacity * sizeof(token_t*));

    // Push a scope. This is important for things like struct and enum tag
    // declarations which only exist within the scope of the function.
    scope_push();

    // TODO we should check for ")" here for an old-style function that is not
    // a prototype

    while (!lexer_accept(STR_PAREN_CLOSE)) {
        if (arg_count > 0) {
            lexer_expect(STR_COMMA, "Expected `,` or `)` after function argument.");
        }
        if (arg_count == arg_capacity) {
            // TODO make this growable.
            fatal("This function has too many parameters.");
        }

        // check for variadic arguments
        if (lexer_accept(STR_ELLIPSIS)) {
            if (arg_count == 0) {
                fatal_token(lexer_token, "At least one non-variadic argument is required before `...`.");
            }
            lexer_expect(STR_PAREN_CLOSE, "Expected `)` after `...`");
            is_variadic = true;
            break;
        }

        // Parse specifier sequence
        specifiers_t specifiers;
        specifiers_init(&specifiers);
        if (!try_parse_declaration_specifiers(&specifiers)) {
            // TODO implicit int, K&R-style function prototypes
            fatal_token(lexer_token, "Expected a declaration specifier (a type) for this function parameter.");
        }

        // Check for specifier errors
        // TODO error should be on correct token. try_parse_declaration_specifiers()
        // should take an option to forbid invalid specifiers
        if (specifiers.storage_specifier != 0 || specifiers.function_specifiers != 0) {
            fatal("Storage and function specifiers are not allowed on function parameters.");
        }

        // Parse declarator (rest of type and name)
        type_t** type = arg_types + arg_count;
        token_t** name = arg_names + arg_count;
        *type = specifiers_make_type(&specifiers);
        specifiers_destroy(&specifiers);
        *name = NULL;
        if (!try_parse_declarator(type, name)) {
            // Check for (void)
            if (arg_count == 0 && type_matches_base(*type, BASE_VOID) && *name == NULL && lexer_accept(STR_PAREN_CLOSE)) {
                type_deref(*type);
                break;
            }
        }

        ++arg_count;
    }

    *type = type_new_function(*type, arg_types, arg_names, arg_count, is_variadic);
    (*type)->scope = scope_take();

    free(arg_types);
    free(arg_names);
}

static bool try_parse_direct_declarator(type_t** type, token_t** /*nullable*/ out_name) {
    bool found = false;

    // Parens before another direct declarator are a parenthesized declarator.
    if (lexer_accept(STR_PAREN_OPEN)) {
        found = true;

        type_t* temp = *type;
        if (!try_parse_declarator(type, out_name)) {
            fatal("Expected declarator after `(`");
        }
        lexer_expect(STR_PAREN_CLOSE, "Expected `)` after parenthesized declarator.");

        // Postfix operators are parsed below. They associate with whatever the
        // parenthesized declarator points to, so we insert them in the
        // declarator list in between.
        while (*type != temp) {
            type = &((*type)->ref);
        }
    }

    // If out_name is NULL, this declarator is abstract. In that case a name
    // cannot be provided and the direct-declarator is optional.
    if (out_name != NULL) {

        // Check for a name
        if (!found && lexer_token->type == token_type_alphanumeric && out_name != NULL) {
            if (*out_name != NULL) {
                fatal("Redundant identifier in declarator: %s", string_cstr(lexer_token->value));
            }
            // TODO We should check that this isn't a keyword. For now we don't bother.
            *out_name = lexer_take();
            found = true;
        }

        if (!found) {
            return false;
        }
    }

    // Function and array declarators have left-to-right associativity, so each
    // one gets inserted into the list after the previous one.
    type_t** brackets = type;
    for (;;) {

        // Square brackets are arrays
        if (lexer_accept(STR_SQUARE_OPEN)) {
            type_t* array;
            if (lexer_accept(STR_SQUARE_CLOSE)) {
                array = type_new_declarator(DECLARATOR_INDETERMINATE);
                array->ref = *brackets;
            } else {
                // TODO if this is not a constant expression, it's a variable-length array
                node_t* expr = parse_assignment_expression();
                array = type_new_array(*brackets, node_eval_32(expr));
                type_deref(*brackets);
                node_delete(expr);
                lexer_expect(STR_SQUARE_CLOSE, "Expected `]` after array length in declarator.");
            }
            *brackets = array;
            brackets = &array->ref;
            continue;
        }

        // Parens after another direct declarator are function arguments.
        if (lexer_accept(STR_PAREN_OPEN)) {
            parse_function_arguments(brackets);
            brackets = &(*brackets)->ref;
            if (lexer_is(STR_PAREN_OPEN)) {
                // TODO should make this check smarter, we can't have an array
                // of functions either, only an array of function pointers.
                // Probably this whole argument block can be moved out above
                // the loop and we only loop on square brackets.
                fatal_token(lexer_token, "A function cannot return a function.");
            }
            continue;
        }

        break;
    }

    // An abstract declarator is always optional.
    return out_name == NULL || found;
}

/**
 * Tries to parse a declarator for the given type.
 *
 * If out_name is NULL, this parses an abstract declarator.
 */
static bool try_parse_declarator(type_t** type, token_t** /*nullable*/ out_name) {

    // Collect pointers
    while (lexer_accept(STR_ASTERISK)) {
        int type_qualifiers = 0;
        try_parse_type_qualifiers(&type_qualifiers);
        // TODO we need these !! to work in cci/1 because otherwise it will
        // implicitly convert to char. we should fix cci/1 so we don't have to
        // (probably just need to alias bool to int instead of char)
        type_t* ptr = type_new_pointer(*type,
                !!(type_qualifiers & TYPE_QUALIFIER_CONST),
                !!(type_qualifiers & TYPE_QUALIFIER_VOLATILE),
                !!(type_qualifiers & TYPE_QUALIFIER_RESTRICT));
        type_deref(*type);
        *type = ptr;
    }

    return try_parse_direct_declarator(type, out_name);
}

static void parse_function_definition(symbol_t* symbol, type_t* type, token_t* name, string_t* asm_name) {

    // apply the scope for prototype tags (in case any struct, union or enum
    // were defined in the prototype)
    assert(type->scope);
    scope_apply(type->scope);

    // create a new scope for the parameters (we don't want to modify our
    // type's scope in case it's shared with other types)
    scope_push();

    // create the function
    assert(type_is_function(type));
    node_t* root = node_new_token(NODE_FUNCTION, name);
    root->type = type_ref(type->ref); // return value
    function_t* function = function_new(type, name, asm_name, root);
    function->symbol = symbol;
    current_function = function;

    // attach parameters
    for (uint32_t i = 0; i < type->count; ++i) {
        token_t* param_name = type->names[i];
        node_t* param = node_new_token(NODE_PARAMETER, param_name);

        if (type_is_array(type->args[i])) {
            // decay arrays to pointers
            param->type = type_new_pointer(type->args[i]->ref, false, false, false);
        } else {
            param->type = type_ref(type->args[i]);
        }

        node_append(root, param);

        // TODO warn about unnamed parameters before whatever version of the standard allowed them
        if (param_name != NULL) {
            // TODO check for duplicate parameter names (here or in declaration below)
            symbol_t* symbol = symbol_new(symbol_kind_variable, param->type, param_name, param_name->value);
            param->symbol = symbol;
            scope_add_symbol(scope_current, symbol);
        }
    }

    // parse
    node_append(root, parse_compound_statement(false));
    if (dump_ast) {
        putchar('\n');
        node_print_tree(root);
        putchar('\n');
    }

    // optimization and codegen
    if (optimization)
        optimize_tree(function->root);
    generate_function(function);
    if (optimization)
        optimize_asm(function);

    // write
    emit_function(function);
    emit_global_divider();

    // done
    scope_pop();
    scope_pop();
    current_function = NULL;
    function_delete(function);
}

static void parse_function_declaration(specifiers_t* specifiers, type_t* type,
        token_t* name, string_t* asm_name, bool is_file_scope)
{
    // create the symbol
    symbol_t* symbol = symbol_new(symbol_kind_function, type, name, asm_name);
    // TODO handle duplicate/redundant declarations
    scope_add_symbol(scope_current, symbol);
    symbol_deref(symbol);

    symbol->linkage = (specifiers->storage_specifier == storage_specifier_static) ?
            symbol_linkage_internal : symbol_linkage_external;

    // check for a function definition
    if (!lexer_is(STR_BRACE_OPEN)) {
        lexer_expect(STR_SEMICOLON, "Expected `;` or `{` after function declaration");
    } else {
        if (!is_file_scope) {
            fatal_token(lexer_token, "Function definitions can only appear at file scope.");
        }
        parse_function_definition(symbol, type, name, asm_name);
    }

    string_deref(asm_name);
    token_deref(name);
    type_deref(type);
}

// Checks and adjusts the types on a re-declaration of a variable.
static void parse_redeclaration_types(symbol_t* old, symbol_t* new) {

    // If one of the types is an array of indeterminate length and the other
    // has a definite length, we can set the length of the indeterminate array.
    // See:
    //     - decl/decl-local-extern-indeterminate.c
    //     - init/init-array-string-indeterminate-extern.c
    if (type_is_declarator(old->type) && type_is_declarator(new->type)) {
        if (old->type->declarator == DECLARATOR_ARRAY && new->type->declarator == DECLARATOR_INDETERMINATE) {
            if (!type_equal(old->type->ref, new->type->ref))
                goto mismatch;
            type_ref(old->type);
            type_deref(new->type);
            new->type = old->type;
            return;
        }
        if (new->type->declarator == DECLARATOR_ARRAY && old->type->declarator == DECLARATOR_INDETERMINATE) {
            if (!type_equal(new->type->ref, old->type->ref))
                goto mismatch;
            type_ref(new->type);
            type_deref(old->type);
            old->type = new->type;
            return;
        }
    }

    // The type must otherwise match.
    if (!type_equal(old->type, new->type))
        goto mismatch;

    return;

mismatch:
    fatal_token(new->token, "Variable re-declared at file scope with a different type.");
}

static void parse_local_extern_variable_declaration(node_t* parent,
        specifiers_t* specifiers, symbol_t* symbol)
{
    // Check to see if there's already a symbol with this name in this scope.
    symbol_t* previous = scope_find_symbol(scope_current, symbol->name, false);
    if (previous) {

        // It must also be extern.
        if (previous->linkage == symbol_linkage_none) {
            fatal_token(symbol->token, "Variable re-declared in block scope.");
        }

        // Compare types and adjust if necessary.
        parse_redeclaration_types(previous, symbol);

        // Ignore the new declaration.
        return;
    }
    scope_add_symbol(scope_current, symbol);

    // Check to see if there's a symbol at file scope.
    assert(scope_current != scope_global);
    symbol_t* global = scope_find_symbol(scope_global, symbol->name, false);
    if (global) {

        // The types must match.
        if (!type_equal(symbol->type, global->type)) {
            fatal_token(symbol->token, "`extern` variable re-declared with different type.");
        }

        // Adopt the linkage of the file scope symbol.
        symbol->linkage = global->linkage;

    } else {
        symbol->linkage = symbol_linkage_external;

        // Create a global symbol and mark it hidden. This will ensure that the
        // type and linkage can be checked if it's re-declared after our
        // declaration goes out of scope.
        global = symbol_clone(symbol);
        global->is_hidden = true;
        scope_add_symbol(scope_global, global);
        symbol_deref(global);
    }
}

static void parse_local_variable_declaration(node_t* parent, specifiers_t* specifiers,
        symbol_t* symbol, node_t* initializer)
{
    // Check to see if there's already a symbol with this name in this scope.
    symbol_t* previous = scope_find_symbol(scope_current, symbol->name, false);
    if (previous) {
        fatal_token(symbol->token, "Variable re-declared in block scope.");
    }
    scope_add_symbol(scope_current, symbol);

    // Handle local automatic variables and other specifiers.
    switch (specifiers->storage_specifier) {
        case storage_specifier_none:
        case storage_specifier_auto:
        case storage_specifier_register: {
            symbol->is_defined = true;

            node_t* node = node_new_token(NODE_VARIABLE, symbol->token);
            node->type = type_new_base(BASE_VOID);
            node->symbol = symbol_ref(symbol);
            node_append(parent, node);
            if (initializer) {
                node_append(node, initializer);
            }

            break;
        }

        case storage_specifier_typedef:
        case storage_specifier_extern:
            // these are handled separately
            fatal("Internal error: invalid storage specifier for local variable declaration");

        case storage_specifier_static: {
            symbol->is_defined = true;
            symbol->linkage = symbol_linkage_internal;

            // Generate a unique asm name
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s%x_%s", MISC_LABEL_PREFIX,
                        next_label++, symbol->asm_name->bytes);
                buf[sizeof(buf) - 1] = 0;
                string_deref(symbol->asm_name);
                symbol->asm_name = string_intern_cstr(buf);
            }

            break;
        }
    }

}

static void parse_global_variable_declaration(specifiers_t* specifiers, symbol_t* symbol, node_t* initializer) {
    symbol->linkage = (specifiers->storage_specifier == storage_specifier_static) ?
            symbol_linkage_internal : symbol_linkage_external;
    symbol->is_tentative = (initializer == NULL) &&
        (specifiers->storage_specifier != storage_specifier_extern);
    symbol->is_defined = initializer != NULL;

    // Check to see if there's already a symbol with this name.
    assert(scope_global == scope_current);
    symbol_t* previous = scope_find_symbol(scope_current, symbol->name, false);
    if (previous) {

        // Compare types and adjust if necessary.
        parse_redeclaration_types(previous, symbol);

        // If this is `extern`, adopt the linkage of the previous declaration;
        // otherwise, the linkage must match.
        if (specifiers->storage_specifier == storage_specifier_extern) {
            symbol->linkage = previous->linkage;
        } else if (previous->linkage != symbol->linkage) {
            fatal_token(symbol->token, "Variable re-declared at file scope with a different linkage.");
        }

        // Variables can only be defined once.
        if (previous->is_defined && symbol->is_defined) {
            fatal_token(symbol->token, "Variable re-defined at file scope.");
        }

        // Replace the previous declaration if:
        // - This declaration is a definition and the previous one is not; or
        // - This declaration is tentative and the previous is neither
        //   tentative nor a definition (i.e. it is extern)
        if (symbol->is_defined || (symbol->is_tentative && !previous->is_defined && !previous->is_tentative)) {
            scope_remove_symbol(scope_current, previous);
        } else {
            return;
        }
    }
    scope_add_symbol(scope_current, symbol);
}

static void parse_variable_declaration(node_t* /*nullable*/ parent,
        specifiers_t* specifiers, type_t* type, token_t* name, string_t* asm_name)
{
    if (lexer_is(STR_BRACE_OPEN)) {
        fatal("Cannot initialize a variable with `{`.");
    }

    // Collect the initializer
    node_t* initializer = NULL;
    if (lexer_is(STR_ASSIGN)) {
        if (specifiers->storage_specifier == storage_specifier_extern) {
            fatal_token(lexer_token, "Cannot initialize a variable with `extern` storage specifier.");
        }
        lexer_consume();
        initializer = parse_initializer(type);
    }

    symbol_t* symbol = symbol_new(symbol_kind_variable, type, name, asm_name);
    type_deref(type);

    // Handle file scope, block scope non-extern, and block scope extern separately
    if (parent) {
        if (specifiers->storage_specifier == storage_specifier_extern) {
            parse_local_extern_variable_declaration(parent, specifiers, symbol);
        } else {
            parse_local_variable_declaration(parent, specifiers, symbol, initializer);
        }
    } else {
        parse_global_variable_declaration(specifiers, symbol, initializer);
    }

    // If we have an initializer for an array of indeterminate size, we can now
    // set its size.
    // (This has to happen after the lookup for a previous symbol since the
    // previous symbol might declare a size.)
    type = symbol->type;
    if (initializer && type_is_declarator(type) && type->declarator == DECLARATOR_INDETERMINATE) {
        size_t count;
        if (initializer->kind == NODE_INITIALIZER_LIST) {
            count = vector_count(&initializer->children);
        } else if (initializer->kind == NODE_STRING) {
            count = type_size(initializer->type);
        } else {
            fatal_token(name, "Invalid initializer for array of indeterminate length.");
        }
        type_t* new_type = type_new_array(type->ref, count);
        type_deref(type);
        symbol->type = new_type;
    }

    // And finally, if this is a definition with linkage, emit it.
    if (symbol->is_defined && symbol->linkage != symbol_linkage_none) {
        generate_static_variable(symbol, initializer);
    }

    symbol_deref(symbol);
    string_deref(asm_name);
    token_deref(name);
}



/*
 * Declaration
 */

type_t* try_parse_type(void) {

    // Parse specifier sequence
    specifiers_t specifiers;
    specifiers_init(&specifiers);
    if (!try_parse_declaration_specifiers(&specifiers)) {
        specifiers_destroy(&specifiers);
        return NULL;
    }

    // Storage and function specifiers are not allowed.
    if (specifiers.storage_specifier != 0) {
        fatal("Storage specifiers are not allowed on this type declaration.");
    }
    if (specifiers.function_specifiers != 0) {
        fatal("Function specifiers are not allowed on this type declaration.");
    }

    // Parse declarator
    type_t* type = specifiers_make_type(&specifiers);
    try_parse_declarator(&type, NULL);

    specifiers_destroy(&specifiers);
    return type;
}

static string_t* parse_asm_name(node_t* /*nullable*/ parent, specifiers_t* specifiers, token_t* name, type_t* type) {

    // check for keyword
    bool is_asm = lexer_is(STR_ASM);
    if (!is_asm && !lexer_is(STR_ASM_X)) {
        return string_ref(name->value);
    }

    // do some error checks
    if (is_asm)
        warn(warning_extra_keywords, lexer_token, "`asm` is a GNU extension. (Use `__asm__` or pass `-fasm` or `-fgnu-extensions` or `-std=gnu*`.)");
    if (parent && specifiers->storage_specifier != storage_specifier_extern &&
            (!type_is_function(type) || specifiers->storage_specifier != storage_specifier_none))
    {
        fatal_token(lexer_token, "Cannot provide an asm name for a local symbol.");
    }

    // parse it
    lexer_consume();
    lexer_expect(STR_PAREN_OPEN, "Expected `(` for an asm name declaration.");
    if (lexer_token->type != token_type_string)
        fatal_token(lexer_token, "Expected a string in this asm name declaration.");
    string_t* asm_name = string_ref(lexer_token->value);
    lexer_consume();

    // check for string concatenations
    while (lexer_token->type == token_type_string) {
        // (This is really inefficient because we're doing two copies per
        // additional string fragment plus all the unnecessary string
        // interning. It really doesn't matter though because concatenated asm
        // names are rare.)
        size_t total = string_length(asm_name) + string_length(lexer_token->value);
        char* c = malloc(total);
        memcpy(c, string_cstr(asm_name), string_length(asm_name));
        memcpy(c + string_length(asm_name), string_cstr(lexer_token->value), string_length(lexer_token->value));
        string_deref(asm_name);
        asm_name = string_intern_bytes(c, total);
        free(c);
        lexer_consume();
    }

    lexer_expect(STR_PAREN_CLOSE, "Expected `)` after asm name declaration.");
    return asm_name;
}

bool try_parse_declaration(node_t* /*nullable*/ parent) {
    //printf("\n-------------------\ntrying to parse new decl - %s\n", lexer_token->value->bytes);

    // Parse specifier sequence. At file scope we allow a empty specifier list
    // if implicit int is enabled. TODO make that an option
    specifiers_t specifiers;
    specifiers_init(&specifiers);
    if (!try_parse_declaration_specifiers(&specifiers) && parent != NULL) {
        specifiers_destroy(&specifiers);
        return false;
    }
    type_t* base_type = specifiers_make_type(&specifiers);

    // Parse comma-separated list of declarators.
    bool first_declarator = true;
    for (;;) {
        type_t* type = type_ref(base_type);
        token_t* name = NULL;
        if (!try_parse_declarator(&type, &name)) {
            if (specifiers.type_specifiers & (TYPE_SPECIFIER_RECORD | TYPE_SPECIFIER_ENUM)) {
                // A struct, union or record declaration is allowed to have no
                // declarators (because they can define the contents or, for
                // structs and unions, can be forward declarations.)
            } else {
                fatal("Expected a declarator for this global declaration.");
            }
        }

        // A no-name declaration is not necessarily an error, for example it
        // could be a struct or enum.
        // TODO check if it's actually a struct or enum
        // TODO make sure it's not a typedef, extern, etc.
        if (name == NULL) {
            type_deref(type);
            goto declarator_done;
        }

        /*
        printf("parsed name: `%s` type:\n    ", string_cstr(name->value));
        type_print(type);
        printf("\n    ");
        type_print_words(type);
        printf("\n");
        */

        // Check for a typedef
        if (specifiers.storage_specifier == storage_specifier_typedef) {
            scope_add_type(scope_current, NAMESPACE_TYPEDEF, name, type);
            if (lexer_is(STR_ASSIGN) || lexer_is(STR_BRACE_OPEN)) {
                fatal_token(name, "A definition cannot be provided for a `typedef` declaration.");
            }
            type_deref(type);
            token_deref(name);
            goto declarator_done;
        }

        // Parse an asm name (or default to the declared name)
        string_t* asm_name = parse_asm_name(parent, &specifiers, name, type);

        // Check for a function
        //printf("asm name %s\n", asm_name->bytes);
        //type_print(type);
        if (type_is_declarator(type) && type->declarator == DECLARATOR_FUNCTION) {
            if (!first_declarator && lexer_is(STR_BRACE_OPEN)) {
                fatal_token(lexer_token,
                        "A function definition cannot appear on a declaration that has multiple declarators.");
            }
            //printf("    is func\n");
            parse_function_declaration(&specifiers, type, name, asm_name, parent == NULL);
            break;
        } else {
            //printf("    is var\n");
            parse_variable_declaration(parent, &specifiers, type, name, asm_name);
        }

    declarator_done:
        if (lexer_accept(STR_COMMA)) {
            first_declarator = false;
            continue;
        }
        lexer_expect(STR_SEMICOLON, "Expected `;` or `,` at end of declarator.");
        break;
    }

    type_deref(base_type);
    specifiers_destroy(&specifiers);
    return true;
}

void parse_global(void) {
    if (!try_parse_declaration(NULL)) {
        fatal("Expected a declaration at file scope.");
    }
}
