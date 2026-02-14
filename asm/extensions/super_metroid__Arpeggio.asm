; @game super_metroid
; @extension Arpeggio

.org $5728

; Init Hook (replaces mov.w $0400+X, A at 0x1773)
; A = 0 on entry. Execute replaced instruction and clear
; arpeggio state.
ArpInit:
    MOV $0400+X, A
    MOV $0420+X, A
    MOV $0421+X, A
    RET

; VCMD FC Handler (jump table entry at 0x1B9A)
; Reads arpeggio offsets from data stream.
; High nibble = semitone offset 1, low nibble = semitone offset 2.
; 00 = off. Cycles base/+x/+y each tick.
ArpVcmd:
    MOV $0420+X, A
    MOV A, #$00
    MOV $0421+X, A
    RET

; Note Reset Hook (replaces mov.w $0100+X, A at 0x1665)
; A = 0 on entry. Resets arpeggio counter alongside the
; original vibrato step register.
ArpNoteReset:
    MOV $0100+X, A
    MOV $0421+X, A
    RET

; Tick Path Hook (replaces call GetTempPitch at 0x1D23)
; Calls GetTempPitch first, then applies arpeggio offset.
ArpTick:
    CALL $1B3B
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

; Non-tick Path Hook (replaces call GetTempPitch at 0x1DA8)
; Calls GetTempPitch, then applies current arpeggio offset
; without advancing the counter.
ArpNonTick:
    CALL $1B3B
    MOV A, $0420+X
    BEQ ArpNonTickDone
    MOV A, $0421+X
    JMP ArpApply
ArpNonTickDone:
    RET

.patch $1BBC, "VCMD Parameter Count"
    .byte $01

.patch $1B9A, "VCMD Jump Table Entry"
    .dw ArpVcmd

.patch $1773, "Init Hook"
    CALL ArpInit

.patch $1665, "Note Reset Hook"
    CALL ArpNoteReset

.patch $1D23, "Tick Path Hook"
    CALL ArpTick

.patch $1DA8, "Non-tick Path Hook"
    CALL ArpNonTick
