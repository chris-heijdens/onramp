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

#include "parse_expr.h"

#include "common.h"
#include "function.h"
#include "record.h"
#include "strings.h"
#include "libo-error.h"
#include "node.h"
#include "type.h"
#include "token.h"
#include "lexer.h"
#include "type.h"
#include "symbol.h"
#include "options.h"
#include "scope.h"
#include "parse_decl.h"
#include "parse_stmt.h"
#include "emit.h"
#include "arithmetic.h"

static int next_string;

static node_t* parse_unary_expression(void);

void parse_expr_init(void) {
}

void parse_expr_destroy(void) {
}

/**
 * Choose a type for a parsed number.
 *
 * This implements the table in C17 6.4.4.1.5 .
 */
static base_t parse_number_type(token_t* token, u64_t* number, int base,
        bool suffix_unsigned, bool suffix_long, bool suffix_long_long)
{
    // ull suffix
    if (suffix_unsigned && suffix_long_long) {
        return BASE_UNSIGNED_LONG_LONG;
    }

    // number > INT64_MAX
    u64_t ref;
    llong_set_u(&ref, 1);
    llong_shl(&ref, 63);
    if (llong_geu(number, &ref)) {
        if (base == 10 && !suffix_unsigned) {
            // The spec allows us to upgrade this to an "extended integer
            // type". We do the same as GCC and Clang: upgrade to unsigned and
            // warn.
            warn(warning_implicitly_unsigned_literal, token,
                    "This base 10 integer literal does not fit in `signed long long` so its type is `unsigned long long`. (Explicit `u` suffix is recommended.)");
        }
        return BASE_UNSIGNED_LONG_LONG;
    }

    // ll suffix
    if (suffix_long_long) {
        return BASE_SIGNED_LONG_LONG;
    }

    // number > UINT32_MAX
    llong_set_u(&ref, 1);
    llong_shl(&ref, 32);
    if (llong_geu(number, &ref)) {
        return suffix_unsigned ? BASE_UNSIGNED_LONG_LONG : BASE_SIGNED_LONG_LONG;
    }

    // base-10 signed number > INT32_MAX
    if (base == 10 && !suffix_unsigned) {
        llong_set_u(&ref, 1);
        llong_shl(&ref, 31);
        if (llong_geu(number, &ref)) {
            return BASE_SIGNED_LONG_LONG;
        }
    }

    // ul suffix
    if (suffix_unsigned && suffix_long) {
        return BASE_UNSIGNED_LONG;
    }

    // non-base-10 number > INT32_MAX
    if (base != 10) {
        llong_set_u(&ref, 1);
        llong_shl(&ref, 31);
        if (llong_geu(number, &ref)) {
            return suffix_long ? BASE_UNSIGNED_LONG : BASE_UNSIGNED_INT;
        }
    }

    // l suffix
    if (suffix_long) {
        return BASE_SIGNED_LONG;
    }

    // u suffix
    if (suffix_unsigned) {
        return BASE_UNSIGNED_INT;
    }

    // default int
    return BASE_SIGNED_INT;
}

/**
 * Parses a number.
 */
static node_t* parse_number(void) {
    assert(lexer_token->type == token_type_number);
    node_t* node = node_new_lexer(NODE_NUMBER);

    const char* p = string_cstr(node->token->value);
    unsigned base = 0;

    // detecting leading 0x/0X for hex
    if (*p == '0') {
        char x = *(p + 1);
        if ((x == 'x') | (x == 'X')) {
            base = 16;
            p = (p + 2);
        }
    }

    // detect leading binary 0b/0B for binary
    if (base == 0) {
        if (*p == '0') {
            char b = *(p + 1);
            if ((b == 'b') | (b == 'B')) {
                // TODO binary number literals are C23 only
                base = 2;
                p = (p + 2);
            }
        }
    }

    // detect leading 0 for octal
    if (base == 0) {
        if (*p == '0') {
            base = 8;
        }
    }

    // otherwise assume decimal
    if (base == 0) {
        base = 10;
    }

    // an octal constant is allowed to have a digit separator after the 0
    // prefix. other prefixes are not.
    if (base != 8 && *p == '\'') {
        // TODO this should probably be a warning
        fatal("A digit separator is not allowed between an 0x/0b prefix and the first digit.");
    }

    // accumulate digits
    bool was_separator;
    u64_t value;
    llong_clear(&value);
    while (1) {
        if (*p == '\'') {
            // TODO digit separators are C23 only
            p = (p + 1);
            was_separator = true;
            continue;
        }

        // TODO hex_to_int in libo
        unsigned digit = 99;
        if ((*p >= '0') & (*p <= '9')) {
            digit = (*p - '0');
        }
        if ((*p >= 'a') & (*p <= 'f')) {
            digit = ((*p - 'a') + 10);
        }
        if ((*p >= 'A') & (*p <= 'F')) {
            digit = ((*p - 'A') + 10);
        }
        if (digit >= base) {
            break;
        }
        was_separator = false;

        // Add the digit, checking for overflow
        u64_t temp;
        llong_set(&temp, &value);
        llong_mul_u(&temp, base);
        if (llong_ltu(&temp, &value)) {
            goto out_of_range;
        }
        llong_add_u(&temp, digit);
        if (llong_ltu(&temp, &value)) {
            goto out_of_range;
        }
        llong_set(&value, &temp);

        p = (p + 1);
    }

    if (was_separator) {
        // TODO this should probably be a warning
        fatal("A digit separator is not allowed at the end of a number.");
    }

    // parse out the suffix
    bool suffix_unsigned = false;
    bool suffix_long = false;
    bool suffix_long_long = false;
    while (*p) {

        // parse long
        if ((*p == 'l') | (*p == 'L')) {
            if (suffix_long_long) {
                fatal("`long long long` integer suffix is not supported.");
            }
            if (suffix_long) {
                suffix_long = false;
                suffix_long_long = true;
            } else {
                suffix_long = true;
            }
            p = (p + 1);
            continue;
        }

        // parse unsigned
        if ((*p == 'u') | (*p == 'U')) {
            if (suffix_unsigned) {
                fatal("Redundant `u` suffix on integer literal.");
            }
            suffix_unsigned = true;
            p = (p + 1);
            continue;
        }

        // unrecognized. try to give slightly better error mesagge
        if (((*p == '.') | ((*p == 'e') | (*p == 'E'))) |
                ((*p == 'p') | (*p == 'P')))
        {
            fatal("TODO floating point literals are not yet supported");
        }
        fatal("Malformed number literal.");
    }

    // Choose a type.
    node->type = type_new_base(parse_number_type(node->token, &value, base,
            suffix_unsigned, suffix_long, suffix_long_long));

    if (type_is_long_long(node->type)) {
        memcpy(&node->u64, &value, sizeof(node->u64));
    } else {
        node->u32 = u64_low(&value);
    }
    return node;

out_of_range:
    fatal_token(node->token, "Number does not fit in a 64-bit integer.");
}

