; The MIT License (MIT)
; Copyright (c) 2023-2024 Fraser Heavy Software
; This test case is part of the Onramp compiler project.

=test
; "this is not a string"
"; this is not a comment"

=main
    add r0 '00 '00     ; zero r0
    ldw rip '00 rsp    ; ret
