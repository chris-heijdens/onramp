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

#include "compile.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "emit.h"
#include "locals.h"
#include "global.h"

static int compile_inhibit;

void compile_init(void) {
}

void compile_destroy(void) {
}

void compile_inhibit_push(void) {
    compile_inhibit = (compile_inhibit + 1);
    emit_set_enabled(false);
}

void compile_inhibit_pop(void) {
    compile_inhibit = (compile_inhibit - 1);
    emit_set_enabled(compile_inhibit == 0);
}

static char compile_storage_glyph(storage_t storage) {
    assert(storage != STORAGE_TYPEDEF);
    if (storage == STORAGE_STATIC) {
        return '@';
    }
    return '=';
}

void compile_global_variable(const type_t* type, const char* name, storage_t storage) {
    emit_label(compile_storage_glyph(storage), name);
    emit_newline();

    int size = type_size(type);
    int i = 0;
    while (i < size) {
        if (i > 0) {
            if ((i & 3) == 0) {
                emit_char(' ');
            }
            if ((i & 15) == 0) {
                emit_newline();
            }
        }
        emit_quoted_byte(0x00);
        i = (i + 1);
    }
    emit_newline();
    emit_global_divider();
}

void compile_enum_value(const char* name, int value) {
    emit_label('@', name);
    emit_newline();
    emit_int(value);
    emit_newline();
    emit_global_divider();
}

void compile_return(void) {
    emit_term("leave");
    emit_newline();
    emit_term("ret");
    emit_newline();
}

void compile_zero(void) {
    emit_term("zero");
    emit_term("r0");
    emit_newline();
}

void compile_function_open(const global_t* global) {
    const char* name = global_name(global);
    int param_count = global_function_param_count(global);

    // We don't know the stack frame size yet so we'll emit the function
    // prologue at the end and then jump back here. (Technically we're
    // compiling most of the function as a different symbol.)
    emit_prefixed_label('@', "_F_", name);
    emit_newline();

    // The first four arguments arrive in registers, the rest come on the
    // stack. We copy them to their new offsets.
    // Note that we don't touch the variadic arguments. The user has to access
    // the with va_arg().
    int i = 0;
    while (i < param_count) {
        if (i < 4) {
            emit_term("stw");
            emit_register(i);
            emit_term("rfp");
            emit_int(-(i + 1) << 2);
            emit_newline();
        }
        if (i >= 4) {
            emit_term("ldw");
            emit_term("r9");
            emit_term("rfp");
            emit_int((i - 2) << 2);
            emit_newline();

            emit_term("stw");
            emit_term("r9");
            emit_term("rfp");
            emit_int(-(i + 1) << 2);
            emit_newline();
        }
        i = (i + 1);
    }
}

void compile_function_close(const global_t* global, storage_t storage) {
    const char* name = global_name(global);

    // add a return to the function in case it didn't return on its own
    // main() needs to return 0 if execution reaches the end of the function
    // without a return statement.
    //TODO uncomment this
    //if (0 == strcmp(name, "main")) {
        emit_term("zero");
        emit_term("r0");
        emit_newline();
    //}
    emit_term("leave");
    emit_newline();
    emit_term("ret");
    emit_newline();

    // emit the function prologue
    emit_newline();
    emit_label(compile_storage_glyph(storage), name);
    emit_newline();
    emit_term("enter");
    emit_newline();

    // set up the stack frame (now that we know its size)
    //locals_pop(locals_count - arg_count);
    //printf("closing function, %i vars left, %i max, %i vars in function\n", locals_count, compile_function_max_locals_count, compile_function_max_locals_count - locals_count);
    if (locals_frame_size > 0) {
        if (locals_frame_size < 0x80) {
            // the frame size fits in a mix-type byte
            emit_term("sub");
            emit_term("rsp");
            emit_term("rsp");
            emit_int(locals_frame_size);
            emit_newline();
        }
    }
    if (locals_frame_size >= 0x80) {
        // the frame size needs to go in a temporary register
        emit_term("imw");
        emit_term("r9");
        emit_int(locals_frame_size);
        emit_newline();
        emit_term("sub");
        emit_term("rsp");
        emit_term("rsp");
        emit_term("r9");
        emit_newline();
    }

    // jump to the top of the function
    emit_term("jmp");
    emit_prefixed_label('^', "_F_", name);
    emit_global_divider();
}