static node_t* parse_character(void) {
    assert(lexer_token->type == token_type_character);
    node_t* node = node_new_lexer(NODE_CHARACTER);
    // TODO char prefixes
    node->u32 = node->token->value->bytes[0];
    node->type = type_new_base(BASE_SIGNED_INT);
    return node;
}

node_t* parse_string(void) {
    assert(lexer_token->type == token_type_string);
    int label = next_string++;
    token_t* first = token_ref(lexer_token);

    // String literals are emitted on-the-fly. We currently don't merge
    // identical string literals and we don't bother to optimize away string
    // literals that are only used in e.g. sizeof. We'll let the linker's
    // garbage collection clean them up.
    emit_source_location(lexer_token);
    emit_char('@');
    emit_cstr(STRING_LABEL_PREFIX);
    emit_hex_number(label);
    emit_newline();

    // adjacent string literals aren't concatenated in memory; we just emit
    // them one after another into the assembly.
    size_t length = 0;
    do {
        if (lexer_token->prefix != token_prefix_none) {
            fatal_token(lexer_token, "TODO string prefixes not yet implemented");
        }
        length += string_length(lexer_token->value);
        emit_source_location(lexer_token);
        emit_cstr(ASM_INDENT);
        emit_string_literal(lexer_token->value);
        emit_newline();
        lexer_consume();
    } while (lexer_token->type == token_type_string);

    // append null-terminator
    ++length;
    emit_cstr(ASM_INDENT);
    emit_quoted_byte(0);
    emit_newline();
    emit_newline();

    // The type of a string literal is `char[]`. It is not const even though
    // modifying it is undefined behaviour. (On Onramp, modifying string
    // literals works because there is no memory protection so we should try to
    // improve warnings around this.)
    type_t* base = type_new_base(BASE_CHAR);
    node_t* node = node_new_token(NODE_STRING, first);
    node->type = type_new_array(base, length);

    node->string_label = label;
    type_deref(base);
    token_deref(first);
    return node;
}

static node_t* parse_statement_expression(token_t* paren) {
    assert(string_equal_cstr(paren->value, "("));
    assert(lexer_is(STR_BRACE_OPEN));

    // We warn against the opening brace (since this is the most obvious
    // "wrong" token when they are disabled), but we use the opening paren as
    // the sequence token. The opening paren marks the "real" start of the
    // statement expression and it makes it obvious that it's a statement
    // expression in an AST dump.
    warn(warning_statement_expressions, lexer_token,
            "Statement expressions are a GNU extension.");

    lexer_consume();
    node_t* sequence = node_new_token(NODE_SEQUENCE, paren);
    token_deref(paren);

    scope_push();
    while (!lexer_accept(STR_BRACE_CLOSE)) {
        // We have another statement. Cast the previous one to void. (Only the
        // last statement can be non-void.)
        if (sequence->last_child) {
            node_t* node = node_detach(sequence->last_child);
            node_append(sequence, node_cast_base(node, BASE_VOID, NULL));
        }
        parse_declaration_or_statement(sequence, false);
    }
    scope_pop();

    lexer_expect(STR_PAREN_CLOSE, "Expected `)` after `}` of statement expression.");
    sequence->type = sequence->last_child ?
            type_ref(sequence->last_child->type) :
            type_new_base(BASE_VOID); // empty expression statement is allowed

    return sequence;
}

