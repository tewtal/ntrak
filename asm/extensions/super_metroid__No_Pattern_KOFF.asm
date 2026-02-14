; @game super_metroid
; @extension No Pattern KOFF

.org $5780

; VCMD FD Handler (jump table entry at 0x1B9C)
; Reads param from data stream: 0=off (normal KOFF), nonzero=on (skip KOFF)
; Directly patches the BEQ offset byte at $1CC6 in the engine code:
;   $23 = branch to $1CEA (.note_end, does KOFF) - normal behavior
;   $2A = branch to $1CF1 (.time_left, skips KOFF) - no-koff enabled
NoKoffVcmd:
    BEQ NoKoffDisable
    MOV A, #$2A
    MOV $1CC6, A
    RET

; Init Hook (replaces 5-byte sequence at 0x1776)
; A = 0 on entry. Execute replaced instructions and restore
; normal KOFF behavior.
NoKoffInit:
    MOV $B1+X, A
    MOV $C1+X, A
    PUSH A
    MOV A, #$23
    MOV $1CC6, A
    POP A
    RET

; Disable no-pattern-koff (restore vanilla branch target)
NoKoffDisable:
    MOV A, #$23
    MOV $1CC6, A
    RET

.patch $1BBD, "VCMD Parameter Count"
    .byte $01

.patch $1B9C, "VCMD Jump Table Entry"
    .dw NoKoffVcmd

.patch $1776, "Init Hook"
    CALL NoKoffInit
    NOP
