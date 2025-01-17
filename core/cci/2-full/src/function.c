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

#include "function.h"

#include <stdlib.h>

#include "block.h"
#include "type.h"
#include "node.h"
#include "token.h"

function_t* function_new(type_t* type, token_t* name,
        string_t* asm_name, node_t* root)
{
    function_t* function = malloc(sizeof(function_t));
    function->type = type_ref(type);
    function->name = token_ref(name);
    function->asm_name = string_ref(asm_name);
    function->root = root;
    vector_init(&function->blocks);
    function->variadic_offset = -1;
    function->name_label = -1;
    return function;
}

void function_delete(function_t* function) {
    // TODO free blocks
    size_t count = vector_count(&function->blocks);
    for (size_t i = 0; i < count; ++i)
        block_delete(vector_at(&function->blocks, i));
    vector_destroy(&function->blocks);
    node_delete(function->root);
    string_deref(function->asm_name);
    token_deref(function->name);
    type_deref(function->type);
    free(function);
}

void function_add_block(function_t* function, block_t* block) {
    vector_append(&function->blocks, block);
}