void compile_immediate(int x) {
    emit_term("imw");
    emit_term("r0");
    emit_int(x);
    emit_newline();
}

type_t* compile_immediate_signed_int(int x) {
    compile_immediate(x);
    return type_new_base(BASE_SIGNED_INT);
}

type_t* compile_sizeof(type_t* type) {
    size_t size = type_size(type);
    type_delete(type);
    compile_immediate(size);
    return type_new_base(BASE_UNSIGNED_INT);
}

type_t* compile_character_literal(char c) {
    emit_term("mov");
    emit_term("r0");
    emit_character_literal(c);
    emit_newline();
    // character literals are int, not char
    return type_new_base(BASE_SIGNED_INT);
}

void compile_stack_shift(int offset) {
    if (offset >= -0x70) {
        if (offset < 0x80) {
            // the offset fits in a mix-type byte
            emit_term("add");
            emit_term("rsp");
            emit_term("rsp");
            emit_int(offset);
            emit_newline();
            return;
        }
    }

    // the offset needs an immediate load
    emit_term("imw");
    emit_register(9);
    emit_int(offset);
    emit_newline();
    emit_term("add");
    emit_term("rsp");
    emit_term("rsp");
    emit_register(9);
    emit_newline();
}

static void compile_load_offset(bool load, int offset, int register_num,
        const char* ref)
{
    const char* op = "add";
    if (load) {
        op = "ldw";
    }

    if (offset >= -0x70) {
        if (offset < 0x80) {
            // the offset fits in a mix-type byte
            emit_term(op);
            emit_register(register_num);
            emit_term(ref);
            emit_int(offset);
            emit_newline();
            return;
        }
    }

    // the offset needs an immediate load
    emit_term("imw");
    emit_register(register_num);
    emit_int(offset);
    emit_newline();
    emit_term(op);
    emit_register(register_num);
    emit_term(ref);
    emit_register(register_num);
    emit_newline();
}

void compile_frame_offset(bool load, int offset, int register_num) {
    compile_load_offset(load, offset, register_num, "rfp");
}

void compile_stack_offset(bool load, int offset, int register_num) {
    compile_load_offset(load, offset, register_num, "rsp");
}

void compile_load(int register_num) {
    emit_term("ldw");
    emit_register(register_num);
    emit_register(register_num);
    emit_term("0");
    emit_newline();
}

type_t* compile_load_variable(const char* name) {

    // locals shadow globals so we check locals first.
    const type_t* type;
    int offset;
    bool local = locals_find(name, &type, &offset);
    if (local) {
        compile_frame_offset(false, offset, 0);
    }
    if (!local) {
        global_t* global = global_find(name);
        if (global == NULL) {
            fatal_2("Variable not found: ", name);
        }
        if (global_is_function(global)) {
            fatal_2("Cannot use function as a variable: ", name);
        }
        type = global_type(global);
        emit_term("imw");
        emit_term("r0");
        emit_label('^', name);
        emit_newline();
        emit_term("add");
        emit_term("r0");
        emit_term("rpp");
        emit_term("r0");
        emit_newline();
    }

    // return it as an lvalue
    type_t* ret = type_clone(type);
    return type_set_lvalue(ret, true);
}

void compile_string_literal_invocation(int label_index) {
    emit_term("imw");
    emit_term("r0");
    emit_computed_label('^', STRING_LABEL_PREFIX, label_index);
    emit_newline();
    emit_term("add");
    emit_term("r0");
    emit_term("rpp");
    emit_term("r0");
    emit_newline();
}

void compile_string_literal_definition(int label_index, const char* string) {
    emit_newline();
    emit_computed_label('@', STRING_LABEL_PREFIX, label_index);
    emit_newline();
    emit_string_literal(string);
    emit_quoted_byte(0x00); // null-terminator
    emit_newline();
}

