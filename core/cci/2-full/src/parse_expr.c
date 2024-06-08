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

static int next_string;

static node_t* parse_unary_expression(void);
static node_t* parse_assignment_expression(void);

void parse_expr_init(void) {
}

void parse_expr_destroy(void) {
}

static node_t* parse_number(void) {
    assert(lexer_token->type == token_type_number);
    node_t* node = node_new_lexer(NODE_NUMBER);
    sscanf(string_cstr(node->token->value), "%i", &node->int_value); // TODO manual parse number, error check, don't use sscanf
    node->type = type_new_base(BASE_SIGNED_INT);
    return node;
}

static node_t* parse_character(void) {
    assert(lexer_token->type == token_type_character);
    node_t* node = node_new_lexer(NODE_CHARACTER);
    // TODO char prefixes
    node->int_value = node->token->value->bytes[0];
    node->type = type_new_base(BASE_SIGNED_INT);
    return node;
}

static node_t* parse_string(void) {
    assert(lexer_token->type == token_type_string);
    int label = next_string++;
    token_t* first = token_ref(lexer_token);

    // String literals are emitted on-the-fly. We currently don't merge
    // identical string literals and we don't bother to optimize away string
    // literals that are only used in e.g. sizeof. We'll let the linker's
    // garbage collection clean them up.
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
        emit_cstr("  ");
        emit_string_literal(string_cstr(lexer_token->value));
        emit_newline();
        lexer_consume();
    } while (lexer_token->type == token_type_string);

    // append null-terminator
    emit_cstr("  ");
    emit_quoted_byte(0);
    emit_newline();
    emit_newline();

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

    while (!lexer_accept(STR_BRACE_CLOSE)) {
        parse_statement(sequence, true);
    }

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
        if (!symbol) {
            fatal_token(lexer_token, "No such variable or function");
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
            node_t* node = node_cast(parse_unary_expression(), type, paren);
            lexer_expect(STR_PAREN_CLOSE, "Expected `)` after cast expression");
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
    fatal_token(lexer_token, "Unrecognized token.");
    return NULL;
}

static node_t* parse_function_call(node_t* function) {
    if (!type_is_callable(function->type))
        fatal_token(lexer_token, "Expected callable function before `(`.");

    node_t* call = node_new_lexer(NODE_CALL);
    call->type = type_ref(function->type->ref); // return type of function
    node_append(call, function);

    // collect args
    if (!lexer_accept(STR_PAREN_CLOSE)) {
        while (1) {
            node_append(call, parse_assignment_expression());
            if (lexer_accept(STR_COMMA))
                continue;
            lexer_expect(STR_PAREN_CLOSE, "Expected `,` or `)` after function argument.");
            break;
        }
    }

    return call;
}

static node_t* parse_postfix_expression(void) {

    // A postfix expression starts with a primary expression.
    node_t* node = parse_primary_expression();

    // Check for postfix operators
    while (1) {

        // function call
        if (lexer_is(STR_PAREN_OPEN)) {
            node = parse_function_call(node);
            continue;
        }

        // TODO

        break;
    }

    return node;
}

// Parses an "of" operator, e.g. sizeof, typeof, alignof
static node_t* parse_of(node_kind_t kind) {
    // TODO
    fatal("sizeof/typeof not implemented");
}

static node_t* parse_unary_operator(node_kind_t kind) {
    node_t* node = node_new_lexer(kind);
    node_t* child = parse_unary_expression();
    node_append(node, child);

    switch (kind) {
        case NODE_UNARY_PLUS:
        case NODE_UNARY_MINUS:
        case NODE_BIT_NOT:
            // TODO handle types appropriately, cannot use these on records for
            // example, enums decay to int, need to promote to int. probably
            // other rules, check the spec
            node->type = type_ref(child->type);
            break;
        case NODE_LOGICAL_NOT:
            // TODO also handle types appropriately
            node->type = type_new_base(BASE_SIGNED_INT);
            break;
        case NODE_DEREFERENCE:
            if (!type_is_indirection(child->type)) {
                fatal_token(node->token, "Cannot dereference non-pointer type");
            }
            node->type = type_ref(child->type->ref);
            break;
        case NODE_ADDRESS_OF:
            // TODO child node must be a location
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
        return parse_of(NODE_SIZEOF);
    }
    if (lexer_is(STR_TYPEOF) || lexer_is(STR_TYPEOF_X)) {
        return parse_of(NODE_TYPEOF);
    }
    if (lexer_is(STR_TYPEOF_UNQUAL)) {
        return parse_of(NODE_TYPEOF_UNQUAL);
    }
    if (lexer_is(STR_ALIGNOF) || lexer_is(STR_ALIGNOF_X)) {
        fatal("TODO alignof");
        //return parse_of(NODE_ALIGNOF);
    }

    return parse_postfix_expression();
}

static void parse_binary_conversions(node_t* op, node_t* left, node_t* right) {
    if (!type_is_arithmetic(left->type))
        fatal_token(op->token, "Left side of binary expression must be an arithmetic type.");
    if (!type_is_arithmetic(right->type))
        fatal_token(op->token, "Right side of binary expression must be an arithmetic type.");

    // All operands of binary arithmetic operations must first be promoted to
    // register width.
    left = node_promote(left);
    right = node_promote(right);

    switch (op->kind) {
        case NODE_SHL:
        case NODE_SHR:
            break;
        case NODE_ADD:
        case NODE_SUB:
            // TODO
            break;
        default:
            break;
    }

    op->type = type_ref(left->type);
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

static node_t* parse_conditional_expression(void) {
    node_t* condition = parse_binary_expression(0);
    if (!lexer_is(STR_QUESTION)) {
        return condition;
    }

    if (lexer_is(STR_COLON)) {
        // https://gcc.gnu.org/onlinedocs/gcc/extensions-to-the-c-language-family/conditionals-with-omitted-operands.html
        fatal_token(lexer_token, "TODO support elvis operator");
    }

    node_t* node = node_new_lexer(NODE_IF);
    node_append(node, node_make_predicate(condition));
    node_append(node, parse_expression());
    lexer_expect(STR_COLON, "Expected `:` after true branch of conditional `?` expression.");
    node_append(node, parse_conditional_expression());
    return node;
}

static node_t* parse_assignment_expression(void) {
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

    node_t* right = node_cast(parse_assignment_expression(), left->type, NULL);

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

    node_t* parent = node_new_lexer(NODE_SEQUENCE);
    node_append(parent, node);
    do {
        node_append(parent, parse_assignment_expression());
    } while (lexer_accept(STR_COMMA));
    return parent;
}

node_t* parse_expression(void) {
    return parse_comma_expression();
}

node_t* parse_predicate(void) {
    return node_make_predicate(parse_expression());
}