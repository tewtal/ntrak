; @game zelda_alttp
; @extension Arpeggio

.org $3F62

; VCMD FC Handler
; Reads arpeggio offsets from data stream.
; High nibble = semitone offset 1, low nibble = semitone offset 2.
; 00 = off. Cycles base/+x/+y each tick.
ArpVcmd:
    MOV A, [$30+X]
    INC $30+X
    BNE ArpVcmdStore
    INC $31+X
ArpVcmdStore:
    MOV $0420+X, A
    MOV A, #$00
    MOV $0421+X, A
    RET

; Note Reset Hook (replaces mov.w $0100+X, A at 0x0936)
; A = 0 on entry. Resets arpeggio counter alongside the
; original vibrato step register.
ArpNoteReset:
    MOV $0100+X, A
    MOV $0421+X, A
    RET

; Tick Path Hook (replaces call GetTempPitch at 0x107E)
; Calls GetTempPitch first, then applies arpeggio offset.
ArpTick:
    CALL $0EC3
    MOV A, $0420+X
    BEQ ArpTickDone
    MOV A, $0421+X
    INC A
    CMP A, #$03
    BCC ArpTickStore
    MOV A, #$00
ArpTickStore:
    MOV $0421+X, A
ArpApply:
    BEQ ArpTickSetFlag
    CMP A, #$02
    BEQ ArpTickOfs2
    MOV A, $0420+X
    XCN A
    BRA ArpTickMask
ArpTickOfs2:
    MOV A, $0420+X
ArpTickMask:
    AND A, #$0F
    CLRC
    ADC A, $11
    MOV $11, A
ArpTickSetFlag:
    SET1 $13.7
ArpTickDone:
    RET

; Non-tick Path Hook (replaces call GetTempPitch at 0x1103)
; Calls GetTempPitch, then applies current arpeggio offset
; without advancing the counter.
ArpNonTick:
    CALL $0EC3
    MOV A, $0420+X
    BEQ ArpNonTickDone
    MOV A, $0421+X
    JMP ArpApply
ArpNonTickDone:
    RET

; Init Hook (replaces mov.w $0280+X, A at 0x0AD6)
; A = 0 on entry. Execute replaced instruction and clear
; arpeggio state.
ArpInit:
    MOV $0280+X, A
    MOV $0420+X, A
    MOV $0421+X, A
    RET

.patch $0F3C, "VCMD Parameter Count"
    .byte $01

.patch $0936, "Arpeggio Note Reset"
    CALL ArpNoteReset

.patch $107E, "Arpeggio Tick Path"
    CALL ArpTick

.patch $1103, "Arpeggio Non-tick Path"
    CALL ArpNonTick

.patch $0AD6, "Init Hook"
    CALL ArpInit
