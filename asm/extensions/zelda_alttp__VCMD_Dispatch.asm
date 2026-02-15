; @game zelda_alttp
; @extension VCMD Dispatch

.org $3F00

; VCMD Dispatcher
; Intercepts extension VCMDs (FB/FC/FD) before they reach the
; original ExecuteCommand handler, which lacks jump table entries
; for these commands.
;
; Called from 0x0BC0 in place of "call ExecuteCommand".
; A = command byte on entry.
L3F00:
    CMP A, #$FB
    BEQ $3F10
    CMP A, #$FD
    BEQ $3F60
    CMP A, #$FC
    BEQ $3F82
    JMP $0C4A

.patch $0BC0, "VCMD Dispatcher"
    CALL L3F00