static node_t* parse_primary_expression(void) {

    // an alphanumeric is the name of a variable or function
    if (lexer_token->type == token_type_alphanumeric) {
        symbol_t* symbol = scope_find_symbol(scope_current, lexer_token->value, true);
        if (!symbol || symbol->is_hidden) {
            fatal_token(lexer_token, "No such variable or function: %s", lexer_token->value->bytes);
        }

        if (symbol->kind == symbol_kind_builtin) {
            return parse_builtin(symbol->builtin);
        }

        node_t* node = node_new_lexer(NODE_ACCESS);
        node->symbol = symbol_ref(symbol);
        node->type = type_ref(symbol->type);
        return node;
    }

    // parenthesis
    if (lexer_is(STR_PAREN_OPEN)) {
        token_t* paren = lexer_take();

        // check for a statement expression
        if (lexer_is(STR_BRACE_OPEN)) {
            return parse_statement_expression(paren);
        }

        // check for a cast expression
        type_t* type = try_parse_type();
        if (type) {
            lexer_expect(STR_PAREN_CLOSE, "Expected `)` after cast expression");
            node_t* node = node_cast(node_decay(parse_unary_expression()), type, paren);
            type_deref(type);
            token_deref(paren);
            return node;
        }

        // Otherwise we have a parenthesized expression.
        token_deref(paren);
        node_t* node = parse_expression();
        lexer_expect(STR_PAREN_CLOSE, "Expected `)` after parenthesized expression");
        return node;
    }

    // number
    if (lexer_token->type == token_type_number) {
        return parse_number();
    }

    // character literal
    if (lexer_token->type == token_type_character) {
        return parse_character();
    }

    // string
    if (lexer_token->type == token_type_string) {
        return parse_string();
    }

    // TODO
    fatal_token(lexer_token, "Unrecognized token: `%s`.", lexer_token->value->bytes);
    return NULL;
}

static node_t* parse_function_call(node_t* function) {
    type_t* type = function->type;
    if (type_is_pointer(type))
        type = type->ref;
    if (!type_is_function(type))
        fatal_token(lexer_token, "Expected callable function before `(`.");

    node_t* call = node_new_lexer(NODE_CALL);
    call->type = type_ref(type->ref); // return type of function
    node_append(call, function);

    // collect args, checking types
    bool has_prototype = true; // TODO
    uint32_t arg_count = 0;
    if (!lexer_accept(STR_PAREN_CLOSE)) {
        for (;;) {
            if (has_prototype && !type->is_variadic && arg_count > type->count) {
                fatal_token(call->token, "Too many arguments in function call.");
            }

            node_t* arg = parse_assignment_expression();
            arg = node_decay(arg);

            if (has_prototype && arg_count < type->count) {
                type_t* arg_type = type->args[arg_count];

                // If the function parameter is an array, cast to a pointer.
                // TODO maybe we should decay function parameter types when
                // they are parsed?
                if (type_is_array(arg_type)) {
                    arg_type = type_new_pointer(arg_type->ref, false, false, false);
                } else {
                    type_ref(arg_type);
                }

                arg = node_cast(arg, arg_type, NULL);
                type_deref(arg_type);
            } else {
                // TODO node_promote doesn't promote float to double, we need a
                // separate variadic arg promotion func to do that
                if (type_is_arithmetic(arg->type)) {
                    arg = node_promote(arg);
                }
            }

            node_append(call, arg);
            ++arg_count;

            if (lexer_accept(STR_PAREN_CLOSE)) {
                break;
            }
            if (lexer_accept(STR_COMMA)) {
                continue;
            }
            lexer_expect(STR_PAREN_CLOSE, "Expected `,` or `)` after function argument.");
        }
    }

    if (has_prototype && arg_count < type->count) {
        fatal_token(call->token, "Not enough arguments in function call.");
    }

    return call;
}

static node_t* parse_record_member_access(node_t* record_expr, node_kind_t kind) {
    node_t* access = node_new_lexer(kind);
    node_append(access, record_expr);

    // get the member name
    if (lexer_token->type != token_type_alphanumeric) {
        fatal_token(lexer_token, "Expected an identifier for this struct or union member access.");
        // TODO also make sure it's not a keyword
    }
    access->member = lexer_take();

    // get the record type
    type_t* record_type = record_expr->type;
    if (kind == NODE_MEMBER_PTR) {
        if (!type_is_indirection(record_type)) {
            fatal_token(access->token, "Cannot use `->` on non-pointer.");
        }
        record_type = type_pointed_to(record_type);
    }
    if (!type_matches_base(record_type, BASE_RECORD)) {
        fatal_token(access->token, "Member access operators `.` and `->` can only be used on structs and unions.");
    }

    // make sure it's not incomplete
    record_t* record = record_type->record;

    // lookup the member
    unsigned offset;
    type_t* member_type = record_find(record, access->member->value, &offset);
    if (!member_type) {
        fatal_token(access->member, "This struct or union has no member with this name.");
    }
    access->type = type_ref(member_type);
    access->member_offset = offset;

    return access;
}

