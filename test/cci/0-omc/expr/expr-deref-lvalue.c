// The MIT License (MIT)
// Copyright (c) 2023-2024 Fraser Heavy Software
// This test case is part of the Onramp compiler project.

#include <stdio.h>

const char* word;
const char** words;

int main(void) {
    words = &word;
    *words = "Hello";
    puts(word);
    puts(*(words + 0));
}
