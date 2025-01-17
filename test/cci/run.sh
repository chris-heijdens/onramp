#!/bin/bash

# This script tests the given cci compiler by running it against all .i and .c
# files in all subfolders of this test folder.
#
#     Usage: run.sh <test_folder> <compiler_id> <run_commands...>
#
# Options (must come before other args):
#
#     --nonstd           Run non-standard tests (with .nonstd extension)
#     --output <path>    Output assembly to given path (for comparison purposes)
#
# e.g.
#
#     test/cci/run.sh test/cci/0-omc omc onrampvm build/intermediate/cci-0-omc/cci.oe
#
# If a .c file exists, the preprocessor is run on it first, then the compiler
# is run on the output. If a .i file exists, the compiler is run on it
# directly.
#
# The program is then assembled and linked against libc/3 and run. If assembly,
# linking or execution fails, the test fails.
#
# - If a corresponding .fail file exists, the compiler must fail. Otherwise,
#   the compiler must succeed.
#
# - If a corresponding .args file exists, the contents are passed as
#   command-line arguments to the compiler instead of the default arguments.
#   Use $INPUT for the input filename and $OUTPUT for the output filename.
#
# - If a corresponding .stdout file exists, the program's output must match the
#   contents.
#
# - If a corresponding .status file exists, the program must return with the
#   given status code. Otherwise, the program must return with status 0
#   (success.) (TODO this is deprecated; remove this.)
#
# - If a corresponding .skip file exists, the test is skipped.
#
# - If a corresponding .nonstd file exists and the --nonstd argument is not
#   provided, the test is skipped.

set -e
ROOT=$(dirname $0)/../..

NONSTD=0
OUTPUT_PATH=

# parse command-line options
while true; do
    if [ "$1" == "--nonstd" ]; then
        NONSTD=1
        shift
    elif [ "$1" == "--output" ]; then
        OUTPUT_PATH="$2"
        shift
        shift
    else
        break
    fi
done

if [ "$1" == "" ]; then
    echo "Need folder to test."
    exit 1
fi
if [ "$2" == "" ]; then
    echo "Need command to test."
    exit 1
fi

if ! command -v onrampvm > /dev/null; then
    echo "ERROR: onrampvm is required on PATH."
    exit 1
fi

SOURCE_FOLDER="$1"
shift
COMPILER_ID="$1"
shift
COMMAND="$@"
TEMP_I=/tmp/onramp-test.i
TEMP_OS=/tmp/onramp-test.os
TEMP_OO=/tmp/onramp-test.oo
TEMP_OE=/tmp/onramp-test.oe
TEMP_STDOUT=/tmp/onramp-test.stdout
TEMP_STDERR=/tmp/onramp-test.stderr
TEMP_FILES=/tmp/onramp-test-files
TOTAL_ERRORS=0

# we want address sanitizer to return the same error code as the vm so we can
# detect crashes on both
export ASAN_OPTIONS="$ASAN_OPTIONS:exitcode=125"

# determine macros and libc to use for this compiler
if [ "$COMPILER_ID" = "omc" ]; then
    MACROS="-D__onramp_cci_omc__=1"
elif [ "$COMPILER_ID" = "opc" ]; then
    MACROS="-D__onramp_cci_opc__=1"
elif [ "$COMPILER_ID" = "full" ]; then
    MACROS=
else
    echo "ERROR: Unknown compiler: $COMPILER_ID"
    exit 1
fi
MACROS="$MACROS -I$ROOT/core/libc/common/include"
MACROS="$MACROS -D__onramp__=1 -D__onramp_cci__=1"
# TODO use cpp/2
#MACROS="$MACROS -D__onramp_cpp__=1"
MACROS="$MACROS -D__onramp_cpp__=1 -D__onramp_cpp_omc__=1"
MACROS="$MACROS -include __onramp/__predef.h"

# build dependencies
make -C $ROOT/test/cpp/1-omc/ build  # TODO cpp/2
make -C $ROOT/test/as/2-full/ build
make -C $ROOT/test/ld/2-full/ build
make -C $ROOT/test/libc/3-full/ build
if ! command -v onrampvm > /dev/null; then
    echo "ERROR: onrampvm is required on PATH."
    exit 1
fi

TESTS_PATH="$(basename $(realpath $SOURCE_FOLDER/..))/$(basename $(realpath $SOURCE_FOLDER))"
echo "Running $TESTS_PATH tests on: $COMMAND"

