; @game zelda_alttp
; @extension No Pattern KOFF

.org $3F40

; VCMD FD Handler
; Reads param from data stream: 0=off (normal KOFF), nonzero=on (skip KOFF)
; Directly patches the BEQ offset byte at $1034 in the engine code:
;   $23 = branch to $1058 (.call_loop_over, does KOFF) - normal behavior
;   $2A = branch to $105F (.time_left, skips KOFF) - no-koff enabled
NoKoffVcmd:
    MOV A, [$30+X]
    INC $30+X
    BNE NoKoffVcmdRead
    INC $31+X
NoKoffVcmdRead:
    BEQ NoKoffDisable
    MOV A, #$2A
    MOV $1034, A
    RET

; Disable no-pattern-koff (restore vanilla branch target)
NoKoffDisable:
    MOV A, #$23
    MOV $1034, A
    RET

; Init Hook (replaces mov.w $02F0+X, A at 0x0AD3)
; A = 0 on entry. Execute replaced instruction and restore
; normal KOFF behavior.
NoKoffInit:
    MOV $02F0+X, A
    PUSH A
    MOV A, #$23
    MOV $1034, A
    POP A
    RET

.patch $0F3D, "VCMD Parameter Count"
    .byte $01

.patch $0AD3, "Init Hook"
    CALL NoKoffInit