static node_t* parse_array_subscript(node_t* left) {
    node_t* op = node_new_lexer(NODE_ARRAY_SUBSCRIPT);
    node_t* right = parse_expression();
    lexer_expect(STR_SQUARE_CLOSE, "Expected `]` at the end of array subscript expression.");

    // The array subscript operator is symmetric.
    // Figure out which is the pointer and which is the index
    node_t* ptr;
    node_t* index;
    if (type_is_indirection(left->type)) {
        ptr = left;
        index = right;
    } else if (type_is_indirection(right->type)) {
        ptr = right;
        index = left;
    } else {
        fatal_token(op->token, "One side of this array subscript expression must be a pointer or an array.");
    }
    if (!type_is_complete(ptr->type->ref)) {
        fatal_token(op->token, "Cannot subscript a pointer to an incomplete type.");
    }

    // Cast the index if necessary
    if (!type_is_integer(index->type) && !type_matches_base(index->type, BASE_ENUM)) {
        fatal_token(op->token, "One side of this array subscript expression must be an integer or enum.");
    }
    index = node_cast_base(index, BASE_UNSIGNED_INT, NULL);

    node_append(op, ptr);
    node_append(op, index);
    op->type = type_ref(ptr->type->ref);

    return op;
}

static node_t* parse_post_incdec(node_t* child, node_kind_t kind) {
    node_t* node = node_new_lexer(kind);
    node_append(node, child);
    node->type = type_ref(child->type);
    return node;
}

static node_t* parse_postfix_expression(void) {

    // A postfix expression starts with a primary expression.
    node_t* node = parse_primary_expression();

    // Check for postfix operators
    for (;;) {

        // function call
        if (lexer_is(STR_PAREN_OPEN)) {
            node = parse_function_call(node);
            continue;
        }

        // record member value access
        if (lexer_is(STR_DOT)) {
            node = parse_record_member_access(node, NODE_MEMBER_VAL);
            continue;
        }

        // record member pointer access
        if (lexer_is(STR_ARROW)) {
            node = parse_record_member_access(node, NODE_MEMBER_PTR);
            continue;
        }

        // array subscript
        if (lexer_is(STR_SQUARE_OPEN)) {
            node = parse_array_subscript(node);
            continue;
        }

        // post-increment
        if (lexer_is(STR_PLUS_PLUS)) {
            node = parse_post_incdec(node, NODE_POST_INC);
            continue;
        }

        // post-decrement
        if (lexer_is(STR_MINUS_MINUS)) {
            node = parse_post_incdec(node, NODE_POST_DEC);
            continue;
        }

        break;
    }

    return node;
}

static node_t* parse_sizeof(void) {
    node_t* node = node_new_lexer(NODE_SIZEOF);
    node_t* child;

    if (lexer_accept(STR_PAREN_OPEN)) {

        // Check for sizeof(type). The type declaration must be abstract.
        type_t* type = try_parse_type();
        if (type) {
            // It's a type.
            child = node_new(NODE_TYPE);
            child->type = type;
        } else {
            // Otherwise it's a parenthesized expression.
            child = parse_expression();
        }

        lexer_expect(STR_PAREN_CLOSE, "Expected `)` after expression in `sizeof(`");
    } else {
        // sizeof without parens has high precedence. We only consume a unary
        // expression.
        child = parse_unary_expression();
    }

    if (type_is_function(child->type)) {
        // TODO GCC and Clang allow this and evaluate to 1, only warning
        // under -Wpointer-arith. No idea why.
        fatal_token(node->token, "Cannot take the size of a function.");
    }
    if (!type_is_complete(child->type)) {
        fatal_token(node->token, "Cannot take the size of an incomplete type.");
    }

    node_append(node, child);
    node->type = type_new_base(BASE_UNSIGNED_INT);

    if (type_matches_base(node->type, BASE_VOID)) {
        warn(warning_pointer_arith, node->token, "sizeof(void) is 1 as a GNU extension.");
    }

    return node;
}

// Checks that the given child node type is valid for a unary math operator
static void parse_unary_math_check_type(type_t* type, token_t* token) {
    if (type_matches_base(type, BASE_RECORD)) {
        fatal_token(token, "Cannot apply unary operator `%s` to a struct or union value.", token->value->bytes);
    }
    if (type_matches_base(type, BASE_VOID)) {
        fatal_token(token, "Cannot apply unary operator `%s` to void.", token->value->bytes);
    }
}

static node_t* parse_unary_operator(node_kind_t kind) {
    node_t* node = node_new_lexer(kind);
    node_t* child = parse_unary_expression();

    switch (kind) {
        case NODE_PRE_INC:
        case NODE_PRE_DEC:
            parse_unary_math_check_type(child->type, node->token);
            node_append(node, child);
            node->type = type_ref(child->type);
            break;

        case NODE_UNARY_PLUS:
        case NODE_UNARY_MINUS:
        case NODE_BIT_NOT:
            parse_unary_math_check_type(child->type, node->token);
            if (!type_is_declarator(child->type)) {
                child = node_promote(child);
            }
            node_append(node, child);
            node->type = type_ref(child->type);
            break;

        case NODE_LOGICAL_NOT:
            parse_unary_math_check_type(child->type, node->token);
            node_append(node, child);
            node->type = type_new_base(BASE_SIGNED_INT);
            break;

        case NODE_DEREFERENCE:
            if (!type_is_indirection(child->type)) {
                fatal_token(node->token, "Cannot dereference non-pointer type");
            }
            node_append(node, child);
            node->type = type_ref(child->type->ref);
            break;

        case NODE_ADDRESS_OF:
            // TODO child node must be a location
            node_append(node, child);
            node->type = type_new_pointer(child->type, false, false, false);
            break;

        default:
            fatal("Internal error: not a unary operator");
    }

    return node;
}