const char* compile_load_instruction(int size) {
    if (size == 1) {
        return "ldb";
    }
    if (size == 2) {
        return "lds";
    }
    if (size == 4) {
        return "ldw";
    }
    fatal("Internal error: Cannot perform load of unrecognized size");
}

const char* compile_store_instruction(int size) {
    if (size == 1) {
        return "stb";
    }
    if (size == 2) {
        return "sts";
    }
    if (size == 4) {
        return "stw";
    }
    fatal("Internal error: Cannot perform store of unrecognized size");
}

/**
 * Emits code to dereference the value of the given type stored in the given
 * register.
 */
void compile_dereference(type_t* type, int register_num) {
    if (type_is_array(type)) {
        fatal("Internal error: Cannot dereference this array.");
    }

    // Note that when we load a signed char or short, we don't do sign
    // extension right away. The type is still char or short; it will be sign
    // extended if and when needed. (Otherwise, e.g. copying memory with char*
    // would cause an unnecessary sign extension of every single byte.)

    emit_term(compile_load_instruction(type_size(type)));
    emit_register(register_num);
    emit_term("0");
    emit_register(register_num);
    emit_newline();
}

type_t* compile_lvalue_to_rvalue(type_t* type, int register_num) {
    if (!type_is_lvalue(type)) {
        return type;
    }
    type_set_lvalue(type, false);

    bool is_array = type_is_array(type);
    if (is_array) {
        // An array lvalue is the address of the first element. We want to
        // decay this to an rvalue pointer to the first element. This is
        // already what the register contains so we don't emit any code.
        type_set_array_length(type, TYPE_ARRAY_NONE);
        type_increment_pointers(type);
    }
    if (!is_array) {
        compile_dereference(type, register_num);
    }

    return type;
}

type_t* compile_assign(type_t* left, type_t* right) {

    // We're storing into the left. It must be an lvalue and not an array.
    if (type_is_array(left)) {
        fatal("Assignment location cannot be an array.");
    }
    if (!type_is_lvalue(left)) {
        fatal("Assignment location is not an lvalue.");
    }

    right = compile_lvalue_to_rvalue(right, 0);

    left = type_set_lvalue(left, false);
    size_t size = type_size(left);

    // cast right to left (performing sign extension, etc.)
    type_t* result = compile_cast(right, left, 0);

    // store it
    emit_term(compile_store_instruction(size));
    emit_term("r0");
    emit_term("0");
    emit_term("r1");
    emit_newline();
    return result;
}

/**
 * Calculates the arithmetic factor of the given type and emits it for the given
 * register.
 *
 * For pointers we multiply or divide by the size of the pointed-to type (using
 * shifts if possible for efficiency.)
 */
static void compile_arithmetic_factor(const type_t* type, int register_num, bool multiply) {

    // We can't perform arithmetic on void.
    if (type_is_base(type, BASE_VOID)) {
        fatal("Cannot perform arithmetic on value of `void` type.");
    }

    // All other scalar types have arithmetic factor of 1.
    if (type_indirections(type) == 0) {
        return;
    }

    // Dereference the type
    type_t* deref = type_decrement_indirection(type_clone(type));

    // We can't perform arithmetic on void*.
    if (type_is_base(deref, BASE_VOID)) {
        fatal("Cannot perform arithmetic on value of `void*` type.");
    }

    size_t size = type_size(deref);
    type_delete(deref);
    if (size == 1) {
        return;
    }

    // Calculate the shift for the given size. (We just hardcode a few
    // possibilities.)
    const char* shift_term = 0;
    if (size == 2) {
        shift_term = "1";
    }
    if (size == 4) {
        shift_term = "2";
    }
    if (size == 8) {
        shift_term = "3";
    }
    if (size == 16) {
        shift_term = "4";
    }

    // If we have a recognized size, use shifts. If we're multiplying, emit
    // shl; if we're diving, emit shrs.
    if (shift_term) {
        if (multiply) {
            emit_term("shl");
        }
        if (!multiply) {
            emit_term("shrs");
        }
        emit_register(register_num);
        emit_register(register_num);
        emit_term(shift_term);
        emit_newline();
        return;
    }

    // We don't recognize the size. Emit mul/divs instead.
    // If the size is too big, we need to use an ancillary register.
    if (size > 127) {
        emit_term("imw");
        emit_term("r9");
        emit_int(size);
        emit_newline();
    }
    if (multiply) {
        emit_term("mul");
    }
    if (!multiply) {
        emit_term("divs");
    }
    emit_register(register_num);
    emit_register(register_num);
    if (size > 127) {
        emit_term("r9");
    }
    if (size <= 127) {
        emit_int(size);
    }
    emit_newline();
}