# Collect and sort file list
# Note: We support .i files only because we have test cases that test error
# handling on things that the preprocessor would otherwise break on (such as
# unclosed string and character literals.) Almost all tests are .c files.
rm -f $TEMP_FILES
find $SOURCE_FOLDER/* -name '*.c' >> $TEMP_FILES
find $SOURCE_FOLDER/* -name '*.i' >> $TEMP_FILES
FILES="$(cat $TEMP_FILES | sort)"
rm -f $TEMP_FILES

for TESTFILE in $FILES; do
    THIS_ERROR=0
    BASENAME=$(echo $TESTFILE|sed 's/\..$//')

    if [ -e $BASENAME.skip ] || ( [ $NONSTD -eq 0 ] && [ -e $BASENAME.nonstd ] ); then
        echo "Skipping $BASENAME"
        continue
    fi

    echo "Testing $BASENAME"

    # preprocess
    if echo $TESTFILE | grep -q '\.c$'; then
        $ROOT/build/test/cpp-1-omc/cpp $MACROS $BASENAME.c -o $TEMP_I
        INPUT=$TEMP_I
    else
        INPUT=$BASENAME.i
    fi

    # output file
    if [ "$OUTPUT_PATH" = "" ]; then
        OUTPUT=$TEMP_OS
    else
        OUTPUT=$(echo $OUTPUT_PATH/$BASENAME.os | sed 's@\.\.@.@g')
        mkdir -p "$(dirname "$OUTPUT")"
    fi

    # collect or generate the args
    if [ -e $BASENAME.args ]; then
        # eval echo to expand shell macros
        ARGS=$(eval echo "\"$(cat $BASENAME.args)\"")
    else
        ARGS="$INPUT -o $OUTPUT"
    fi

    # compile
    set +e
    $COMMAND $ARGS 1> $TEMP_STDOUT 2> $TEMP_STDERR
    RET=$?
    set -e

    # check compile status and assembly
    if [ $RET -eq 125 ]; then
        echo "ERROR: compiler crashed on $BASENAME; expected success or error message."
        cat $TEMP_STDERR
        THIS_ERROR=1
    elif ! [ -e $BASENAME.fail ]; then
        if [ $RET -ne 0 ]; then
            echo "ERROR: $BASENAME failed to compile; expected success."
            cat $TEMP_STDERR
            THIS_ERROR=1
        fi
    else
        if [ $RET -eq 0 ]; then
            echo "ERROR: $BASENAME compilation succeeded; expected error."
            THIS_ERROR=1
        fi
    fi

    if ! [ -e $BASENAME.fail ]; then

        # assemble, link and run
        if [ $THIS_ERROR -ne 1 ] && ! $ROOT/build/test/as-2-full/as $OUTPUT -o $TEMP_OO &> /dev/null; then
            echo "ERROR: $BASENAME failed to assemble."
            THIS_ERROR=1
        fi
        if [ $THIS_ERROR -ne 1 ] && ! $ROOT/build/test/ld-2-full/ld \
                -g $ROOT/build/test/libc-3-full/libc.oa $TEMP_OO -o $TEMP_OE &> /dev/null; then
            echo "ERROR: $BASENAME failed to link."
            THIS_ERROR=1
        fi
        if [ $THIS_ERROR -ne 1 ]; then
            set +e
            onrampvm $TEMP_OE >$TEMP_STDOUT 2>/dev/null
            RET=$?
            set -e
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

    fi

    if [ $THIS_ERROR -eq 1 ]; then
        echo "Commands:"
        echo "    make build && \\"
        if echo $TESTFILE | grep -q '\.c$'; then
            echo "    $ROOT/build/test/cpp-1-omc/cpp $MACROS $BASENAME.c -o $TEMP_I && \\"
        fi
        echo "    $COMMAND $ARGS && \\"
        echo "    $ROOT/build/test/as-2-full/as $OUTPUT -o $TEMP_OO && \\"
        echo "    $ROOT/build/test/ld-2-full/ld -g $ROOT/build/test/libc-3-full/libc.oa $TEMP_OO -o $TEMP_OE && \\"
        echo "    onrampvm $TEMP_OE"
        TOTAL_ERRORS=$(( $TOTAL_ERRORS + 1 ))
    fi

    # clean up
    rm -f $TEMP_I
    rm -f $TEMP_OS
    rm -f $TEMP_OO
    rm -f $TEMP_OE
    rm -f $TEMP_STDOUT
    rm -f $TEMP_STDERR
done

if [ $TOTAL_ERRORS -ne 0 ]; then
    echo "$TOTAL_ERRORS tests failed."
    exit 1
fi

echo "Pass."
