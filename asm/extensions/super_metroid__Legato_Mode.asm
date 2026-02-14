; @game super_metroid
; @extension Legato Mode

.org $56E2

; Init Hook (replaces 5-byte sequence at 0x175D)
; Restores the original MOV then clears the channel's legato bit.
LegatoInit:
    MOV A, #$FF
    MOV $0301+X, A
    JMP LegatoClear

; VCMD FB Handler (jump table entry at 0x1B98)
; Reads param from data stream: 0=off, nonzero=on.
; Uses $1C as a bitfield (one bit per channel), $47 = current channel bit.
LegatoVcmd:
    BEQ LegatoClear
    MOV A, $1C
    OR A, $47
    MOV $1C, A
    RET

; Note End Hook (replaces JMP at 0x1CEA)
; If legato active for this channel, skip KOFF write before jumping
; back to the rest of the note-end routine.
LegatoNoteEnd:
    MOV A, $1C
    AND A, $47
    BNE LegatoNoteEndSkip
    MOV A, $47
    MOV Y, #$5C
    CALL $171E
LegatoNoteEndSkip:
    JMP $1CF1

; Rest Check Hook (replaces JMP at 0x163A)
; Handles rest/tie/note boundaries. If rest (Y >= C9), send KOFF
; then jump to rest handler. If tie (Y == C8), skip KOFF.
; Otherwise fall through to note handler.
LegatoRestCheck:
    CMP Y, #$C8
    BCC LegatoRestCheckNote
    BEQ LegatoRestCheckTie
    MOV A, $47
    MOV Y, #$5C
    CALL $171E
LegatoRestCheckTie:
    JMP $1630
LegatoRestCheckNote:
    JMP $163E

; Clear current channel legato bit
LegatoClear:
    PUSH A
    MOV A, $47
    EOR A, #$FF
    OR A, $1C
    MOV $1C, A
    POP A
    RET

.patch $1BBB, "VCMD Parameter Count"
    .byte $01

.patch $1B98, "VCMD Jump Table Entry"
    .dw LegatoVcmd

.patch $175D, "Init Hook"
    CALL LegatoInit
    NOP
    NOP

.patch $163A, "Rest Check Hook"
    JMP LegatoRestCheck

.patch $1CEA, "Note End Hook"
    JMP LegatoNoteEnd