static node_t* parse_unary_expression(void) {

    // check for a unary punctuation operator
    node_kind_t kind = node_kind_of_unary_operator(lexer_token);
    if (kind != NODE_INVALID) {
        return parse_unary_operator(kind);
    }

    // a few other operators
    if (lexer_is(STR_SIZEOF)) {
        return parse_sizeof();
    }
    if (lexer_is(STR_ALIGNOF) || lexer_is(STR_ALIGNOF_X)) {
        fatal("TODO alignof");
    }

    return parse_postfix_expression();
}

static void parse_usual_arithmetic_conversions(node_t** left, node_t** right) {

    // Usual arithmetic conversions always start with promotion.
    *left = node_promote(*left);
    *right = node_promote(*right);

    // We don't have to do anything if the types match.
    type_t* left_type = (*left)->type;
    type_t* right_type = (*right)->type;
    if (type_equal(left_type, right_type))
        return;

    // We can assume both types are arithmetic types at least. This should have
    // been checked before calling this.
    assert(type_is_arithmetic(left_type));
    assert(type_is_arithmetic(right_type));

    // The rest of this basically follows 6.3.1.8 of the C11 spec.

    // If either type is floating point, convert the other to it, in order of
    // rank.
    if (type_matches_base(left_type, BASE_LONG_DOUBLE)) {
        *right = node_cast(*right, left_type, NULL);
        return;
    }
    if (type_matches_base(right_type, BASE_LONG_DOUBLE)) {
        *left = node_cast(*left, right_type, NULL);
        return;
    }
    if (type_matches_base(left_type, BASE_DOUBLE)) {
        *right = node_cast(*right, left_type, NULL);
        return;
    }
    if (type_matches_base(right_type, BASE_DOUBLE)) {
        *left = node_cast(*left, right_type, NULL);
        return;
    }
    if (type_matches_base(left_type, BASE_FLOAT)) {
        *right = node_cast(*right, left_type, NULL);
        return;
    }
    if (type_matches_base(right_type, BASE_FLOAT)) {
        *left = node_cast(*left, right_type, NULL);
        return;
    }

    // Both types are integers. We'll need to compare signedness and rank.
    bool left_signed = type_is_signed_integer(left_type);
    bool right_signed = type_is_signed_integer(right_type);
    int left_rank = type_integer_rank(left_type);
    int right_rank = type_integer_rank(right_type);

    // If both are signed or both are unsigned, cast the lesser rank type to
    // that of the greater rank.
    if (left_signed == right_signed) {
        if (left_rank > right_rank) {
            *right = node_cast(*right, left_type, NULL);
            return;
        } else {
            *left = node_cast(*left, right_type, NULL);
            return;
        }
    }

    // If the signed type has lower rank than the unsigned type, convert to
    // unsigned.
    if (left_signed) {
        if (left_rank < right_rank) {
            *left = node_cast(*left, right_type, NULL);
            return;
        }
    } else {
        if (right_rank < left_rank) {
            *right = node_cast(*right, left_type, NULL);
            return;
        }
    }

    // If the signed type can represent all values of the unsigned type (i.e.
    // it is strictly larger), convert to signed.
    size_t left_size = type_size(left_type);
    size_t right_size = type_size(right_type);
    if (left_signed) {
        if (left_size > right_size) {
            *right = node_cast(*right, left_type, NULL);
            return;
        }
    } else {
        if (right_size > left_size) {
            *left = node_cast(*left, right_type, NULL);
            return;
        }
    }

    // Otherwise, cast both to the unsigned type that corresponds to that of
    // the signed type.
    base_t base = base_unsigned_of_signed((left_signed ? left_type : right_type)->base);
    *left = node_cast_base(*left, base, NULL);
    *right = node_cast_base(*right, base, NULL);
}

static void parse_comparison_conversions(node_t* op, node_t** left, node_t** right) {

    // If the types already match, we're done.
    type_t* left_type = (*left)->type;
    type_t* right_type = (*right)->type;
    if (type_equal(left_type, right_type)) {
        return;
    }

    // If either side is a struct, the types must exactly match. Since we've
    // checked for a match above, it's an error.
    // TODO we should also prevent ordering comparisons with struct/union values. For now code generation will fail on it.
    if (type_matches_base(left_type, BASE_RECORD) || type_matches_base(right_type, BASE_RECORD)) {
        fatal_token(op->token, "Cannot compare a struct or union with a value of a different type.");
    }

    // If either side is a pointer, the other side must be a compatible pointer
    // type, or a void*, or a literal zero.
    // TODO for now just cast to int, we'll skip type checking of pointer comparisons for simplicity
    if (type_is_indirection(left_type)) {
        *left = node_cast_base(*left, BASE_UNSIGNED_INT, NULL);
    }
    if (type_is_indirection(right_type)) {
        *right = node_cast_base(*right, BASE_UNSIGNED_INT, NULL);
    }

    // Otherwise both sides must be arithmetic types. We do the usual
    // arithmetic conversions.
    parse_usual_arithmetic_conversions(left, right);
}

