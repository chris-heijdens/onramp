// The MIT License (MIT)
// Copyright (c) 2024 Fraser Heavy Software
// This test case is part of the Onramp compiler project.

static int x; // tentative definition of x
static int x; // another tentative definition of x

int main(void) {
    return x;
}
