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

#include "emit.h"

#include <ctype.h>
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>

#include "common.h"
#include "lexer.h"



static FILE* output_file;

static bool first_term;

static char* emit_decimal_buffer;

static bool emit_enabled;



/*
 * Low-level write functions
 */

void emit_char(char c) {
    if (!emit_enabled) {
        return;
    }
    fputc(c, output_file);
}

void emit_string(const char* c) {
    if (!emit_enabled) {
        return;
    }
    fputs(c, output_file);
}

static char int_to_hex(int value) {
    if (value <= 9) {
        return '0' + value;
    }
    if (value <= 15) {
        return 'A' + (value - 10);
    }
    fatal("Internal error: invalid hex value");
}

static void emit_hex_char(char nibble) {
    emit_char(int_to_hex(nibble));
}

static void emit_hex_byte(char byte) {
    emit_hex_char((byte >> 4) & 0xF);
    emit_hex_char(byte & 0xF);
}

void emit_hex_number(int number) {
    // We don't have printf() so we have to convert to string manually.
    if (number & 0xF0000000) {emit_hex_char((number >> 28) & 0xF);}
    if (number & 0xFF000000) {emit_hex_char((number >> 24) & 0xF);}
    if (number & 0xFFF00000) {emit_hex_char((number >> 20) & 0xF);}
    if (number & 0xFFFF0000) {emit_hex_char((number >> 16) & 0xF);}
    if (number & 0xFFFFF000) {emit_hex_char((number >> 12) & 0xF);}
    if (number & 0xFFFFFF00) {emit_hex_char((number >> 8) & 0xF);}
    if (number & 0xFFFFFFF0) {emit_hex_char((number >> 4) & 0xF);}
    emit_hex_char(number & 0xF);
}

static bool is_string_char_valid_assembly(char c) {

    // these characters are invalid in a string in Onramp assembly
    if ((c == '\\') | (c == '"')) {
        return false;
    }

    // otherwise it must be a printable character
    return isprint(c);
}



/*
 * Public write functions
 */

void emit_newline(void) {
    emit_char('\n');
    first_term = true;
}

void emit_zero(void) {
    if (first_term) {
        emit_string("  ");
        first_term = false;
    }
    emit_char('0');
    emit_char(' ');
}

void emit_term(const char* keyword) {
    if (first_term) {
        emit_string("  ");
        first_term = false;
    }
    emit_string(keyword);
    emit_char(' ');
}

void emit_register(int index) {
    if (index < 10) {
        emit_char('r');
        emit_char('0' + index);
        emit_char(' ');
        return;
    }

    if (index < 12) {
        emit_char('r');
        emit_char('a' + (index - 10));
        emit_char(' ');
        return;
    }

    if (index == 12) {
        emit_term("rsp");
        return;
    }

    if (index == 13) {
        emit_term("rfp");
        return;
    }

    if (index == 14) {
        emit_term("rpp");
        return;
    }

    if (index == 15) {
        emit_term("rip");
        return;
    }

    fatal("Internal error: invalid register number.");
}

void emit_label(char type, const char* label_name) {
    emit_char(type);
    emit_string(label_name);
    emit_char(' ');
}

void emit_prefixed_label(char type, const char* prefix, const char* label_name) {
    emit_char(type);
    emit_string(prefix);
    emit_string(label_name);
    emit_char(' ');
}

void emit_computed_label(char type, const char* prefix, int label) {
    emit_char(type);
    emit_string(prefix);
    emit_hex_number(label);
    emit_char(' ');
}

static void emit_decimal(int number) {

    // special cases
    if (number == 0) {
        emit_char('0');
        return;
    }
    if (number == INT_MIN) {
        emit_string("-2147483648");
        return;
    }

    // check for negative
    if (number < 0) {
        emit_char('-');
        number = (0 - number);  // TODO fix unary -
    }

    char* buf;
    buf = emit_decimal_buffer;
    *(buf + 11) = 0;
    int i;
    i = 11;

    while (number != 0) {
        i = (i - 1);
        int digit;
        digit = (number - ((number / 10) * 10));
        *(buf + i) = ('0' + digit);
        number = (number / 10);
    }

    emit_string(buf + i);
}

void emit_int(int value) {

    // For small ints we emit in decimal because it's shorter than hex and
    // easier to read. (Negative ints will always be 10 hex characters so it's
    // almost always smaller to do decimal.)
    // TODO this if is kinda dumb, should probably just always do decimal.
    // (unless we want to avoid the division operation in which case these
    // limits should be much smaller.)
    if ((value > -100000000) & (value < 1000000)) {
        emit_decimal(value);
        emit_char(' ');
        return;
    }

    // Other ints are emitted as the full hexadecimal. This works regardless of
    // whether the number is signed, but any negative numbers will be the full
    // 8 characters.
    emit_string("0x");
    emit_hex_number(value);
    emit_char(' ');
}

void emit_quoted_byte(char byte) {
    emit_char('\'');
    emit_hex_byte(byte);
}

void emit_string_literal(const char* str) {
    bool open = false;

    char c = *str;
    while (c) {
        bool valid = is_string_char_valid_assembly(c);
        if (valid != open) {
            emit_char('"');
            open = !open;
        }
        if (valid) {
            emit_char(c);
        }
        if (!valid) {
            emit_quoted_byte(c);
        }

        str = (str + 1);
        c = *str;
    }

    if (open) {
        emit_char('"');
    }
}

void emit_character_literal(char c) {
    if (is_string_char_valid_assembly(c)) {
        emit_char('"');
        emit_char(c);
        emit_char('"');
        return;
    }
    emit_quoted_byte(c);
}

void emit_init(const char* output_filename) {
    emit_decimal_buffer = malloc(12);
    first_term = true;
    emit_enabled = true;

    output_file = fopen(output_filename, "wb");
    if (output_file == NULL) {
        fatal("ERROR: Failed to open output file.");
    }

    // We put the debug info in manual line control mode. We'll be outputting a
    // line increment directive (a lone '#') for each newline in the input.
    emit_string("#line manual\n");
}

void emit_destroy(void) {
    // make sure there's a trailing newline
    emit_char('\n');
    fclose(output_file);
    free(emit_decimal_buffer);
}

void emit_set_enabled(bool enabled) {
    emit_enabled = enabled;
}

bool emit_is_enabled(void) {
    return emit_enabled;
}

void emit_global_divider(void) {
    // emit extra newlines to space out globals
    emit_newline();
    emit_newline();
    emit_newline();
}

void emit_line_increment_directive(void) {
    // always emit line directives
    bool was_enabled = emit_enabled;
    emit_enabled = true;

    if (!first_term) {
        emit_newline();
    }
    emit_char('#');
    emit_char('\n');

    emit_enabled = was_enabled;
}

void emit_line_directive(void) {
    // always emit line directives
    bool was_enabled = emit_enabled;
    emit_enabled = true;

    if (!first_term) {
        emit_newline();
    }
    emit_string("#line ");
    emit_decimal(current_line);
    emit_string(" \"");
    emit_string(current_filename); // TODO check for special characters, escape them somehow
    emit_string("\"\n");

    emit_enabled = was_enabled;
}