type_t* compile_add_sub(bool add, type_t* left, type_t* right) {
    int left_indirections = type_indirections(left);
    int right_indirections = type_indirections(right);

    // If both sides are pointers, the types must match. We adjust for the
    // arithmetic factor afterwards.
    bool both = ((left_indirections > 0) & (right_indirections > 0));

    // Otherwise, at most one side is a pointer. Emit the arithmetic factors
    // now.
    if (!both) {
        // right is in register 0; multiply it by the arithmetic factor of left
        compile_arithmetic_factor(left, 0, true);
        // left is in register 1; multiply it by the arithmetic factor of right
        compile_arithmetic_factor(right, 1, true);
    }

    // Compute the result into r0
    if (add) {
        emit_term("add");
    }
    if (!add) {
        emit_term("sub");
    }
    emit_term("r0");
    emit_term("r1");
    emit_term("r0");
    emit_newline();

    // If both sides were pointers, emit the arithmetic factor on the result.
    if (both) {
        compile_arithmetic_factor(left, 0, false);

        // the return type is int.
        type_delete(left);
        type_delete(right);
        return type_new_base(BASE_SIGNED_INT);
    }

    // Return whichever side is a pointer type.
    if (left_indirections) {
        type_delete(right);
        return left;
    }
    if (right_indirections) {
        type_delete(left);
        return right;
    }

    // If neither is a pointer, they've already been converted to the same type
    // so return either one.
    type_delete(right);
    return left;
}

// If the result of a cmps/cmpu in r0 is 0, r0 is set to 1; otherwise it's set
// to 0.
static void compile_cmp_to_true(void) {
    emit_term("add");
    emit_term("r0");
    emit_term("r0");
    emit_term("1");
    emit_newline();
    emit_term("and");
    emit_term("r0");
    emit_term("r0");
    emit_term("1");
    emit_newline();
}

// If the result of a cmps/cmpu in r0 is 0, r0 is set to 0; otherwise it's set
// to 1.
static void compile_cmp_to_false(void) {
    emit_term("and");
    emit_term("r0");
    emit_term("r0");
    emit_term("1");
    emit_newline();
}

static void compile_compare_signed(void) {
    emit_term("cmps");
    emit_term("r0");
    emit_term("r1");
    emit_term("r0");
    emit_newline();
}

static void compile_compare_unsigned(void) {
    emit_term("cmpu");
    emit_term("r0");
    emit_term("r1");
    emit_term("r0");
    emit_newline();
}

// TODO we'll use this to do unordered comparisons when we get rid of cmpu.
// Until then it's actually more instructions to do it this way.
/*
static void compile_compare_sub(void) {
    emit_term("sub");
    emit_term("r0");
    emit_term("r1");
    emit_term("r0");
    emit_newline();
}
*/

static void compile_compare_term(const char* term) {
    emit_term("cmpu");
    emit_term("r0");
    emit_term("r0");
    emit_term(term);
    emit_newline();
}

void compile_boolean_cast(void) {
    emit_term("bool");
    emit_term("r0");
    emit_term("r0");
    emit_newline();
}

void compile_boolean_not(void) {
    emit_term("isz");
    emit_term("r0");
    emit_term("r0");
    emit_newline();
}

type_t* compile_bitwise_not(void) {
    emit_term("not");
    emit_term("r0");
    emit_term("r0");
    emit_newline();
    return type_new_base(BASE_SIGNED_INT);
}

void compile_basic_op(const char* op) {
    emit_term(op);
    emit_term("r0");
    emit_term("r1");
    emit_term("r0");
    emit_newline();
}

