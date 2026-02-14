; @game zelda_alttp
; @extension Legato Mode

.org $3F10

; VCMD FB Handler
; Reads legato param from data stream: 0=off, nonzero=on
; Uses $1C as a bitfield (one bit per channel), $47 = current channel bit.
; param=0: clear channel bit (legato off)
; param!=0: set channel bit (legato on)
LegatoVcmd:
    MOV A, [$30+X]
    INC $30+X
    BNE LegatoVcmdRead
    INC $31+X
LegatoVcmdRead:
    BEQ LegatoClear
    MOV A, $1C
    OR A, $47
    MOV $1C, A
    RET

; Clear current channel legato bit
LegatoClear:
    PUSH A
    MOV A, $47
    EOR A, #$FF
    AND A, $1C
    MOV $1C, A
    POP A
    RET

; Legato KOFF Hook (replaces call WriteToDSP_Checked at 0x105C)
; On entry: A = channel bitmask ($47), Y = #$5C (KOFF register)
; If legato active for this channel, skip the KOFF write.
LegatoKoffHook:
    MOV A, $1C
    AND A, $47
    BNE LegatoKoffSkip
    MOV A, $47
    CALL $09EF
LegatoKoffSkip:
    RET

; Init Hook (replaces mov.w $03FF+X, A at 0x0AD9)
; A = 0 on entry (from init loop), clears legato bitfield.
LegatoInit:
    MOV $03FF+X, A
    MOV $1C, A
    RET

.patch $0F3B, "VCMD Parameter Count"
    .byte $01

.patch $105C, "Legato KOFF Hook"
    CALL LegatoKoffHook

.patch $0AD9, "Init Hook"
    CALL LegatoInit
