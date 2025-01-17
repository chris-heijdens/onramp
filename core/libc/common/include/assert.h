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



/*
 * <assert.h> is not a normal header file. As per the C spec, this part sits
 * outside the include guard.
 */

#ifndef __onramp_cpp_omc__
    #undef assert
    #ifdef NDEBUG
        #define assert(x) ((void)0)
    #else
        #define assert(x) ((x) ? ((void)0) : __assert_fail(#x, __FILE__, __LINE__, __func__))
    #endif
#endif



/*
 * The rest of the file uses a traditional include guard.
 */

#ifndef __ONRAMP_LIBC_ASSERT_H_INCLUDED
#define __ONRAMP_LIBC_ASSERT_H_INCLUDED

#ifndef __onramp_libc__
    #error "__onramp/__predef.h must be force-included by the preprocessor before any libc headers."
#endif

#ifdef __onramp_cpp_omc__
    // With the omC preprocessor, assert can't be a macro. It's a function and
    // it's always called even under NDEBUG.
    #define assert __assert
    void __assert(int __expression);
#endif

void __assert_fail(const char* __expression, const char* __file,
        int __line, const char* __function);

#ifndef __onramp_cci_omc__
#ifndef __onramp_cci_opc__
#define static_assert _Static_assert
#endif
#endif

#endif
