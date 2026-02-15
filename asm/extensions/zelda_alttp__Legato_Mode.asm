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
    CMP A, #$00
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

; Note End Hook (replaces JMP at 0x1058)
; If legato active for this channel, skip KOFF write before jumping
; back to the rest of the note-end routine.
LegatoNoteEnd:
    MOV A, $1C
    AND A, $47
    BNE LegatoNoteEndSkip
    MOV A, $47
    MOV Y, #$5C
    CALL $09EF
LegatoNoteEndSkip:
    JMP $105F

; Rest Check Hook (replaces JMP at 0x090B)
; Handles rest/tie/note boundaries. If rest (Y >= C9), send KOFF
; then jump to rest handler. If tie (Y == C8), skip KOFF.
; Otherwise fall through to note handler.
LegatoRestCheck:
    CMP Y, #$C8
    BCC LegatoRestCheckNote
    BEQ LegatoRestCheckTie
    MOV A, $47
    MOV Y, #$5C
    CALL $09EF
LegatoRestCheckTie:
    JMP $0901
LegatoRestCheckNote:
    JMP $090F


; Init Hook (replaces mov.w $03FF+X, A at 0x0AD9)
; A = 0 on entry (from init loop), clears legato bitfield.
LegatoInit:
    MOV $03FF+X, A
    MOV $1C, A
    RET

.patch $0F3B, "VCMD Parameter Count"
    .byte $01

.patch $1058, "Note End Hook"
    JMP LegatoNoteEnd

.patch $090B, "Rest Check Hook"
    JMP LegatoRestCheck    

.patch $0AD9, "Init Hook"
    CALL LegatoInit
