// The MIT License (MIT)
// Copyright (c) 2023-2024 Fraser Heavy Software
// This test case is part of the Onramp compiler project.

int main(void) {
    // test of new declarator parser
    if ((char signed)0x123 != 0x23) {
        return 1;
    }
    if ((signed char)0x123 != 0x23) {
        return 1;
    }
    if ((char)0x123 != 0x23) {
        return 1;
    }
    return (int signed)0;
}