static void parse_binary_conversions(node_t* op, node_t* left, node_t* right) {

    // Both sides decay to pointers in all binary expressions.
    // (We should probably skip this under operators in which pointers aren't
    // allowed such as `*`, `/`, `%` and probably others. For now we don't
    // bother.)
    left = node_decay(left);
    right = node_decay(right);

    switch (op->kind) {
        case NODE_ADD:
            // Binary addition requires that at most one side is a pointer, and
            // that the remaining sides are arithmetic types. When adding a
            // pointer, the arithmetic side is cast to a word size unsigned
            // integer.

            // Left side pointer
            if (type_is_pointer(left->type)) {
                if (type_is_pointer(right->type))
                    fatal_token(op->token, "At most one side of a binary addition can be an indirection (i.e. a pointer.)");
                if (!type_is_arithmetic(right->type))
                    fatal_token(op->token, "A pointer can only be added to an arithmetic type.");
                type_t* target = type_new_base(BASE_UNSIGNED_INT);
                right = node_cast(node_promote(right), target, NULL);
                type_deref(target);
                op->type = type_ref(left->type);

            // Right side pointer
            } else if (type_is_pointer(right->type)) {
                if (!type_is_arithmetic(left->type))
                    fatal_token(op->token, "A pointer can only be added to an arithmetic type.");
                type_t* target = type_new_base(BASE_UNSIGNED_INT);
                left = node_cast(node_promote(left), target, NULL);
                type_deref(target);
                op->type = type_ref(right->type);

            // Neither side pointer
            } else {
                if (!type_is_arithmetic(left->type))
                    fatal_token(op->token, "The left side of binary addition must be a pointer or an arithmetic type.");
                if (!type_is_arithmetic(right->type))
                    fatal_token(op->token, "The right side of binary addition must be a pointer or an arithmetic type.");

                parse_usual_arithmetic_conversions(&left, &right);
                op->type = type_ref(left->type);
            }
            break;

        case NODE_SUB: {
            // Subtraction allows two pointers to be subtracted, resulting in
            // ptrdiff_t; or it allows an arithmetic type to be subtracted from
            // a pointer, which moves the pointer; or it allows subtraction of
            // two arithmetic types.

            // Both pointers
            if (type_is_pointer(right->type)) {
                if (!type_is_pointer(left->type))
                    fatal_token(op->token, "Cannot subtract a pointer from a non-pointer.");
                if (!type_compatible_unqual(left->type, right->type))
                    fatal_token(op->token, "Cannot subtract two pointers of incompatible types.");
                op->type = type_new_base(BASE_SIGNED_INT);

            // Left side pointer
            } else if (type_is_pointer(left->type)) {
                if (!type_is_arithmetic(right->type))
                    fatal_token(op->token, "Subtracting from a pointer requires a pointer of compatible type or an arithmetic type.");
                right = node_cast_base(node_promote(right), BASE_UNSIGNED_INT, NULL);
                op->type = type_ref(left->type);

            // Neither side pointer
            } else {
                if (!type_is_arithmetic(left->type))
                    fatal_token(op->token, "The left side of binary subtraction must be a pointer or an arithmetic type.");
                if (!type_is_arithmetic(right->type))
                    fatal_token(op->token, "The right side of binary subtraction must be a pointer or an arithmetic type.");

                parse_usual_arithmetic_conversions(&left, &right);
                op->type = type_ref(left->type);
            }

            break;
        }

        case NODE_EQUAL:
        case NODE_NOT_EQUAL:
        case NODE_LESS:
        case NODE_GREATER:
        case NODE_LESS_OR_EQUAL:
        case NODE_GREATER_OR_EQUAL:
            op->type = type_new_base(BASE_SIGNED_INT);
            parse_comparison_conversions(op, &left, &right);
            break;

        case NODE_SHL:
        case NODE_SHR:
            // TODO shl, shr don't use usual arithmetic conversions. for now we
            // just promote. probably we need to forbid floats, cast right side
            // to int, etc.
            left = node_promote(left);
            right = node_promote(right);
            op->type = type_ref(left->type);
            break;

        case NODE_LOGICAL_OR:
        case NODE_LOGICAL_AND:
            // TODO check the real rules. For now we cast to bool.
            left = node_cast_base(left, BASE_BOOL, NULL);
            right = node_cast_base(right, BASE_BOOL, NULL);
            op->type = type_new_base(BASE_BOOL);
            break;

        default:
            // Other binary operators require that both sides be arithmetic types.
            if (!type_is_arithmetic(left->type))
                fatal_token(op->token, "Left side of binary operator must be an arithmetic type.");
            if (!type_is_arithmetic(right->type))
                fatal_token(op->token, "Right side of binary operator must be an arithmetic type.");
            parse_usual_arithmetic_conversions(&left, &right);
            op->type = type_ref(left->type);
            break;
    }

    node_append(op, left);
    node_append(op, right);
}

static node_t* parse_binary_expression(int min_precedence) {
    node_t* left = parse_unary_expression();

    for (;;) {

        // parse the nodes
        node_kind_t kind = node_kind_of_binary_operator(lexer_token);
        if (kind == NODE_INVALID)
            break;
        int op_precedence = node_kind_precedence_of_binary_operator(kind);
        if (op_precedence < min_precedence)
            break;
        node_t* op = node_new_lexer(kind);
        node_t* right = parse_binary_expression(op_precedence + 1);

        // apply promotions
        parse_binary_conversions(op, left, right);
        left = op;
    }

    return left;
}

/**
 * Applies type conversion rules to the two sides of a conditional expression.
 *
 * Rules are in the C17 spec, 6.5.15 .
 */
