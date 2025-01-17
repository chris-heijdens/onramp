#!/bin/bash

# This script tests the given libc by linking it with each .c file in all
# subfolders of the given test folder and running each test.
#
#     Usage: run.sh <test folder> <libc archive> [preprocessor options]
#
# If a corresponding .stdout file exists, the program must succeed and its
# output must match the file's contents.
#
# If a corresponding .args file exists, the contents are passed as command-line
# arguments to the program.
#
# If a corresponding .status file exists, the program must exit with the given
# status.

if [ "$1" == "" ]; then
    echo "Need folder to test."
    exit 1
fi
if [ "$2" == "" ]; then
    echo "Need libc to test."
    exit 1
fi

# build some dependencies
set -e
ROOT=$(dirname $0)/../..
make -C $ROOT/test/cpp/1-omc/ build   # TODO 2-full
make -C $ROOT/test/cci/2-full/ build
make -C $ROOT/test/as/2-full/ build
make -C $ROOT/test/ld/2-full/ build
if ! command -v onrampvm > /dev/null; then
    echo "ERROR: onrampvm is required on PATH."
    exit 1
fi
set +e

SOURCE_FOLDER="$1"
LIBC_ID="$2"
LIBC="$3"
shift
shift
shift
PREPROCESSOR_OPTIONS="$@"
TEMP_I=/tmp/onramp-test.i
TEMP_OS=/tmp/onramp-test.os
TEMP_OO=/tmp/onramp-test.oo
TEMP_OE=/tmp/onramp-test.oe
TEMP_STDOUT=/tmp/onramp-test.stdout
ANY_ERROR=0

# we want address sanitizer to return the same error code as the vm so we can
# detect crashes on both
export ASAN_OPTIONS="$ASAN_OPTIONS:exitcode=125"

# determine macros and libc to use for this compiler
if [ "$LIBC_ID" = "oo" ]; then
    MACROS="-D__onramp_libc_oo__=1"
elif [ "$LIBC_ID" = "omc" ]; then
    MACROS="-D__onramp_libc_omc__=1"
elif [ "$LIBC_ID" = "opc" ]; then
    MACROS="-D__onramp_libc_opc__=1"
elif ! [ "$LIBC_ID" = "full" ]; then
    echo "ERROR: Unknown libc stage: $LIBC_ID"
    exit 1
fi
MACROS="-D__onramp__=1 -D__onramp_cpp__=1 -D__onramp_cci__=1 $MACROS"
# TODO use cpp/2 once it exists
MACROS="$MACROS -D__onramp_cpp_opc__=1"
MACROS="$MACROS -I $ROOT/core/libc/common/include"
MACROS="$MACROS -include __onramp/__predef.h"

TESTS_PATH="$(basename $(realpath $SOURCE_FOLDER/..))/$(basename $(realpath $SOURCE_FOLDER))"
echo "Running $TESTS_PATH tests on: $LIBC"

FILES="$(find $SOURCE_FOLDER/* -name '*.c')"

for TESTFILE in $FILES; do
    # temporary hack to skip OLD/ tests
    if echo $TESTFILE | grep -q OLD; then
        continue
    fi

    THIS_ERROR=0
    BASENAME=$(echo $TESTFILE|sed 's/\..$//')
    echo "Testing $BASENAME"

    # preprocess
    PREPROCESSOR_ARGS="$PREPROCESSOR_OPTIONS $MACROS"
    $ROOT/build/test/cpp-1-omc/cpp $PREPROCESSOR_ARGS $BASENAME.c -o $TEMP_I
    if [ $? -ne 0 ]; then
        echo "ERROR: $BASENAME failed to preprocess."
        THIS_ERROR=1
    fi

    # compile
    if [ $THIS_ERROR -ne 1 ]; then
        $ROOT/build/test/cci-2-full/cci -g $TEMP_I -o $TEMP_OS #&> /dev/null
        if [ $? -ne 0 ]; then
            echo "ERROR: $BASENAME failed to compile."
            THIS_ERROR=1
        fi
    fi

    # assemble
    if [ $THIS_ERROR -ne 1 ]; then
        $ROOT/build/test/as-2-full/as $TEMP_OS -o $TEMP_OO &> /dev/null
        if [ $? -ne 0 ]; then
            echo "ERROR: $BASENAME failed to assemble."
            THIS_ERROR=1
        fi
    fi

    # link
    if [ $THIS_ERROR -ne 1 ]; then
        $ROOT/build/test/ld-2-full/ld -g $LIBC $TEMP_OO -o $TEMP_OE &> /dev/null
        if [ $? -ne 0 ]; then
            echo "ERROR: $BASENAME failed to link."
            THIS_ERROR=1
        fi
    fi

    # collect the args
    if [ -e $BASENAME.args ]; then
        # eval echo to expand shell macros
        ARGS="$(eval echo $(cat $BASENAME.args))"
    else
        ARGS=""
    fi

    # run
    if [ $THIS_ERROR -ne 1 ]; then
        onrampvm $TEMP_OE $ARGS >$TEMP_STDOUT 2>/dev/null
        RET=$?
    fi

    # check run status
    if [ $THIS_ERROR -ne 1 ]; then
        if [ -e $BASENAME.status ]; then
            EXPECTED=$(cat $BASENAME.status)
            if [ $RET -ne $EXPECTED ]; then
                echo "ERROR: $BASENAME exited with status $RET, expected status $EXPECTED"
                THIS_ERROR=1
            fi
        else
            if [ $RET -ne 0 ]; then
                echo "ERROR: $BASENAME exited with status $RET, expected success"
                THIS_ERROR=1
            fi
        fi
    fi

    # check stdout
    if [ $THIS_ERROR -ne 1 ] && [ -e $BASENAME.stdout ] && ! diff -q $BASENAME.stdout $TEMP_STDOUT >/dev/null; then
        echo "ERROR: $BASENAME stdout did not match expected"
        THIS_ERROR=1
    fi

    # print commands
    if [ $THIS_ERROR -eq 1 ]; then
        echo "Commands:"
        echo "    make build && \\"
        echo "    $ROOT/build/test/cpp-1-omc/cpp $PREPROCESSOR_ARGS $BASENAME.c -o $TEMP_I && \\"
        echo "    $ROOT/build/test/cci-2-full/cci -g $TEMP_I -o $TEMP_OS && \\"
        echo "    $ROOT/build/test/as-2-full/as $TEMP_OS -o $TEMP_OO && \\"
        echo "    $ROOT/build/test/ld-2-full/ld -g $LIBC $TEMP_OO -o $TEMP_OE && \\"
        echo "    onrampvm $TEMP_OE >$TEMP_STDOUT"
        if [ -e $BASENAME.stdout ]; then
            echo "    diff -q $BASENAME.stdout $TEMP_STDOUT"
        fi
        ANY_ERROR=1
    fi

    # clean up
    rm -f $TEMP_I
    rm -f $TEMP_OS
    rm -f $TEMP_OO
    rm -f $TEMP_OE
    rm -f $TEMP_STDOUT
done

if [ $ANY_ERROR -eq 1 ]; then
    echo "Errors occurred."
    exit 1
fi

echo "Pass."
