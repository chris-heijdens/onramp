// The MIT License (MIT)
// Copyright (c) 2023-2024 Fraser Heavy Software
// This test case is part of the Onramp compiler project.

#define FOO(x, y) y, x
#define BAR FOO
BAR(1, 2)