static void parse_conditional_expression_types(node_t** left, node_t** right) {
    *left = node_decay(*left);
    *right = node_decay(*right);

    type_t* left_type = (*left)->type;
    type_t* right_type = (*right)->type;
    token_t* left_token = (*right)->token;
    token_t* right_token = (*right)->token;

    // One side is a pointer.
    if (type_is_indirection(left_type) || type_is_indirection(right_type)) {

        // Check if pointers are equal
        if (type_equal_unqual(left_type, right_type)) {
            // TODO apply qualifiers to both types
            return;
        }

        node_t** ptr;
        node_t** other;
        if (type_is_indirection(left_type)) {
            ptr = left;
            other = right;
        } else {
            ptr = right;
            other = left;
        }

        // If the other side is null or a void pointer, cast it to the pointer type
        type_t* other_type = (*other)->type;
        if (node_is_null(*other) || (type_is_indirection(other_type) &&
                    type_matches_base(other_type->ref, BASE_VOID)))
        {
            *other = node_cast(*other, (*ptr)->type, NULL);
            return;
        }

        fatal("TODO find compatible ptr type");
    }

    // Both sides are arithmetic
    if (type_is_arithmetic(left_type) != type_is_arithmetic(right_type)) {
        fatal_token(right_token, "Both or neither side of this conditional expression can be an arithmetic type.");
    }
    if (type_is_arithmetic(left_type)) {
        parse_usual_arithmetic_conversions(left, right);
        return;
    }

    // Both sides are structs
    if (type_matches_base(left_type, BASE_RECORD) != type_matches_base(right_type, BASE_RECORD)) {
        fatal_token(right_token, "Both or neither side of this conditional expression can be a struct or union type.");
    }
    if (type_matches_base(left_type, BASE_RECORD)) {
        if (left_type->record != right_type->record) {
            fatal_token(right_token, "The sides of a conditional expression cannot have different struct or union types.");
        }
        return;
    }

    // Both sides are enums
    if (type_matches_base(left_type, BASE_ENUM) != type_matches_base(right_type, BASE_ENUM)) {
        fatal_token(right_token, "Both or neither side of this conditional expression can be an enum type.");
    }
    if (type_matches_base(left_type, BASE_ENUM)) {
        if (left_type->enum_ != right_type->enum_) {
            fatal_token(right_token, "The sides of a conditional expression cannot have different enum types.");
        }
        return;
    }

    // Both sides are void
    // TODO if one side is void should we cast the other to it?
    if (type_matches_base(left_type, BASE_VOID) != type_matches_base(right_type, BASE_VOID)) {
        fatal_token(right_token, "Both or neither side of this conditional expression can be void.");
    }
    if (type_matches_base(left_type, BASE_VOID)) {
        return;
    }

    fatal_token(left_token, "Incompatible types in conditional expression.");
}

static node_t* parse_conditional_expression(void) {
    node_t* condition = parse_binary_expression(0);
    if (!lexer_is(STR_QUESTION)) {
        return condition;
    }
    node_t* conditional = node_new_lexer(NODE_IF);

    if (lexer_is(STR_COLON)) {
        // https://gcc.gnu.org/onlinedocs/gcc/extensions-to-the-c-language-family/conditionals-with-omitted-operands.html
        fatal_token(lexer_token, "TODO support elvis operator");
    }

    node_t* left = parse_expression();
    token_t* colon = token_ref(lexer_token);
    lexer_expect(STR_COLON, "Expected `:` after true branch of conditional `?` expression.");
    node_t* right = parse_conditional_expression();

    parse_conditional_expression_types(&left, &right);

    node_append(conditional, node_make_predicate(condition));
    node_append(conditional, left);
    node_append(conditional, right);

    conditional->type = type_ref(left->type);

    token_deref(colon);
    return conditional;
}

node_t* parse_assignment_expression(void) {
    node_t* left = parse_conditional_expression();

    node_kind_t kind = node_kind_of_assignment_operator(lexer_token);
    if (kind == NODE_INVALID) {
        // not an assignment
        return left;
    }
    if (!node_is_location(left)) {
        fatal_token(left->token, "Left side of assignment operator must be a storage location (an l-value).");
    }

    token_t* token = lexer_take();
    node_t* right = parse_assignment_expression();

    if (type_is_pointer(left->type) && kind != NODE_ASSIGN) {
        // In a compound assignment to a pointer, the value should be treated
        // as an pointer-size integer (i.e. we are shifting or masking a
        // pointer.)
        right = node_cast_base(right, BASE_UNSIGNED_INT, NULL);
    } else {
        // In all other cases the value must be convertible to the target. This
        // is an implicit cast so it'll warn or error if the types don't match.
        right = node_cast(right, left->type, NULL);
    }

    node_t* assign = node_new_token(kind, token);
    token_deref(token);
    assign->type = type_ref(left->type);
    node_append(assign, left);
    node_append(assign, right);
    return assign;
}

static node_t* parse_comma_expression(void) {
    node_t* node = parse_assignment_expression();
    if (!lexer_is(STR_COMMA)) {
        return node;
    }

    node_t* sequence = node_new_lexer(NODE_SEQUENCE);
    do {
        node_append(sequence, node_cast_base(node, BASE_VOID, NULL));
        node = parse_assignment_expression();
    } while (lexer_accept(STR_COMMA));

    sequence->type = type_ref(node->type);
    node_append(sequence, node);
    return sequence;
}