type_t* compile_comparison(const char* op, type_t* left, type_t* right) {
    if (!type_is_compatible(left, right)) {
        fatal("Cannot perform comparison between incompatible types.");
    }

    // The types of comparison operands must be compatible. Integers have
    // already been promoted to the same type.
    //
    // Comparing pointers is a bit more complicated because there are special
    // cases: for example a void* can be compared with any pointer, and any
    // pointer can be compared with 0 to check for null.
    //
    // We don't have a great way to enforce these rules so instead we don't
    // bother. We can implement better checks in the final stage.

    // Comparison operators return int. We can clean up the types now.
    bool is_signed = (type_is_signed(left) & type_is_signed(right));
    type_delete(left);
    type_delete(right);
    type_t* ret = type_new_base(BASE_SIGNED_INT);

    // Unordered comparisons use unsigned even if the types are signed.
    if (0 == strcmp(op, "==")) {
        compile_compare_unsigned();
        compile_cmp_to_true();
        return ret;
    }
    if (0 == strcmp(op, "!=")) {
        compile_compare_unsigned();
        compile_cmp_to_false();
        return ret;
    }

    // We use signed comparison for signed ints only; we use unsigned for both
    // unsigned ints and pointers.
    if (is_signed) {
        compile_compare_signed();
    }
    if (!is_signed) {
        compile_compare_unsigned();
    }

    if (0 == strcmp(op, "<")) {
        compile_compare_term("-1");
        compile_cmp_to_true();
        return ret;
    }
    if (0 == strcmp(op, ">")) {
        compile_compare_term("1");
        compile_cmp_to_true();
        return ret;
    }
    if (0 == strcmp(op, "<=")) {
        compile_compare_term("1");
        compile_cmp_to_false();
        return ret;
    }
    if (0 == strcmp(op, ">=")) {
        compile_compare_term("-1");
        compile_cmp_to_false();
        return ret;
    }

    fatal("Internal error: Unhandled comparison operator");
}

type_t* compile_promote(type_t* type, int register_num) {

    // An integer promotion cannot be an l-value.
    type = compile_lvalue_to_rvalue(type, register_num);

    // Pointers don't get promoted.
    if (type_indirections(type) > 0) {
        return type;
    }

    // If we're already width int, there's nothing to promote. (We don't
    // support any larger types.)
    if (type_is_base(type, BASE_SIGNED_INT)) {
        return type;
    }
    if (type_is_base(type, BASE_UNSIGNED_INT)) {
        return type;
    }

    // Otherwise we always promote to int.
    return compile_cast(type, type_new_base(BASE_SIGNED_INT), register_num);
}

