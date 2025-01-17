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



# This is the portable build script for Onramp. It builds all components.
#
# Like all scripts in `core/`, this script is written for the Onramp
# shell, which is a subset of POSIX shell. We have very little actual shell
# functionality here so it's pretty limited in what it can do but it's enough
# to bootstrap Onramp.
#
# This can also be run in an ordinary POSIX shell for ease of debugging. You'll
# need `onrampvm` on your PATH and you'll need to have built `hex/onramp` first.
# Try the setup scripts in `scripts/` to do this automatically, for example
# `scripts/posix/build.sh`.
#
# See `docs/bootstrap-path.md` to follow what this script does.



set -e

# First we get our linker and libc up
sh core/ld/0-global/build.sh
sh core/ar/0-cat/build.sh
sh core/libc/0-oo/build.sh

# Next we build an assembler
sh core/libo/0-oo/build.sh
sh core/as/0-basic/build.sh
sh core/as/1-compound/build.sh

# Next build our omC compiler
sh core/cpp/0-strip/build.sh
sh core/cci/0-omc/build.sh

# Build the rest of the omC toolchain
sh core/cpp/1-omc/build.sh
sh core/ld/1-omc/build.sh
sh core/libc/1-omc/build.sh
sh core/cc/build.sh

# Build the opC toolchain
sh core/cci/1-opc/build.sh
sh core/libc/2-opc/build.sh

# Build the full C compiler
sh core/libo/1-opc/build.sh
sh core/ld/2-full/build.sh
sh core/cci/2-full/build.sh

# Build the rest of the C toolchain
sh core/cpp/2-full/build.sh
sh core/libc/3-full/build.sh
sh core/as/2-full/build.sh

# Install the libc headers
sh core/libc/common/build.sh

# Rebuild our C toolchain with itself
sh core/libc/3-full/rebuild.sh
sh core/libo/1-opc/rebuild.sh
sh core/cc/rebuild.sh
sh core/ld/2-full/rebuild.sh
sh core/as/2-full/rebuild.sh
sh core/cci/2-full/rebuild.sh
sh core/cpp/2-full/rebuild.sh

## # Build the last few tools we need
sh core/hex/1-c89/build.sh
# TODO: The final stage archive tool does not exist yet. For now we provide the
# previous stage.
## sh core/ar/1-unix/build.sh
cp build/intermediate/ar-0-cat/ar.oe build/output/bin/ar.oe
