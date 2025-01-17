#!/bin/sh

# The MIT License (MIT)
#
# Copyright (c) 2023-2024 Fraser Heavy Software
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

set -e
mkdir -p build/intermediate/cpp-2-full

echo
echo === Building cpp/2-full

# TODO cpp/2 doesn't exist yet. for now we just build cpp/1 and call it cpp/2
# in order to complete the full bootstrap.

echo Compiling cpp/1 as cpp/2
onrampvm build/intermediate/cc/cc.oe \
    @core/cpp/2-full/build-ccargs \
    -c core/cpp/1-omc/cpp.c \
    -o build/intermediate/cpp-2-full/cpp.oo

echo Linking cpp/2-full
onrampvm build/intermediate/ld-2-full/ld.oe \
    build/intermediate/libc-2-opc/libc.oa \
    build/intermediate/libo-1-opc/libo.oa \
    build/intermediate/cpp-2-full/cpp.oo \
    -o build/intermediate/cpp-2-full/cpp.oe