node_t* parse_expression(void) {
    return parse_comma_expression();
}

node_t* parse_predicate(void) {
    return node_make_predicate(parse_expression());
}

node_t* parse_constant_expression(void) {
    // Comma and assignment operators are not allowed in a constant expression
    // so we start at a conditional expression. Note that we don't check
    // whether the expression actually is constant; evaluation will fail if it
    // isn't.
    // TODO we should check that the forbidden operators are not used
    // recursively.
    return parse_conditional_expression();
}

static void parse_va_list_arg(node_t* builtin) {
    node_t* arg = parse_assignment_expression();
    if (!type_matches_base(arg->type, BASE_VA_LIST)) {
        fatal_token(arg->token, "Expected a `va_list` as argument to `%s`.", builtin->token->value->bytes);
    }
    node_append(builtin, arg);
}

static node_t* parse_builtin_va_arg(node_t* builtin) {
    lexer_expect(STR_PAREN_OPEN, "Expected `(` after `va_arg`.");
    parse_va_list_arg(builtin);
    lexer_expect(STR_COMMA, "Expected `,` after expression in `va_arg`.");
    builtin->type = try_parse_type();
    if (builtin->type == NULL) {
        fatal_token(lexer_token, "Expected type after `,` in `va_arg`.");
    }
    lexer_expect(STR_PAREN_CLOSE, "Expected `)` after type of `va_arg`.");
    return builtin;
}

static node_t* parse_builtin_va_start(node_t* builtin) {
    builtin->type = type_new_base(BASE_VOID);
    lexer_expect(STR_PAREN_OPEN, "Expected `(` after `va_start`.");
    parse_va_list_arg(builtin);

    // TODO: C23 only requires the first argument. Additional arguments are
    // ignored and not evaluated. We'll wrap this behaviour in our va_start
    // macro in libc so any extra arguments won't get here. If the argument is
    // provided to the builtin, we're on an older language standard so it
    // must be the name of the final parameter.
    if (lexer_accept(STR_COMMA)) {
        if (lexer_token->type != token_type_alphanumeric) {
            fatal_token(lexer_token, "Expected the name of the final named parameter after `va_start`.");
        }
        // TODO for now we don't bother to check that it actually matches. We just discard it.
        lexer_consume();
    }

    lexer_expect(STR_PAREN_CLOSE, "Expected `)` after contents of `va_start`.");
    return builtin;
}

static node_t* parse_builtin_va_end(node_t* builtin) {
    builtin->type = type_new_base(BASE_VOID);
    lexer_expect(STR_PAREN_OPEN, "Expected `(` after `va_end`.");
    parse_va_list_arg(builtin);
    lexer_expect(STR_PAREN_CLOSE, "Expected `)` after expression in `va_end`.");
    return builtin;
}

static node_t* parse_builtin_va_copy(node_t* builtin) {
    builtin->type = type_new_base(BASE_VOID);
    lexer_expect(STR_PAREN_OPEN, "Expected `(` after `va_copy`.");
    parse_va_list_arg(builtin);
    lexer_expect(STR_COMMA, "Expected `,` after first argument to `va_copy`.");
    parse_va_list_arg(builtin);
    lexer_expect(STR_PAREN_CLOSE, "Expected `)` after second argument to `va_copy`.");
    return builtin;
}

static node_t* parse_builtin_func(node_t* builtin) {
    node_t* string = node_new_token(NODE_STRING, current_function->name);
    type_t* base = type_new_base(BASE_CHAR);
    string->type = type_new_array(base, string_length(string->token->value) + 1);
    type_deref(base);

    if (current_function->name_label == -1) {
        // This is the first time we've seen __func__ in this function. Emit
        // the name as its own symbol.

        current_function->name_label = next_string++;

        emit_source_location(lexer_token);
        emit_char('@');
        emit_cstr(STRING_LABEL_PREFIX);
        emit_hex_number(current_function->name_label);
        emit_newline();

        emit_cstr(ASM_INDENT);
        emit_string_literal(string->token->value);
        emit_newline();

        emit_cstr(ASM_INDENT);
        emit_quoted_byte(0);
        emit_newline();
        emit_newline();
    }
    string->string_label = current_function->name_label;

    // When not optimizing, we append the string node to the builtin instead of
    // returning the string directly. This way we can see the builtin __func__
    // node in a tree dump which makes it easier to debug.
    if (optimization) {
        node_delete(builtin);
        return string;
    }
    node_append(builtin, string);
    builtin->type = type_ref(string->type);
    return builtin;
}

node_t* parse_builtin(builtin_t builtin) {
    node_t* node = node_new_lexer(NODE_BUILTIN);
    node->builtin = builtin;

    switch (builtin) {
        case BUILTIN_VA_ARG: return parse_builtin_va_arg(node);
        case BUILTIN_VA_START: return parse_builtin_va_start(node);
        case BUILTIN_VA_END: return parse_builtin_va_end(node);
        case BUILTIN_VA_COPY: return parse_builtin_va_copy(node);
        case BUILTIN_FUNC: return parse_builtin_func(node);
    }

    fatal("Internal error: cannot parse unrecognized builtin.");
}