// Compiles a binary operation.
// The left value is in r1.
// The right value is in r0.
type_t* compile_binary_op(const char* op, type_t* left, type_t* right) {

    /*
    fputs("compile ", stdout);
    type_print(left);
    fputs(" ", stdout);
    fputs(op, stdout);
    fputs(" ", stdout);
    type_print(right);
    fputc('\n', stdout);
    */

    // We start by promoting both types to register width. This also converts
    // l-values to r-values and catches errors like trying to do arithmetic on
    // structs.
    right = compile_promote(right, 0);
    left = compile_promote(left, 1);

    // Handle shift operators
    if (0 == strcmp(op, "<<")) {
        type_delete(right);
        compile_basic_op("shl");
        return left;
    }
    if (0 == strcmp(op, ">>")) {
        type_delete(right);
        if (type_is_signed(left)) {
            compile_basic_op("shrs");
            return left;
        }
        compile_basic_op("shru");
        return left;
    }

    // All other binary operators convert both sides to a common type. If one
    // type is signed int and the other is unsigned int, we convert signed to
    // unsigned. Otherwise we do nothing. (We leave pointers intact; they'll be
    // handled specially by the operators that handle them.)
    if (type_is_unsigned(left)) {
        if (type_is_signed(right)) {
            right = compile_cast(right, type_new_base(BASE_UNSIGNED_INT), 0);
        }
    }
    if (type_is_unsigned(right)) {
        if (type_is_signed(left)) {
            left = compile_cast(left, type_new_base(BASE_UNSIGNED_INT), 1);
        }
    }

    // add/sub
    if (0 == strcmp(op, "+")) {
        return compile_add_sub(true, left, right);
    }
    if (0 == strcmp(op, "-")) {
        return compile_add_sub(false, left, right);
    }

    // comparisons
    if (0 == strcmp(op, "==")) {
        return compile_comparison(op, left, right);
    }
    if (0 == strcmp(op, "!=")) {
        return compile_comparison(op, left, right);
    }
    if (0 == strcmp(op, "<")) {
        return compile_comparison(op, left, right);
    }
    if (0 == strcmp(op, ">")) {
        return compile_comparison(op, left, right);
    }
    if (0 == strcmp(op, "<=")) {
        return compile_comparison(op, left, right);
    }
    if (0 == strcmp(op, ">=")) {
        return compile_comparison(op, left, right);
    }

    // The rest of these operate only on integers.
    if (!type_is_integer(left)) {
        fatal_3("Left side of `", op, "` must be an integer.");
    }
    if (!type_is_integer(right)) {
        fatal_3("Right side of `", op, "` must be an integer.");
    }
    type_t* ret = left;
    type_delete(right);

    // mul/div/mod
    if (0 == strcmp(op, "*")) {
        compile_basic_op("mul");
        return ret;
    }
    if (0 == strcmp(op, "/")) {
        if (type_is_signed(left)) {
            compile_basic_op("divs");
            return ret;
        }
        compile_basic_op("divu");
        return ret;
    }
    if (0 == strcmp(op, "%")) {
        if (type_is_signed(left)) {
            compile_basic_op("mods");
            return ret;
        }
        compile_basic_op("modu");
        return ret;
    }

    // bitwise ops
    if (0 == strcmp(op, "|")) {
        compile_basic_op("or");
        return ret;
    }
    if (0 == strcmp(op, "&")) {
        compile_basic_op("and");
        return ret;
    }
    if (0 == strcmp(op, "^")) {
        compile_basic_op("xor");
        return ret;
    }

    fatal_2("op not yet implemented: ", op);
    return NULL;
}

static void compile_cast_impl(const char* op, int register_num) {
    if (op) {
        emit_term(op);
        emit_register(register_num);
        emit_register(register_num);
        emit_newline();
    }
}

/*
 * Returns the equivalent base type of the given type for the purpose of
 * compiling casts. Pointers are considered to have base type unsigned int.
 */
static base_t compile_cast_base(const type_t* type) {
    if (type_indirections(type) > 0) {
        return BASE_UNSIGNED_INT;
    }
    return type_base(type);
}

type_t* compile_cast(type_t* current_type, type_t* desired_type, int register_num) {
    base_t desired_base = compile_cast_base(desired_type);
    base_t current_base = compile_cast_base(current_type);
    type_delete(current_type);

    if (desired_base == current_base) {
        return desired_type;
    }

    // If the current type is signed char or signed short, we can't assume the
    // upper bits have been set yet so we need to do sign extension if
    // converting to a larger type. In some cases this needs to be multiple
    // casts, for example casting `signed char` to `unsigned short` requires
    // `sxb` followed by `trs`.
    //
    // There must be a simpler way of doing this without causing too much
    // inefficiency. Probably could check type_size() to decide whether to emit
    // sxb or sxs on the source. We can experiment with simpler code after we
    // have a bunch of passing tests.

    if (desired_base == BASE_SIGNED_CHAR) {
        compile_cast_impl("sxb", register_num);
        return desired_type;
    }

    if (desired_base == BASE_UNSIGNED_CHAR) {
        compile_cast_impl("trb", register_num);
        return desired_type;
    }

    if (desired_base == BASE_SIGNED_SHORT) {
        if (current_base == BASE_SIGNED_CHAR) {
            compile_cast_impl("sxb", register_num);
            return desired_type;
        }
        if (current_base == BASE_UNSIGNED_CHAR) {
            // nothing
            return desired_type;
        }
        compile_cast_impl("sxs", register_num);
        return desired_type;
    }

    if (desired_base == BASE_UNSIGNED_SHORT) {
        if (current_base == BASE_SIGNED_CHAR) {
            compile_cast_impl("sxb", register_num);
            compile_cast_impl("trs", register_num);
            return desired_type;
        }
        if (current_base == BASE_UNSIGNED_CHAR) {
            // nothing
            return desired_type;
        }
        if (current_base == BASE_UNSIGNED_SHORT) {
            // nothing
            return desired_type;
        }
        compile_cast_impl("trs", register_num);
        return desired_type;
    }

    if (current_base == BASE_SIGNED_CHAR) {
        compile_cast_impl("sxb", register_num);
        return desired_type;
    }
    if (current_base == BASE_SIGNED_SHORT) {
        compile_cast_impl("sxs", register_num);
        return desired_type;
    }
    // nothing
    return desired_type;
}

void compile_jump(int label) {
    emit_term("jmp");
    emit_computed_label('&', JUMP_LABEL_PREFIX, label);
    emit_newline();
}

void compile_jump_if_zero(int label) {
    emit_term("jz");
    emit_term("r0");
    emit_computed_label('&', JUMP_LABEL_PREFIX, label);
    emit_newline();
}

void compile_jump_if_not_zero(int label) {
    emit_term("jnz");
    emit_term("r0");
    emit_computed_label('&', JUMP_LABEL_PREFIX, label);
    emit_newline();
}

static void compile_user_label_name(const char* function, const char* name) {
    emit_string(USER_LABEL_PREFIX);
    emit_hex_number(strlen(function));
    emit_char('_');
    emit_string(function);
    emit_char('_');
    emit_string(name);
}

void compile_goto(const char* function, const char* name) {
    emit_term("jmp");
    emit_char('&');
    compile_user_label_name(function, name);
    emit_newline();
}

void compile_label(int label) {
    emit_computed_label(':', JUMP_LABEL_PREFIX, label);
    emit_newline();
}

void compile_user_label(const char* function, const char* name) {
    emit_char(':');
    compile_user_label_name(function, name);
    emit_newline();
}

void compile_push(int register_number) {
    emit_term("push");
    emit_register(register_number);
    emit_newline();
}

void compile_pop(int register_number) {
    emit_term("pop");
    emit_register(register_number);
    emit_newline();
}

void compile_stack_load(int register_number) {
    emit_term("ldw");
    emit_register(register_number);
    emit_term("rsp");
    emit_term("0");
    emit_newline();
}

void compile_global_divider(void) {
    emit_global_divider();
}

type_t* compile_operator_dereference(type_t* type) {

    // If this is already an lvalue, we dereference it now.
    bool is_lvalue = type_is_lvalue(type);
    bool is_array = type_is_array(type);
    if (is_lvalue) {
        if (is_array) {
            // The register already contains the address of the first
            // element so we emit no code. We just remove the array, which
            // removes an indirection.
            type_set_array_length(type, TYPE_ARRAY_NONE);
        }
        if (!is_array) {
            compile_dereference(type, 0);
            type = type_decrement_indirection(type);
        }
        return type;
    }

    // Otherwise we make it an lvalue. It will be dereferenced if and when it is
    // needed.
    if (is_array) {
        fatal("Internal error: cannot dereference r-value array");
    }
    type = type_decrement_indirection(type);
    type = type_set_lvalue(type, true);
    return type;

}

void compile_offset(int offset) {
    if (offset == 0) {
        // nothing to do
        return;
    }

    if (offset >= -112) {
        if (offset <= 127) {
            // the offset can be applied in a mix-type byte
            emit_term("add");
            emit_term("r0");
            emit_term("r0");
            emit_int(offset);
            emit_newline();
            return;
        }
    }

    // the offset needs to go in a temporary register
    emit_term("imw");
    emit_term("r9");
    emit_int(offset);
    emit_newline();
    emit_term("add");
    emit_term("r0");
    emit_term("r0");
    emit_term("r9");
    emit_newline();
}

void compile_mov(int dest, int src) {
    emit_term("mov");
    emit_register(dest);
    emit_register(src);
    emit_newline();
}
