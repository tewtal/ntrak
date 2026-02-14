;;; $1500: Engine start and main loop ;;;
{
$1500: 20        clrp               ; Direct page = 0
$1501: CD CF     mov   x,#$CF       ;\
$1503: BD        mov   sp,x         ;} S = $01CF
$1504: E8 00     mov   a,#$00       ;\
$1506: 5D        mov   x,a          ;|
                                    ;|
$1507: AF        mov   (x)+,a       ;} Clear $00..DF
$1508: C8 E0     cmp   x,#$E0       ;|
$150A: D0 FB     bne   $1507        ;/
$150C: CD 00     mov   x,#$00       ;\
$150E: E8 00     mov   a,#$00       ;|
$1510: C4 D2     mov   $D2,a        ;|
$1512: 8F 05 D3  mov   $D3,#$05     ;|
                                    ;|
$1515: C7 D2     mov   ($D2+x),a    ;} Clear $0500..14FF (echo buffer)
$1517: AB D2     inc   $D2          ;|
$1519: D0 FA     bne   $1515        ;|
$151B: AB D3     inc   $D3          ;|
$151D: 78 15 D3  cmp   $D3,#$15     ;|
$1520: D0 F3     bne   $1515        ;/
$1522: E8 20     mov   a,#$20       ;\
$1524: C4 EE     mov   $EE,a        ;|
$1526: E8 00     mov   a,#$00       ;|
$1528: C4 EF     mov   $EF,a        ;} Clear $20..2E (>_<)
$152A: E8 0F     mov   a,#$0F       ;|
$152C: C5 90 03  mov   $0390,a      ;|
$152F: 3F D7 1E  call  $1ED7        ;/
$1532: E8 D0     mov   a,#$D0       ;\
$1534: C4 EE     mov   $EE,a        ;|
$1536: E8 00     mov   a,#$00       ;|
$1538: C4 EF     mov   $EF,a        ;} Clear $D0..EE (harmless off by one error)
$153A: E8 1F     mov   a,#$1F       ;|
$153C: C5 90 03  mov   $0390,a      ;|
$153F: 3F D7 1E  call  $1ED7        ;/
$1542: E8 91     mov   a,#$91       ;\
$1544: C4 EE     mov   $EE,a        ;|
$1546: E8 03     mov   a,#$03       ;|
$1548: C4 EF     mov   $EF,a        ;} Clear $0391..FF
$154A: E8 6F     mov   a,#$6F       ;|
$154C: C5 90 03  mov   $0390,a      ;|
$154F: 3F D7 1E  call  $1ED7        ;/
$1552: E8 40     mov   a,#$40       ;\
$1554: C4 EE     mov   $EE,a        ;|
$1556: E8 04     mov   a,#$04       ;|
$1558: C4 EF     mov   $EF,a        ;} Clear $0440..BE
$155A: E8 7F     mov   a,#$7F       ;|
$155C: C5 90 03  mov   $0390,a      ;|
$155F: 3F D7 1E  call  $1ED7        ;/
$1562: BC        inc   a            ;\
$1563: 3F AB 1A  call  $1AAB        ;} Set up echo with echo delay = 1
$1566: A2 48     set5  $48          ; Disable echo buffer writes
$1568: E8 60     mov   a,#$60       ;\
$156A: 8D 0C     mov   y,#$0C       ;} DSP left track master volume = 60h
$156C: 3F 26 17  call  $1726        ;/
$156F: 8D 1C     mov   y,#$1C       ;\
$1571: 3F 26 17  call  $1726        ;} DSP right track master volume = 60h
$1574: E8 6D     mov   a,#$6D       ;\
$1576: 8D 5D     mov   y,#$5D       ;} DSP sample table address = $6D00
$1578: 3F 26 17  call  $1726        ;/
$157B: E8 F0     mov   a,#$F0       ;\
$157D: C5 F1 00  mov   $00F1,a      ;} Clear $F4..F7, and stop timers (and set an unused bit)
$1580: E8 10     mov   a,#$10       ;\
$1582: C5 FA 00  mov   $00FA,a      ;} Timer 0 divider = 10h (2 ms)
$1585: C4 53     mov   $53,a        ; Music tempo = 1000h (31.3 ticks / second)
$1587: E8 01     mov   a,#$01       ;\
$1589: C5 F1 00  mov   $00F1,a      ;} Enable timer 0

; LOOP_MAIN
$158C: E4 1B     mov   a,$1B        ;\
$158E: D0 6E     bne   $15FE        ;} If [disable note processing] != 0: go to BRANCH_MUSIC_TRACK
$1590: 8D 0A     mov   y,#$0A       ; Y = Ah

; LOOP_UPDATE_DSP
$1592: AD 05     cmp   y,#$05       ;\
$1594: F0 07     beq   $159D        ;} If [Y] = 5: go to BRANCH_FLG
$1596: B0 08     bcs   $15A0        ; If [Y] > 5: go to BRANCH_DO_UPDATE_DSP
$1598: 69 4D 4C  cmp   ($4C),($4D)  ;\
$159B: D0 11     bne   $15AE        ;} If [echo timer] != [echo delay]: go to BRANCH_NEXT

; BRANCH_FLG
$159D: E3 4C 0E  bbs7  $4C,$15AE    ; If [echo timer] < 0: go to BRANCH_NEXT

; BRANCH_DO_UPDATE_DSP
;  __________ [Y]
; |    ______ [$1E52 + [Y] - 1]
; |   |    __ [$1E5C + [Y] - 1]
; |   |   |
; 1:  $2C $61 ; Echo volume left
; 2:  $3C $63 ; Echo volume right
; 3:  $0D $4E ; Echo feedback volume
; 4:  $4D $4A ; Echo enable flags
; 5:  $6C $48 ; FLG
; 6:  $4C $45 ; Key on flags
; 7:  $5C $0E ; Clear key off flags
; 8:  $3D $49 ; Noise enable flags
; 9:  $2D $4B ; Pitch modulation enable flags
; Ah: $5C $46 ; Key off flags
$15A0: F6 51 1E  mov   a,$1E51+y    ;\
$15A3: C5 F2 00  mov   $00F2,a      ;|
$15A6: F6 5B 1E  mov   a,$1E5B+y    ;|
$15A9: 5D        mov   x,a          ;} DSP register [$1E52 + [Y] - 1] = [[$1E5C + [Y] - 1]]
$15AA: E6        mov   a,(x)        ;|
$15AB: C5 F3 00  mov   $00F3,a      ;/

; BRANCH_NEXT
$15AE: FE E2     dbnz  y,$1592      ; If [--Y] != 0: go to LOOP_UPDATE_DSP
$15B0: CB 45     mov   $45,y        ; Clear key on flags
$15B2: CB 46     mov   $46,y        ; Clear key off flags
$15B4: E4 18     mov   a,$18        ;\
$15B6: 44 19     eor   a,$19        ;|
$15B8: 5C        lsr   a            ;|
$15B9: 5C        lsr   a            ;} $18 = [$18] >> 1, sign = [$18].1 ^ [$19].1 ^ 1
$15BA: ED        notc               ;} $19 = [$19] >> 1, sign = [$18].0
$15BB: 6B 18     ror   $18          ;|
$15BD: 6B 19     ror   $19          ;/

; Note: Reading timer 0 output resets it to zero. Timer 0 is clocked at 2 ms / tick and only ever read here.
; So effectively we're just making sure loop iterations are *at least* 2 ms long
$15BF: EC FD 00  mov   y,$00FD      ;\
$15C2: F0 FB     beq   $15BF        ;} Wait for timer 0 output to be non-zero
$15C4: 6D        push  y            ; Push time since last loop
$15C5: E8 20     mov   a,#$20       ;\
$15C7: CF        mul   ya           ;|
$15C8: 60        clrc               ;} Sound effects clock += (time since last loop) * 20h
$15C9: 84 43     adc   a,$43        ;|
$15CB: C4 43     mov   $43,a        ;/
$15CD: 90 24     bcc   $15F3        ; If not exceeded FFh: go to BRANCH_SOUND_FX_END
$15CF: 3F E7 1E  call  $1EE7        ; Handle CPU IO 1
$15D2: CD 01     mov   x,#$01       ;\
$15D4: 3F 21 16  call  $1621        ;} Write/read CPU IO 1
$15D7: E5 A9 04  mov   a,$04A9
$15DA: D0 08     bne   $15E4
$15DC: 3F 57 31  call  $3157        ; Handle CPU IO 2
$15DF: CD 02     mov   x,#$02       ;\
$15E1: 3F 21 16  call  $1621        ;} Write/read CPU IO 2

$15E4: 3F 06 47  call  $4706        ; Handle CPU IO 3
$15E7: CD 03     mov   x,#$03       ;\
$15E9: 3F 21 16  call  $1621        ;} Write/read CPU IO 3
$15EC: 69 4D 4C  cmp   ($4C),($4D)  ;\
$15EF: F0 02     beq   $15F3        ;} If [echo timer] != [echo delay]:
$15F1: AB 4C     inc   $4C          ; Increment echo timer

; BRANCH_SOUND_FX_END
$15F3: E4 53     mov   a,$53        ;\
$15F5: EE        pop   y            ;|
$15F6: CF        mul   ya           ;|
$15F7: 60        clrc               ;} Music track clock += (time since last loop) * ([music tempo] / 100h)
$15F8: 84 51     adc   a,$51        ;|
$15FA: C4 51     mov   $51,a        ;/
$15FC: 90 0A     bcc   $1608        ; If not exceeded FFh: go to BRANCH_MUSIC_TRACK_END

; BRANCH_MUSIC_TRACK
$15FE: 3F 93 17  call  $1793        ; Handle music track
$1601: CD 00     mov   x,#$00       ;\
$1603: 3F 21 16  call  $1621        ;} Write/read CPU IO 0
$1606: 2F 84     bra   $158C        ; Go to LOOP_MAIN

; BRANCH_MUSIC_TRACK_END
$1608: E4 04     mov   a,$04        ;\
$160A: F0 12     beq   $161E        ;} If [value for CPU IO 0] = 0: go to LOOP_MAIN
$160C: CD 00     mov   x,#$00       ; X = 0 (track index)
$160E: 8F 01 47  mov   $47,#$01     ; Current music voice bitset = 1

; LOOP_TRACK
$1611: F4 31     mov   a,$31+x      ;\
$1613: F0 03     beq   $1618        ;} If [track pointer] & FF00h != 0:
$1615: 3F 7A 1D  call  $1D7A        ; Update playing track

$1618: 3D        inc   x            ;\
$1619: 3D        inc   x            ;} X += 2 (next track)
$161A: 0B 47     asl   $47          ; Current music voice bitset <<= 1
$161C: D0 F3     bne   $1611        ; If [current music voice bitset] != 0: go to LOOP_TRACK

$161E: 5F 8C 15  jmp   $158C        ; Go to LOOP_MAIN
}


;;; $1621: Write/read CPU IO [X] ;;;
{
;; Parameter:
;;     X: CPU IO index
$1621: F4 04     mov   a,$04+x      ;\
$1623: D5 F4 00  mov   $00F4+x,a    ;} CPU IO [X] = [$04 + [X]]

$1626: F5 F4 00  mov   a,$00F4+x    ;\
$1629: 75 F4 00  cmp   a,$00F4+x    ;} Wait for CPU IO [X] to stabilise (supposedly, if the CPU and APU read and write at the same time, the result is incorrect)
$162C: D0 F8     bne   $1626        ;/
$162E: D4 00     mov   $00+x,a      ; $00 + [X] = [CPU IO [X]]
$1630: 6F        ret
}


;;; $1631: Process new note ;;;
{
;; Parameters:
;;     A: Note. Range is 80h..DFh
;;     Y: Note (same as A)
$1631: AD CA     cmp   y,#$CA       ;\
$1633: 90 05     bcc   $163A        ;} If [Y] >= CAh:
$1635: 3F F9 18  call  $18F9        ; Select instrument
$1638: 8D A4     mov   y,#$A4       ; Y = A4h (C_4)

$163A: AD C8     cmp   y,#$C8       ;\
$163C: B0 F2     bcs   $1630        ;} If [Y] >= C8h (rest or tie): return
$163E: E4 1A     mov   a,$1A        ;\
$1640: 24 47     and   a,$47        ;} If voice is sound effect enabled: return
$1642: D0 EC     bne   $1630        ;/
$1644: DD        mov   a,y          ;\
$1645: 28 7F     and   a,#$7F       ;|
$1647: 60        clrc               ;|
$1648: 84 50     adc   a,$50        ;} Track note = ([Y] & 7Fh) + [music transpose] + [track transpose]
$164A: 60        clrc               ;|
$164B: 95 F0 02  adc   a,$02F0+x    ;|
$164E: D5 61 03  mov   $0361+x,a    ;/
$1651: F5 81 03  mov   a,$0381+x    ;\
$1654: D5 60 03  mov   $0360+x,a    ;} Track subnote = [track subtranspose]
$1657: F5 B1 02  mov   a,$02B1+x    ;\
$165A: 5C        lsr   a            ;|
$165B: E8 00     mov   a,#$00       ;} Track vibrato phase = ([track dynamic vibrato length] & 1) << 7
$165D: 7C        ror   a            ;|
$165E: D5 A0 02  mov   $02A0+x,a    ;/
$1661: E8 00     mov   a,#$00       ;\
$1663: D4 B0     mov   $B0+x,a      ;} Track vibrato delay timer = 0
$1665: D5 00 01  mov   $0100+x,a    ; Track dynamic vibrato timer = 0
$1668: D5 D0 02  mov   $02D0+x,a    ; Track tremolo phase = 0
$166B: D4 C0     mov   $C0+x,a      ; Track tremolo delay timer = 0
$166D: 09 47 5E  or    ($5E),($47)  ; Music voice volume update bitset |= [current music voice bitset]
$1670: 09 47 45  or    ($45),($47)  ; Key on flags |= [current music voice bitset]
$1673: F5 80 02  mov   a,$0280+x    ;\
$1676: D4 A0     mov   $A0+x,a      ;} Track pitch slide length = [track slide length]
$1678: F0 1E     beq   $1698        ; If [track pitch slide length] = 0: go to BRANCH_NO_PITCH_SLIDE
$167A: F5 81 02  mov   a,$0281+x    ;\
$167D: D4 A1     mov   $A1+x,a      ;} Track pitch slide delay = [track slide delay]
$167F: F5 90 02  mov   a,$0290+x    ;\
$1682: D0 0A     bne   $168E        ;} If [track slide direction] = slide in:
$1684: F5 61 03  mov   a,$0361+x    ;\
$1687: 80        setc               ;|
$1688: B5 91 02  sbc   a,$0291+x    ;} Track note -= [track slide extent]
$168B: D5 61 03  mov   $0361+x,a    ;/

$168E: F5 91 02  mov   a,$0291+x    ;\
$1691: 60        clrc               ;} A = [track note] + [track slide extent]
$1692: 95 61 03  adc   a,$0361+x    ;/
$1695: 3F 23 1B  call  $1B23        ; Set track target pitch

; BRANCH_NO_PITCH_SLIDE
$1698: 3F 3B 1B  call  $1B3B        ; $11.$10 = [track note]
}


;;; $169B: Play note after psychoacoustic adjustment ;;;
{
; If [note] >= 34h (E_5):
;     Note += ([note] - 34h) / 100h
; Else if [note] < 13h (G_2):
;     Note += -1 + ([note] - 13h) / 80h

; I can only assume this adjustment is made to account for the human perception of tones a la the Bark scale

$169B: 8D 00     mov   y,#$00       ; Y = 0
$169D: E4 11     mov   a,$11        ;\
$169F: 80        setc               ;} A = [note] - 34h
$16A0: A8 34     sbc   a,#$34       ;/
$16A2: B0 09     bcs   $16AD        ; If [A] < 0:
$16A4: E4 11     mov   a,$11        ;\
$16A6: 80        setc               ;} A = [note] - 13h
$16A7: A8 13     sbc   a,#$13       ;/
$16A9: B0 06     bcs   $16B1        ; If [A] >= 0: go to play note
$16AB: DC        dec   y            ; Y = -1
$16AC: 1C        asl   a            ; A *= 2

$16AD: 7A 10     addw  ya,$10       ;\
$16AF: DA 10     movw  $10,ya       ;} Note.subnote += [Y].[A]
}


;;; $16B1: Play note ;;;
{
; Coming into this function, $11.$10 is the note to be played, range of $11 is 0..53h = C_1..B_7.
; $11 (the whole part of the note) is decomposed into a key (0..11) and an octave (0..6)

; $1E66..7F is a table of multipliers to be used for the key.
; The multiplier is adjusted for the fractional part of the note by linear interpolation of the closest values from the table.
;
; So given
;     i_0 = x_0 = [$11]
;     i_1 = x_1 = [$11] + 1
;
; the indices for the $1E66 table for the keys less than and greater than [$11].[$10] respectively,
; let
;     y_0 = [$1E66 + i_0 * 2]
;     y_1 = [$1E66 + i_1 * 2]
;
; be the pitch corresponding multipliers and let x be the fractional part [$10] / 100h, then
;     y = x * (y_1 - y_0) / (x_1 - x_0) + y_0
;
; is the interpolated pitch multiplier. Note that x_1 - x_0 = 1

; The resulting pitch multiplier corresponds to octave 6, which is halved for each octave less than 6 the input note is

$16B1: 4D        push  x            ; Save X (track index)
$16B2: E4 11     mov   a,$11        ;\
$16B4: 1C        asl   a            ;|
$16B5: 8D 00     mov   y,#$00       ;} X = [note] / 12
$16B7: CD 18     mov   x,#$18       ;} Y = [note] % 12 * 2
$16B9: 9E        div   ya,x         ;|
$16BA: 5D        mov   x,a          ;/
$16BB: F6 67 1E  mov   a,$1E67+y    ;\
$16BE: C4 15     mov   $15,a        ;|
$16C0: F6 66 1E  mov   a,$1E66+y    ;|
$16C3: C4 14     mov   $14,a        ;|
$16C5: F6 69 1E  mov   a,$1E69+y    ;|
$16C8: 2D        push  a            ;|
$16C9: F6 68 1E  mov   a,$1E68+y    ;|
$16CC: EE        pop   y            ;|
$16CD: 9A 14     subw  ya,$14       ;|
$16CF: EB 10     mov   y,$10        ;} $14 = (([$1E66 + [Y] + 2] - [$1E66 + [Y]]) * [subnote] / 100h + [$1E66 + [Y]]) * 2
$16D1: CF        mul   ya           ;|
$16D2: DD        mov   a,y          ;|
$16D3: 8D 00     mov   y,#$00       ;|
$16D5: 7A 14     addw  ya,$14       ;|
$16D7: CB 15     mov   $15,y        ;|
$16D9: 1C        asl   a            ;|
$16DA: 2B 15     rol   $15          ;|
$16DC: C4 14     mov   $14,a        ;/
$16DE: 2F 04     bra   $16E4        ;\
                                    ;|
$16E0: 4B 15     lsr   $15          ;|
$16E2: 7C        ror   a            ;|
$16E3: 3D        inc   x            ;} $14 >>= 6 - [X]
                                    ;|
$16E4: C8 06     cmp   x,#$06       ;|
$16E6: D0 F8     bne   $16E0        ;|
$16E8: C4 14     mov   $14,a        ;/
$16EA: CE        pop   x            ; Restore X (track index)
$16EB: F5 20 02  mov   a,$0220+x    ;\
$16EE: EB 15     mov   y,$15        ;|
$16F0: CF        mul   ya           ;|
$16F1: DA 16     movw  $16,ya       ;|
$16F3: F5 20 02  mov   a,$0220+x    ;|
$16F6: EB 14     mov   y,$14        ;|
$16F8: CF        mul   ya           ;|
$16F9: 6D        push  y            ;|
$16FA: F5 21 02  mov   a,$0221+x    ;|
$16FD: EB 14     mov   y,$14        ;|
$16FF: CF        mul   ya           ;} $16 = [$14] * [track instrument pitch multiplier] / 100h (16-bit x 16-bit -> 24-bit multiplication before division)
$1700: 7A 16     addw  ya,$16       ;|
$1702: DA 16     movw  $16,ya       ;|
$1704: F5 21 02  mov   a,$0221+x    ;|
$1707: EB 15     mov   y,$15        ;|
$1709: CF        mul   ya           ;|
$170A: FD        mov   y,a          ;|
$170B: AE        pop   a            ;|
$170C: 7A 16     addw  ya,$16       ;|
$170E: DA 16     movw  $16,ya       ;/
$1710: 7D        mov   a,x          ;\
$1711: 9F        xcn   a            ;|
$1712: 5C        lsr   a            ;|
$1713: 08 02     or    a,#$02       ;|
$1715: FD        mov   y,a          ;} DSP voice pitch scaler * 1000h = [$16] if voice is not sound effect enabled
$1716: E4 16     mov   a,$16        ;|
$1718: 3F 1E 17  call  $171E        ;|
$171B: FC        inc   y            ;|
$171C: E4 17     mov   a,$17        ;/
}


;;; $171E: DSP register [Y] = [A] if voice is not sound effect enabled ;;;
{
$171E: 2D        push  a            ;\
$171F: E4 47     mov   a,$47        ;|
$1721: 24 1A     and   a,$1A        ;} If voice is sound effect enabled: return
$1723: AE        pop   a            ;|
$1724: D0 06     bne   $172C        ;/
}


;;; $1726: DSP register [Y] = [A] ;;;
{
$1726: CC F2 00  mov   $00F2,y
$1729: C5 F3 00  mov   $00F3,a
$172C: 6F        ret
}


;;; $172D..1E1C: Music ;;;
{
;;; $172D: YA = next tracker command ;;;
{
; YA = [[tracker pointer]]
; Tracker pointer += 2
$172D: 8D 00     mov   y,#$00
$172F: F7 40     mov   a,($40)+y
$1731: 3A 40     incw  $40
$1733: 2D        push  a
$1734: F7 40     mov   a,($40)+y
$1736: 3A 40     incw  $40
$1738: FD        mov   y,a
$1739: AE        pop   a
$173A: 6F        ret
}


;;; $173B: Load new music data ;;;
{
$173B: 3F 8B 1E  call  $1E8B        ; Receive data from CPU
$173E: C4 08     mov   $08,a        ; Previous value read from CPU IO 0 = 0
}


;;; $1740: Load new music track ;;;
{
;; Parameters:
;;     A: Music track to load. Caller is responsible for setting previous value read from CPU IO 0
$1740: C4 04     mov   $04,a        ; Value for CPU IO 0 = [A]
$1742: 1C        asl   a            ;\
$1743: 5D        mov   x,a          ;|
$1744: F5 1F 58  mov   a,$581F+x    ;|
$1747: FD        mov   y,a          ;} Tracker pointer = [$5820 + ([A] - 1) * 2]
$1748: F5 1E 58  mov   a,$581E+x    ;|
$174B: DA 40     movw  $40,ya       ;/
$174D: 8F 02 0C  mov   $0C,#$02     ; Music track status = new music track loaded
}


;;; $1750: Key off music voices ;;;
{
$1750: E4 1A     mov   a,$1A        ;\
$1752: 48 FF     eor   a,#$FF       ;} Key off flags |= ~[enabled sound effect voices]
$1754: 0E 46 00  tset1 $0046        ;/
$1757: 6F        ret
}


;;; $1758: Music track initialisation ;;;
{
$1758: CD 0E     mov   x,#$0E       ; X = Eh (track index)
$175A: 8F 80 47  mov   $47,#$80     ; Current music voice bitset = 80h

; LOOP
$175D: E8 FF     mov   a,#$FF       ;\
$175F: D5 01 03  mov   $0301+x,a    ;} Track volume = FFxxh
$1762: E8 0A     mov   a,#$0A       ;\
$1764: 3F 52 19  call  $1952        ;} Track panning bias = A00h, no phase inversion
$1767: D5 11 02  mov   $0211+x,a    ; Track instrument index = 0
$176A: D5 81 03  mov   $0381+x,a    ; Track subtranspose = 0
$176D: D5 F0 02  mov   $02F0+x,a    ; Track transpose = 0
$1770: D5 80 02  mov   $0280+x,a    ; Track slide length = 0
$1773: D5 00 04  mov   $0400+x,a    ; Track skip new notes flag = 0
$1776: D4 B1     mov   $B1+x,a      ; Track vibrato extent = 0
$1778: D4 C1     mov   $C1+x,a      ; Track tremolo extent = 0
$177A: 1D        dec   x            ;\
$177B: 1D        dec   x            ;} X -= 2
$177C: 4B 47     lsr   $47          ; Current music voice bitset >>= 1
$177E: D0 DD     bne   $175D        ; If [current music voice bitset] != 0: go to LOOP
$1780: C4 5A     mov   $5A,a        ; Dynamic music volume timer = 0
$1782: C4 68     mov   $68,a        ; Dynamic echo volume timer = 0
$1784: C4 54     mov   $54,a        ; Dynamic music tempo timer = 0
$1786: C4 50     mov   $50,a        ; Music transpose = 0
$1788: C4 42     mov   $42,a        ; Tracker timer = 0
$178A: C4 5F     mov   $5F,a        ; Percussion instruments index = 0
$178C: 8F C0 59  mov   $59,#$C0     ; Music volume = C0xxh
$178F: 8F 20 53  mov   $53,#$20     ; Music tempo = 20xxh
$1792: 6F        ret
}


;;; $1793: Handle music track ;;;
{
$1793: EB 08     mov   y,$08        ; Y = [previous value read from CPU IO 0]
$1795: E4 00     mov   a,$00        ;\
$1797: C4 08     mov   $08,a        ;} Previous value read from CPU IO 0 = [value read from CPU IO 0]
$1799: 68 F0     cmp   a,#$F0       ;\
$179B: F0 B3     beq   $1750        ;} If [value read from CPU IO 0] = F0h: go to key off music voices
$179D: 68 F1     cmp   a,#$F1       ;\
$179F: F0 08     beq   $17A9        ;} If [value read from CPU IO 0] != F1h:
$17A1: 68 FF     cmp   a,#$FF       ;\
$17A3: F0 96     beq   $173B        ;} If [value read from CPU IO 0] = FFh: go to load new music data
$17A5: 7E 00     cmp   y,$00        ;\
$17A7: D0 97     bne   $1740        ;} If [value read from CPU IO 0] != [Y]: go to load new music track [value read from CPU IO 0]

$17A9: E4 04     mov   a,$04        ;\
$17AB: F0 E5     beq   $1792        ;} If [value for CPU IO 0] = 0: return
$17AD: E4 0C     mov   a,$0C        ;\
$17AF: F0 5A     beq   $180B        ;} If [music track status] = music track playing: go to BRANCH_MUSIC_TRACK_PLAYING
$17B1: 6E 0C A4  dbnz  $0C,$1758    ; Decrement music track status, if needed to initialise: go to music track initialisation

; LOOP_TRACKER
$17B4: 3F 2D 17  call  $172D        ; YA = next tracker command
$17B7: D0 22     bne   $17DB        ; If [Y] != 0: go to BRANCH_LOAD_NEW_TRACK_DATA
$17B9: FD        mov   y,a          ;\
$17BA: F0 84     beq   $1740        ;} If [A] = 0: go to load new music track 0
$17BC: 68 80     cmp   a,#$80       ;\
$17BE: F0 06     beq   $17C6        ;} If [A] != 80h:
$17C0: 68 81     cmp   a,#$81       ;\
$17C2: D0 06     bne   $17CA        ;} If [A] != 81h: go to BRANCH_PROCESS_TRACKER_TIMER
$17C4: E8 00     mov   a,#$00       ; A = 0

$17C6: C4 1B     mov   $1B,a        ; Disable note processing = [A]
$17C8: 2F EA     bra   $17B4        ; Go to LOOP_TRACKER

; BRANCH_PROCESS_TRACKER_TIMER
$17CA: 8B 42     dec   $42          ; Decrement tracker timer
$17CC: 10 02     bpl   $17D0        ; If [tracker timer] < 0:
$17CE: C4 42     mov   $42,a        ; Tracker timer = [A]

$17D0: 3F 2D 17  call  $172D        ; YA = next tracker command
$17D3: F8 42     mov   x,$42        ;\
$17D5: F0 DD     beq   $17B4        ;} If [tracker timer] = 0: go to LOOP_TRACKER
$17D7: DA 40     movw  $40,ya       ; Tracker pointer = [YA]
$17D9: 2F D9     bra   $17B4        ; Go to LOOP_TRACKER

; BRANCH_LOAD_NEW_TRACK_DATA
$17DB: DA 16     movw  $16,ya       ;\
$17DD: 8D 0F     mov   y,#$0F       ;|
                                    ;|
$17DF: F7 16     mov   a,($16)+y    ;} Track pointers = [[YA]..[YA]+Fh]
$17E1: D6 30 00  mov   $0030+y,a    ;|
$17E4: DC        dec   y            ;|
$17E5: 10 F8     bpl   $17DF        ;/
$17E7: CD 00     mov   x,#$00       ; X = 0 (track index)
$17E9: 8F 01 47  mov   $47,#$01     ; Current music voice bitset = 1

; LOOP_LOAD_NEW_TRACK_DATA
$17EC: F4 31     mov   a,$31+x      ;\
$17EE: F0 0A     beq   $17FA        ;} If [track pointer] & FF00h != 0:
$17F0: F5 11 02  mov   a,$0211+x    ;\
$17F3: D0 05     bne   $17FA        ;} If [track instrument index] = 0:
$17F5: E8 00     mov   a,#$00       ; A = 0
$17F7: 3F F9 18  call  $18F9        ; Select instrument

$17FA: E8 00     mov   a,#$00       ;\
$17FC: D4 80     mov   $80+x,a      ;} Track repeated subsection counter = 0
$17FE: D4 90     mov   $90+x,a      ; Track dynamic volume timer = 0
$1800: D4 91     mov   $91+x,a      ; Track dynamic panning timer = 0
$1802: BC        inc   a            ;\
$1803: D4 70     mov   $70+x,a      ;} Track timer = 1
$1805: 3D        inc   x            ;\
$1806: 3D        inc   x            ;} X += 2 (next track)
$1807: 0B 47     asl   $47          ; Current music voice bitset <<= 1
$1809: D0 E1     bne   $17EC        ; If [current music voice bitset] != 0: go to LOOP_LOAD_NEW_TRACK_DATA

; BRANCH_MUSIC_TRACK_PLAYING
$180B: CD 00     mov   x,#$00       ; X = 0 (track index)
$180D: D8 5E     mov   $5E,x        ; Music voice volume update bitset = 0
$180F: 8F 01 47  mov   $47,#$01     ; Current music voice bitset = 1

; LOOP_TRACK
$1812: D8 44     mov   $44,x        ; Track index = [X]
$1814: F4 31     mov   a,$31+x      ;\
$1816: F0 72     beq   $188A        ;} If [track pointer] & FF00h = 0: go to BRANCH_NO_TRACK_COMMANDS
$1818: 9B 70     dec   $70+x        ; Decrement note track timer
$181A: D0 64     bne   $1880        ; If [track note timer] != 0: go to BRANCH_NOTE_IS_PLAYING

; LOOP_TRACK_COMMAND
$181C: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$181F: D0 17     bne   $1838        ; If [A] = 0: (end of section)
$1821: F4 80     mov   a,$80+x      ;\
$1823: F0 8F     beq   $17B4        ;} If [track repeated subsection counter] = 0: go to LOOP_TRACKER
$1825: 3F 40 1A  call  $1A40        ; Track pointer = [track repeated subsection address]
$1828: 9B 80     dec   $80+x        ; Decrement track repeated subsection counter
$182A: D0 F0     bne   $181C        ; If [track repeated subsection counter] != 0: go to LOOP_TRACK_COMMAND
$182C: F5 30 02  mov   a,$0230+x    ;\
$182F: D4 30     mov   $30+x,a      ;|
$1831: F5 31 02  mov   a,$0231+x    ;} Track pointer = [track repeated subsection return address]
$1834: D4 31     mov   $31+x,a      ;/
$1836: 2F E4     bra   $181C        ; Go to LOOP_TRACK_COMMAND

$1838: 30 20     bmi   $185A        ; If [A] < 80h:
$183A: D5 00 02  mov   $0200+x,a    ; Track note length = [A]
$183D: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1840: 30 18     bmi   $185A        ; If [A] < 80h:
$1842: 2D        push  a            ;\
$1843: 9F        xcn   a            ;|
$1844: 28 07     and   a,#$07       ;|
$1846: FD        mov   y,a          ;} Track note ring length multiplier * 100h = [$5800 + ([A] >> 4 & 7)]
$1847: F6 00 58  mov   a,$5800+y    ;|
$184A: D5 01 02  mov   $0201+x,a    ;|
$184D: AE        pop   a            ;/
$184E: 28 0F     and   a,#$0F       ;\
$1850: FD        mov   y,a          ;|
$1851: F6 08 58  mov   a,$5808+y    ;} Track note volume multiplier * 100h = [$5808 + ([A] & Fh)]
$1854: D5 10 02  mov   $0210+x,a    ;/
$1857: 3F EF 18  call  $18EF        ; Y = A = next track data byte

$185A: 68 E0     cmp   a,#$E0       ;\
$185C: 90 05     bcc   $1863        ;} If [A] >= E0h:
$185E: 3F DD 18  call  $18DD        ; Handle track command
$1861: 2F B9     bra   $181C        ; Go to LOOP_TRACK_COMMAND

; 80h <= [A] < E0h
$1863: F5 00 04  mov   a,$0400+x    ;\
$1866: 04 1B     or    a,$1B        ;} If [track skip new notes flag] | [disable note processing] = 0:
$1868: D0 04     bne   $186E        ;/
$186A: DD        mov   a,y          ; A = [Y]
$186B: 3F 31 16  call  $1631        ; Process new note

$186E: F5 00 02  mov   a,$0200+x    ;\
$1871: D4 70     mov   $70+x,a      ;} Track note timer = [track note length]
$1873: FD        mov   y,a          ;\
$1874: F5 01 02  mov   a,$0201+x    ;|
$1877: CF        mul   ya           ;|
$1878: DD        mov   a,y          ;|
$1879: D0 01     bne   $187C        ;} Track note ring timer = max(1, [track note timer] * [track note ring length multiplier])
$187B: BC        inc   a            ;|
                                    ;|
$187C: D4 71     mov   $71+x,a      ;/
$187E: 2F 07     bra   $1887        ; Go to BRANCH_NEXT

; BRANCH_NOTE_IS_PLAYING
$1880: E4 1B     mov   a,$1B        ;\
$1882: D0 06     bne   $188A        ;} If [disable note processing] != 0: go to BRANCH_NO_TRACK_COMMANDS
$1884: 3F 88 1C  call  $1C88        ; Handle current note

; BRANCH_NEXT
$1887: 3F 03 1B  call  $1B03        ; Execute track command F9h if requested

; BRANCH_NO_TRACK_COMMANDS
$188A: 3D        inc   x            ;\
$188B: 3D        inc   x            ;} X += 2 (next track)
$188C: 0B 47     asl   $47          ; Current music voice bitset <<= 1
$188E: D0 82     bne   $1812        ; If [current music voice bitset] != 0: go to LOOP_TRACK
$1890: E4 54     mov   a,$54        ;\
$1892: F0 0B     beq   $189F        ;} If [dynamic music tempo timer] = 0: go to BRANCH_DYNAMIC_TEMPO_END
$1894: BA 56     movw  ya,$56       ;\
$1896: 7A 52     addw  ya,$52       ;} YA = [music tempo] + [music tempo delta]
$1898: 6E 54 02  dbnz  $54,$189D    ; Decrement dynamic music tempo timer, if now zero:
$189B: BA 54     movw  ya,$54       ; YA = 0

$189D: DA 52     movw  $52,ya       ; Music tempo = [YA]

; BRANCH_DYNAMIC_TEMPO_END
$189F: E4 68     mov   a,$68        ;\
$18A1: F0 15     beq   $18B8        ;} If [dynamic echo volume timer] = 0: go to BRANCH_DYNAMIC_ECHO_END
$18A3: BA 64     movw  ya,$64       ;\
$18A5: 7A 60     addw  ya,$60       ;} Echo volume left += [echo volume left delta]
$18A7: DA 60     movw  $60,ya       ;/
$18A9: BA 66     movw  ya,$66       ;\
$18AB: 7A 62     addw  ya,$62       ;} YA = [echo volume right] + [echo volume right delta]
$18AD: 6E 68 06  dbnz  $68,$18B6    ; Decrement dynamic echo volume timer, if now zero:
$18B0: BA 68     movw  ya,$68       ;\
$18B2: DA 60     movw  $60,ya       ;} Echo volume left = 0
$18B4: EB 6A     mov   y,$6A        ; YA = [target echo volume right] * 100h

$18B6: DA 62     movw  $62,ya       ; Echo volume right = [YA]

; BRANCH_DYNAMIC_ECHO_END
$18B8: E4 5A     mov   a,$5A        ;\
$18BA: F0 0E     beq   $18CA        ;} If [dynamic music volume timer] = 0: go to BRANCH_DYNAMIC_VOLUME_END
$18BC: BA 5C     movw  ya,$5C       ;\
$18BE: 7A 58     addw  ya,$58       ;} YA = [music volume multiplier] * 10000h + [music volume multiplier delta] * 10000h
$18C0: 6E 5A 02  dbnz  $5A,$18C5    ; Decrement dynamic music volume timer, if now zero:
$18C3: BA 5A     movw  ya,$5A       ; YA = 0

$18C5: DA 58     movw  $58,ya       ; Music volume multiplier * 10000h = [YA]
$18C7: 8F FF 5E  mov   $5E,#$FF     ; Music voice volume update bitset = FFh

; BRANCH_DYNAMIC_VOLUME_END
$18CA: CD 00     mov   x,#$00       ; X = 0 (track index)
$18CC: 8F 01 47  mov   $47,#$01     ; Current music voice bitset = 1

; LOOP_TRACK_VOLUME
$18CF: F4 31     mov   a,$31+x      ;\
$18D1: F0 03     beq   $18D6        ;} If [track pointer] & FF00h != 0:
$18D3: 3F BF 1B  call  $1BBF        ; Handle track volume

$18D6: 3D        inc   x            ;\
$18D7: 3D        inc   x            ;} X += 2 (next track)
$18D8: 0B 47     asl   $47          ; Current music voice bitset <<= 1
$18DA: D0 F3     bne   $18CF        ; If [current music voice bitset] != 0: go to LOOP_TRACK_VOLUME
$18DC: 6F        ret
}


;;; $18DD: Handle track command ;;;
{
$18DD: 1C        asl   a            ;\
$18DE: FD        mov   y,a          ;|
$18DF: F6 A3 1A  mov   a,$1AA3+y    ;|
$18E2: 2D        push  a            ;} Push [$1B62 + ([A] - E0h) * 2] (this address will be jumped to on return)
$18E3: F6 A2 1A  mov   a,$1AA2+y    ;|
$18E6: 2D        push  a            ;/
$18E7: DD        mov   a,y          ;\
$18E8: 5C        lsr   a            ;|
$18E9: FD        mov   y,a          ;} A = [$1BA0 + [A] - E0h] (number of command parameter bytes)
$18EA: F6 40 1B  mov   a,$1B40+y    ;/
$18ED: F0 08     beq   $18F7        ; If [A] = 0: Y = 0 and return
}


;;; $18EF: Y = A = next track data byte ;;;
{
$18EF: E7 30     mov   a,($30+x)
}


;;; $18F1: Increment track pointer ;;;
{
$18F1: BB 30     inc   $30+x
$18F3: D0 02     bne   $18F7
$18F5: BB 31     inc   $31+x

$18F7: FD        mov   y,a
$18F8: 6F        ret
}


;;; $18F9: Track command E0h - select instrument ;;;
{
$18F9: D5 11 02  mov   $0211+x,a    ; Track instrument index = [A]
}


;;; $18FC: Set voice's instrument settings ;;;
{
;; Parameters:
;;     A: Instrument index
;;     X: Track index

; If [A] >= 80h:
;     A = [A] - CAh + [percussion instruments index]

; $14 = $6C00 + [A] * 6

; If voice is sound effect enabled:
;     Return

; If [[$14]] & 80h: (always false in vanilla)
;     Enable voice noise with frequency [[$14]], voice source number = 0
; Else:
;     Disable voice noise, voice source number = [[$14]]
;
; Voice ADSR settings = [[$14] + 1]
; Voice gain settings = [[$14] + 3]
; Track instrument pitch multiplier = [[$14] + 4] * 100h + [[$14] + 5]

$18FC: FD        mov   y,a          ;\
$18FD: 10 06     bpl   $1905        ;} If [A] >= 80h:
$18FF: 80        setc               ;\
$1900: A8 CA     sbc   a,#$CA       ;|
$1902: 60        clrc               ;} A = [A] - CAh + [percussion instruments index]
$1903: 84 5F     adc   a,$5F        ;/

$1905: 8D 06     mov   y,#$06       ;\
$1907: CF        mul   ya           ;|
$1908: DA 14     movw  $14,ya       ;|
$190A: 60        clrc               ;} $14 = $6C00 + [A] * 6
$190B: 98 00 14  adc   $14,#$00     ;|
$190E: 98 6C 15  adc   $15,#$6C     ;/
$1911: E4 1A     mov   a,$1A        ;\
$1913: 24 47     and   a,$47        ;} If current voice is sound effect enabled: return
$1915: D0 3A     bne   $1951        ;/
$1917: 4D        push  x            ; Save X
$1918: 7D        mov   a,x          ;\
$1919: 9F        xcn   a            ;|
$191A: 5C        lsr   a            ;} X = [X] / 2 * 10h | 4 (voice's DSP sample tale index)
$191B: 08 04     or    a,#$04       ;|
$191D: 5D        mov   x,a          ;/
$191E: 8D 00     mov   y,#$00       ; Y = 0
$1920: F7 14     mov   a,($14)+y    ;\
$1922: 10 0E     bpl   $1932        ;} If [[$14]] & 80h != 0:
$1924: 28 1F     and   a,#$1F       ;\
$1926: 38 20 48  and   $48,#$20     ;} Noise frequency = [[$14]] & 1Fh
$1929: 0E 48 00  tset1 $0048        ;/
$192C: 09 47 49  or    ($49),($47)  ; Enable noise on current voice
$192F: DD        mov   a,y          ; A = 0
$1930: 2F 07     bra   $1939        ; Go to BRANCH_DSP

$1932: E4 47     mov   a,$47        ;\
$1934: 4E 49 00  tclr1 $0049        ;} Disable noise on current voice

; LOOP_DSP
$1937: F7 14     mov   a,($14)+y    ; A = [[$14] + [Y]]

; BRANCH_DSP
$1939: C9 F2 00  mov   $00F2,x      ;\
$193C: C5 F3 00  mov   $00F3,a      ;} DSP register [X] = [A]
$193F: 3D        inc   x            ; Increment X
$1940: FC        inc   y            ; Increment Y
$1941: AD 04     cmp   y,#$04       ;\
$1943: D0 F2     bne   $1937        ;} If [Y] != 4: go to LOOP_DSP
$1945: CE        pop   x            ; Restore X
$1946: F7 14     mov   a,($14)+y    ;\
$1948: D5 21 02  mov   $0221+x,a    ;|
$194B: FC        inc   y            ;} Track instrument pitch multiplier = [[$14] + 4] * 100h + [[$14] + 5]
$194C: F7 14     mov   a,($14)+y    ;|
$194E: D5 20 02  mov   $0220+x,a    ;/

$1951: 6F        ret
}


;;; $1952: Track command E1h - static panning ;;;
{
$1952: D5 51 03  mov   $0351+x,a    ; Track phase inversion options = [A]
$1955: 28 1F     and   a,#$1F       ;\
$1957: D5 31 03  mov   $0331+x,a    ;|
$195A: E8 00     mov   a,#$00       ;} Track panning bias = ([A] & 1Fh) * 100h
$195C: D5 30 03  mov   $0330+x,a    ;/
$195F: 6F        ret
}


;;; $1960: Track command E2h - dynamic panning ;;;
{
$1960: D4 91     mov   $91+x,a      ; Track dynamic panning timer = [A]
$1962: 2D        push  a
$1963: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1966: D5 50 03  mov   $0350+x,a    ; Track target panning bias = [A]
$1969: 80        setc               ;\
$196A: B5 31 03  sbc   a,$0331+x    ;|
$196D: CE        pop   x            ;|
$196E: 3F 46 1B  call  $1B46        ;} Track panning bias delta = ([track target panning bias] - [track panning bias] / 100h) * 100h / [track dynamic panning timer]
$1971: D5 40 03  mov   $0340+x,a    ;|
$1974: DD        mov   a,y          ;|
$1975: D5 41 03  mov   $0341+x,a    ;/
$1978: 6F        ret
}


;;; $1979: Track command E3h - static vibrato ;;;
{
$1979: D5 B0 02  mov   $02B0+x,a    ; Track vibrato delay = [A]
$197C: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$197F: D5 A1 02  mov   $02A1+x,a    ; Track vibrato rate = [A]
$1982: 3F EF 18  call  $18EF        ; Y = A = next track data byte
}


;;; $1985: Track command E4h - end vibrato ;;;
{
; Recall that parameterless track commands are called with Y = A = 0
$1985: D4 B1     mov   $B1+x,a      ; Track vibrato extent = [A]
$1987: D5 C1 02  mov   $02C1+x,a    ; Track static vibrato extent = [A]
$198A: E8 00     mov   a,#$00       ;\
$198C: D5 B1 02  mov   $02B1+x,a    ;} Track dynamic vibrato length = 0
$198F: 6F        ret
}


;;; $1990: Track command F0h - dynamic vibrato ;;;
{
; Not used by any Super Metroid tracks
$1990: D5 B1 02  mov   $02B1+x,a    ; Track dynamic vibrato length = [A]
$1993: 2D        push  a            ;\
$1994: 8D 00     mov   y,#$00       ;|
$1996: F4 B1     mov   a,$B1+x      ;|
$1998: CE        pop   x            ;} Track vibrato extent delta = [track vibrato extent] / [track dynamic vibrato length]
$1999: 9E        div   ya,x         ;|
$199A: F8 44     mov   x,$44        ;|
$199C: D5 C0 02  mov   $02C0+x,a    ;/
$199F: 6F        ret
}


;;; $19A0: Track command E5h - static music volume ;;;
{
$19A0: E8 00     mov   a,#$00       ;\
$19A2: DA 58     movw  $58,ya       ;} Music volume multiplier * 100h = [Y]
$19A4: 6F        ret
}


;;; $19A5: Track command E6h - dynamic music volume ;;;
{
$19A5: C4 5A     mov   $5A,a        ; Dynamic music volume timer = [A]
$19A7: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$19AA: C4 5B     mov   $5B,a        ; Target music volume multiplier * 100h = [A]
$19AC: 80        setc               ;\
$19AD: A4 59     sbc   a,$59        ;|
$19AF: F8 5A     mov   x,$5A        ;} Music volume multiplier delta * 10000h = ([target music volume multiplier] * 100h - [music volume multiplier] * 100h) * 100h / [dynamic music volume timer]
$19B1: 3F 46 1B  call  $1B46        ;|
$19B4: DA 5C     movw  $5C,ya       ;/
$19B6: 6F        ret
}


;;; $19B7: Track command E7h - static music tempo ;;;
{
$19B7: E8 00     mov   a,#$00       ;\
$19B9: DA 52     movw  $52,ya       ;} Music tempo = [Y] * 100h
$19BB: 6F        ret
}


;;; $19BC: Track command E8h - dynamic music tempo ;;;
{
; Music tempo appears to be set to zero after the timer has expired (see $18B8)
$19BC: C4 54     mov   $54,a        ; Dynamic music tempo timer = [A]
$19BE: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$19C1: C4 55     mov   $55,a        ; Target music tempo = [A]
$19C3: 80        setc               ;\
$19C4: A4 53     sbc   a,$53        ;|
$19C6: F8 54     mov   x,$54        ;} Music tempo delta = ([target music tempo] - [music tempo] / 100h) * 100h / [dynamic music tempo timer]
$19C8: 3F 46 1B  call  $1B46        ;|
$19CB: DA 56     movw  $56,ya       ;/
$19CD: 6F        ret
}


;;; $19CE: Track command E9h - music transpose ;;;
{
$19CE: C4 50     mov   $50,a        ; Music tranpose = [A]
$19D0: 6F        ret
}


;;; $19D1: Track command EAh - transpose ;;;
{
$19D1: D5 F0 02  mov   $02F0+x,a    ; Track transpose = [A]
$19D4: 6F        ret
}


;;; $19D5: Track command EBh - tremolo ;;;
{
$19D5: D5 E0 02  mov   $02E0+x,a    ; Track tremolo delay = [A]
$19D8: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$19DB: D5 D1 02  mov   $02D1+x,a    ; Track tremolo rate = [A]
$19DE: 3F EF 18  call  $18EF        ; Y = A = next track data byte
}


;;; $19E1: Track command ECh - end tremolo ;;;
{
; Recall that parameterless track commands are called with Y = A = 0
$19E1: D4 C1     mov   $C1+x,a      ; Track tremolo extent = [A]
$19E3: 6F        ret
}


;;; $19E4: Track command F1h - slide out ;;;
{
$19E4: E8 01     mov   a,#$01
$19E6: 2F 02     bra   $19EA
}


;;; $19E8: Track command F2h - slide in ;;;
{
; Not used by any Super Metroid tracks
$19E8: E8 00     mov   a,#$00
}


;;; $19EA: Set track slide ;;;
{
$19EA: D5 90 02  mov   $0290+x,a    ; Track slide direction = [A]
$19ED: DD        mov   a,y          ;\
$19EE: D5 81 02  mov   $0281+x,a    ;} Track slide delay = [Y]
$19F1: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$19F4: D5 80 02  mov   $0280+x,a    ; Track slide length = [A]
$19F7: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$19FA: D5 91 02  mov   $0291+x,a    ; Track slide extent = [A]
$19FD: 6F        ret
}


;;; $19FE: Track command F3h - end slide ;;;
{
; Recall that parameterless track commands are called with Y = A = 0
$19FE: D5 80 02  mov   $0280+x,a    ; Track slide length = 0
$1A01: 6F        ret
}


;;; $1A02: Track command EDh - static volume ;;;
{
$1A02: D5 01 03  mov   $0301+x,a    ;\
$1A05: E8 00     mov   a,#$00       ;} Track volume multiplier * 100h = [A]
$1A07: D5 00 03  mov   $0300+x,a    ;/
$1A0A: 6F        ret
}


;;; $1A0B: Track command EEh - dynamic volume ;;;
{
$1A0B: D4 90     mov   $90+x,a      ; Track dynamic volume timer = [A]
$1A0D: 2D        push  a
$1A0E: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A11: D5 20 03  mov   $0320+x,a    ; Track target volume multiplier * 100h = [A]
$1A14: 80        setc               ;\
$1A15: B5 01 03  sbc   a,$0301+x    ;|
$1A18: CE        pop   x            ;|
$1A19: 3F 46 1B  call  $1B46        ;} Track volume multiplier delta * 10000h = ([track target volume multiplier] * 100h - [track volume multiplier] * 100h) * 100h / [track dynamic volume timer]
$1A1C: D5 10 03  mov   $0310+x,a    ;|
$1A1F: DD        mov   a,y          ;|
$1A20: D5 11 03  mov   $0311+x,a    ;/
$1A23: 6F        ret
}


;;; $1A24: Track command F4h - subtranspose ;;;
{
$1A24: D5 81 03  mov   $0381+x,a    ; Track subtranspose = [A]
$1A27: 6F        ret
}


;;; $1A28: Track command EFh - repeat subsection ;;;
{
$1A28: D5 40 02  mov   $0240+x,a    ; Track repeated subsection address = [A]
$1A2B: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A2E: D5 41 02  mov   $0241+x,a    ; Track repeated subsection address += [A] * 100h
$1A31: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A34: D4 80     mov   $80+x,a      ; Track repeated subsection counter = [A]
$1A36: F4 30     mov   a,$30+x      ;\
$1A38: D5 30 02  mov   $0230+x,a    ;|
$1A3B: F4 31     mov   a,$31+x      ;} Track repeated subsection return address = [track pointer]
$1A3D: D5 31 02  mov   $0231+x,a    ;/
}


;;; $1A40: Track pointer = [track repeated subsection address] ;;;
{
$1A40: F5 40 02  mov   a,$0240+x
$1A43: D4 30     mov   $30+x,a
$1A45: F5 41 02  mov   a,$0241+x
$1A48: D4 31     mov   $31+x,a
$1A4A: 6F        ret
}


;;; $1A4B: Track command F5h - static echo ;;;
{
$1A4B: C4 4A     mov   $4A,a        ; Echo enable flags = [A]
$1A4D: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A50: E8 00     mov   a,#$00       ;\
$1A52: DA 60     movw  $60,ya       ;} Echo volume left = [Y]
$1A54: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A57: E8 00     mov   a,#$00       ;\
$1A59: DA 62     movw  $62,ya       ;} Echo volume right = [Y]
$1A5B: B2 48     clr5  $48          ; Enable echo buffer writes
$1A5D: 6F        ret
}


;;; $1A5E: Track command F8h - dynamic echo volume ;;;
{
; Not used by any Super Metroid tracks
$1A5E: C4 68     mov   $68,a        ; Dynamic echo volume timer = [A]
$1A60: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A63: C4 69     mov   $69,a        ; Target echo volume left = [A]
$1A65: 80        setc               ;\
$1A66: A4 61     sbc   a,$61        ;|
$1A68: F8 68     mov   x,$68        ;} Echo volume left delta = ([target echo volume left] - [echo volume left] * 100h) * 100h / [dynamic echo volume timer]
$1A6A: 3F 46 1B  call  $1B46        ;|
$1A6D: DA 64     movw  $64,ya       ;/
$1A6F: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A72: C4 6A     mov   $6A,a        ; Target echo volume right = [A]
$1A74: 80        setc               ;\
$1A75: A4 63     sbc   a,$63        ;|
$1A77: F8 68     mov   x,$68        ;} Echo volume right delta = ([target echo volume right] - [echo volume right] * 100h) * 100h / [dynamic echo volume timer]
$1A79: 3F 46 1B  call  $1B46        ;|
$1A7C: DA 66     movw  $66,ya       ;/
$1A7E: 6F        ret
}


;;; $1A7F: Track command F6h - end echo ;;;
{
; Not used by any Super Metroid tracks
; Recall that parameterless track commands are called with Y = A = 0
$1A7F: DA 60     movw  $60,ya       ; Echo volume left = 0
$1A81: DA 62     movw  $62,ya       ; Echo volume right = 0
$1A83: A2 48     set5  $48          ; Disable echo buffer writes
$1A85: 6F        ret
}


;;; $1A86: Track command F7h - echo parameters ;;;
{
$1A86: 3F AB 1A  call  $1AAB        ; Set up echo with echo delay = [A]
$1A89: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A8C: C4 4E     mov   $4E,a        ; Echo feedback volume = [A]
$1A8E: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1A91: C5 B1 04  mov   $04B1,a      ; Echo FIR filter index = [A]
$1A94: 8D 08     mov   y,#$08       ;\
$1A96: CF        mul   ya           ;} X = [echo FIR filter index] * 8
$1A97: 5D        mov   x,a          ;/
$1A98: 8D 0F     mov   y,#$0F       ; Y = DSP echo FIR filter coefficient 0

; LOOP
$1A9A: F5 32 1E  mov   a,$1E32+x    ;\
$1A9D: 3F 26 17  call  $1726        ;} DSP register [Y] = [$1E32 + [X]]
$1AA0: 3D        inc   x            ; Increment X
$1AA1: DD        mov   a,y          ;\
$1AA2: 60        clrc               ;|
$1AA3: 88 10     adc   a,#$10       ;} Y += 10h (next echo FIR filter coefficient)
$1AA5: FD        mov   y,a          ;/
$1AA6: 10 F2     bpl   $1A9A        ; If [Y] >= 0: go to LOOP
$1AA8: F8 44     mov   x,$44        ; X = [track index]
$1AAA: 6F        ret
}


;;; $1AAB: Set up echo with echo delay = [A] ;;;
{
$1AAB: C4 4D     mov   $4D,a        ; Echo delay = [A]
$1AAD: 8D 7D     mov   y,#$7D       ;\
$1AAF: CC F2 00  mov   $00F2,y      ;|
$1AB2: E5 F3 00  mov   a,$00F3      ;} If [DSP echo delay] = [echo delay]: go to BRANCH_NO_CHANGE
$1AB5: 64 4D     cmp   a,$4D        ;|
$1AB7: F0 2B     beq   $1AE4        ;/
$1AB9: 28 0F     and   a,#$0F       ;\
$1ABB: 48 FF     eor   a,#$FF       ;|
$1ABD: F3 4C 03  bbc7  $4C,$1AC3    ;|
$1AC0: 60        clrc               ;} Echo timer = min(0, [echo timer]) - 1 - [DSP echo delay]
$1AC1: 84 4C     adc   a,$4C        ;|
                                    ;|
$1AC3: C4 4C     mov   $4C,a        ;/
$1AC5: 8D 04     mov   y,#$04       ; Y = 4

; LOOP
;  ______ [Y]
; |    __ [$1E52 + [Y] - 1]
; |   |
; 1:  $2C ; Echo volume left
; 2:  $3C ; Echo volume right
; 3:  $0D ; Echo feedback volume
; 4:  $4D ; Echo enable flags
$1AC7: F6 51 1E  mov   a,$1E51+y    ;\
$1ACA: C5 F2 00  mov   $00F2,a      ;|
$1ACD: E8 00     mov   a,#$00       ;} DSP register [$1E52 + [Y] - 1] = 0
$1ACF: C5 F3 00  mov   $00F3,a      ;/
$1AD2: FE F3     dbnz  y,$1AC7      ; If [--Y] != 0: go to LOOP
$1AD4: E4 48     mov   a,$48        ;\
$1AD6: 08 20     or    a,#$20       ;|
$1AD8: 8D 6C     mov   y,#$6C       ;} DSP FLG = [FLG] | 20h (disable echo buffer writes)
$1ADA: 3F 26 17  call  $1726        ;/
$1ADD: E4 4D     mov   a,$4D        ;\
$1ADF: 8D 7D     mov   y,#$7D       ;} DSP echo delay = [echo delay]
$1AE1: 3F 26 17  call  $1726        ;/

; BRANCH_NO_CHANGE
$1AE4: 1C        asl   a            ;\
$1AE5: 1C        asl   a            ;|
$1AE6: 1C        asl   a            ;|
$1AE7: 48 FF     eor   a,#$FF       ;} DSP echo buffer address = $1500 - [echo delay] * 800h
$1AE9: 80        setc               ;} Return
$1AEA: 88 15     adc   a,#$15       ;|
$1AEC: 8D 6D     mov   y,#$6D       ;|
$1AEE: 5F 26 17  jmp   $1726        ;/
}


;;; $1AF1: Track command FAh - set percussion instruments index ;;;
{
$1AF1: C4 5F     mov   $5F,a        ; Percussion instruments index = [A]
$1AF3: 6F        ret
}


;;; $1AF4: Track command FBh - skip byte ;;;
{
; Not used by any Super Metroid tracks
$1AF4: 3F F1 18  call  $18F1        ; Increment track pointer
$1AF7: 6F        ret
}


;;; $1AF8: Track command FCh - skip all new notes ;;;
{
; Not used by any Super Metroid tracks
; Recall that parameterless track commands are called with Y = A = 0
$1AF8: BC        inc   a            ;\
$1AF9: D5 00 04  mov   $0400+x,a    ;} Track skip new notes flag = 1
$1AFC: 6F        ret
}


;;; $1AFD: Track command FDh - stop sound effects and disable music note processing ;;;
{
; Not used by any Super Metroid tracks
; Recall that parameterless track commands are called with Y = A = 0
$1AFD: BC        inc   a            ; Disable note processing = 1
}


;;; $1AFE: Track command FEh - resume sound effects and enable music note processing ;;;
{
; Not used by any Super Metroid tracks
; Recall that parameterless track commands are called with Y = A = 0
$1AFE: C4 1B     mov   $1B,a        ; Disable note processing = 0
$1B00: 5F 50 17  jmp   $1750        ; Key off music voices
}


;;; $1B03: Execute track command F9h if requested ;;;
{
$1B03: F4 A0     mov   a,$A0+x      ;\
$1B05: D0 33     bne   $1B3A        ;} If [track pitch slide length] != 0: return
$1B07: E7 30     mov   a,($30+x)    ;\
$1B09: 68 F9     cmp   a,#$F9       ;} If [[track pointer]] != F9h: return
$1B0B: D0 2D     bne   $1B3A        ;/
$1B0D: 3F F1 18  call  $18F1        ; Increment track pointer
$1B10: 3F EF 18  call  $18EF        ; Y = A = next track data byte
}


;;; $1B13: Track command F9h - pitch slide ;;;
{
$1B13: D4 A1     mov   $A1+x,a      ; Track pitch slide delay = [A]
$1B15: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1B18: D4 A0     mov   $A0+x,a      ; Track pitch slide length = [A]
$1B1A: 3F EF 18  call  $18EF        ; Y = A = next track data byte
$1B1D: 60        clrc               ;\
$1B1E: 84 50     adc   a,$50        ;} A += [music transpose] + [track transpose]
$1B20: 95 F0 02  adc   a,$02F0+x    ;/
}


;;; $1B23: Set track target pitch ;;;
{
$1B23: 28 7F     and   a,#$7F       ;\
$1B25: D5 80 03  mov   $0380+x,a    ;} Track target pitch = [A] & 7Fh
$1B28: 80        setc               ;\
$1B29: B5 61 03  sbc   a,$0361+x    ;|
$1B2C: FB A0     mov   y,$A0+x      ;|
$1B2E: 6D        push  y            ;|
$1B2F: CE        pop   x            ;} Track pitch delta = ([track target pitch] - [track pitch]) * 100h / [track pitch slide length]
$1B30: 3F 46 1B  call  $1B46        ;|
$1B33: D5 70 03  mov   $0370+x,a    ;|
$1B36: DD        mov   a,y          ;|
$1B37: D5 71 03  mov   $0371+x,a    ;/
$1B3A: 6F        ret
}


;;; $1B3B: $11.$10 = [track note] ;;;
{
$1B3B: F5 61 03  mov   a,$0361+x
$1B3E: C4 11     mov   $11,a
$1B40: F5 60 03  mov   a,$0360+x
$1B43: C4 10     mov   $10,a
$1B45: 6F        ret
}


;;; $1B46: YA = [A] * 100h / [X] ;;;
{
;; Parameters:
;;     A: Quotient / 100h
;;     X: Divisor
;;     Carry: If set, quotient is assumed to be negative. Otherwise, unsigned division
$1B46: ED        notc               ;\
$1B47: 6B 12     ror   $12          ;} If carry clear:
$1B49: 10 03     bpl   $1B4E        ;/
$1B4B: 48 FF     eor   a,#$FF       ;\
$1B4D: BC        inc   a            ;} A = -[A]

$1B4E: 8D 00     mov   y,#$00       ;\
$1B50: 9E        div   ya,x         ;|
$1B51: 2D        push  a            ;|
$1B52: E8 00     mov   a,#$00       ;} YA = [A] / [X] * 100h + ([A] % [X]) * 100h / [X]
$1B54: 9E        div   ya,x         ;|
$1B55: EE        pop   y            ;/
$1B56: F8 44     mov   x,$44        ; X = [track index]
}


;;; $1B58: If [$12] & 80h: YA = -[YA] ;;;
{
$1B58: F3 12 06  bbc7  $12,$1B61
$1B5B: DA 14     movw  $14,ya
$1B5D: BA 0E     movw  ya,$0E
$1B5F: 9A 14     subw  ya,$14

$1B61: 6F        ret
}


;;; $1B62: Jump table for track command handling ;;;
{
; Indexed with (instruction - E0h) * 2
$1B62:           dw 18F9, 1952, 1960, 1979, 1985, 19A0, 19A5, 19B7, 19BC, 19CE, 19D1, 19D5, 19E1, 1A02, 1A0B, 1A28,
                    1990, 19E4, 19E8, 19FE, 1A24, 1A4B, 1A7F, 1A86, 1A5E, 1B13, 1AF1, 1AF4, 1AF8, 1AFD, 1AFE
}


;;; $1BA0: Number of command parameter bytes ;;;
{
; Indexed with instruction - E0h
$1BA0:           db 01, 01, 02, 03, 00, 01, 02, 01, 02, 01, 01, 03, 00, 01, 02, 03,
                    01, 03, 03, 00, 01, 03, 00, 03, 03, 03, 01, 02, 00, 00, 00
}


;;; $1BBF: Handle track volume ;;;
{
$1BBF: F4 90     mov   a,$90+x      ;\
$1BC1: F0 24     beq   $1BE7        ;} If [track dynamic volume timer] = 0: go to BRANCH_DYNAMIC_VOLUME_END
$1BC3: 09 47 5E  or    ($5E),($47)  ; Music voice volume update bitset |= [current music voice bitset]
$1BC6: 9B 90     dec   $90+x        ; Decrement track dynamic volume timer
$1BC8: D0 0A     bne   $1BD4        ; If [track dynamic volume timer] = 0:
$1BCA: E8 00     mov   a,#$00       ;\
$1BCC: D5 00 03  mov   $0300+x,a    ;} Track volume multiplier = [track target volume multiplier]
$1BCF: F5 20 03  mov   a,$0320+x    ;/
$1BD2: 2F 10     bra   $1BE4

$1BD4: 60        clrc               ;\ Else ([track dynamic volume timer] != 0):
$1BD5: F5 00 03  mov   a,$0300+x    ;|
$1BD8: 95 10 03  adc   a,$0310+x    ;|
$1BDB: D5 00 03  mov   $0300+x,a    ;} Track volume multiplier += [track volume multiplier delta]
$1BDE: F5 01 03  mov   a,$0301+x    ;|
$1BE1: 95 11 03  adc   a,$0311+x    ;/

$1BE4: D5 01 03  mov   $0301+x,a

; BRANCH_DYNAMIC_VOLUME_END
$1BE7: FB C1     mov   y,$C1+x      ;\
$1BE9: F0 23     beq   $1C0E        ;} If [track tremolo extent] = 0: go to BRANCH_NO_TREMOLO
$1BEB: F5 E0 02  mov   a,$02E0+x    ;\
$1BEE: DE C0 1B  cbne  $C0+x,$1C0C  ;} If [track tremolo delay] != [track tremolo delay timer]: go to BRANCH_TREMOLO_DELAY
$1BF1: 09 47 5E  or    ($5E),($47)  ; Music voice volume update bitset |= [current music voice bitset]
$1BF4: F5 D0 02  mov   a,$02D0+x    ; A = [track tremolo phase]
$1BF7: 10 07     bpl   $1C00        ; If [track tremolo phase] < 0:
$1BF9: FC        inc   y            ;\
$1BFA: D0 04     bne   $1C00        ;} If [track tremolo extent] = FFh:
$1BFC: E8 80     mov   a,#$80       ; A = 80h
$1BFE: 2F 04     bra   $1C04

$1C00: 60        clrc               ;\ Else ([track tremolo phase] >= 0 or [track tremolo extent] != FFh):
$1C01: 95 D1 02  adc   a,$02D1+x    ;} A += [track tremolo rate]

$1C04: D5 D0 02  mov   $02D0+x,a    ; Track tremolo phase = [A]
$1C07: 3F 00 1E  call  $1E00        ; Calculate track output volume multiplier
$1C0A: 2F 07     bra   $1C13        ; Go to BRANCH_TREMOLO_END

; BRANCH_TREMOLO_DELAY
$1C0C: BB C0     inc   $C0+x        ; Increment track tremolo delay timer

; BRANCH_NO_TREMOLO
$1C0E: E8 FF     mov   a,#$FF       ;\
$1C10: 3F 0B 1E  call  $1E0B        ;} Calculate track output volume multiplier - no tremolo

; BRANCH_TREMOLO_END
$1C13: F4 91     mov   a,$91+x      ;\
$1C15: F0 24     beq   $1C3B        ;} If [track dynamic panning timer] = 0: go to BRANCH_DYNAMIC_PANNING_END
$1C17: 09 47 5E  or    ($5E),($47)  ; Music voice volume update bitset |= [current music voice bitset]
$1C1A: 9B 91     dec   $91+x        ; Decrement track dynamic panning timer
$1C1C: D0 0A     bne   $1C28        ; If [track dynamic panning timer] = 0:
$1C1E: E8 00     mov   a,#$00       ;\
$1C20: D5 30 03  mov   $0330+x,a    ;} Track panning bias = 0
$1C23: F5 50 03  mov   a,$0350+x    ;/
$1C26: 2F 10     bra   $1C38

$1C28: 60        clrc               ;\ Else ([track dynamic panning timer] != 0):
$1C29: F5 30 03  mov   a,$0330+x    ;|
$1C2C: 95 40 03  adc   a,$0340+x    ;|
$1C2F: D5 30 03  mov   $0330+x,a    ;} Track panning bias += [track $0340]
$1C32: F5 31 03  mov   a,$0331+x    ;|
$1C35: 95 41 03  adc   a,$0341+x    ;/

$1C38: D5 31 03  mov   $0331+x,a

; BRANCH_DYNAMIC_PANNING_END
$1C3B: E4 47     mov   a,$47        ;\
$1C3D: 24 5E     and   a,$5E        ;} If [music voice volume update bitset] & [current music voice bitset] = 0: return
$1C3F: F0 46     beq   $1C87        ;/
$1C41: F5 31 03  mov   a,$0331+x    ;\
$1C44: FD        mov   y,a          ;|
$1C45: F5 30 03  mov   a,$0330+x    ;} $10 = [track panning bias]
$1C48: DA 10     movw  $10,ya       ;/
}


;;; $1C4A: Calculate and write DSP voice volumes if voice is not sound effect enabled ;;;
{
; This function does panned volume calculation where [$10] / 1400h is the panning bias (so 0 is fully right, 1400h is fully left).

; $1E1D..31 is a table of multipliers to be used for values of [$10] that are multiples of 100h,
; the multiplier used for values of [$10] that are not multiples of 100h is given by linearly interpolation of the closest values from the table.

; So given
;     i_0 = [$10] / 100h
;     i_1 = [$10] / 100h + 1
;
; the indices for the $1E1D table for the multiples of 100h less than and greater than [$10] respectively,
; let
;     y_0 = [$1E1D + i_0]
;     y_1 = [$1E1D + i_1]
;
; be the volume multipliers corresponding to values of $10
;     x_0 = i_0 * 100h
;     x_1 = i_1 * 100h
;
; and let x be the value of [$10], then
;     y = (x - x_0) * (y_1 - y_0) / (x_1 - x_0) + y_0
;
; is the interpolated volume multiplier. Note that x_1 - x_0 = 100h and x - x_0 = [$10] % 100h


; Let i = [$10] / 100h
; Let dy = [$1E1D + i + 1] - [$1E1D + i]
; Let x_l = [$10] % 100h
; Let x_r = (1400h - [$10]) % 100h
; Let y_0 = [$1E1D + i]

; Left volume  = (dy * x_l / 100h + y_0) * [track $0321] / 100h
; Right volume = (dy * x_r / 100h + y_0) * [track $0321] / 100h

; If [track $0351] & 80h != 0:
;     Left volume *= -1

; If [track $0351] & 40h != 0:
;     Right volume *= -1

$1C4A: 7D        mov   a,x          ;\
$1C4B: 9F        xcn   a            ;|
$1C4C: 5C        lsr   a            ;} $12 = [X] / 2 * 10h (voice's DSP left volume)
$1C4D: C4 12     mov   $12,a        ;/

; LOOP
$1C4F: EB 11     mov   y,$11        ;\
$1C51: F6 1E 1E  mov   a,$1E1E+y    ;|
$1C54: 80        setc               ;|
$1C55: B6 1D 1E  sbc   a,$1E1D+y    ;|
$1C58: EB 10     mov   y,$10        ;|
$1C5A: CF        mul   ya           ;|
$1C5B: DD        mov   a,y          ;} Y = (([$1E1D + [$10] / 100h + 1] - [$1E1D + [$10] / 100h]) * ([$10] % 100h) / 100h + [$1E1D + [$10] / 100h]) * [track output volume multiplier]
$1C5C: EB 11     mov   y,$11        ;|
$1C5E: 60        clrc               ;|
$1C5F: 96 1D 1E  adc   a,$1E1D+y    ;|
$1C62: FD        mov   y,a          ;|
$1C63: F5 21 03  mov   a,$0321+x    ;|
$1C66: CF        mul   ya           ;/
$1C67: F5 51 03  mov   a,$0351+x    ;\
$1C6A: 1C        asl   a            ;} Carry = [track phase inversion options] & 80h != 0
$1C6B: 13 12 01  bbc0  $12,$1C6F    ; If [$12] % 2 = 1:
$1C6E: 1C        asl   a            ; Carry = [track phase inversion options] & 40h != 0

$1C6F: DD        mov   a,y          ; A = [Y]
$1C70: 90 03     bcc   $1C75        ; If carry set:
$1C72: 48 FF     eor   a,#$FF       ;\
$1C74: BC        inc   a            ;} A = -[A]

$1C75: EB 12     mov   y,$12        ;\
$1C77: 3F 1E 17  call  $171E        ;} DSP register [$12] = [A] if voice is not sound effect enabled
$1C7A: 8D 14     mov   y,#$14       ;\
$1C7C: E8 00     mov   a,#$00       ;|
$1C7E: 9A 10     subw  ya,$10       ;} $10 = 1400h - [$10]
$1C80: DA 10     movw  $10,ya       ;/
$1C82: AB 12     inc   $12          ; Increment $12
$1C84: 33 12 C8  bbc1  $12,$1C4F    ; If [$12] % 4 < 2: go to LOOP

$1C87: 6F        ret
}


;;; $1C88: Handle current note ;;;
{
$1C88: F4 71     mov   a,$71+x      ;\
$1C8A: F0 65     beq   $1CF1        ;} If [track note ring timer] = 0: go to BRANCH_CONTINUE_PLAYING
$1C8C: 9B 71     dec   $71+x        ; Decrement track note ring timer
$1C8E: F0 05     beq   $1C95        ; If [track note ring timer] != 0:
$1C90: E8 02     mov   a,#$02       ;\
$1C92: DE 70 5C  cbne  $70+x,$1CF1  ;} If [track note timer] != 2: go to BRANCH_CONTINUE_PLAYING

; Note ring has ended or note is ending in two ticks
$1C95: F4 80     mov   a,$80+x      ;\
$1C97: C4 17     mov   $17,a        ;} $17 = [track repeated subsection counter]
$1C99: F4 30     mov   a,$30+x      ;\
$1C9B: FB 31     mov   y,$31+x      ;} YA = [track pointer]

; LOOP_SECTIONS
$1C9D: DA 14     movw  $14,ya       ; $14 = [YA]
$1C9F: 8D 00     mov   y,#$00       ; Y = 0

; LOOP_COMMANDS
$1CA1: F7 14     mov   a,($14)+y    ; A = [[$14] + [Y]]
$1CA3: F0 1E     beq   $1CC3        ; If [A] = 0: go to BRANCH_END
$1CA5: 30 07     bmi   $1CAE        ; If [A] & 80h != 0: go to BRANCH_COMMAND

; LOOP_NOTE_PARAMETERS
$1CA7: FC        inc   y            ; Increment Y
$1CA8: 30 40     bmi   $1CEA        ; If [Y] >= 80h: go to BRANCH_NOTE
$1CAA: F7 14     mov   a,($14)+y    ; A = [[$14] + [Y]]
$1CAC: 10 F9     bpl   $1CA7        ; If [A] & 80h = 0: go to LOOP_NOTE_PARAMETERS

; BRANCH_COMMAND
$1CAE: 68 C8     cmp   a,#$C8       ;\
$1CB0: F0 3F     beq   $1CF1        ;} If [A] = C8h (tie note): go to BRANCH_CONTINUE_PLAYING
$1CB2: 68 EF     cmp   a,#$EF       ;\
$1CB4: F0 29     beq   $1CDF        ;} If [A] = EFh: go to BRANCH_REPEAT_SUBSECTION
$1CB6: 68 E0     cmp   a,#$E0       ;\
$1CB8: 90 30     bcc   $1CEA        ;} If [A] < E0h: go to BRANCH_NOTE
$1CBA: 6D        push  y            ;\
$1CBB: FD        mov   y,a          ;|
$1CBC: AE        pop   a            ;} Y += [$1BA0 + [A] - E0h] (number of command parameter bytes)
$1CBD: 96 C0 1A  adc   a,$1AC0+y    ;|
$1CC0: FD        mov   y,a          ;/
$1CC1: 2F DE     bra   $1CA1        ; Go to LOOP_COMMANDS

; BRANCH_END
$1CC3: E4 17     mov   a,$17        ;\
$1CC5: F0 23     beq   $1CEA        ;} If [$17] = 0: go to BRANCH_NOTE
$1CC7: 8B 17     dec   $17          ; Decrement $17
$1CC9: D0 0A     bne   $1CD5        ; If [$17] = 0:
$1CCB: F5 31 02  mov   a,$0231+x    ;\
$1CCE: 2D        push  a            ;|
$1CCF: F5 30 02  mov   a,$0230+x    ;} YA = [track repeated subsection return address]
$1CD2: EE        pop   y            ;/
$1CD3: 2F C8     bra   $1C9D        ; Go to LOOP_SECTIONS

$1CD5: F5 41 02  mov   a,$0241+x    ;\
$1CD8: 2D        push  a            ;|
$1CD9: F5 40 02  mov   a,$0240+x    ;} YA = [track repeated subsection address]
$1CDC: EE        pop   y            ;/
$1CDD: 2F BE     bra   $1C9D        ; Go to LOOP_SECTIONS

; BRANCH_REPEAT_SUBSECTION
$1CDF: FC        inc   y            ;\
$1CE0: F7 14     mov   a,($14)+y    ;|
$1CE2: 2D        push  a            ;|
$1CE3: FC        inc   y            ;} YA = [[$14] + [Y] + 1]
$1CE4: F7 14     mov   a,($14)+y    ;|
$1CE6: FD        mov   y,a          ;|
$1CE7: AE        pop   a            ;/
$1CE8: 2F B3     bra   $1C9D        ; Go to LOOP_SECTIONS

; BRANCH_NOTE
$1CEA: E4 47     mov   a,$47        ;\
$1CEC: 8D 5C     mov   y,#$5C       ;} DSP key off flags = [current music voice bitset] if voice is not sound effect enabled
$1CEE: 3F 1E 17  call  $171E        ;/

; BRANCH_CONTINUE_PLAYING
$1CF1: F2 13     clr7  $13          ; Note modified flag &= ~80h
$1CF3: F4 A0     mov   a,$A0+x      ;\
$1CF5: F0 2C     beq   $1D23        ;} If [track pitch slide timer] = 0: go to BRANCH_PITCH_SLIDE_END
$1CF7: F4 A1     mov   a,$A1+x      ;\
$1CF9: F0 04     beq   $1CFF        ;} If [track pitch slide delay timer] != 0:
$1CFB: 9B A1     dec   $A1+x        ; Decrement track pitch slide delay timer
$1CFD: 2F 24     bra   $1D23        ; Go to BRANCH_PITCH_SLIDE_END

$1CFF: E2 13     set7  $13          ; Note modified flag |= 80h
$1D01: 9B A0     dec   $A0+x        ; Decrement track pitch slide timer
$1D03: D0 0B     bne   $1D10        ; If [track pitch slide timer] = 0:
$1D05: F5 81 03  mov   a,$0381+x    ;\
$1D08: D5 60 03  mov   $0360+x,a    ;} Track subnote = [track subtranspose]
$1D0B: F5 80 03  mov   a,$0380+x    ;\
$1D0E: 2F 10     bra   $1D20        ;} Track note = [track target note]

$1D10: 60        clrc               ;\ Else ([track pitch slide timer] != 0):
$1D11: F5 60 03  mov   a,$0360+x    ;|
$1D14: 95 70 03  adc   a,$0370+x    ;|
$1D17: D5 60 03  mov   $0360+x,a    ;} Track note += [track note delta]
$1D1A: F5 61 03  mov   a,$0361+x    ;|
$1D1D: 95 71 03  adc   a,$0371+x    ;/

$1D20: D5 61 03  mov   $0361+x,a

; BRANCH_PITCH_SLIDE_END
$1D23: 3F 3B 1B  call  $1B3B        ; $11.$10 = [track note]
$1D26: F4 B1     mov   a,$B1+x      ;\
$1D28: F0 4C     beq   $1D76        ;} If [track vibrato extent] = 0: go to play note if note was modified
$1D2A: F5 B0 02  mov   a,$02B0+x    ;\
$1D2D: DE B0 44  cbne  $B0+x,$1D74  ;} If [track vibrato delay] != [track vibrato delay timer]: go to play note if note was modified and increment track vibrato delay timer
$1D30: F5 00 01  mov   a,$0100+x    ;\
$1D33: 75 B1 02  cmp   a,$02B1+x    ;} If [track dynamic vibrato timer] = [track dynamic vibrato length]:
$1D36: D0 05     bne   $1D3D        ;/
$1D38: F5 C1 02  mov   a,$02C1+x    ; A = [track static vibrato extent]
$1D3B: 2F 0D     bra   $1D4A        ; Go to BRANCH_DYNAMIC_VIBRATO_END

$1D3D: 40        setp               ;\
$1D3E: BB 00     inc   $00+x        ;} Increment track dynamic vibrato timer
$1D40: 20        clrp               ;/
$1D41: FD        mov   y,a          ;\
$1D42: F0 02     beq   $1D46        ;} If [track dynamic vibrato timer] = 1: A = 0
$1D44: F4 B1     mov   a,$B1+x      ;} Else: A = [track vibrato extent]

$1D46: 60        clrc               ;\
$1D47: 95 C0 02  adc   a,$02C0+x    ;} A += [track vibrato extent delta]

; BRANCH_DYNAMIC_VIBRATO_END
$1D4A: D4 B1     mov   $B1+x,a      ; Track vibrato extent = [A]
$1D4C: F5 A0 02  mov   a,$02A0+x    ;\
$1D4F: 60        clrc               ;|
$1D50: 95 A1 02  adc   a,$02A1+x    ;} Track vibrato phase += [track vibrato rate]
$1D53: D5 A0 02  mov   $02A0+x,a    ;/
}


;;; $1D56: Play note with vibrato ;;;
{
$1D56: C4 12     mov   $12,a        ; $12 = [track vibrato phase]
$1D58: 1C        asl   a            ;\
$1D59: 1C        asl   a            ;} A = [track vibrato phase] * 4
$1D5A: 90 02     bcc   $1D5E        ; If [track vibrato phase] & 40h != 0:
$1D5C: 48 FF     eor   a,#$FF       ; A = ~[A]

$1D5E: FD        mov   y,a
$1D5F: F4 B1     mov   a,$B1+x      ;\
$1D61: 68 F1     cmp   a,#$F1       ;} If [track vibrato extent] >= F1h:
$1D63: 90 05     bcc   $1D6A        ;/
$1D65: 28 0F     and   a,#$0F       ;\
$1D67: CF        mul   ya           ;} YA = [A] * ([track vibrato extent] & Fh)
$1D68: 2F 04     bra   $1D6E

$1D6A: CF        mul   ya           ;\ Else ([track vibrato extent] < F1h):
$1D6B: DD        mov   a,y          ;} YA = [A] * [track vibrato extent] / 100h
$1D6C: 8D 00     mov   y,#$00       ;/

$1D6E: 3F EB 1D  call  $1DEB        ; $10 += [YA] * sgn([track vibrato phase])

$1D71: 5F 9B 16  jmp   $169B        ; Go to play note after psychoacoustic adjustment
}


;;; $1D74: Play note if note was modified and increment track vibrato delay timer ;;;
{
$1D74: BB B0     inc   $B0+x        ; Increment track vibrato delay timer
}


;;; $1D76: Play note if note was modified ;;;
{
$1D76: E3 13 F8  bbs7  $13,$1D71    ; If note was modified: go to play note after psychoacoustic adjustment
$1D79: 6F        ret
}


;;; $1D7A: Update playing track ;;;
{
$1D7A: F2 13     clr7  $13          ; Note modified flag &= ~80h
$1D7C: F4 C1     mov   a,$C1+x      ;\
$1D7E: F0 09     beq   $1D89        ;} If [track tremolo extent] != 0:
$1D80: F5 E0 02  mov   a,$02E0+x    ;\
$1D83: DE C0 03  cbne  $C0+x,$1D89  ;} If [track tremolo delay] = [track tremolo delay timer]:
$1D86: 3F F3 1D  call  $1DF3        ; Update playing track output volume multiplier

$1D89: F5 31 03  mov   a,$0331+x    ;\
$1D8C: FD        mov   y,a          ;|
$1D8D: F5 30 03  mov   a,$0330+x    ;} $10 = [track panning bias]
$1D90: DA 10     movw  $10,ya       ;/
$1D92: F4 91     mov   a,$91+x      ;\
$1D94: F0 0A     beq   $1DA0        ;} If [track dynamic panning timer] != 0:
$1D96: F5 41 03  mov   a,$0341+x    ;\
$1D99: FD        mov   y,a          ;|
$1D9A: F5 40 03  mov   a,$0340+x    ;} $10 += [track panning bias delta] * [music track clock] / 100h
$1D9D: 3F D5 1D  call  $1DD5        ;/

$1DA0: F3 13 03  bbc7  $13,$1DA6    ; If note was modified != 0:
$1DA3: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled

$1DA6: F2 13     clr7  $13          ; Note modified flag &= ~80h
$1DA8: 3F 3B 1B  call  $1B3B        ; $11.$10 = [track note]
$1DAB: F4 A0     mov   a,$A0+x      ;\
$1DAD: F0 0E     beq   $1DBD        ;} If [track pitch slide timer] != 0:
$1DAF: F4 A1     mov   a,$A1+x      ;\
$1DB1: D0 0A     bne   $1DBD        ;} If [track pitch slide delay timer] = 0:
$1DB3: F5 71 03  mov   a,$0371+x    ;\
$1DB6: FD        mov   y,a          ;|
$1DB7: F5 70 03  mov   a,$0370+x    ;} $11.$10 += [track note delta] * [music track clock] / 100h
$1DBA: 3F D5 1D  call  $1DD5        ;/

$1DBD: F4 B1     mov   a,$B1+x      ;\
$1DBF: F0 B5     beq   $1D76        ;} If [track vibrato extent] = 0: go to play note if note was modified
$1DC1: F5 B0 02  mov   a,$02B0+x    ;\
$1DC4: DE B0 AF  cbne  $B0+x,$1D76  ;} If [track vibrato delay] != [track vibrato delay timer]: go to play note if note was modified
$1DC7: EB 51     mov   y,$51        ;\
$1DC9: F5 A1 02  mov   a,$02A1+x    ;|
$1DCC: CF        mul   ya           ;|
$1DCD: DD        mov   a,y          ;} A = [music track clock] * [track vibrato rate] / 100h + [track vibrato phase]
$1DCE: 60        clrc               ;|
$1DCF: 95 A0 02  adc   a,$02A0+x    ;/
$1DD2: 5F 56 1D  jmp   $1D56        ; Go to play note with vibrato
}


;;; $1DD5: $10 += ±[YA] * [music track clock] / 100h ;;;
{
; Multiply rate by percent of tic to add to current value to get actual value for output this moment.
; Used by panning, looks like others can use it too
$1DD5: E2 13     set7  $13          ; Note modified flag |= 80h
$1DD7: CB 12     mov   $12,y        ;\
$1DD9: 3F 58 1B  call  $1B58        ;|
$1DDC: 6D        push  y            ;|
$1DDD: EB 51     mov   y,$51        ;|
$1DDF: CF        mul   ya           ;|
$1DE0: CB 14     mov   $14,y        ;} YA = |[YA]| * [music track clock] / 100h
$1DE2: 8F 00 15  mov   $15,#$00     ;|
$1DE5: EB 51     mov   y,$51        ;|
$1DE7: AE        pop   a            ;|
$1DE8: CF        mul   ya           ;|
$1DE9: 7A 14     addw  ya,$14       ;/
}


;;; $1DEB: $10 += [YA] * sgn([$12]) ;;;
{
$1DEB: 3F 58 1B  call  $1B58        ; If [$12] & 80h: YA = -[YA]
$1DEE: 7A 10     addw  ya,$10       ;\
$1DF0: DA 10     movw  $10,ya       ;} $10 += [YA]
$1DF2: 6F        ret
}


;;; $1DF3: Update playing track output volume multiplier ;;;
{
$1DF3: E2 13     set7  $13          ; Note modified flag |= 80h
$1DF5: EB 51     mov   y,$51        ;\
$1DF7: F5 D1 02  mov   a,$02D1+x    ;|
$1DFA: CF        mul   ya           ;|
$1DFB: DD        mov   a,y          ;} A = [music track clock] * [track tremolo rate] / 100h + [track tremolo phase]
$1DFC: 60        clrc               ;|
$1DFD: 95 D0 02  adc   a,$02D0+x    ;/
}


;;; $1E00: Calculate track output volume multiplier ;;;
{
;; Parameters:
;;     A: Track tremolo phase
$1E00: 1C        asl   a            ;\
$1E01: 90 02     bcc   $1E05        ;} A = |[A]| * 2 - ([A] >> 7)
$1E03: 48 FF     eor   a,#$FF       ;/

$1E05: FB C1     mov   y,$C1+x      ;\
$1E07: CF        mul   ya           ;|
$1E08: DD        mov   a,y          ;} A = FFh - [A] * [track tremolo extent] / 100h
$1E09: 48 FF     eor   a,#$FF       ;/
}


;;; $1E0B: Calculate track output volume multiplier - no tremolo ;;;
{
$1E0B: EB 59     mov   y,$59        ;\
$1E0D: CF        mul   ya           ;|
$1E0E: F5 10 02  mov   a,$0210+x    ;|
$1E11: CF        mul   ya           ;|
$1E12: F5 01 03  mov   a,$0301+x    ;|
$1E15: CF        mul   ya           ;} Track output volume multiplier = ([A] * [music volume multiplier] * [track note volume multiplier] * [track volume multiplier])²
$1E16: DD        mov   a,y          ;|
$1E17: CF        mul   ya           ;|
$1E18: DD        mov   a,y          ;|
$1E19: D5 21 03  mov   $0321+x,a    ;/
$1E1C: 6F        ret
}
}


;;; $1E1D: Panning volume multipliers ;;;
{
$1E1D:           db 00, 01, 03, 07, 0D, 15, 1E, 29, 34, 42, 51, 5E, 67, 6E, 73, 77,
                    7A, 7C, 7D, 7E, 7F
}


;;; $1E32: Echo FIR filters ;;;
{
$1E32:           db 7F,00,00,00,00,00,00,00 ; Sharp echo
$1E3A:           db 58,BF,DB,F0,FE,07,0C,0C ; Echo + reverb
$1E42:           db 0C,21,2B,2B,13,FE,F3,F9 ; Smooth echo
$1E4A:           db 34,33,00,D9,E5,01,FC,EB ; ???
}


;;; $1E52: DSP register addresses for DSP update ;;;
{
$1E52:           db 2C, 3C, 0D, 4D, 6C, 4C, 5C, 3D, 2D, 5C
}


;;; $1E5C: Direct page addresses for DSP update ;;;
{
$1E5C:           db 61, 63, 4E, 4A, 48, 45, 0E, 49, 4B, 46
}


;;; $1E66: Pitch table ;;;
{
; Octave 7
; C_7, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B, C_8
; In decimal:
;     2143, 2270, 2405, 2548, 2700, 2860, 3030, 3211, 3402, 3604, 3818, 4045, 4286

; Thus A_4 = 450.5, is this table actually in Hz? It's higher than the typical 440 Hz, but it accounts for the APU clock slowing down over time
; Ratio is a constant ~1.059 which is ~2^(1/12) as one would expect.
$1E66:           dw 085F, 08DE, 0965, 09F4, 0A8C, 0B2C, 0BD6, 0C8B, 0D4A, 0E14, 0EEA, 0FCD, 10BE
}


;;; $1E80: "*Ver S1.20*" ;;;
{
$1E80:           db 2A, 56, 65, 72, 20, 53, 31, 2E, 32, 30, 2A
}


;;; $1E8B: Receive data from CPU ;;;
{
; Data format:
;     ssss dddd [xx xx...] (data block 0)
;     ssss dddd [xx xx...] (data block 1)
;     ...
;     0000 aaaa
; Where:
;     s = data block size in bytes
;     d = destination address
;     x = data
;     a = entry address. Ignored (used by boot ROM for first APU transfer)

; CPU IO 0..1 = AAh BBh
; Wait until [CPU IO 0] = CCh
; For each data block:
;     Destination address = [CPU IO 2..3]
;     Echo [CPU IO 0]
;     [CPU IO 1] != 0
;     Index = 0
;     For each data byte:
;         Wait until [CPU IO 0] = index
;         Echo index back through [CPU IO 0]
;         Destination address + index = [CPU IO 1]
;         Increment index
;         If index = 0:
;             Destination address += 100h
;     [CPU IO 0] > index
; Entry address = [CPU IO 2..3] (ignored)
; Echo [CPU IO 0]
; [CPU IO 1] == 0

$1E8B: E8 AA     mov   a,#$AA       ;\
$1E8D: C5 F4 00  mov   $00F4,a      ;|
$1E90: E8 BB     mov   a,#$BB       ;} CPU IO 0..1 = AAh BBh
$1E92: C5 F5 00  mov   $00F5,a      ;/

$1E95: E5 F4 00  mov   a,$00F4      ;\
$1E98: 68 CC     cmp   a,#$CC       ;} Wait until [CPU IO 0] = CCh
$1E9A: D0 F9     bne   $1E95        ;/
$1E9C: 2F 20     bra   $1EBE        ; Go to BRANCH_PROCESS_DATA_BLOCK

; LOOP_DATA_BLOCK
$1E9E: EC F4 00  mov   y,$00F4      ; Y = [CPU IO 0] (CTR)
$1EA1: D0 FB     bne   $1E9E        ; If CTR != 0: go to LOOP_DATA_BLOCK

; LOOP_DATA_BYTE
$1EA3: 5E F4 00  cmp   y,$00F4      ;\
$1EA6: D0 0F     bne   $1EB7        ;} If [CPU IO 0] = CTR:
$1EA8: E5 F5 00  mov   a,$00F5
$1EAB: CC F4 00  mov   $00F4,y      ; Echo [CPU IO 0]
$1EAE: D7 14     mov   ($14)+y,a    ; $14 + CTR = [CPU IO 1]
$1EB0: FC        inc   y            ; Increment CTR
$1EB1: D0 F0     bne   $1EA3        ; If [Y] != 0: go to LOOP_DATA_BYTE
$1EB3: AB 15     inc   $15          ; $14 += 100h
$1EB5: 2F EC     bra   $1EA3        ; Go to LOOP_DATA_BYTE

$1EB7: 10 EA     bpl   $1EA3        ; If [CPU IO 0] < CTR: go to LOOP_DATA_BYTE
$1EB9: 5E F4 00  cmp   y,$00F4      ;\
$1EBC: 10 E5     bpl   $1EA3        ;} If [CPU IO 0] <= CTR: go to LOOP_DATA_BYTE (a double check)

; BRANCH_PROCESS_DATA_BLOCK
$1EBE: E5 F6 00  mov   a,$00F6      ;\
$1EC1: EC F7 00  mov   y,$00F7      ;} $14 = [CPU IO 2..3] (destination address)
$1EC4: DA 14     movw  $14,ya       ;/
$1EC6: EC F4 00  mov   y,$00F4      ;\
$1EC9: E5 F5 00  mov   a,$00F5      ;} Echo [CPU IO 0]
$1ECC: CC F4 00  mov   $00F4,y      ;/
$1ECF: D0 CD     bne   $1E9E        ; If [CPU IO 1] != 0: go to LOOP_DATA_BLOCK
$1ED1: CD 31     mov   x,#$31       ;\
$1ED3: C9 F1 00  mov   $00F1,x      ;} Reset CPU IO input latches and enable/reset timer 0
$1ED6: 6F        ret
}


;;; $1ED7: Clear [$0390] bytes from [$EE] ;;;
{
$1ED7: E8 00     mov   a,#$00
$1ED9: 8D 00     mov   y,#$00

$1EDB: D7 EE     mov   ($EE)+y,a
$1EDD: FC        inc   y
$1EDE: 5E 90 03  cmp   y,$0390
$1EE1: D0 F8     bne   $1EDB
$1EE3: 6F        ret
}


;;; $1EE4..3153: Sound library 1 ;;;
{
;;; $1EE4: Go to process sound 1 ;;;
{
$1EE4: 5F D1 1F  jmp   $1FD1
}


;;; $1EE7: Handle CPU IO 1 ;;;
{
; BUG: All sound 1 channels are being reset if a new sound effect causes a current sound effect to stop
;      However, some of the channels might have already been reset by reaching the end of their instruction list
;      and in the time since then, the voice may have been allocated to a sound effect in a different library
;      Re-resetting that channel will erroneously mark the voice as available for allocation,
;      allowing two sound effects to have the same voice allocated to them
;      This is the cause of the laser door opening sound glitch

$1EE7: EB 09     mov   y,$09        ; Y = [previous value read from CPU IO 1]
$1EE9: E4 01     mov   a,$01        ;\
$1EEB: C4 09     mov   $09,a        ;} Previous value read from CPU IO 1 = [value read from CPU IO 1]
$1EED: C4 05     mov   $05,a        ; Value for CPU IO 1 = [value read from CPU IO 1]
$1EEF: 7E 01     cmp   y,$01        ;\
$1EF1: D0 06     bne   $1EF9        ;} If [Y] != [value read from CPU IO 1]: go to BRANCH_CHANGE

; BRANCH_NO_CHANGE
$1EF3: E5 92 03  mov   a,$0392      ;\
$1EF6: D0 EC     bne   $1EE4        ;} If [current sound 1] != 0: go to process sound 1
$1EF8: 6F        ret                ; Return

; BRANCH_CHANGE
$1EF9: 68 00     cmp   a,#$00       ;\
$1EFB: F0 F6     beq   $1EF3        ;} If [value read from CPU IO 1] = 0: go to BRANCH_NO_CHANGE
$1EFD: E4 01     mov   a,$01        ;\
$1EFF: 68 02     cmp   a,#$02       ;} If [value read from CPU IO 1] != 2 (silence):
$1F01: F0 09     beq   $1F0C        ;/
$1F03: 68 01     cmp   a,#$01       ;\
$1F05: F0 05     beq   $1F0C        ;} If [value read from CPU IO 1] != 1 (power bomb explosion):
$1F07: E5 BB 04  mov   a,$04BB      ;\
$1F0A: D0 E7     bne   $1EF3        ;} If [sound 1 priority] != 0: go to BRANCH_NO_CHANGE

$1F0C: E5 92 03  mov   a,$0392      ;\
$1F0F: F0 11     beq   $1F22        ;} If [current sound 1] != 0:
$1F11: E8 00     mov   a,#$00       ;\
$1F13: C5 B3 03  mov   $03B3,a      ;} Sound 1 enabled voices = 0
$1F16: 3F 32 27  call  $2732        ; Reset sound 1 channel 0
$1F19: 3F 75 27  call  $2775        ; Reset sound 1 channel 1
$1F1C: 3F B8 27  call  $27B8        ; Reset sound 1 channel 2
$1F1F: 3F FB 27  call  $27FB        ; Reset sound 1 channel 3

$1F22: E8 00     mov   a,#$00
$1F24: C5 E1 03  mov   $03E1,a      ; Sound 1 channel 0 legato flag = 0
$1F27: C5 E8 03  mov   $03E8,a      ; Sound 1 channel 1 legato flag = 0
$1F2A: C5 EF 03  mov   $03EF,a      ; Sound 1 channel 2 legato flag = 0
$1F2D: C5 F6 03  mov   $03F6,a      ; Sound 1 channel 3 legato flag = 0
$1F30: E4 05     mov   a,$05        ;\
$1F32: 9C        dec   a            ;|
$1F33: 1C        asl   a            ;} Current sound 1 index = ([value for CPU IO 1] - 1) * 2
$1F34: C5 93 03  mov   $0393,a      ;/
$1F37: E9 93 03  mov   x,$0393      ;\
$1F3A: F5 ED 2A  mov   a,$2AED+x    ;|
$1F3D: C4 22     mov   $22,a        ;|
$1F3F: 3D        inc   x            ;} Sound 1 instruction list pointer set = [$2AED + [current sound 1 index]]
$1F40: F5 ED 2A  mov   a,$2AED+x    ;|
$1F43: C4 23     mov   $23,a        ;/
$1F45: E4 05     mov   a,$05        ;\
$1F47: C5 92 03  mov   $0392,a      ;} Current sound 1 = [value for CPU IO 1]
$1F4A: 3F 75 28  call  $2875        ; Go to [$1F4D + [current sound 1 index]]

$1F4D:           dw 2A5F, 2A63, 2A63, 2A63, 2A63, 2A63, 2A63, 2A67, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B,
                    2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B, 2A6B,
                    2A6B, 2A6B, 2A6B, 2A6F, 2A73, 2A73, 2A77, 2A7B, 2A7B, 2A7B, 2A7B, 2A7F, 2A83, 2A87, 2A8B, 2A8B,
                    2A8B, 2A8B, 2A8F, 2A93, 2A97, 2A9B, 2A9B, 2A9B, 2A9B, 2A9B, 2A9B, 2A9B, 2A9B, 2A9B, 2A9B, 2A9F,
                    2AA3, 2AA7
}


;;; $1FD1: Process sound 1 ;;;
{
$1FD1: E8 FF     mov   a,#$FF       ;\
$1FD3: 65 A4 03  cmp   a,$03A4      ;} If [sound 1 initialisation flag] != FFh:
$1FD6: F0 6C     beq   $2044        ;/
$1FD8: 3F B2 28  call  $28B2        ; Sound 1 initialisation
$1FDB: 8D 00     mov   y,#$00       ;\
$1FDD: F7 22     mov   a,($22)+y    ;|
$1FDF: C4 2A     mov   $2A,a        ;} $2A = [[sound 1 channel instruction list pointer set]]
$1FE1: 3F 57 2A  call  $2A57        ;|
$1FE4: C4 2B     mov   $2B,a        ;/
$1FE6: 3F 57 2A  call  $2A57        ;\
$1FE9: C4 2C     mov   $2C,a        ;|
$1FEB: 3F 57 2A  call  $2A57        ;} $2C = [[sound 1 channel instruction list pointer set] + 2]
$1FEE: C4 2D     mov   $2D,a        ;/
$1FF0: 3F 57 2A  call  $2A57        ;\
$1FF3: C4 2E     mov   $2E,a        ;|
$1FF5: 3F 57 2A  call  $2A57        ;} $2E = [[sound 1 channel instruction list pointer set] + 4]
$1FF8: C4 2F     mov   $2F,a        ;/
$1FFA: 3F 57 2A  call  $2A57        ;\
$1FFD: C4 D0     mov   $D0,a        ;|
$1FFF: 3F 57 2A  call  $2A57        ;} $D0 = [[sound 1 channel instruction list pointer set] + 6]
$2002: C4 D1     mov   $D1,a        ;/
$2004: E5 AF 03  mov   a,$03AF      ;\
$2007: 3F 5B 2A  call  $2A5B        ;} Sound 1 channel 0 DSP index = [sound 1 channel 0 voice index] * 8
$200A: C5 B4 03  mov   $03B4,a      ;/
$200D: E5 B0 03  mov   a,$03B0      ;\
$2010: 3F 5B 2A  call  $2A5B        ;} Sound 1 channel 1 DSP index = [sound 1 channel 1 voice index] * 8
$2013: C5 B5 03  mov   $03B5,a      ;/
$2016: E5 B1 03  mov   a,$03B1      ;\
$2019: 3F 5B 2A  call  $2A5B        ;} Sound 1 channel 2 DSP index = [sound 1 channel 2 voice index] * 8
$201C: C5 B6 03  mov   $03B6,a      ;/
$201F: E5 B2 03  mov   a,$03B2      ;\
$2022: 3F 5B 2A  call  $2A5B        ;} Sound 1 channel 3 DSP index = [sound 1 channel 3 voice index] * 8
$2025: C5 B7 03  mov   $03B7,a      ;/
$2028: 8D 00     mov   y,#$00       ;\
$202A: CC 94 03  mov   $0394,y      ;|
$202D: CC 95 03  mov   $0395,y      ;} Sound 1 channel instruction list indices = 0
$2030: CC 96 03  mov   $0396,y      ;|
$2033: CC 97 03  mov   $0397,y      ;/
$2036: 8D 01     mov   y,#$01       ;\
$2038: CC 98 03  mov   $0398,y      ;|
$203B: CC 99 03  mov   $0399,y      ;} Sound 1 channel instruction timers = 1
$203E: CC 9A 03  mov   $039A,y      ;|
$2041: CC 9B 03  mov   $039B,y      ;/

$2044: E8 FF     mov   a,#$FF       ;\
$2046: 65 9C 03  cmp   a,$039C      ;} If [sound 1 channel 0 disable byte] = FFh:
$2049: D0 03     bne   $204E        ;/
$204B: 5F FE 21  jmp   $21FE        ; Go to BRANCH_CHANNEL_0_END

; Channel 0
{
$204E: 8C 98 03  dec   $0398        ; Decrement sound 1 channel 0 instruction timer
$2051: F0 03     beq   $2056        ; If [sound 1 channel 0 instruction timer] != 0:
$2053: 5F 97 21  jmp   $2197        ; Go to BRANCH_PROCESS_CHANNEL_0_INSTRUCTION_END

$2056: E5 E1 03  mov   a,$03E1      ;\
$2059: F0 02     beq   $205D        ;} If sound 1 channel 0 legato flag enabled:
$205B: 2F 43     bra   $20A0        ; Go to LOOP_CHANNEL_0_COMMANDS

$205D: E8 00     mov   a,#$00       ;\
$205F: C5 E0 03  mov   $03E0,a      ;} Disable sound 1 channel 0 pitch slide
$2062: C5 DE 03  mov   $03DE,a      ; Sound 1 channel 0 subnote delta = 0
$2065: C5 DF 03  mov   $03DF,a      ; Sound 1 channel 0 target note = 0
$2068: E8 FF     mov   a,#$FF       ;\
$206A: 65 C0 03  cmp   a,$03C0      ;} If [sound 1 channel 0 release flag] != FFh:
$206D: F0 16     beq   $2085        ;/
$206F: E5 A6 03  mov   a,$03A6      ;\
$2072: 04 46     or    a,$46        ;} Key off flags |= [sound 1 channel 0 voice bitset]
$2074: C4 46     mov   $46,a        ;/
$2076: E8 02     mov   a,#$02       ;\
$2078: C5 C1 03  mov   $03C1,a      ;} Sound 1 channel 0 release timer = 2
$207B: E8 01     mov   a,#$01       ;\
$207D: C5 98 03  mov   $0398,a      ;} Sound 1 channel 0 instruction timer = 1
$2080: E8 FF     mov   a,#$FF       ;\
$2082: C5 C0 03  mov   $03C0,a      ;} Sound 1 channel 0 release flag = FFh

$2085: 8C C1 03  dec   $03C1        ; Decrement sound 1 channel 0 release timer
$2088: F0 03     beq   $208D        ; If [sound 1 channel 0 release timer] != 0:
$208A: 5F FE 21  jmp   $21FE        ; Go to BRANCH_CHANNEL_0_END

$208D: E8 00     mov   a,#$00       ;\
$208F: C5 C0 03  mov   $03C0,a      ;} Sound 1 channel 0 release flag = 0
$2092: E5 AA 03  mov   a,$03AA      ;\
$2095: 24 47     and   a,$47        ;} Current music voice bitset &= [sound 1 channel 0 voice mask]
$2097: C4 47     mov   $47,a        ;/
$2099: E5 AA 03  mov   a,$03AA      ;\
$209C: 24 49     and   a,$49        ;} Noise enable flags &= [sound 1 channel 0 voice mask]
$209E: C4 49     mov   $49,a        ;/

; LOOP_CHANNEL_0_COMMANDS
$20A0: 3F 4F 28  call  $284F        ; A = next sound 1 channel 0 data byte
$20A3: 68 FA     cmp   a,#$FA       ;\
$20A5: D0 00     bne   $20A7        ;|
                                    ;} If [A] = F9h:
$20A7: 68 F9     cmp   a,#$F9       ;|
$20A9: D0 14     bne   $20BF        ;/
$20AB: 3F 4F 28  call  $284F        ;\
$20AE: C5 D0 03  mov   $03D0,a      ;} Sound 1 channel 0 ADSR settings = next sound 1 channel 0 data byte
$20B1: 3F 4F 28  call  $284F        ;\
$20B4: C5 D1 03  mov   $03D1,a      ;} Sound 1 channel 0 ADSR settings |= next sound 1 channel 0 data byte << 8
$20B7: E8 FF     mov   a,#$FF       ;\
$20B9: C5 D8 03  mov   $03D8,a      ;} Sound 1 channel 0 update ADSR settings flag = FFh
$20BC: 5F A0 20  jmp   $20A0        ; Go to LOOP_CHANNEL_0_COMMANDS

$20BF: 68 F5     cmp   a,#$F5       ;\
$20C1: D0 05     bne   $20C8        ;} If [A] = F5h:
$20C3: C5 E2 03  mov   $03E2,a      ; Enable sound 1 channel 0 pitch slide legato
$20C6: 2F 09     bra   $20D1

$20C8: 68 F8     cmp   a,#$F8       ;\ Else ([A] != F5h):
$20CA: D0 19     bne   $20E5        ;} If [A] != F8h: go to BRANCH_CHANNEL_0_PITCH_SLIDE_END
$20CC: E8 00     mov   a,#$00       ;\
$20CE: C5 E2 03  mov   $03E2,a      ;} Disable sound 1 channel 0 pitch slide legato

$20D1: 3F 4F 28  call  $284F        ;\
$20D4: C5 DE 03  mov   $03DE,a      ;} Sound 1 channel 0 subnote delta = next sound 1 channel 0 data byte
$20D7: 3F 4F 28  call  $284F        ;\
$20DA: C5 DF 03  mov   $03DF,a      ;} Sound 1 channel 0 target note = next sound 1 channel 0 data byte
$20DD: E8 FF     mov   a,#$FF       ;\
$20DF: C5 E0 03  mov   $03E0,a      ;} Enable sound 1 channel 0 pitch slide = FFh
$20E2: 3F 4F 28  call  $284F        ; A = next sound 1 channel 0 data byte

; BRANCH_CHANNEL_0_PITCH_SLIDE_END
$20E5: 68 FF     cmp   a,#$FF       ;\
$20E7: D0 06     bne   $20EF        ;} If [A] = FFh:
$20E9: 3F 32 27  call  $2732        ; Reset sound 1 channel 0
$20EC: 5F FE 21  jmp   $21FE        ; Go to BRANCH_CHANNEL_0_END

$20EF: 68 FE     cmp   a,#$FE       ;\
$20F1: D0 0F     bne   $2102        ;} If [A] = FEh:
$20F3: 3F 4F 28  call  $284F        ;\
$20F6: C5 C8 03  mov   $03C8,a      ;} Sound 1 channel 0 repeat counter = next sound 1 channel 0 data byte
$20F9: E5 94 03  mov   a,$0394      ;\
$20FC: C5 CC 03  mov   $03CC,a      ;} Sound 1 channel 0 repeat point = [sound 1 channel 0 instruction list index]
$20FF: 3F 4F 28  call  $284F        ; A = next sound 1 channel 0 data byte

$2102: 68 FD     cmp   a,#$FD       ;\
$2104: D0 11     bne   $2117        ;} If [A] != FDh: go to BRANCH_CHANNEL_0_REPEAT_COMMAND
$2106: 8C C8 03  dec   $03C8        ; Decrement sound 1 channel 0 repeat counter
$2109: D0 03     bne   $210E        ; If [sound 1 channel 0 repeat counter] = 0:
$210B: 5F A0 20  jmp   $20A0        ; Go to LOOP_CHANNEL_0_COMMANDS

; LOOP_CHANNEL_0_REPEAT_COMMAND
$210E: E5 CC 03  mov   a,$03CC      ;\
$2111: C5 94 03  mov   $0394,a      ;} Sound 1 channel 0 instruction list index = [sound 1 channel 0 repeat point]
$2114: 3F 4F 28  call  $284F        ; A = next sound 1 channel 0 data byte

; BRANCH_CHANNEL_0_REPEAT_COMMAND
$2117: 68 FB     cmp   a,#$FB       ;\
$2119: D0 03     bne   $211E        ;} If [A] = FBh:
$211B: 5F 0E 21  jmp   $210E        ; Go to LOOP_CHANNEL_0_REPEAT_COMMAND

$211E: 68 FC     cmp   a,#$FC       ;\
$2120: D0 0A     bne   $212C        ;} If [A] = FCh:
$2122: E5 A6 03  mov   a,$03A6      ;\
$2125: 04 49     or    a,$49        ;} Noise enable flags |= [sound 1 channel 0 voice bitset]
$2127: C4 49     mov   $49,a        ;/
$2129: 5F A0 20  jmp   $20A0        ; Go to LOOP_CHANNEL_0_COMMANDS

; Process note instruction
$212C: E9 AF 03  mov   x,$03AF      ; X = [sound 1 channel 0 voice index]
$212F: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$2132: 3F 4F 28  call  $284F        ;\
$2135: E9 AF 03  mov   x,$03AF      ;} Track output volume = next sound 1 channel 0 data byte
$2138: D5 21 03  mov   $0321+x,a    ;/
$213B: E8 00     mov   a,#$00       ;\
$213D: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = 0
$2140: 3F 4F 28  call  $284F        ;\
$2143: C4 11     mov   $11,a        ;} $10 = (next sound 1 channel 0 data byte) * 100h
$2145: 8F 00 10  mov   $10,#$00     ;/
$2148: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled
$214B: 3F 4F 28  call  $284F        ; A = next sound 1 channel 0 data byte
$214E: 68 F6     cmp   a,#$F6       ;\
$2150: F0 08     beq   $215A        ;} If [A] != F6h:
$2152: C5 DC 03  mov   $03DC,a      ; Sound 1 channel 0 note = [A]
$2155: E8 00     mov   a,#$00       ;\
$2157: C5 DD 03  mov   $03DD,a      ;} Sound 1 channel 0 subnote = 0

$215A: EC DC 03  mov   y,$03DC      ;\
$215D: E5 DD 03  mov   a,$03DD      ;} $11.$10 = [sound 1 channel 0 note]
$2160: DA 10     movw  $10,ya       ;/
$2162: E9 AF 03  mov   x,$03AF      ; X = [sound 1 channel 0 voice index]
$2165: 3F B1 16  call  $16B1        ; Play note
$2168: 3F 4F 28  call  $284F        ;\
$216B: C5 98 03  mov   $0398,a      ;} Sound 1 channel 0 instruction timer = next sound 1 channel 0 data byte
$216E: E5 D8 03  mov   a,$03D8      ;\
$2171: F0 18     beq   $218B        ;} If [sound 1 channel 0 update ADSR settings flag] != 0:
$2173: E5 B4 03  mov   a,$03B4      ;\
$2176: 08 05     or    a,#$05       ;|
$2178: FD        mov   y,a          ;|
$2179: E5 D0 03  mov   a,$03D0      ;|
$217C: 3F 26 17  call  $1726        ;|
$217F: E5 B4 03  mov   a,$03B4      ;} DSP sound 1 channel 0 ADSR settings = [sound 1 channel 0 ADSR settings]
$2182: 08 06     or    a,#$06       ;|
$2184: FD        mov   y,a          ;|
$2185: E5 D1 03  mov   a,$03D1      ;|
$2188: 3F 26 17  call  $1726        ;/

$218B: E5 E1 03  mov   a,$03E1      ;\
$218E: D0 07     bne   $2197        ;} If sound 1 channel 0 legato disabled:
$2190: E5 A6 03  mov   a,$03A6      ;\
$2193: 04 45     or    a,$45        ;} Key on flags |= [sound 1 channel 0 voice bitset]
$2195: C4 45     mov   $45,a        ;/

; BRANCH_PROCESS_CHANNEL_0_INSTRUCTION_END
$2197: E5 E0 03  mov   a,$03E0      ;\
$219A: 68 FF     cmp   a,#$FF       ;} If sound 1 channel 0 pitch slide disabled: go to BRANCH_CHANNEL_0_END
$219C: D0 60     bne   $21FE        ;/
$219E: E5 E2 03  mov   a,$03E2      ;\
$21A1: F0 05     beq   $21A8        ;} If sound 1 channel 0 pitch slide legato enabled:
$21A3: E8 FF     mov   a,#$FF       ;\
$21A5: C5 E1 03  mov   $03E1,a      ;} Enable sound 1 channel 0 legato

$21A8: E5 DC 03  mov   a,$03DC      ;\
$21AB: 65 DF 03  cmp   a,$03DF      ;} If [sound 1 channel 0 note] >= [sound 1 channel 0 target note]:
$21AE: 90 21     bcc   $21D1        ;/
$21B0: E5 DD 03  mov   a,$03DD      ;\
$21B3: 80        setc               ;|
$21B4: A5 DE 03  sbc   a,$03DE      ;} Sound 1 channel 0 subnote -= [sound 1 channel 0 subnote delta]
$21B7: C5 DD 03  mov   $03DD,a      ;/
$21BA: B0 34     bcs   $21F0        ; If [sound 1 channel 0 subnote] < 0:
$21BC: 8C DC 03  dec   $03DC        ; Decrement sound 1 channel 0 note
$21BF: E5 DF 03  mov   a,$03DF      ;\
$21C2: 65 DC 03  cmp   a,$03DC      ;} If [sound 1 channel 0 target note] = [sound 1 channel 0 note]:
$21C5: D0 29     bne   $21F0        ;/
$21C7: E8 00     mov   a,#$00       ;\
$21C9: C5 E0 03  mov   $03E0,a      ;} Disable sound 1 channel 0 pitch slide
$21CC: C5 E1 03  mov   $03E1,a      ; Disable sound 1 channel 0 legato
$21CF: 2F 1F     bra   $21F0

$21D1: E5 DE 03  mov   a,$03DE      ;\ Else ([sound 1 channel 0 note] < [sound 1 channel 0 target note]):
$21D4: 60        clrc               ;|
$21D5: 85 DD 03  adc   a,$03DD      ;} Sound 1 channel 0 subnote += [sound 1 channel 0 subnote delta]
$21D8: C5 DD 03  mov   $03DD,a      ;/
$21DB: 90 13     bcc   $21F0        ; If [sound 1 channel 0 subnote] >= 100h:
$21DD: AC DC 03  inc   $03DC        ; Increment sound 1 channel 0 note
$21E0: E5 DF 03  mov   a,$03DF      ;\
$21E3: 65 DC 03  cmp   a,$03DC      ;} If [sound 1 channel 0 target note] = [sound 1 channel 0 note]:
$21E6: D0 08     bne   $21F0        ;/
$21E8: E8 00     mov   a,#$00       ;\
$21EA: C5 E0 03  mov   $03E0,a      ;} Disable sound 1 channel 0 pitch slide
$21ED: C5 E1 03  mov   $03E1,a      ; Disable sound 1 channel 0 legato

$21F0: E5 DD 03  mov   a,$03DD      ;\
$21F3: EC DC 03  mov   y,$03DC      ;} $11.$10 = [sound 1 channel 0 note]
$21F6: DA 10     movw  $10,ya       ;/
$21F8: E9 AF 03  mov   x,$03AF      ; X = [sound 1 channel 0 voice index]
$21FB: 3F B1 16  call  $16B1        ; Play note
}

; BRANCH_CHANNEL_0_END
$21FE: E8 FF     mov   a,#$FF       ;\
$2200: 65 9D 03  cmp   a,$039D      ;} If [sound 1 channel 1 disable byte] = FFh:
$2203: D0 03     bne   $2208        ;/
$2205: 5F BA 23  jmp   $23BA        ; Go to BRANCH_CHANNEL_1_END

; Channel 1
{
$2208: 8C 99 03  dec   $0399        ; Decrement sound 1 channel 1 instruction timer
$220B: F0 03     beq   $2210        ; If [sound 1 channel 1 instruction timer] != 0:
$220D: 5F 4D 23  jmp   $234D        ; Go to BRANCH_PROCESS_CHANNEL_1_INSTRUCTION_END

$2210: E5 E8 03  mov   a,$03E8      ;\
$2213: F0 02     beq   $2217        ;} If sound 1 channel 1 legato flag enabled:
$2215: 2F 43     bra   $225A        ; Go to LOOP_CHANNEL_1_COMMANDS

$2217: E8 00     mov   a,#$00       ;\
$2219: C5 E7 03  mov   $03E7,a      ;} Disable sound 1 channel 1 pitch slide
$221C: C5 E5 03  mov   $03E5,a      ; Sound 1 channel 1 subnote delta = 0
$221F: C5 E6 03  mov   $03E6,a      ; Sound 1 channel 1 target note = 0
$2222: E8 FF     mov   a,#$FF       ;\
$2224: 65 C2 03  cmp   a,$03C2      ;} If [sound 1 channel 1 release flag] != FFh:
$2227: F0 16     beq   $223F        ;/
$2229: E5 A7 03  mov   a,$03A7      ;\
$222C: 04 46     or    a,$46        ;} Key off flags |= [sound 1 channel 1 voice bitset]
$222E: C4 46     mov   $46,a        ;/
$2230: E8 02     mov   a,#$02       ;\
$2232: C5 C3 03  mov   $03C3,a      ;} Sound 1 channel 1 release timer = 2
$2235: E8 01     mov   a,#$01       ;\
$2237: C5 99 03  mov   $0399,a      ;} Sound 1 channel 1 instruction timer = 1
$223A: E8 FF     mov   a,#$FF       ;\
$223C: C5 C2 03  mov   $03C2,a      ;} Sound 1 channel 1 release flag = FFh

$223F: 8C C3 03  dec   $03C3        ; Decrement sound 1 channel 1 release timer
$2242: F0 03     beq   $2247        ; If [sound 1 channel 1 release timer] != 0:
$2244: 5F BA 23  jmp   $23BA        ; Go to BRANCH_CHANNEL_1_END

$2247: E8 00     mov   a,#$00       ;\
$2249: C5 C2 03  mov   $03C2,a      ;} Sound 1 channel 1 release flag = 0
$224C: E5 AB 03  mov   a,$03AB      ;\
$224F: 24 47     and   a,$47        ;} Current music voice bitset &= [sound 1 channel 1 voice mask]
$2251: C4 47     mov   $47,a        ;/
$2253: E5 AB 03  mov   a,$03AB      ;\
$2256: 24 49     and   a,$49        ;} Noise enable flags &= [sound 1 channel 1 voice mask]
$2258: C4 49     mov   $49,a        ;/

; LOOP_CHANNEL_1_COMMANDS
$225A: 3F 58 28  call  $2858        ; A = next sound 1 channel 1 data byte
$225D: 68 F9     cmp   a,#$F9       ;\
$225F: D0 14     bne   $2275        ;} If [A] = F9h:
$2261: 3F 58 28  call  $2858        ;\
$2264: C5 D2 03  mov   $03D2,a      ;} Sound 1 channel 1 ADSR settings = next sound 1 channel 1 data byte
$2267: 3F 58 28  call  $2858        ;\
$226A: C5 D3 03  mov   $03D3,a      ;} Sound 1 channel 1 ADSR settings |= next sound 1 channel 1 data byte << 8
$226D: E8 FF     mov   a,#$FF       ;\
$226F: C5 D9 03  mov   $03D9,a      ;} Sound 1 channel 1 update ADSR settings flag = FFh
$2272: 5F 5A 22  jmp   $225A        ; Go to LOOP_CHANNEL_1_COMMANDS

$2275: 68 F5     cmp   a,#$F5       ;\
$2277: D0 05     bne   $227E        ;} If [A] = F5h:
$2279: C5 E9 03  mov   $03E9,a      ; Enable sound 1 channel 1 pitch slide legato
$227C: 2F 09     bra   $2287

$227E: 68 F8     cmp   a,#$F8       ;\ Else ([A] != F5h):
$2280: D0 19     bne   $229B        ;} If [A] != F8h: go to BRANCH_CHANNEL_1_PITCH_SLIDE_END
$2282: E8 00     mov   a,#$00       ;\
$2284: C5 E9 03  mov   $03E9,a      ;} Disable sound 1 channel 1 pitch slide legato

$2287: 3F 58 28  call  $2858        ;\
$228A: C5 E5 03  mov   $03E5,a      ;} Sound 1 channel 1 subnote delta = next sound 1 channel 1 data byte
$228D: 3F 58 28  call  $2858        ;\
$2290: C5 E6 03  mov   $03E6,a      ;} Sound 1 channel 1 target note = next sound 1 channel 1 data byte
$2293: E8 FF     mov   a,#$FF       ;\
$2295: C5 E7 03  mov   $03E7,a      ;} Enable sound 1 channel 1 pitch slide = FFh
$2298: 3F 58 28  call  $2858        ; A = next sound 1 channel 1 data byte

; BRANCH_CHANNEL_1_PITCH_SLIDE_END
$229B: 68 FF     cmp   a,#$FF       ;\
$229D: D0 06     bne   $22A5        ;} If [A] = FFh:
$229F: 3F 75 27  call  $2775        ; Reset sound 1 channel 1
$22A2: 5F BA 23  jmp   $23BA        ; Go to BRANCH_CHANNEL_1_END

$22A5: 68 FE     cmp   a,#$FE       ;\
$22A7: D0 0F     bne   $22B8        ;} If [A] = FEh:
$22A9: 3F 58 28  call  $2858        ;\
$22AC: C5 C9 03  mov   $03C9,a      ;} Sound 1 channel 1 repeat counter = next sound 1 channel 1 data byte
$22AF: E5 95 03  mov   a,$0395      ;\
$22B2: C5 CD 03  mov   $03CD,a      ;} Sound 1 channel 1 repeat point = [sound 1 channel 1 instruction list index]
$22B5: 3F 58 28  call  $2858        ; A = next sound 1 channel 1 data byte

$22B8: 68 FD     cmp   a,#$FD       ;\
$22BA: D0 11     bne   $22CD        ;} If [A] != FDh: go to BRANCH_CHANNEL_1_REPEAT_COMMAND
$22BC: 8C C9 03  dec   $03C9        ; Decrement sound 1 channel 1 repeat counter
$22BF: D0 03     bne   $22C4        ; If [sound 1 channel 1 repeat counter] = 0:
$22C1: 5F 5A 22  jmp   $225A        ; Go to LOOP_CHANNEL_1_COMMANDS

; LOOP_CHANNEL_1_REPEAT_COMMAND
$22C4: E5 CD 03  mov   a,$03CD      ;\
$22C7: C5 95 03  mov   $0395,a      ;} Sound 1 channel 1 instruction list index = [sound 1 channel 1 repeat point]
$22CA: 3F 58 28  call  $2858        ; A = next sound 1 channel 1 data byte

; BRANCH_CHANNEL_1_REPEAT_COMMAND
$22CD: 68 FB     cmp   a,#$FB       ;\
$22CF: D0 03     bne   $22D4        ;} If [A] = FBh:
$22D1: 5F C4 22  jmp   $22C4        ; Go to LOOP_CHANNEL_1_REPEAT_COMMAND

$22D4: 68 FC     cmp   a,#$FC       ;\
$22D6: D0 0A     bne   $22E2        ;} If [A] = FCh:
$22D8: E5 A7 03  mov   a,$03A7      ;\
$22DB: 04 49     or    a,$49        ;} Noise enable flags |= [sound 1 channel 1 voice bitset]
$22DD: C4 49     mov   $49,a        ;/
$22DF: 5F 5A 22  jmp   $225A        ; Go to LOOP_CHANNEL_1_COMMANDS

$22E2: E9 B0 03  mov   x,$03B0      ; X = [sound 1 channel 1 voice index]
$22E5: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$22E8: 3F 58 28  call  $2858        ;\
$22EB: E9 B0 03  mov   x,$03B0      ;} Track output volume = next sound 1 channel 1 data byte
$22EE: D5 21 03  mov   $0321+x,a    ;/
$22F1: E8 00     mov   a,#$00       ;\
$22F3: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = 0
$22F6: 3F 58 28  call  $2858        ;\
$22F9: C4 11     mov   $11,a        ;} $10 = (next sound 1 channel 1 data byte) * 100h
$22FB: 8F 00 10  mov   $10,#$00     ;/
$22FE: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled
$2301: 3F 58 28  call  $2858        ; A = next sound 1 channel 1 data byte
$2304: 68 F6     cmp   a,#$F6       ;\
$2306: F0 08     beq   $2310        ;} If [A] != F6h:
$2308: C5 E3 03  mov   $03E3,a      ; Sound 1 channel 1 note = [A]
$230B: E8 00     mov   a,#$00       ;\
$230D: C5 E4 03  mov   $03E4,a      ;} Sound 1 channel 1 subnote = 0

$2310: EC E3 03  mov   y,$03E3      ;\
$2313: E5 E4 03  mov   a,$03E4      ;} $11.$10 = [sound 1 channel 1 note]
$2316: DA 10     movw  $10,ya       ;/
$2318: E9 B0 03  mov   x,$03B0      ; X = [sound 1 channel 1 voice index]
$231B: 3F B1 16  call  $16B1        ; Play note
$231E: 3F 58 28  call  $2858        ;\
$2321: C5 99 03  mov   $0399,a      ;} Sound 1 channel 1 instruction timer = next sound 1 channel 1 data byte
$2324: E5 D9 03  mov   a,$03D9      ;\
$2327: F0 18     beq   $2341        ;} If [sound 1 channel 1 update ADSR settings flag] != 0:
$2329: E5 B5 03  mov   a,$03B5      ;\
$232C: 08 05     or    a,#$05       ;|
$232E: FD        mov   y,a          ;|
$232F: E5 D2 03  mov   a,$03D2      ;|
$2332: 3F 26 17  call  $1726        ;|
$2335: E5 B5 03  mov   a,$03B5      ;} DSP sound 1 channel 1 ADSR settings = [sound 1 channel 1 ADSR settings]
$2338: 08 06     or    a,#$06       ;|
$233A: FD        mov   y,a          ;|
$233B: E5 D3 03  mov   a,$03D3      ;|
$233E: 3F 26 17  call  $1726        ;/

$2341: E5 E8 03  mov   a,$03E8      ;\
$2344: D0 07     bne   $234D        ;} If sound 1 channel 1 legato disabled:
$2346: E5 A7 03  mov   a,$03A7      ;\
$2349: 04 45     or    a,$45        ;} Key on flags |= [sound 1 channel 1 voice bitset]
$234B: C4 45     mov   $45,a        ;/

; BRANCH_PROCESS_CHANNEL_1_INSTRUCTION_END
$234D: 00        nop
$234E: 00        nop
$234F: 00        nop
$2350: 00        nop
$2351: 00        nop
$2352: 00        nop
$2353: E5 E7 03  mov   a,$03E7      ;\
$2356: 68 FF     cmp   a,#$FF       ;} If sound 1 channel 1 pitch slide disabled: go to BRANCH_CHANNEL_1_END
$2358: D0 60     bne   $23BA        ;/
$235A: E5 E9 03  mov   a,$03E9      ;\
$235D: F0 05     beq   $2364        ;} If sound 1 channel 1 pitch slide legato enabled:
$235F: E8 FF     mov   a,#$FF       ;\
$2361: C5 E8 03  mov   $03E8,a      ;} Enable sound 1 channel 1 legato

$2364: E5 E3 03  mov   a,$03E3      ;\
$2367: 65 E6 03  cmp   a,$03E6      ;} If [sound 1 channel 1 note] >= [sound 1 channel 1 target note]:
$236A: 90 21     bcc   $238D        ;/
$236C: E5 E4 03  mov   a,$03E4      ;\
$236F: 80        setc               ;|
$2370: A5 E5 03  sbc   a,$03E5      ;} Sound 1 channel 1 subnote -= [sound 1 channel 1 subnote delta]
$2373: C5 E4 03  mov   $03E4,a      ;/
$2376: B0 34     bcs   $23AC        ; If [sound 1 channel 1 subnote] < 0:
$2378: 8C E3 03  dec   $03E3        ; Decrement sound 1 channel 1 note
$237B: E5 E6 03  mov   a,$03E6      ;\
$237E: 65 E3 03  cmp   a,$03E3      ;} If [sound 1 channel 1 target note] = [sound 1 channel 1 note]:
$2381: D0 29     bne   $23AC        ;/
$2383: E8 00     mov   a,#$00       ;\
$2385: C5 E7 03  mov   $03E7,a      ;} Disable sound 1 channel 1 pitch slide
$2388: C5 E8 03  mov   $03E8,a      ; Disable sound 1 channel 1 legato
$238B: 2F 1F     bra   $23AC

$238D: E5 E5 03  mov   a,$03E5      ;\ Else ([sound 1 channel 1 note] < [sound 1 channel 1 target note]):
$2390: 60        clrc               ;|
$2391: 85 E4 03  adc   a,$03E4      ;} Sound 1 channel 1 subnote += [sound 1 channel 1 subnote delta]
$2394: C5 E4 03  mov   $03E4,a      ;/
$2397: 90 13     bcc   $23AC        ; If [sound 1 channel 1 subnote] >= 100h:
$2399: AC E3 03  inc   $03E3        ; Increment sound 1 channel 1 note
$239C: E5 E6 03  mov   a,$03E6      ;\
$239F: 65 E3 03  cmp   a,$03E3      ;} If [sound 1 channel 1 target note] = [sound 1 channel 1 note]:
$23A2: D0 08     bne   $23AC        ;/
$23A4: E8 00     mov   a,#$00       ;\
$23A6: C5 E7 03  mov   $03E7,a      ;} Disable sound 1 channel 1 pitch slide
$23A9: C5 E8 03  mov   $03E8,a      ; Disable sound 1 channel 1 legato

$23AC: E5 E4 03  mov   a,$03E4      ;\
$23AF: EC E3 03  mov   y,$03E3      ;} $11.$10 = [sound 1 channel 1 note]
$23B2: DA 10     movw  $10,ya       ;/
$23B4: E9 B0 03  mov   x,$03B0      ; X = [sound 1 channel 1 voice index]
$23B7: 3F B1 16  call  $16B1        ; Play note
}

; BRANCH_CHANNEL_1_END
$23BA: E8 FF     mov   a,#$FF       ;\
$23BC: 65 9E 03  cmp   a,$039E      ;} If [sound 1 channel 2 disable byte] = FFh:
$23BF: D0 03     bne   $23C4        ;/
$23C1: 5F 76 25  jmp   $2576        ; Go to BRANCH_CHANNEL_2_END

; Channel 2
{
$23C4: 8C 9A 03  dec   $039A        ; Decrement sound 1 channel 2 instruction timer
$23C7: F0 03     beq   $23CC        ; If [sound 1 channel 2 instruction timer] != 0:
$23C9: 5F 09 25  jmp   $2509        ; Go to BRANCH_PROCESS_CHANNEL_2_INSTRUCTION_END

$23CC: E5 EF 03  mov   a,$03EF      ;\
$23CF: F0 02     beq   $23D3        ;} If sound 1 channel 2 legato flag enabled:
$23D1: 2F 43     bra   $2416        ; Go to LOOP_CHANNEL_2_COMMANDS

$23D3: E8 00     mov   a,#$00       ;\
$23D5: C5 EE 03  mov   $03EE,a      ;} Disable sound 1 channel 2 pitch slide
$23D8: C5 EC 03  mov   $03EC,a      ; Sound 1 channel 2 subnote delta = 0
$23DB: C5 ED 03  mov   $03ED,a      ; Sound 1 channel 2 target note = 0
$23DE: E8 FF     mov   a,#$FF       ;\
$23E0: 65 C4 03  cmp   a,$03C4      ;} If [sound 1 channel 2 release flag] != FFh:
$23E3: F0 16     beq   $23FB        ;/
$23E5: E5 A8 03  mov   a,$03A8      ;\
$23E8: 04 46     or    a,$46        ;} Key off flags |= [sound 1 channel 2 voice bitset]
$23EA: C4 46     mov   $46,a        ;/
$23EC: E8 02     mov   a,#$02       ;\
$23EE: C5 C5 03  mov   $03C5,a      ;} Sound 1 channel 2 release timer = 2
$23F1: E8 01     mov   a,#$01       ;\
$23F3: C5 9A 03  mov   $039A,a      ;} Sound 1 channel 2 instruction timer = 1
$23F6: E8 FF     mov   a,#$FF       ;\
$23F8: C5 C4 03  mov   $03C4,a      ;} Sound 1 channel 2 release flag = FFh

$23FB: 8C C5 03  dec   $03C5        ; Decrement sound 1 channel 2 release timer
$23FE: F0 03     beq   $2403        ; If [sound 1 channel 2 release timer] != 0:
$2400: 5F 76 25  jmp   $2576        ; Go to BRANCH_CHANNEL_2_END

$2403: E8 00     mov   a,#$00       ;\
$2405: C5 C4 03  mov   $03C4,a      ;} Sound 1 channel 2 release flag = 0
$2408: E5 AC 03  mov   a,$03AC      ;\
$240B: 24 47     and   a,$47        ;} Current music voice bitset &= [sound 1 channel 2 voice mask]
$240D: C4 47     mov   $47,a        ;/
$240F: E5 AC 03  mov   a,$03AC      ;\
$2412: 24 49     and   a,$49        ;} Noise enable flags &= [sound 1 channel 2 voice mask]
$2414: C4 49     mov   $49,a        ;/

; LOOP_CHANNEL_2_COMMANDS
$2416: 3F 61 28  call  $2861        ; A = next sound 1 channel 2 data byte
$2419: 68 F9     cmp   a,#$F9       ;\
$241B: D0 14     bne   $2431        ;} If [A] = F9h:
$241D: 3F 61 28  call  $2861        ;\
$2420: C5 D4 03  mov   $03D4,a      ;} Sound 1 channel 2 ADSR settings = next sound 1 channel 2 data byte
$2423: 3F 61 28  call  $2861        ;\
$2426: C5 D5 03  mov   $03D5,a      ;} Sound 1 channel 2 ADSR settings |= next sound 1 channel 2 data byte << 8
$2429: E8 FF     mov   a,#$FF       ;\
$242B: C5 DA 03  mov   $03DA,a      ;} Sound 1 channel 2 update ADSR settings flag = FFh
$242E: 5F 16 24  jmp   $2416        ; Go to LOOP_CHANNEL_2_COMMANDS

$2431: 68 F5     cmp   a,#$F5       ;\
$2433: D0 05     bne   $243A        ;} If [A] = F5h:
$2435: C5 F0 03  mov   $03F0,a      ; Enable sound 1 channel 2 pitch slide legato
$2438: 2F 09     bra   $2443

$243A: 68 F8     cmp   a,#$F8       ;\ Else ([A] != F5h):
$243C: D0 19     bne   $2457        ;} If [A] != F8h: go to BRANCH_CHANNEL_2_PITCH_SLIDE_END
$243E: E8 00     mov   a,#$00       ;\
$2440: C5 F0 03  mov   $03F0,a      ;} Disable sound 1 channel 2 pitch slide legato

$2443: 3F 61 28  call  $2861        ;\
$2446: C5 EC 03  mov   $03EC,a      ;} Sound 1 channel 2 subnote delta = next sound 1 channel 2 data byte
$2449: 3F 61 28  call  $2861        ;\
$244C: C5 ED 03  mov   $03ED,a      ;} Sound 1 channel 2 target note = next sound 1 channel 2 data byte
$244F: E8 FF     mov   a,#$FF       ;\
$2451: C5 EE 03  mov   $03EE,a      ;} Enable sound 1 channel 2 pitch slide = FFh
$2454: 3F 61 28  call  $2861        ; A = next sound 1 channel 2 data byte

; BRANCH_CHANNEL_2_PITCH_SLIDE_END
$2457: 68 FF     cmp   a,#$FF       ;\
$2459: D0 06     bne   $2461        ;} If [A] = FFh:
$245B: 3F B8 27  call  $27B8        ; Reset sound 1 channel 2
$245E: 5F 76 25  jmp   $2576        ; Go to BRANCH_CHANNEL_2_END

$2461: 68 FE     cmp   a,#$FE       ;\
$2463: D0 0F     bne   $2474        ;} If [A] = FEh:
$2465: 3F 61 28  call  $2861        ;\
$2468: C5 CA 03  mov   $03CA,a      ;} Sound 1 channel 2 repeat counter = next sound 1 channel 2 data byte
$246B: E5 96 03  mov   a,$0396      ;\
$246E: C5 CE 03  mov   $03CE,a      ;} Sound 1 channel 2 repeat point = [sound 1 channel 2 instruction list index]
$2471: 3F 61 28  call  $2861        ; A = next sound 1 channel 2 data byte

$2474: 68 FD     cmp   a,#$FD       ;\
$2476: D0 11     bne   $2489        ;} If [A] != FDh: go to BRANCH_CHANNEL_2_REPEAT_COMMAND
$2478: 8C CA 03  dec   $03CA        ; Decrement sound 1 channel 2 repeat counter
$247B: D0 03     bne   $2480        ; If [sound 1 channel 2 repeat counter] = 0:
$247D: 5F 16 24  jmp   $2416        ; Go to LOOP_CHANNEL_2_COMMANDS

; LOOP_CHANNEL_2_REPEAT_COMMAND
$2480: E5 CE 03  mov   a,$03CE      ;\
$2483: C5 96 03  mov   $0396,a      ;} Sound 1 channel 2 instruction list index = [sound 1 channel 2 repeat point]
$2486: 3F 61 28  call  $2861        ; A = next sound 1 channel 2 data byte

; BRANCH_CHANNEL_2_REPEAT_COMMAND
$2489: 68 FB     cmp   a,#$FB       ;\
$248B: D0 03     bne   $2490        ;} If [A] = FBh:
$248D: 5F 80 24  jmp   $2480        ; Go to LOOP_CHANNEL_2_REPEAT_COMMAND

$2490: 68 FC     cmp   a,#$FC       ;\
$2492: D0 0A     bne   $249E        ;} If [A] = FCh:
$2494: E5 A8 03  mov   a,$03A8      ;\
$2497: 04 49     or    a,$49        ;} Noise enable flags |= [sound 1 channel 2 voice bitset]
$2499: C4 49     mov   $49,a        ;/
$249B: 5F 16 24  jmp   $2416        ; Go to LOOP_CHANNEL_2_COMMANDS

$249E: E9 B1 03  mov   x,$03B1      ; X = [sound 1 channel 2 voice index]
$24A1: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$24A4: 3F 61 28  call  $2861        ;\
$24A7: E9 B1 03  mov   x,$03B1      ;} Track output volume = next sound 1 channel 2 data byte
$24AA: D5 21 03  mov   $0321+x,a    ;/
$24AD: E8 00     mov   a,#$00       ;\
$24AF: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = 0
$24B2: 3F 61 28  call  $2861        ;\
$24B5: C4 11     mov   $11,a        ;} $10 = (next sound 1 channel 2 data byte) * 100h
$24B7: 8F 00 10  mov   $10,#$00     ;/
$24BA: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled
$24BD: 3F 61 28  call  $2861        ; A = next sound 1 channel 2 data byte
$24C0: 68 F6     cmp   a,#$F6       ;\
$24C2: F0 08     beq   $24CC        ;} If [A] != F6h:
$24C4: C5 EA 03  mov   $03EA,a      ; Sound 1 channel 2 note = [A]
$24C7: E8 00     mov   a,#$00       ;\
$24C9: C5 EB 03  mov   $03EB,a      ;} Sound 1 channel 2 subnote = 0

$24CC: EC EA 03  mov   y,$03EA      ;\
$24CF: E5 EB 03  mov   a,$03EB      ;} $11.$10 = [sound 1 channel 2 note]
$24D2: DA 10     movw  $10,ya       ;/
$24D4: E9 B1 03  mov   x,$03B1      ; X = [sound 1 channel 2 voice index]
$24D7: 3F B1 16  call  $16B1        ; Play note
$24DA: 3F 61 28  call  $2861        ;\
$24DD: C5 9A 03  mov   $039A,a      ;} Sound 1 channel 2 instruction timer = next sound 1 channel 2 data byte
$24E0: E5 DA 03  mov   a,$03DA      ;\
$24E3: F0 18     beq   $24FD        ;} If [sound 1 channel 2 update ADSR settings flag] != 0:
$24E5: E5 B6 03  mov   a,$03B6      ;\
$24E8: 08 05     or    a,#$05       ;|
$24EA: FD        mov   y,a          ;|
$24EB: E5 D4 03  mov   a,$03D4      ;|
$24EE: 3F 26 17  call  $1726        ;|
$24F1: E5 B6 03  mov   a,$03B6      ;} DSP sound 1 channel 2 ADSR settings = [sound 1 channel 2 ADSR settings]
$24F4: 08 06     or    a,#$06       ;|
$24F6: FD        mov   y,a          ;|
$24F7: E5 D5 03  mov   a,$03D5      ;|
$24FA: 3F 26 17  call  $1726        ;/

$24FD: E5 EF 03  mov   a,$03EF      ;\
$2500: D0 07     bne   $2509        ;} If sound 1 channel 2 legato disabled:
$2502: E5 A8 03  mov   a,$03A8      ;\
$2505: 04 45     or    a,$45        ;} Key on flags |= [sound 1 channel 2 voice bitset]
$2507: C4 45     mov   $45,a        ;/

; BRANCH_PROCESS_CHANNEL_2_INSTRUCTION_END
$2509: 00        nop
$250A: 00        nop
$250B: 00        nop
$250C: 00        nop
$250D: 00        nop
$250E: 00        nop
$250F: E5 EE 03  mov   a,$03EE      ;\
$2512: 68 FF     cmp   a,#$FF       ;} If sound 1 channel 2 pitch slide disabled: go to BRANCH_CHANNEL_2_END
$2514: D0 60     bne   $2576        ;/
$2516: E5 F0 03  mov   a,$03F0      ;\
$2519: F0 05     beq   $2520        ;} If sound 1 channel 2 pitch slide legato enabled:
$251B: E8 FF     mov   a,#$FF       ;\
$251D: C5 EF 03  mov   $03EF,a      ;} Enable sound 1 channel 2 legato

$2520: E5 EA 03  mov   a,$03EA      ;\
$2523: 65 ED 03  cmp   a,$03ED      ;} If [sound 1 channel 2 note] >= [sound 1 channel 2 target note]:
$2526: 90 21     bcc   $2549        ;/
$2528: E5 EB 03  mov   a,$03EB      ;\
$252B: 80        setc               ;|
$252C: A5 EC 03  sbc   a,$03EC      ;} Sound 1 channel 2 subnote -= [sound 1 channel 2 subnote delta]
$252F: C5 EB 03  mov   $03EB,a      ;/
$2532: B0 34     bcs   $2568        ; If [sound 1 channel 2 subnote] < 0:
$2534: 8C EA 03  dec   $03EA        ; Decrement sound 1 channel 2 note
$2537: E5 ED 03  mov   a,$03ED      ;\
$253A: 65 EA 03  cmp   a,$03EA      ;} If [sound 1 channel 2 target note] = [sound 1 channel 2 note]:
$253D: D0 29     bne   $2568        ;/
$253F: E8 00     mov   a,#$00       ;\
$2541: C5 EE 03  mov   $03EE,a      ;} Disable sound 1 channel 2 pitch slide
$2544: C5 EF 03  mov   $03EF,a      ; Disable sound 1 channel 2 legato
$2547: 2F 1F     bra   $2568

$2549: E5 EC 03  mov   a,$03EC      ;\ Else ([sound 1 channel 2 note] < [sound 1 channel 2 target note]):
$254C: 60        clrc               ;|
$254D: 85 EB 03  adc   a,$03EB      ;} Sound 1 channel 2 subnote += [sound 1 channel 2 subnote delta]
$2550: C5 EB 03  mov   $03EB,a      ;/
$2553: 90 13     bcc   $2568        ; If [sound 1 channel 2 subnote] >= 100h:
$2555: AC EA 03  inc   $03EA        ; Increment sound 1 channel 2 note
$2558: E5 ED 03  mov   a,$03ED      ;\
$255B: 65 EA 03  cmp   a,$03EA      ;} If [sound 1 channel 2 target note] = [sound 1 channel 2 note]:
$255E: D0 08     bne   $2568        ;/
$2560: E8 00     mov   a,#$00       ;\
$2562: C5 EE 03  mov   $03EE,a      ;} Disable sound 1 channel 2 pitch slide
$2565: C5 EF 03  mov   $03EF,a      ; Disable sound 1 channel 2 legato

$2568: E5 EB 03  mov   a,$03EB      ;\
$256B: EC EA 03  mov   y,$03EA      ;} $11.$10 = [sound 1 channel 2 note]
$256E: DA 10     movw  $10,ya       ;/
$2570: E9 B1 03  mov   x,$03B1      ; X = [sound 1 channel 2 voice index]
$2573: 3F B1 16  call  $16B1        ; Play note
}

; BRANCH_CHANNEL_2_END
$2576: E8 FF     mov   a,#$FF       ;\
$2578: 65 9F 03  cmp   a,$039F      ;} If [sound 1 channel 3 disable byte] = FFh:
$257B: D0 03     bne   $2580        ;/
$257D: 5F 31 27  jmp   $2731        ; Return

; Channel 3
{
$2580: 8C 9B 03  dec   $039B        ; Decrement sound 1 channel 3 instruction timer
$2583: F0 03     beq   $2588        ; If [sound 1 channel 3 instruction timer] != 0:
$2585: 5F C5 26  jmp   $26C5        ; Go to BRANCH_PROCESS_CHANNEL_3_INSTRUCTION_END

$2588: E5 F6 03  mov   a,$03F6      ;\
$258B: F0 02     beq   $258F        ;} If sound 1 channel 3 legato flag enabled:
$258D: 2F 43     bra   $25D2        ; Go to LOOP_CHANNEL_3_COMMANDS

$258F: E8 00     mov   a,#$00       ;\
$2591: C5 F5 03  mov   $03F5,a      ;} Disable sound 1 channel 3 pitch slide
$2594: C5 F3 03  mov   $03F3,a      ; Sound 1 channel 3 subnote delta = 0
$2597: C5 F4 03  mov   $03F4,a      ; Sound 1 channel 3 target note = 0
$259A: E8 FF     mov   a,#$FF       ;\
$259C: 65 C6 03  cmp   a,$03C6      ;} If [sound 1 channel 3 release flag] != FFh:
$259F: F0 16     beq   $25B7        ;/
$25A1: E5 A9 03  mov   a,$03A9      ;\
$25A4: 04 46     or    a,$46        ;} Key off flags |= [sound 1 channel 3 voice bitset]
$25A6: C4 46     mov   $46,a        ;/
$25A8: E8 02     mov   a,#$02       ;\
$25AA: C5 C7 03  mov   $03C7,a      ;} Sound 1 channel 3 release timer = 2
$25AD: E8 01     mov   a,#$01       ;\
$25AF: C5 9B 03  mov   $039B,a      ;} Sound 1 channel 3 instruction timer = 1
$25B2: E8 FF     mov   a,#$FF       ;\
$25B4: C5 C6 03  mov   $03C6,a      ;} Sound 1 channel 3 release flag = FFh

$25B7: 8C C7 03  dec   $03C7        ; Decrement sound 1 channel 3 release timer
$25BA: F0 03     beq   $25BF        ; If [sound 1 channel 3 release timer] != 0:
$25BC: 5F 31 27  jmp   $2731        ; Return

$25BF: E8 00     mov   a,#$00       ;\
$25C1: C5 C6 03  mov   $03C6,a      ;} Sound 1 channel 3 release flag = 0
$25C4: E5 AD 03  mov   a,$03AD      ;\
$25C7: 24 47     and   a,$47        ;} Current music voice bitset &= [sound 1 channel 3 voice mask]
$25C9: C4 47     mov   $47,a        ;/
$25CB: E5 AD 03  mov   a,$03AD      ;\
$25CE: 24 49     and   a,$49        ;} Noise enable flags &= [sound 1 channel 3 voice mask]
$25D0: C4 49     mov   $49,a        ;/

; LOOP_CHANNEL_3_COMMANDS
$25D2: 3F 6A 28  call  $286A        ; A = next sound 1 channel 3 data byte
$25D5: 68 F9     cmp   a,#$F9       ;\
$25D7: D0 14     bne   $25ED        ;} If [A] = F9h:
$25D9: 3F 6A 28  call  $286A        ;\
$25DC: C5 D6 03  mov   $03D6,a      ;} Sound 1 channel 3 ADSR settings = next sound 1 channel 3 data byte
$25DF: 3F 6A 28  call  $286A        ;\
$25E2: C5 D7 03  mov   $03D7,a      ;} Sound 1 channel 3 ADSR settings |= next sound 1 channel 3 data byte << 8
$25E5: E8 FF     mov   a,#$FF       ;\
$25E7: C5 DB 03  mov   $03DB,a      ;} Sound 1 channel 3 update ADSR settings flag = FFh
$25EA: 5F D2 25  jmp   $25D2        ; Go to LOOP_CHANNEL_3_COMMANDS

$25ED: 68 F5     cmp   a,#$F5       ;\
$25EF: D0 05     bne   $25F6        ;} If [A] = F5h:
$25F1: C5 F7 03  mov   $03F7,a      ; Enable sound 1 channel 3 pitch slide legato
$25F4: 2F 09     bra   $25FF

$25F6: 68 F8     cmp   a,#$F8       ;\ Else ([A] != F5h):
$25F8: D0 19     bne   $2613        ;} If [A] != F8h: go to BRANCH_CHANNEL_3_PITCH_SLIDE_END
$25FA: E8 00     mov   a,#$00       ;\
$25FC: C5 F7 03  mov   $03F7,a      ;} Disable sound 1 channel 3 pitch slide legato

$25FF: 3F 6A 28  call  $286A        ;\
$2602: C5 F3 03  mov   $03F3,a      ;} Sound 1 channel 3 subnote delta = next sound 1 channel 3 data byte
$2605: 3F 6A 28  call  $286A        ;\
$2608: C5 F4 03  mov   $03F4,a      ;} Sound 1 channel 3 target note = next sound 1 channel 3 data byte
$260B: E8 FF     mov   a,#$FF       ;\
$260D: C5 F5 03  mov   $03F5,a      ;} Enable sound 1 channel 3 pitch slide = FFh
$2610: 3F 6A 28  call  $286A        ; A = next sound 1 channel 3 data byte

; BRANCH_CHANNEL_3_PITCH_SLIDE_END
$2613: 68 FF     cmp   a,#$FF       ;\
$2615: D0 06     bne   $261D        ;} If [A] = FFh:
$2617: 3F FB 27  call  $27FB        ; Reset sound 1 channel 3
$261A: 5F 31 27  jmp   $2731        ; Return

$261D: 68 FE     cmp   a,#$FE       ;\
$261F: D0 0F     bne   $2630        ;} If [A] = FEh:
$2621: 3F 6A 28  call  $286A        ;\
$2624: C5 CB 03  mov   $03CB,a      ;} Sound 1 channel 3 repeat counter = next sound 1 channel 3 data byte
$2627: E5 97 03  mov   a,$0397      ;\
$262A: C5 CF 03  mov   $03CF,a      ;} Sound 1 channel 3 repeat point = [sound 1 channel 3 instruction list index]
$262D: 3F 6A 28  call  $286A        ; A = next sound 1 channel 3 data byte

$2630: 68 FD     cmp   a,#$FD       ;\
$2632: D0 11     bne   $2645        ;} If [A] != FDh: go to BRANCH_CHANNEL_3_REPEAT_COMMAND
$2634: 8C CB 03  dec   $03CB        ; Decrement sound 1 channel 3 repeat counter
$2637: D0 03     bne   $263C        ; If [sound 1 channel 3 repeat counter] = 0:
$2639: 5F D2 25  jmp   $25D2        ; Go to LOOP_CHANNEL_3_COMMANDS

; LOOP_CHANNEL_3_REPEAT_COMMAND
$263C: E5 CF 03  mov   a,$03CF      ;\
$263F: C5 97 03  mov   $0397,a      ;} Sound 1 channel 3 instruction list index = [sound 1 channel 3 repeat point]
$2642: 3F 6A 28  call  $286A        ; A = next sound 1 channel 3 data byte

; BRANCH_CHANNEL_3_REPEAT_COMMAND
$2645: 68 FB     cmp   a,#$FB       ;\
$2647: D0 03     bne   $264C        ;} If [A] = FBh:
$2649: 5F 3C 26  jmp   $263C        ; Go to LOOP_CHANNEL_3_REPEAT_COMMAND

$264C: 68 FC     cmp   a,#$FC       ;\
$264E: D0 0A     bne   $265A        ;} If [A] = FCh:
$2650: E5 A9 03  mov   a,$03A9      ;\
$2653: 04 49     or    a,$49        ;} Noise enable flags |= [sound 1 channel 3 voice bitset]
$2655: C4 49     mov   $49,a        ;/
$2657: 5F D2 25  jmp   $25D2        ; Go to LOOP_CHANNEL_3_COMMANDS

$265A: E9 B2 03  mov   x,$03B2      ; X = [sound 1 channel 3 voice index]
$265D: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$2660: 3F 6A 28  call  $286A        ;\
$2663: E9 B2 03  mov   x,$03B2      ;} Track output volume = next sound 1 channel 3 data byte
$2666: D5 21 03  mov   $0321+x,a    ;/
$2669: E8 00     mov   a,#$00       ;\
$266B: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = 0
$266E: 3F 6A 28  call  $286A        ;\
$2671: C4 11     mov   $11,a        ;} $10 = (next sound 1 channel 3 data byte) * 100h
$2673: 8F 00 10  mov   $10,#$00     ;/
$2676: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled
$2679: 3F 6A 28  call  $286A        ; A = next sound 1 channel 3 data byte
$267C: 68 F6     cmp   a,#$F6       ;\
$267E: F0 08     beq   $2688        ;} If [A] != F6h:
$2680: C5 F1 03  mov   $03F1,a      ; Sound 1 channel 3 note = [A]
$2683: E8 00     mov   a,#$00       ;\
$2685: C5 F2 03  mov   $03F2,a      ;} Sound 1 channel 3 subnote = 0

$2688: EC F1 03  mov   y,$03F1      ;\
$268B: E5 F2 03  mov   a,$03F2      ;} $11.$10 = [sound 1 channel 3 note]
$268E: DA 10     movw  $10,ya       ;/
$2690: E9 B2 03  mov   x,$03B2      ; X = [sound 1 channel 3 voice index]
$2693: 3F B1 16  call  $16B1        ; Play note
$2696: 3F 6A 28  call  $286A        ;\
$2699: C5 9B 03  mov   $039B,a      ;} Sound 1 channel 3 instruction timer = next sound 1 channel 3 data byte
$269C: E5 DB 03  mov   a,$03DB      ;\
$269F: F0 18     beq   $26B9        ;} If [sound 1 channel 3 update ADSR settings flag] != 0:
$26A1: E5 B7 03  mov   a,$03B7      ;\
$26A4: 08 05     or    a,#$05       ;|
$26A6: FD        mov   y,a          ;|
$26A7: E5 D6 03  mov   a,$03D6      ;|
$26AA: 3F 26 17  call  $1726        ;|
$26AD: E5 B7 03  mov   a,$03B7      ;} DSP sound 1 channel 3 ADSR settings = [sound 1 channel 3 ADSR settings]
$26B0: 08 06     or    a,#$06       ;|
$26B2: FD        mov   y,a          ;|
$26B3: E5 D7 03  mov   a,$03D7      ;|
$26B6: 3F 26 17  call  $1726        ;/

$26B9: E5 F6 03  mov   a,$03F6      ;\
$26BC: D0 07     bne   $26C5        ;} If sound 1 channel 3 legato disabled:
$26BE: E5 A9 03  mov   a,$03A9      ;\
$26C1: 04 45     or    a,$45        ;} Key on flags |= [sound 1 channel 3 voice bitset]
$26C3: C4 45     mov   $45,a        ;/

; BRANCH_PROCESS_CHANNEL_3_INSTRUCTION_END
$26C5: 00        nop
$26C6: 00        nop
$26C7: 00        nop
$26C8: 00        nop
$26C9: 00        nop
$26CA: E5 F5 03  mov   a,$03F5      ;\
$26CD: 68 FF     cmp   a,#$FF       ;} If sound 1 channel 3 pitch slide disabled: return
$26CF: D0 60     bne   $2731        ;/
$26D1: E5 F7 03  mov   a,$03F7      ;\
$26D4: F0 05     beq   $26DB        ;} If sound 1 channel 3 pitch slide legato enabled:
$26D6: E8 FF     mov   a,#$FF       ;\
$26D8: C5 F6 03  mov   $03F6,a      ;} Enable sound 1 channel 3 legato

$26DB: E5 F1 03  mov   a,$03F1      ;\
$26DE: 65 F4 03  cmp   a,$03F4      ;} If [sound 1 channel 3 note] >= [sound 1 channel 3 target note]:
$26E1: 90 21     bcc   $2704        ;/
$26E3: E5 F2 03  mov   a,$03F2      ;\
$26E6: 80        setc               ;|
$26E7: A5 F3 03  sbc   a,$03F3      ;} Sound 1 channel 3 subnote -= [sound 1 channel 3 subnote delta]
$26EA: C5 F2 03  mov   $03F2,a      ;/
$26ED: B0 34     bcs   $2723        ; If [sound 1 channel 3 subnote] < 0:
$26EF: 8C F1 03  dec   $03F1        ; Decrement sound 1 channel 3 note
$26F2: E5 F4 03  mov   a,$03F4      ;\
$26F5: 65 F1 03  cmp   a,$03F1      ;} If [sound 1 channel 3 target note] = [sound 1 channel 3 note]:
$26F8: D0 29     bne   $2723        ;/
$26FA: E8 00     mov   a,#$00       ;\
$26FC: C5 F5 03  mov   $03F5,a      ;} Disable sound 1 channel 3 pitch slide
$26FF: C5 F6 03  mov   $03F6,a      ; Disable sound 1 channel 3 legato
$2702: 2F 1F     bra   $2723

$2704: E5 F3 03  mov   a,$03F3      ;\ Else ([sound 1 channel 3 note] < [sound 1 channel 3 target note]):
$2707: 60        clrc               ;|
$2708: 85 F2 03  adc   a,$03F2      ;} Sound 1 channel 3 subnote += [sound 1 channel 3 subnote delta]
$270B: C5 F2 03  mov   $03F2,a      ;/
$270E: 90 13     bcc   $2723        ; If [sound 1 channel 3 subnote] >= 100h:
$2710: AC F1 03  inc   $03F1        ; Increment sound 1 channel 3 note
$2713: E5 F4 03  mov   a,$03F4      ;\
$2716: 65 F1 03  cmp   a,$03F1      ;} If [sound 1 channel 3 target note] = [sound 1 channel 3 note]:
$2719: D0 08     bne   $2723        ;/
$271B: E8 00     mov   a,#$00       ;\
$271D: C5 F5 03  mov   $03F5,a      ;} Disable sound 1 channel 3 pitch slide
$2720: C5 F6 03  mov   $03F6,a      ; Disable sound 1 channel 3 legato

$2723: E5 F2 03  mov   a,$03F2      ;\
$2726: EC F1 03  mov   y,$03F1      ;} $11.$10 = [sound 1 channel 3 note]
$2729: DA 10     movw  $10,ya       ;/
$272B: E9 B2 03  mov   x,$03B2      ; X = [sound 1 channel 3 voice index]
$272E: 3F B1 16  call  $16B1        ; Play note
}

$2731: 6F        ret
}


;;; $2732: Reset sound 1 channel 0 ;;;
{
$2732: E8 FF     mov   a,#$FF       ;\
$2734: C5 9C 03  mov   $039C,a      ;} Sound 1 channel 0 disable byte = FFh
$2737: E8 00     mov   a,#$00       ;\
$2739: C5 D8 03  mov   $03D8,a      ;} Sound 1 channel 0 update ADSR settings flag = 0
$273C: E5 B3 03  mov   a,$03B3      ;\
$273F: 25 AA 03  and   a,$03AA      ;} Sound 1 enabled voices &= [sound 1 channel 0 mask]
$2742: C5 B3 03  mov   $03B3,a      ;/
$2745: E4 1A     mov   a,$1A        ;\
$2747: 25 AA 03  and   a,$03AA      ;} Enabled sound effect voices &= [sound 1 channel 0 mask]
$274A: C4 1A     mov   $1A,a        ;/
$274C: E4 47     mov   a,$47        ;\
$274E: 05 A6 03  or    a,$03A6      ;} Current music voice bitset |= [sound 1 channel 0 voice bitset]
$2751: C4 47     mov   $47,a        ;/
$2753: E4 46     mov   a,$46        ;\
$2755: 05 A6 03  or    a,$03A6      ;} Key off flags |= [sound 1 channel 0 voice bitset]
$2758: C4 46     mov   $46,a        ;/
$275A: E9 AF 03  mov   x,$03AF      ; X = [sound 1 channel 0 voice index]
$275D: F5 11 02  mov   a,$0211+x    ; A = [track instrument index]
$2760: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$2763: E9 AF 03  mov   x,$03AF      ; X = [sound 1 channel 0 voice index]
$2766: E5 B8 03  mov   a,$03B8      ;\
$2769: D5 21 03  mov   $0321+x,a    ;} Track output volume = [sound 1 channel 0 backup of track output volume]
$276C: E5 B9 03  mov   a,$03B9      ;\
$276F: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = [sound 1 channel 0 backup of track phase inversion options]
$2772: 5F 3E 28  jmp   $283E        ; Go to reset sound 1 if no enabled voices
}


;;; $2775: Reset sound 1 channel 1 ;;;
{
$2775: E8 FF     mov   a,#$FF       ;\
$2777: C5 9D 03  mov   $039D,a      ;} Sound 1 channel 1 disable byte = FFh
$277A: E8 00     mov   a,#$00       ;\
$277C: C5 D9 03  mov   $03D9,a      ;} Sound 1 channel 1 update ADSR settings flag = 0
$277F: E5 B3 03  mov   a,$03B3      ;\
$2782: 25 AB 03  and   a,$03AB      ;} Sound 1 enabled voices &= [sound 1 channel 1 mask]
$2785: C5 B3 03  mov   $03B3,a      ;/
$2788: E4 1A     mov   a,$1A        ;\
$278A: 25 AB 03  and   a,$03AB      ;} Enabled sound effect voices &= [sound 1 channel 1 mask]
$278D: C4 1A     mov   $1A,a        ;/
$278F: E4 47     mov   a,$47        ;\
$2791: 05 A7 03  or    a,$03A7      ;} Current music voice bitset |= [sound 1 channel 1 voice bitset]
$2794: C4 47     mov   $47,a        ;/
$2796: E4 46     mov   a,$46        ;\
$2798: 05 A7 03  or    a,$03A7      ;} Key off flags |= [sound 1 channel 1 voice bitset]
$279B: C4 46     mov   $46,a        ;/
$279D: E9 B0 03  mov   x,$03B0      ; X = [sound 1 channel 1 voice index]
$27A0: F5 11 02  mov   a,$0211+x    ; A = [track instrument index]
$27A3: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$27A6: E9 B0 03  mov   x,$03B0      ; X = [sound 1 channel 1 voice index]
$27A9: E5 BA 03  mov   a,$03BA      ;\
$27AC: D5 21 03  mov   $0321+x,a    ;} Track output volume = [sound 1 channel 1 backup of track output volume]
$27AF: E5 BB 03  mov   a,$03BB      ;\
$27B2: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = [sound 1 channel 1 backup of track phase inversion options]
$27B5: 5F 3E 28  jmp   $283E        ; Go to reset sound 1 if no enabled voices
}


;;; $27B8: Reset sound 1 channel 2 ;;;
{
$27B8: E8 FF     mov   a,#$FF       ;\
$27BA: C5 9E 03  mov   $039E,a      ;} Sound 1 channel 2 disable byte = FFh
$27BD: E8 00     mov   a,#$00       ;\
$27BF: C5 DA 03  mov   $03DA,a      ;} Sound 1 channel 2 update ADSR settings flag = 0
$27C2: E5 B3 03  mov   a,$03B3      ;\
$27C5: 25 AC 03  and   a,$03AC      ;} Sound 1 enabled voices &= [sound 1 channel 2 mask]
$27C8: C5 B3 03  mov   $03B3,a      ;/
$27CB: E4 1A     mov   a,$1A        ;\
$27CD: 25 AC 03  and   a,$03AC      ;} Enabled sound effect voices &= [sound 1 channel 2 mask]
$27D0: C4 1A     mov   $1A,a        ;/
$27D2: E4 47     mov   a,$47        ;\
$27D4: 05 A8 03  or    a,$03A8      ;} Current music voice bitset |= [sound 1 channel 2 voice bitset]
$27D7: C4 47     mov   $47,a        ;/
$27D9: E4 46     mov   a,$46        ;\
$27DB: 05 A8 03  or    a,$03A8      ;} Key off flags |= [sound 1 channel 2 voice bitset]
$27DE: C4 46     mov   $46,a        ;/
$27E0: E9 B1 03  mov   x,$03B1      ; X = [sound 1 channel 2 voice index]
$27E3: F5 11 02  mov   a,$0211+x    ; A = [track instrument index]
$27E6: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$27E9: E9 B1 03  mov   x,$03B1      ; X = [sound 1 channel 2 voice index]
$27EC: E5 BC 03  mov   a,$03BC      ;\
$27EF: D5 21 03  mov   $0321+x,a    ;} Track output volume = [sound 1 channel 2 backup of track output volume]
$27F2: E5 BD 03  mov   a,$03BD      ;\
$27F5: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = [sound 1 channel 2 backup of track phase inversion options]
$27F8: 5F 3E 28  jmp   $283E        ; Go to reset sound 1 if no enabled voices
}


;;; $27FB: Reset sound 1 channel 3 ;;;
{
$27FB: E8 FF     mov   a,#$FF       ;\
$27FD: C5 9F 03  mov   $039F,a      ;} Sound 1 channel 3 disable byte = FFh
$2800: E8 00     mov   a,#$00       ;\
$2802: C5 DB 03  mov   $03DB,a      ;} Sound 1 channel 3 update ADSR settings flag = 0
$2805: E5 B3 03  mov   a,$03B3      ;\
$2808: 25 AD 03  and   a,$03AD      ;} Sound 1 enabled voices &= [sound 1 channel 3 mask]
$280B: C5 B3 03  mov   $03B3,a      ;/
$280E: E4 1A     mov   a,$1A        ;\
$2810: 25 AD 03  and   a,$03AD      ;} Enabled sound effect voices &= [sound 1 channel 3 mask]
$2813: C4 1A     mov   $1A,a        ;/
$2815: E4 47     mov   a,$47        ;\
$2817: 05 A9 03  or    a,$03A9      ;} Current music voice bitset |= [sound 1 channel 3 voice bitset]
$281A: C4 47     mov   $47,a        ;/
$281C: E4 46     mov   a,$46        ;\
$281E: 05 A9 03  or    a,$03A9      ;} Key off flags |= [sound 1 channel 3 voice bitset]
$2821: C4 46     mov   $46,a        ;/
$2823: E9 B2 03  mov   x,$03B2      ; X = [sound 1 channel 3 voice index]
$2826: F5 11 02  mov   a,$0211+x    ; A = [track instrument index]
$2829: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$282C: E9 B2 03  mov   x,$03B2      ; X = [sound 1 channel 3 voice index]
$282F: E5 BE 03  mov   a,$03BE      ;\
$2832: D5 21 03  mov   $0321+x,a    ;} Track output volume = [sound 1 channel 3 backup of track output volume]
$2835: E5 BF 03  mov   a,$03BF      ;\
$2838: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = [sound 1 channel 3 backup of track phase inversion options]
$283B: 5F 3E 28  jmp   $283E        ; Go to reset sound 1 if no enabled voices
}


;;; $283E: Reset sound 1 if no enabled voices ;;;
{
; Merge point of reset sound 1 channel routines
$283E: E5 B3 03  mov   a,$03B3      ;\
$2841: D0 0B     bne   $284E        ;} If [sound 1 enabled voices] = 0:
$2843: E8 00     mov   a,#$00       ;\
$2845: C5 92 03  mov   $0392,a      ;} Current sound 1 = 0
$2848: C5 BB 04  mov   $04BB,a      ; Sound 1 priority = 0
$284B: C5 A4 03  mov   $03A4,a      ; Sound 1 initialisation flag = 0

$284E: 6F        ret
}


;;; $284F: A = next sound 1 channel 0 data byte ;;;
{
$284F: EC 94 03  mov   y,$0394      ;\
$2852: F7 2A     mov   a,($2A)+y    ;} A = [[$2A] + [$0394++]]
$2854: AC 94 03  inc   $0394        ;/
$2857: 6F        ret
}


;;; $2858: A = next sound 1 channel 1 data byte ;;;
{
$2858: EC 95 03  mov   y,$0395      ;\
$285B: F7 2C     mov   a,($2C)+y    ;} A = [[$2C] + [$0395++]]
$285D: AC 95 03  inc   $0395        ;/
$2860: 6F        ret
}


;;; $2861: A = next sound 1 channel 2 data byte ;;;
{
$2861: EC 96 03  mov   y,$0396      ;\
$2864: F7 2E     mov   a,($2E)+y    ;} A = [[$2E] + [$0396++]]
$2866: AC 96 03  inc   $0396        ;/
$2869: 6F        ret
}


;;; $286A: A = next sound 1 channel 3 data byte ;;;
{
$286A: EC 97 03  mov   y,$0397      ;\
$286D: F7 D0     mov   a,($D0)+y    ;} A = [[$D0] + [$0397++]]
$286F: AC 97 03  inc   $0397        ;/
$2872: 6F        ret
}


;;; $2873: Unused. ret ;;;
{
$2873: 6F        ret
}


;;; $2874: Unused. ret ;;;
{
$2874: 6F        ret
}


;;; $2875: Go to jump table entry [A] - 1 ;;;
{
; Used by other sound libraries too
$2875: 68 01     cmp   a,#$01       ;\
$2877: F0 1C     beq   $2895        ;} If [A] = 1: go to BRANCH_POINTLESS_SPECIAL_CASE
$2879: 9C        dec   a            ;\
$287A: 1C        asl   a            ;} Y = ([A] - 1) * 2
$287B: FD        mov   y,a          ;/

; BRANCH_CONTINUE
$287C: AE        pop   a            ;\
$287D: C5 20 00  mov   $0020,a      ;|
$2880: AE        pop   a            ;} Pop return address to $20
$2881: C5 21 00  mov   $0021,a      ;/
$2884: F7 20     mov   a,($20)+y    ;\
$2886: 5D        mov   x,a          ;|
$2887: FC        inc   y            ;|
$2888: F7 20     mov   a,($20)+y    ;} $20 = [[$20] + [Y]]
$288A: C5 21 00  mov   $0021,a      ;|
$288D: C9 20 00  mov   $0020,x      ;/
$2890: CD 00     mov   x,#$00       ;\
$2892: 1F 20 00  jmp   ($0020+x)    ;} Go to [$20]

; BRANCH_POINTLESS_SPECIAL_CASE
$2895: 8D 00     mov   y,#$00       ; Y = 0
$2897: 5F 7C 28  jmp   $287C        ; Go to BRANCH_CONTINUE
}


;;; $289A: Sound 1 channel variable pointers ;;;
{
$289A:           dw 03A6, 03A7, 03A8, 03A9 ; Sound 1 channel voice bitsets
$28A2:           dw 03AA, 03AB, 03AC, 03AD ; Sound 1 channel voice masks
$28AA:           dw 03AF, 03B0, 03B1, 03B2 ; Sound 1 channel voice indices
}


;;; $28B2: Sound 1 initialisation ;;;
{
$28B2: E8 09     mov   a,#$09       ;\
$28B4: C5 A5 03  mov   $03A5,a      ;} Voice ID = 9
$28B7: E4 1A     mov   a,$1A        ;\
$28B9: C5 A3 03  mov   $03A3,a      ;} Remaining enabled sound effect voices = [enabled sound effect voices]
$28BC: E8 FF     mov   a,#$FF       ;\
$28BE: C5 A4 03  mov   $03A4,a      ;} Sound 1 initialisation flag = FFh
$28C1: E8 00     mov   a,#$00
$28C3: C5 AE 03  mov   $03AE,a      ; Sound 1 channel index * 2 = 0
$28C6: C5 A0 03  mov   $03A0,a      ; Sound 1 channel index = 0
$28C9: C5 A6 03  mov   $03A6,a      ;\
$28CC: C5 A7 03  mov   $03A7,a      ;|
$28CF: C5 A8 03  mov   $03A8,a      ;} Sound 1 channel voice bitsets = 0
$28D2: C5 A9 03  mov   $03A9,a      ;/
$28D5: C5 AF 03  mov   $03AF,a      ;\
$28D8: C5 B0 03  mov   $03B0,a      ;|
$28DB: C5 B1 03  mov   $03B1,a      ;} Sound 1 channel voice indices = 0
$28DE: C5 B2 03  mov   $03B2,a      ;/
$28E1: E8 FF     mov   a,#$FF
$28E3: C5 AA 03  mov   $03AA,a      ;\
$28E6: C5 AB 03  mov   $03AB,a      ;|
$28E9: C5 AC 03  mov   $03AC,a      ;} Sound 1 channel voice masks = FFh
$28EC: C5 AD 03  mov   $03AD,a      ;/
$28EF: C5 9C 03  mov   $039C,a      ;\
$28F2: C5 9D 03  mov   $039D,a      ;|
$28F5: C5 9E 03  mov   $039E,a      ;} Sound 1 channel disable bytes = FFh
$28F8: C5 9F 03  mov   $039F,a      ;/

; LOOP
$28FB: 8C A5 03  dec   $03A5        ; Decrement voice ID
$28FE: F0 7E     beq   $297E        ; If [voice ID] = 0: return
$2900: 0C A3 03  asl   $03A3        ; Remaining enabled sound effect voices <<= 1
$2903: B0 F6     bcs   $28FB        ; If carry set: go to LOOP
$2905: E8 00     mov   a,#$00       ;\
$2907: 65 A1 03  cmp   a,$03A1      ;} If [number of sound 1 voices to set up] = 0: return
$290A: F0 72     beq   $297E        ;/
$290C: 8C A1 03  dec   $03A1        ; Decrement number of sound 1 voices to set up
$290F: E8 00     mov   a,#$00       ;\
$2911: E9 A0 03  mov   x,$03A0      ;} Sound 1 channel disable byte = 0
$2914: D5 9C 03  mov   $039C+x,a    ;/
$2917: AC A0 03  inc   $03A0        ; Increment sound 1 channel index
$291A: E5 AE 03  mov   a,$03AE      ;\
$291D: 5D        mov   x,a          ;} Y = [sound 1 channel index] * 2
$291E: FD        mov   y,a          ;/
$291F: F5 9A 28  mov   a,$289A+x    ;\
$2922: C4 24     mov   $24,a        ;} $24 = sound 1 channel voice bitset
$2924: F5 A2 28  mov   a,$28A2+x    ;\
$2927: C4 26     mov   $26,a        ;} $26 = sound 1 channel voice mask
$2929: F5 AA 28  mov   a,$28AA+x    ;\
$292C: C4 28     mov   $28,a        ;} $28 = sound 1 channel voice index
$292E: 3D        inc   x
$292F: F5 9A 28  mov   a,$289A+x
$2932: C4 25     mov   $25,a
$2934: F5 A2 28  mov   a,$28A2+x
$2937: C4 27     mov   $27,a
$2939: F5 AA 28  mov   a,$28AA+x
$293C: C4 29     mov   $29,a
$293E: AC AE 03  inc   $03AE        ;\
$2941: AC AE 03  inc   $03AE        ;} Sound 1 channel index * 2 += 2
$2944: E5 A5 03  mov   a,$03A5      ;\
$2947: C5 A2 03  mov   $03A2,a      ;|
$294A: 8C A2 03  dec   $03A2        ;} Voice index = ([voice ID] - 1) * 2
$294D: 60        clrc               ;|
$294E: 0C A2 03  asl   $03A2        ;/
$2951: E9 A2 03  mov   x,$03A2      ;\
$2954: F5 21 03  mov   a,$0321+x    ;} Sound 1 channel backup of track output volume = [track output volume]
$2957: D6 B8 03  mov   $03B8+y,a    ;/
$295A: FC        inc   y            ;\
$295B: F5 51 03  mov   a,$0351+x    ;} Sound 1 channel backup of track phase inversion options = [track phase inversion options]
$295E: D6 B8 03  mov   $03B8+y,a    ;/
$2961: 8D 00     mov   y,#$00       ;\
$2963: E5 A2 03  mov   a,$03A2      ;} Sound 1 channel voice index = [voice index]
$2966: D7 28     mov   ($28)+y,a    ;/
$2968: E5 A5 03  mov   a,$03A5      ;\
$296B: 3F 75 28  call  $2875        ;} Go to [$296E + [voice index]]
$296E:           dw 2A3C, 2A21, 2A06, 29EB, 29D0, 29B5, 299A, 297F

$297E: 6F        ret

$297F: E2 1A     set7  $1A          ; Enable voice 7
$2981: F2 47     clr7  $47          ; Current music voice bitset &= ~80h
$2983: F2 4A     clr7  $4A          ; Disable echo on voice 7
$2985: E8 80     mov   a,#$80       ;\
$2987: 05 B3 03  or    a,$03B3      ;} Sound 1 enabled voices |= 80h
$298A: C5 B3 03  mov   $03B3,a      ;/
$298D: 8D 00     mov   y,#$00
$298F: E8 80     mov   a,#$80       ;\
$2991: D7 24     mov   ($24)+y,a    ;} Sound 1 channel voice bitset = 80h
$2993: E8 7F     mov   a,#$7F       ;\
$2995: D7 26     mov   ($26)+y,a    ;} Sound 1 channel voice mask = ~80h
$2997: 5F FB 28  jmp   $28FB        ; Go to LOOP

$299A: C2 1A     set6  $1A          ; Enable voice 6
$299C: D2 47     clr6  $47          ; Current music voice bitset &= ~40h
$299E: D2 4A     clr6  $4A          ; Disable echo on voice 6
$29A0: E8 40     mov   a,#$40       ;\
$29A2: 05 B3 03  or    a,$03B3      ;} Sound 1 enabled voices |= 40h
$29A5: C5 B3 03  mov   $03B3,a      ;/
$29A8: 8D 00     mov   y,#$00
$29AA: E8 40     mov   a,#$40       ;\
$29AC: D7 24     mov   ($24)+y,a    ;} Sound 1 channel voice bitset = 40h
$29AE: E8 BF     mov   a,#$BF       ;\
$29B0: D7 26     mov   ($26)+y,a    ;} Sound 1 channel voice mask = ~40h
$29B2: 5F FB 28  jmp   $28FB        ; Go to LOOP

$29B5: A2 1A     set5  $1A          ; Enable voice 5
$29B7: B2 47     clr5  $47          ; Current music voice bitset &= ~20h
$29B9: B2 4A     clr5  $4A          ; Disable echo on voice 5
$29BB: E8 20     mov   a,#$20       ;\
$29BD: 05 B3 03  or    a,$03B3      ;} Sound 1 enabled voices |= 20h
$29C0: C5 B3 03  mov   $03B3,a      ;/
$29C3: 8D 00     mov   y,#$00
$29C5: E8 20     mov   a,#$20       ;\
$29C7: D7 24     mov   ($24)+y,a    ;} Sound 1 channel voice bitset = 20h
$29C9: E8 DF     mov   a,#$DF       ;\
$29CB: D7 26     mov   ($26)+y,a    ;} Sound 1 channel voice mask = ~20h
$29CD: 5F FB 28  jmp   $28FB        ; Go to LOOP

$29D0: 82 1A     set4  $1A          ; Enable voice 4
$29D2: 92 47     clr4  $47          ; Current music voice bitset &= ~10h
$29D4: 92 4A     clr4  $4A          ; Disable echo on voice 4
$29D6: E8 10     mov   a,#$10       ;\
$29D8: 05 B3 03  or    a,$03B3      ;} Sound 1 enabled voices |= 10h
$29DB: C5 B3 03  mov   $03B3,a      ;/
$29DE: 8D 00     mov   y,#$00
$29E0: E8 10     mov   a,#$10       ;\
$29E2: D7 24     mov   ($24)+y,a    ;} Sound 1 channel voice bitset = 10h
$29E4: E8 EF     mov   a,#$EF       ;\
$29E6: D7 26     mov   ($26)+y,a    ;} Sound 1 channel voice mask = ~10h
$29E8: 5F FB 28  jmp   $28FB        ; Go to LOOP

$29EB: 62 1A     set3  $1A          ; Enable voice 3
$29ED: 72 47     clr3  $47          ; Current music voice bitset &= ~8
$29EF: 72 4A     clr3  $4A          ; Disable echo on voice 3
$29F1: E8 08     mov   a,#$08       ;\
$29F3: 05 B3 03  or    a,$03B3      ;} Sound 1 enabled voices |= 8
$29F6: C5 B3 03  mov   $03B3,a      ;/
$29F9: 8D 00     mov   y,#$00
$29FB: E8 08     mov   a,#$08       ;\
$29FD: D7 24     mov   ($24)+y,a    ;} Sound 1 channel voice bitset = 8
$29FF: E8 F7     mov   a,#$F7       ;\
$2A01: D7 26     mov   ($26)+y,a    ;} Sound 1 channel voice mask = ~8
$2A03: 5F FB 28  jmp   $28FB        ; Go to LOOP

$2A06: 42 1A     set2  $1A          ; Enable voice 2
$2A08: 52 47     clr2  $47          ; Current music voice bitset &= ~4
$2A0A: 52 4A     clr2  $4A          ; Disable echo on voice 2
$2A0C: E8 04     mov   a,#$04       ;\
$2A0E: 05 B3 03  or    a,$03B3      ;} Sound 1 enabled voices |= 4
$2A11: C5 B3 03  mov   $03B3,a      ;/
$2A14: 8D 00     mov   y,#$00
$2A16: E8 04     mov   a,#$04       ;\
$2A18: D7 24     mov   ($24)+y,a    ;} Sound 1 channel voice bitset = 4
$2A1A: E8 FB     mov   a,#$FB       ;\
$2A1C: D7 26     mov   ($26)+y,a    ;} Sound 1 channel voice mask = ~4
$2A1E: 5F FB 28  jmp   $28FB        ; Go to LOOP

$2A21: 22 1A     set1  $1A          ; Enable voice 1
$2A23: 32 47     clr1  $47          ; Current music voice bitset &= ~2
$2A25: 32 4A     clr1  $4A          ; Disable echo on voice 1
$2A27: E8 02     mov   a,#$02       ;\
$2A29: 05 B3 03  or    a,$03B3      ;} Sound 1 enabled voices |= 2
$2A2C: C5 B3 03  mov   $03B3,a      ;/
$2A2F: 8D 00     mov   y,#$00
$2A31: E8 02     mov   a,#$02       ;\
$2A33: D7 24     mov   ($24)+y,a    ;} Sound 1 channel voice bitset = 2
$2A35: E8 FD     mov   a,#$FD       ;\
$2A37: D7 26     mov   ($26)+y,a    ;} Sound 1 channel voice mask = ~2
$2A39: 5F FB 28  jmp   $28FB        ; Go to LOOP

$2A3C: 02 1A     set0  $1A          ; Enable voice 0
$2A3E: 12 47     clr0  $47          ; Current music voice bitset &= ~1
$2A40: 12 4A     clr0  $4A          ; Disable echo on voice 0
$2A42: E8 01     mov   a,#$01       ;\
$2A44: 05 B3 03  or    a,$03B3      ;} Sound 1 enabled voices |= 1
$2A47: C5 B3 03  mov   $03B3,a      ;/
$2A4A: 8D 00     mov   y,#$00
$2A4C: E8 01     mov   a,#$01       ;\
$2A4E: D7 24     mov   ($24)+y,a    ;} Sound 1 channel voice bitset = 1
$2A50: E8 FE     mov   a,#$FE       ;\
$2A52: D7 26     mov   ($26)+y,a    ;} Sound 1 channel voice mask = ~1
$2A54: 5F FB 28  jmp   $28FB        ; Go to LOOP
}


;;; $2A57: A = next sound 1 channel instruction list pointer ;;;
{
$2A57: FC        inc   y            ;\
$2A58: F7 22     mov   a,($22)+y    ;} A = [[$22] + [++Y]]
$2A5A: 6F        ret
}


;;; $2A5B: A *= 8 ;;;
{
$2A5B: 1C        asl   a
$2A5C: 1C        asl   a
$2A5D: 1C        asl   a
$2A5E: 6F        ret
}


;;; $2A5F: Sound 1 configurations ;;;
{
;;; $2A5F: Sound 1 configuration - sound 1 - number of voices = 4, priority = 0 ;;;
{
; 1: Power bomb explosion
$2A5F: 3F E2 2A  call  $2AE2    ; Number of sound 1 voices = 4, sound 1 priority = 0
$2A62: 6F        ret
}


;;; $2A63: Sound 1 configuration - sounds 2..7 - number of voices = 1, priority = 0 ;;;
{
; 2: Silence
; 3: Missile
; 4: Super missile
; 5: Grapple start
; 6: Grappling
; 7: Grapple end
$2A63: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A66: 6F        ret
}


;;; $2A67: Sound 1 configuration - sound 8 - number of voices = 2, priority = 0 ;;;
{
; 8: Charging beam
$2A67: 3F C1 2A  call  $2AC1    ; Number of sound 1 voices = 2, sound 1 priority = 0
$2A6A: 6F        ret
}


;;; $2A6B: Sound 1 configuration - sounds 9..23h - number of voices = 1, priority = 0 ;;;
{
; 9: X-ray
; Ah: X-ray end
; Bh: Uncharged power beam
; Ch: Uncharged ice beam
; Dh: Uncharged wave beam
; Eh: Uncharged ice + wave beam
; Fh: Uncharged spazer beam
; 10h: Uncharged spazer + ice beam
; 11h: Uncharged spazer + ice + wave beam
; 12h: Uncharged spazer + wave beam
; 13h: Uncharged plasma beam
; 14h: Uncharged plasma + ice beam
; 15h: Uncharged plasma + ice + wave beam
; 16h: Uncharged plasma + wave beam
; 17h: Charged power beam
; 18h: Charged ice beam
; 19h: Charged wave beam
; 1Ah: Charged ice + wave beam
; 1Bh: Charged spazer beam
; 1Ch: Charged spazer + ice beam
; 1Dh: Charged spazer + ice + wave beam
; 1Eh: Charged spazer + wave beam
; 1Fh: Charged plasma beam / hyper beam
; 20h: Charged plasma + ice beam
; 21h: Charged plasma + ice + wave beam
; 22h: Charged plasma + wave beam / post-credits Samus shoots screen
; 23h: Ice SBA
$2A6B: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A6E: 6F        ret
}


;;; $2A6F: Sound 1 configuration - sound 24h - number of voices = 2, priority = 0 ;;;
{
; 24h: Ice SBA end
$2A6F: 3F C1 2A  call  $2AC1    ; Number of sound 1 voices = 2, sound 1 priority = 0
$2A72: 6F        ret
}


;;; $2A73: Sound 1 configuration - sounds 25h/26h - number of voices = 1, priority = 0 ;;;
{
; 25h: Spazer SBA
; 26h: Spazer SBA end
$2A73: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A76: 6F        ret
}


;;; $2A77: Sound 1 configuration - sound 27h - number of voices = 2, priority = 0 ;;;
{
; 27h: Plasma SBA
$2A77: 3F C1 2A  call  $2AC1    ; Number of sound 1 voices = 2, sound 1 priority = 0
$2A7A: 6F        ret
}


;;; $2A7B: Sound 1 configuration - sounds 28h..2Bh - number of voices = 1, priority = 0 ;;;
{
; 28h: Wave SBA
; 29h: Wave SBA end
; 2Ah: Selected save file
; 2Bh: (Empty)
$2A7B: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A7E: 6F        ret
}


;;; $2A7F: Sound 1 configuration - sound 2Ch - number of voices = 1, priority = 0 ;;;
{
; 2Ch: (Empty)
$2A7F: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A82: 6F        ret
}


;;; $2A83: Sound 1 configuration - sound 2Dh - number of voices = 1, priority = 0 ;;;
{
; 2Dh: (Empty)
$2A83: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A86: 6F        ret
}


;;; $2A87: Sound 1 configuration - sound 2Eh - number of voices = 4, priority = 0 ;;;
{
; 2Eh: Saving
$2A87: 3F D7 2A  call  $2AD7    ; Number of sound 1 voices = 4, sound 1 priority = 0
$2A8A: 6F        ret
}


;;; $2A8B: Sound 1 configuration - sounds 2Fh..32h - number of voices = 1, priority = 0 ;;;
{
; 2Fh: Underwater space jump (without gravity suit)
; 30h: Resumed spin jump
; 31h: Spin jump
; 32h: Spin jump end
$2A8B: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A8E: 6F        ret
}


;;; $2A8F: Sound 1 configuration - sound 33h - number of voices = 2, priority = 0 ;;;
{
; 33h: Screw attack
$2A8F: 3F C1 2A  call  $2AC1    ; Number of sound 1 voices = 2, sound 1 priority = 0
$2A92: 6F        ret
}


;;; $2A93: Sound 1 configuration - sound 34h - number of voices = 1, priority = 0 ;;;
{
; 34h: Screw attack end
$2A93: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A96: 6F        ret
}


;;; $2A97: Sound 1 configuration - sound 35h - number of voices = 1, priority = 1 ;;;
{
; 35h: Samus damaged
$2A97: 3F B6 2A  call  $2AB6    ; Number of sound 1 voices = 1, sound 1 priority = 1
$2A9A: 6F        ret
}


;;; $2A9B: Sound 1 configuration - sounds 36h..3Fh - number of voices = 1, priority = 0 ;;;
{
; 36h: Scrolling map
; 37h: Moved cursor / toggle reserve mode
; 38h: Menu option selected
; 39h: Switch HUD item
; 3Ah: (Empty)
; 3Bh: Hexagon map -> square map transition
; 3Ch: Square map -> hexagon map transition
; 3Dh: Dud shot
; 3Eh: Space jump
; 3Fh: Unused. Resumed space jump
$2A9B: 3F AB 2A  call  $2AAB    ; Number of sound 1 voices = 1, sound 1 priority = 0
$2A9E: 6F        ret
}


;;; $2A9F: Sound 1 configuration - sound 40h - number of voices = 3, priority = 1 ;;;
{
; 40h: Mother Brain's rainbow beam
$2A9F: 3F CC 2A  call  $2ACC    ; Number of sound 1 voices = 3, sound 1 priority = 1
$2AA2: 6F        ret
}


;;; $2AA3: Sound 1 configuration - sound 41h - number of voices = 2, priority = 0 ;;;
{
; 41h: Resume charging beam
$2AA3: 3F C1 2A  call  $2AC1    ; Number of sound 1 voices = 2, sound 1 priority = 0
$2AA6: 6F        ret
}


;;; $2AA7: Sound 1 configuration - sound 42h - number of voices = 2, priority = 0 ;;;
{
; 42h: Unused
$2AA7: 3F C1 2A  call  $2AC1    ; Number of sound 1 voices = 2, sound 1 priority = 0
$2AAA: 6F        ret
}
}


;;; $2AAB: Number of sound 1 voices = 1, sound 1 priority = 0 ;;;
{
; 2: Silence
; 3: Missile
; 4: Super missile
; 5: Grapple start
; 6: Grappling
; 7: Grapple end
; 9: X-ray
; Ah: X-ray end
; Bh: Uncharged power beam
; Ch: Uncharged ice beam
; Dh: Uncharged wave beam
; Eh: Uncharged ice + wave beam
; Fh: Uncharged spazer beam
; 10h: Uncharged spazer + ice beam
; 11h: Uncharged spazer + ice + wave beam
; 12h: Uncharged spazer + wave beam
; 13h: Uncharged plasma beam
; 14h: Uncharged plasma + ice beam
; 15h: Uncharged plasma + ice + wave beam
; 16h: Uncharged plasma + wave beam
; 17h: Charged power beam
; 18h: Charged ice beam
; 19h: Charged wave beam
; 1Ah: Charged ice + wave beam
; 1Bh: Charged spazer beam
; 1Ch: Charged spazer + ice beam
; 1Dh: Charged spazer + ice + wave beam
; 1Eh: Charged spazer + wave beam
; 1Fh: Charged plasma beam / hyper beam
; 20h: Charged plasma + ice beam
; 21h: Charged plasma + ice + wave beam
; 22h: Charged plasma + wave beam / post-credits Samus shoots screen
; 23h: Ice SBA
; 25h: Spazer SBA
; 26h: Spazer SBA end
; 28h: Wave SBA
; 29h: Wave SBA end
; 2Ah: Selected save file
; 2Bh: (Empty)
; 2Ch: (Empty)
; 2Dh: (Empty)
; 2Fh: Underwater space jump (without gravity suit)
; 30h: Resumed spin jump
; 31h: Spin jump
; 32h: Spin jump end
; 34h: Screw attack end
; 36h: Scrolling map
; 37h: Moved cursor / toggle reserve mode
; 38h: Menu option selected
; 39h: Switch HUD item
; 3Ah: (Empty)
; 3Bh: Hexagon map -> square map transition
; 3Ch: Square map -> hexagon map transition
; 3Dh: Dud shot
; 3Eh: Space jump
; 3Fh: Unused. Resumed space jump

$2AAB: E8 01     mov   a,#$01
$2AAD: C5 A1 03  mov   $03A1,a
$2AB0: E8 00     mov   a,#$00
$2AB2: C5 BB 04  mov   $04BB,a
$2AB5: 6F        ret
}


;;; $2AB6: Number of sound 1 voices = 1, sound 1 priority = 1 ;;;
{
; 35h: Samus damaged
$2AB6: E8 01     mov   a,#$01
$2AB8: C5 A1 03  mov   $03A1,a
$2ABB: E8 01     mov   a,#$01
$2ABD: C5 BB 04  mov   $04BB,a
$2AC0: 6F        ret
}


;;; $2AC1: Number of sound 1 voices = 2, sound 1 priority = 0 ;;;
{
; 8: Charging beam
; 24h: Ice SBA end
; 27h: Plasma SBA
; 33h: Screw attack
; 41h: Resume charging beam
; 42h: Unused
$2AC1: E8 02     mov   a,#$02
$2AC3: C5 A1 03  mov   $03A1,a
$2AC6: E8 00     mov   a,#$00
$2AC8: C5 BB 04  mov   $04BB,a
$2ACB: 6F        ret
}


;;; $2ACC: Number of sound 1 voices = 3, sound 1 priority = 1 ;;;
{
; 40h: Mother Brain's rainbow beam
$2ACC: E8 03     mov   a,#$03
$2ACE: C5 A1 03  mov   $03A1,a
$2AD1: E8 01     mov   a,#$01
$2AD3: C5 BB 04  mov   $04BB,a
$2AD6: 6F        ret
}


;;; $2AD7: Number of sound 1 voices = 4, sound 1 priority = 0 ;;;
{
; 2Eh: Saving
$2AD7: E8 04     mov   a,#$04
$2AD9: C5 A1 03  mov   $03A1,a
$2ADC: E8 00     mov   a,#$00
$2ADE: C5 BB 04  mov   $04BB,a
$2AE1: 6F        ret
}


;;; $2AE2: Number of sound 1 voices = 4, sound 1 priority = 0 ;;;
{
; 1: Power bomb explosion
$2AE2: E8 04     mov   a,#$04
$2AE4: C5 A1 03  mov   $03A1,a
$2AE7: E8 00     mov   a,#$00
$2AE9: C5 BB 04  mov   $04BB,a
$2AEC: 6F        ret
}


;;; $2AED: Sound 1 instruction lists ;;;
{
$2AED:           dw 2B71, 2BAF, 2BB7, 2BC4, 2BD1, 2BFC, 2C2F, 2C37, 2CFF, 2D12, 2D1A, 2D27, 2D4B, 2D5D, 2D5F, 2D76,
                    2D95, 2D97, 2D99, 2DA6, 2DA8, 2DAA, 2DAC, 2DC8, 2DE7, 2DFE, 2E00, 2E17, 2E19, 2E1B, 2E1D, 2E34,
                    2E36, 2E38, 2E3A, 2E68, 2EAF, 2ED0, 2EF6, 2F2C, 2F37, 2F3F, 2F47, 2F4A, 2F4D, 2F50, 2FB0, 2FB8,
                    2FC3, 2FDD, 2FE5, 3040, 3048, 3055, 305D, 3065, 3070, 3078, 307B, 308B, 309B, 30A8, 30D6, 30E1,
                    312A, 313F

; Instruction list format:
{
; Commands:
;     F5h dd tt - legato pitch slide with subnote delta = d, target note = t
;     F8h dd tt -        pitch slide with subnote delta = d, target note = t
;     F9h aaaa - voice's ADSR settings = a
;     FBh - repeat
;     FCh - enable noise
;     FDh - decrement repeat counter and repeat if non-zero
;     FEh cc - set repeat pointer with repeat counter = c
;     FFh - end

; Otherwise:
;     ii vv pp nn tt
;     i: Instrument index
;     v: Volume
;     p: Panning
;     n: Note. F6h is a tie
;     t: Length
}

; Sound 1: Power bomb explosion
$2B71:           dw 2B79, 2B82, 2B93, 2BA1
$2B79:           db F5,B0,C7, 05,D0,0A,98,46, FF
$2B82:           db F5,A0,C7, 09,D0,0F,80,50, F5,50,80, 09,D0,0A,AB,46, FF
$2B93:           db 09,D0,0F,87,10, F5,B0,C7, 05,D0,0F,80,60, FF
$2BA1:           db 09,D0,05,82,30, F5,A0,80, 05,D0,05,C7,60, FF

; Sound 2: Silence
$2BAF:           dw 2BB1
$2BB1:           db 15,00,0A,BC,03, FF

; Sound 3: Missile
$2BB7:           dw 2BB9
$2BB9:           db 00,D8,0A,95,08, 01,D8,0A,8B,30, FF

; Sound 4: Super missile
$2BC4:           dw 2BC6
$2BC6:           db 00,D0,0A,95,08, 01,D0,0A,90,30, FF

; Sound 5: Grapple start
$2BD1:           dw 2BD3
$2BD3:           db 01,80,0A,9D,10, 02,50,0A,93,07, 02,50,0A,93,03, 02,50,0A,93,05, 02,50,0A,93,08, 02,50,0A,93,04, 02,50,0A,93,06, 02,50,0A,93,04, FF

; Sound 6: Grappling
$2BFC:           dw 2BFE
$2BFE:           db 0D,50,0A,80,03, 0D,50,0A,85,04,
                    FE,00, 02,50,0A,93,07, 02,50,0A,93,03, 02,50,0A,93,05, 02,50,0A,93,08, 02,50,0A,93,04, 02,50,0A,93,06, 02,50,0A,93,04, FB,
                    FF

; Sound 7: Grapple end
$2C2F:           dw 2C31
$2C31:           db 02,50,0A,93,05, FF

; Sound 8: Charging beam
$2C37:           dw 2C3B, 2C51
$2C3B:           db 05,00,0A,B4,15, F5,30,C7, 05,50,0A,B7,25,
                    FE,00, 07,60,0A,C7,30, FB,
                    FF
$2C51:           db 02,00,0A,9C,07, 02,10,0A,9C,03, 02,00,0A,9C,05, 02,20,0A,9C,08

; Shared by charging beam and resume charging beam
$2C65:           db 02,20,0A,9C,04, 02,30,0A,9C,06, 02,00,0A,9C,04, 02,30,0A,9C,03, 02,30,0A,9C,07, 02,00,0A,9C,0A, 02,30,0A,9C,03, 02,00,0A,9C,04, 02,40,0A,9C,03, 02,40,0A,9C,07, 02,00,0A,9C,05, 02,40,0A,9C,06, 02,40,0A,9C,03, 02,00,0A,9C,0A, 02,50,0A,9C,03, 02,50,0A,9C,03, 02,60,0A,9C,05, 02,00,0A,9C,06, 02,60,0A,9C,07, 02,00,0A,9C,03, 02,60,0A,9C,04, 02,60,0A,9C,03, 02,00,0A,9C,03,
                    FE,00, 02,40,0A,9C,05, 02,40,0A,9C,06, 02,40,0A,9C,07, 02,40,0A,9C,03, 02,40,0A,9C,04, 02,40,0A,9C,03, 02,40,0A,9C,03, FB,
                    FF

; Sound 9: X-ray
$2CFF:           dw 2D01
$2D01:           db F5,70,AD, 06,40,0A,A4,40,
                    FE,00, 06,40,0A,AD,F0, FB,
                    FF

; Sound Ah: X-ray end
$2D12:           dw 2D14
$2D14:           db 06,00,0A,AD,03, FF

; Sound Bh: Uncharged power beam
$2D1A:           dw 2D1C
$2D1C:           db 04,90,0A,89,03, 04,90,0A,84,0E, FF

; Sound Ch: Uncharged ice beam
$2D27:           dw 2D29

; Uncharged ice / ice + wave beam
$2D29:           db 04,B0,0A,8B,03, 04,B0,0A,89,07, F5,90,C7, 10,90,0A,BC,0A, 10,60,0A,C3,06, 10,30,0A,C7,03, 10,20,0A,C7,03, FF

; Sound Dh: Uncharged wave beam
$2D4B:           dw 2D4D
$2D4D:           db 04,90,0A,89,03, 04,70,0A,84,0B, 04,30,0A,84,08, FF

; Sound Eh: Uncharged ice + wave beam
$2D5D:           dw 2D29

; Sound Fh: Uncharged spazer beam
$2D5F:           dw 2D61

; Uncharged spazer / spazer + wave beam
$2D61:           db 00,D0,0A,98,0C, 04,C0,0A,80,10, 04,30,0A,80,08, 04,10,0A,80,06, FF

; Sound 10h: Uncharged spazer + ice beam
$2D76:           dw 2D78

; Uncharged spazer + ice / spazer + ice + wave / plasma + ice / plasma + ice + wave beam
$2D78:           db 00,D0,0A,98,0C, F5,90,C7, 10,90,0A,BC,0A, 10,60,0A,C3,06, 10,30,0A,C7,03, 10,20,0A,C7,03, FF

; Sound 11h: Uncharged spazer + ice + wave beam
$2D95:           dw 2D78

; Sound 12h: Uncharged spazer + wave beam
$2D97:           dw 2D61

; Sound 13h: Uncharged plasma beam
$2D99:           dw 2D9B

; Uncharged plasma / plasma + wave beam
$2D9B:           db 00,D0,0A,98,0C, 04,B0,0A,80,13, FF

; Sound 14h: Uncharged plasma + ice beam
$2DA6:           dw 2D78

; Sound 15h: Uncharged plasma + ice + wave beam
$2DA8:           dw 2D78

; Sound 16h: Uncharged plasma + wave beam
$2DAA:           dw 2D9B

; Sound 17h: Charged power beam
$2DAC:           dw 2DAE
$2DAE:           db 04,D0,0A,84,05, 04,D0,0A,80,0C, 02,80,0A,98,03, 02,60,0A,98,03, 02,50,0A,98,03, FF

; Sound 18h: Charged ice beam
$2DC8:           dw 2DCA

; Charged ice / ice + wave / spazer + ice / spazer + ice + wave / plasma + ice / plasma + ice + wave beam
$2DCA:           db 00,E0,0A,98,0C, F5,B0,C7, 10,E0,0A,BC,0A, 10,70,0A,C3,06, 10,30,0A,C7,03, 10,20,0A,C7,03, FF

; Sound 19h: Charged wave beam
$2DE7:           dw 2DE9
$2DE9:           db 04,E0,0A,84,03, 04,E0,0A,80,10, 04,50,0A,80,04, 04,30,0A,80,09, FF

; Sound 1Ah: Charged ice + wave beam
$2DFE:           dw 2DCA

; Sound 1Bh: Charged spazer beam
$2E00:           dw 2E02

; Charged spazer / spazer + wave beam
$2E02:           db 00,D0,0A,95,08, 04,D0,0A,80,0F, 04,80,0A,80,0D, 04,20,0A,80,0A, FF

; Sound 1Ch: Charged spazer + ice beam
$2E17:           dw 2DCA

; Sound 1Dh: Charged spazer + ice + wave beam
$2E19:           dw 2DCA

; Sound 1Eh: Charged spazer + wave beam
$2E1B:           dw 2E02

; Sound 1Fh: Charged plasma beam / hyper beam
$2E1D:           dw 2E1F

; Charged plasma / hyper / plasma + wave beam
$2E1F:           db 00,D0,0A,98,0E, 04,D0,0A,80,10, 04,70,0A,80,10, 04,30,0A,80,10, FF

; Sound 20h: Charged plasma + ice beam
$2E34:           dw 2DCA

; Sound 21h: Charged plasma + ice + wave beam
$2E36:           dw 2DCA

; Sound 22h: Charged plasma + wave beam / post-credits Samus shoots screen
$2E38:           dw 2E1F

; Sound 23h: Ice SBA
$2E3A:           dw 2E3C
$2E3C:           db FE,00, 10,50,0A,C0,03, 10,50,0A,C1,03, 10,60,0A,C3,03, 10,60,0A,C5,03, 10,70,0A,C7,03, 10,60,0A,C5,03, 10,50,0A,C3,03, 10,50,0A,C1,03, FB, FF

; Sound 24h: Ice SBA end
$2E68:           dw 2E6C, 2E9F
$2E6C:           db 10,D0,0A,BC,0A, 10,70,0A,C3,06, 10,30,0A,C7,03, 10,20,0A,C7,03, 10,50,0A,C3,06, 10,40,0A,C7,03, 10,40,0A,C7,03, 10,30,0A,C3,06, 10,20,0A,C7,03, 10,20,0A,C7,03, FF
$2E9F:           db 04,D0,0A,80,10, 04,70,0A,80,10, 04,30,0A,80,10, FF

; Sound 25h: Spazer SBA
$2EAF:           dw 2EB1
$2EB1:           db 04,D0,0A,80,10, 04,70,0A,80,10, 04,30,0A,80,02, 04,D0,0A,80,10, 04,70,0A,80,10, 04,30,0A,80,10, FF

; Sound 26h: Spazer SBA end
$2ED0:           dw 2ED2
$2ED2:           db 04,D0,0A,80,10, 04,70,0A,80,04, 04,30,0A,80,02, 04,30,0A,80,06, 04,30,0A,80,06, 04,70,0A,80,07, 04,70,0A,80,07, FF

; Sound 27h: Plasma SBA
$2EF6:           dw 2EFA, 2F13
$2EFA:           db F5,30,C7, 07,90,0A,B7,25, F5,30,B7, 07,90,0A,F6,25, F5,B0,C7, 07,90,0A,F6,25, FF
$2F13:           db F5,30,C7, 05,90,0A,B7,27, F5,30,B7, 05,90,0A,F6,27, F5,B0,C7, 05,90,0A,F6,27, FF

; Sound 28h: Wave SBA
$2F2C:           dw 2F2E
$2F2E:           db F5,30,C7, 05,50,0A,B7,25, FF

; Sound 29h: Wave SBA end
$2F37:           dw 2F39
$2F39:           db 05,00,0A,B7,03, FF

; Sound 2Ah: Selected save file
$2F3F:           dw 2F41
$2F41:           db 07,90,0A,C5,12, FF

; Sound 2Bh: (Empty)
$2F47:           dw 2F49
$2F49:           db FF

; Sound 2Ch: (Empty)
$2F4A:           dw 2F4C
$2F4C:           db FF

; Sound 2Dh: (Empty)
$2F4D:           dw 2F4F
$2F4F:           db FF

; Sound 2Eh: Saving
$2F50:           dw 2F58, 2F6E, 2F84, 2F9A
$2F58:           db F5,F0,B1, 06,45,0A,99,19, 06,45,0A,B1,80, F5,F0,99, 06,45,0A,B1,19, FF
$2F6E:           db F5,F0,A7, 06,45,0A,8F,19, 06,45,0A,A7,80, F5,F0,8F, 06,45,0A,A7,19, FF
$2F84:           db F5,F0,A0, 06,45,0A,88,19, 06,45,0A,A0,80, F5,F0,88, 06,45,0A,A0,19, FF
$2F9A:           db F5,F0,98, 06,45,0A,80,19, 06,45,0A,98,80, F5,F0,80, 06,45,0A,98,19, FF

; Sound 2Fh: Underwater space jump (without gravity suit)
$2FB0:           dw 2FB2
$2FB2:           db 07,80,0A,C7,10, FF

; Sound 30h: Resumed spin jump
$2FB8:           dw 2FBA
$2FBA:           db FE,00, 07,80,0A,C7,10, FB, FF

; Sound 31h: Spin jump
$2FC3:           dw 2FC5
$2FC5:           db 07,30,0A,C5,10, 07,40,0A,C6,10, 07,50,0A,C7,10,
                    FE,00, 07,80,0A,C7,10, FB,
                    FF

; Sound 32h: Spin jump end
$2FDD:           dw 2FDF
$2FDF:           db 0A,00,0A,87,03, FF

; Sound 33h: Screw attack
$2FE5:           dw 2FE9, 3015
$2FE9:           db 07,30,0A,C7,04, 07,40,0A,C7,05, 07,50,0A,C7,06, 07,60,0A,C7,07, 07,70,0A,C7,09, 07,80,0A,C7,0D, 07,80,0A,C7,0F,
                    FE,00, 07,80,0A,C7,10, FB,
                    FF
$3015:           db F5,E0,BC, 05,60,0A,98,0E, F5,E0,BC, 05,70,0A,A4,08, F5,E0,BC, 05,80,0A,B0,06,
                    FE,00, 05,80,0A,BC,03, 05,80,0A,C4,03, 05,80,0A,C6,03, FB,
                    FF

; Sound 34h: Screw attack end
$3040:           dw 3042
$3042:           db 0A,00,0A,87,03, FF

; Sound 35h: Samus damaged
$3048:           dw 304A
$304A:           db 13,60,0A,A4,10, 13,10,0A,A4,07, FF

; Sound 36h: Scrolling map
$3055:           dw 3057
$3057:           db 0C,60,0A,B0,02, FF

; Sound 37h: Moved cursor / toggle reserve mode
$305D:           dw 305F
$305F:           db 03,60,0A,9C,04, FF

; Sound 38h: Menu option selected
$3065:           dw 3067
$3067:           db F5,90,C7, 15,90,0A,B0,15, FF

; Sound 39h: Switch HUD item
$3070:           dw 3072
$3072:           db 03,40,0A,9C,03, FF

; Sound 3Ah: (Empty)
$3078:           dw 307A
$307A:           db FF

; Sound 3Bh: Hexagon map -> square map transition
$307B:           dw 307D
$307D:           db 05,90,0A,9C,0B, F5,F0,C2, 05,90,0A,9C,12, FF

; Sound 3Ch: Square map -> hexagon map transition
$308B:           dw 308D
$308D:           db 05,90,0A,9C,0B, F5,F0,80, 05,90,0A,9C,12, FF

; Sound 3Dh: Dud shot
$309B:           dw 309D
$309D:           db 08,70,0A,99,03, 08,70,0A,9C,05, FF

; Sound 3Eh: Space jump
$30A8:           dw 30AA
$30AA:           db 07,30,0A,C7,04, 07,40,0A,C7,05, 07,50,0A,C7,06, 07,60,0A,C7,07, 07,70,0A,C7,09, 07,80,0A,C7,0D, 07,80,0A,C7,0F,
                    FE,00, 07,80,0A,C7,10, FB,
                    FF

; Sound 3Fh: Unused. Resumed space jump
$30D6:           dw 30D8
$30D8:           db FE,00, 07,80,0A,C7,10, FB, FF

; Sound 40h: Mother Brain's rainbow beam
$30E1:           dw 30E7, 3118, 3121
$30E7:           db FE,00, 23,D0,0A,89,07, 23,D0,0A,8B,07, 23,D0,0A,8C,07, 23,D0,0A,8E,07, 23,D0,0A,90,07, 23,D0,0A,91,07, 23,D0,0A,93,07, 23,D0,0A,95,07, 23,D0,0A,97,07, FB, FF
$3118:           db FE,00, 06,D0,0A,BA,F0, FB, FF
$3121:           db FE,00, 06,D0,0A,B3,F0, FB, FF

; Sound 41h: Resume charging beam
$312A:           dw 312E, 2C65
$312E:           db F5,70,C7, 05,50,0A,C0,03,
                    FE,00, 07,60,0A,C7,30, FB,
                    FF

; Sound 42h: Unused
$313F:           dw 3143, 3149
$3143:           db 24,A0,0A,9C,20, FF
$3149:           db 24,00,0A,9D,05, 24,80,0A,95,40, FF
}
}


;;; $3154..4702: Sound library 2 ;;;
{
;;; $3154: Go to process sound 2 ;;;
{
$3154: 5F AF 32  jmp   $32AF
}


;;; $3157: Handle CPU IO 2 ;;;
{
; BUG: All sound 2 channels are being reset if a new sound effect causes a current sound effect to stop
;      However, some of the channels might have already been reset by reaching the end of their instruction list
;      and in the time since then, the voice may have been allocated to a sound effect in a different library
;      Re-resetting that channel will erroneously mark the voice as available for allocation,
;      allowing two sound effects to have the same voice allocated to them
;      This is the cause of the laser door opening sound glitch

$3157: EB 0A     mov   y,$0A        ; Y = [previous value read from CPU IO 2]
$3159: E4 02     mov   a,$02        ;\
$315B: C4 0A     mov   $0A,a        ;} Previous value read from CPU IO 2 = [value read from CPU IO 2]
$315D: C4 06     mov   $06,a        ; Value for CPU IO 2 = [value read from CPU IO 2]
$315F: 7E 02     cmp   y,$02        ;\
$3161: D0 06     bne   $3169        ;} If [Y] != [value read from CPU IO 2]: go to BRANCH_CHANGE

; BRANCH_NO_CHANGE
$3163: E5 F8 03  mov   a,$03F8      ;\
$3166: D0 EC     bne   $3154        ;} If [current sound 2] != 0: go to process sound 2
$3168: 6F        ret                ; Return

; BRANCH_CHANGE
$3169: 68 00     cmp   a,#$00       ;\
$316B: F0 F6     beq   $3163        ;} If [value read from CPU IO 2] = 0: go to BRANCH_NO_CHANGE
$316D: E4 02     mov   a,$02        ;\
$316F: 68 71     cmp   a,#$71       ;} If [value read from CPU IO 2] != 71h (silence):
$3171: F0 09     beq   $317C        ;/
$3173: 68 7E     cmp   a,#$7E       ;\
$3175: F0 05     beq   $317C        ;} If [value read from CPU IO 2] != 7Eh (Mother Brain's cry - high pitch / Phantoon's dying cry):
$3177: E5 BC 04  mov   a,$04BC      ;\
$317A: D0 E7     bne   $3163        ;} If [sound 2 priority] != 0: go to BRANCH_NO_CHANGE

$317C: E5 F8 03  mov   a,$03F8      ;\
$317F: F0 0B     beq   $318C        ;} If [current sound 2] != 0:
$3181: E8 00     mov   a,#$00       ;\
$3183: C5 4D 04  mov   $044D,a      ;} Sound 2 enabled voices = 0
$3186: 3F 6D 36  call  $366D        ; Reset sound 2 channel 0
$3189: 3F B0 36  call  $36B0        ; Reset sound 2 channel 1

$318C: E8 00     mov   a,#$00
$318E: C5 67 04  mov   $0467,a      ; Sound 2 channel 0 legato flag = 0
$3191: C5 6E 04  mov   $046E,a      ; Sound 2 channel 1 legato flag = 0
$3194: E4 06     mov   a,$06        ;\
$3196: 9C        dec   a            ;|
$3197: 1C        asl   a            ;} Current sound 2 index = ([value for CPU IO 2] - 1) * 2
$3198: C5 F9 03  mov   $03F9,a      ;/
$319B: E9 F9 03  mov   x,$03F9      ;\
$319E: F5 B3 39  mov   a,$39B3+x    ;|
$31A1: C4 D4     mov   $D4,a        ;|
$31A3: 3D        inc   x            ;} Sound 2 instruction list pointer set = [$39B3 + [current sound 2 index]]
$31A4: F5 B3 39  mov   a,$39B3+x    ;|
$31A7: C4 D5     mov   $D5,a        ;/
$31A9: E4 06     mov   a,$06        ;\
$31AB: C5 F8 03  mov   $03F8,a      ;} Current sound 2 = [value for CPU IO 2]
$31AE: 3F 75 28  call  $2875        ; Go to [$31B1 + [current sound 2 index]]

$31B1:           dw 38B7, 38B7, 38B7, 38B7, 38B7, 38BB, 38BB, 38BF, 38C3, 38C3, 38C3, 38C3, 38C3, 38C3, 38C3, 38C3,
                    38C3, 38C3, 38C3, 38C3, 38C3, 38C7, 38C7, 38CB, 38CF, 38D3, 38D7, 38DB, 38DF, 38E3, 38E7, 38EB,
                    38EB, 38EB, 38EB, 38EB, 38EB, 38EB, 38EF, 38F3, 38F3, 38F3, 38F3, 38F7, 38FB, 38FF, 3903, 3903,
                    3903, 3903, 3903, 3903, 3907, 390B, 390F, 390F, 3913, 3913, 3913, 3913, 3913, 3913, 3913, 3913,
                    3913, 3913, 3913, 3913, 3913, 3917, 391B, 391B, 391B, 391B, 391B, 391B, 391B, 391F, 3923, 3927,
                    3927, 392B, 392B, 392F, 3933, 3937, 393B, 393F, 3943, 3947, 394B, 394B, 394B, 394B, 394B, 394B,
                    394B, 394B, 394F, 3953, 3953, 3953, 3953, 3953, 3953, 3953, 3953, 3953, 3953, 3957, 395B, 395F,
                    395F, 3963, 3963, 3963, 3967, 396B, 396F, 3973, 3977, 3977, 3977, 397B, 397F, 397F, 3983
}


;;; $32AF: Process sound 2 ;;;
{
$32AF: E8 FF     mov   a,#$FF       ;\
$32B1: 65 44 04  cmp   a,$0444      ;} If [sound 2 initialisation flag] != FFh:
$32B4: F0 3A     beq   $32F0        ;/
$32B6: 3F 22 37  call  $3722        ; Sound 2 initialisation
$32B9: 8D 00     mov   y,#$00       ;\
$32BB: F7 D4     mov   a,($D4)+y    ;|
$32BD: C4 DC     mov   $DC,a        ;} $DC = [[sound 2 channel instruction list pointer set]]
$32BF: 3F AF 38  call  $38AF        ;|
$32C2: C4 DD     mov   $DD,a        ;/
$32C4: 3F AF 38  call  $38AF        ;\
$32C7: C4 DE     mov   $DE,a        ;|
$32C9: 3F AF 38  call  $38AF        ;} $DE = [[sound 2 channel instruction list pointer set] + 2]
$32CC: C4 DF     mov   $DF,a        ;/
$32CE: E5 4B 04  mov   a,$044B      ;\
$32D1: 3F B3 38  call  $38B3        ;} Sound 2 channel 0 DSP index = [sound 2 channel 0 voice index] * 8
$32D4: C5 4E 04  mov   $044E,a      ;/
$32D7: E5 4C 04  mov   a,$044C      ;\
$32DA: 3F B3 38  call  $38B3        ;} Sound 2 channel 1 DSP index = [sound 2 channel 1 voice index] * 8
$32DD: C5 4F 04  mov   $044F,a      ;/
$32E0: 8D 00     mov   y,#$00       ;\
$32E2: CC FA 03  mov   $03FA,y      ;} Sound 2 channel instruction list indices = 0
$32E5: CC FB 03  mov   $03FB,y      ;/
$32E8: 8D 01     mov   y,#$01       ;\
$32EA: CC FC 03  mov   $03FC,y      ;} Sound 2 channel instruction timers = 1
$32ED: CC FD 03  mov   $03FD,y      ;/

$32F0: E8 FF     mov   a,#$FF       ;\
$32F2: 65 FE 03  cmp   a,$03FE      ;} If [sound 2 channel 0 disable byte] = FFh:
$32F5: D0 03     bne   $32FA        ;/
$32F7: 5F B0 34  jmp   $34B0        ; Go to BRANCH_CHANNEL_0_END

; Channel 0
{
$32FA: 8C FC 03  dec   $03FC        ; Decrement sound 2 channel 0 instruction timer
$32FD: F0 03     beq   $3302        ; If [sound 2 channel 0 instruction timer] != 0:
$32FF: 5F 43 34  jmp   $3443        ; Go to BRANCH_PROCESS_CHANNEL_0_INSTRUCTION_END

$3302: E5 67 04  mov   a,$0467      ;\
$3305: F0 02     beq   $3309        ;} If sound 2 channel 0 legato flag enabled:
$3307: 2F 43     bra   $334C        ; Go to LOOP_CHANNEL_0_COMMANDS

$3309: E8 00     mov   a,#$00       ;\
$330B: C5 66 04  mov   $0466,a      ;} Disable sound 2 channel 0 pitch slide
$330E: C5 64 04  mov   $0464,a      ; Sound 2 channel 0 subnote delta = 0
$3311: C5 65 04  mov   $0465,a      ; Sound 2 channel 0 target note = 0
$3314: E8 FF     mov   a,#$FF       ;\
$3316: 65 54 04  cmp   a,$0454      ;} If [sound 2 channel 0 release flag] != FFh:
$3319: F0 16     beq   $3331        ;/
$331B: E5 46 04  mov   a,$0446      ;\
$331E: 04 46     or    a,$46        ;} Key off flags |= [sound 2 channel 0 voice bitset]
$3320: C4 46     mov   $46,a        ;/
$3322: E8 02     mov   a,#$02       ;\
$3324: C5 55 04  mov   $0455,a      ;} Sound 2 channel 0 release timer = 2
$3327: E8 01     mov   a,#$01       ;\
$3329: C5 FC 03  mov   $03FC,a      ;} Sound 2 channel 0 instruction timer = 1
$332C: E8 FF     mov   a,#$FF       ;\
$332E: C5 54 04  mov   $0454,a      ;} Sound 2 channel 0 release flag = FFh

$3331: 8C 55 04  dec   $0455        ; Decrement sound 2 channel 0 release timer
$3334: F0 03     beq   $3339        ; If [sound 2 channel 0 release timer] != 0:
$3336: 5F B0 34  jmp   $34B0        ; Go to BRANCH_CHANNEL_0_END

$3339: E8 00     mov   a,#$00       ;\
$333B: C5 54 04  mov   $0454,a      ;} Sound 2 channel 0 release flag = 0
$333E: E5 48 04  mov   a,$0448      ;\
$3341: 24 47     and   a,$47        ;} Current music voice bitset &= [sound 2 channel 0 voice mask]
$3343: C4 47     mov   $47,a        ;/
$3345: E5 48 04  mov   a,$0448      ;\
$3348: 24 49     and   a,$49        ;} Noise enable flags &= [sound 2 channel 0 voice mask]
$334A: C4 49     mov   $49,a        ;/

; LOOP_CHANNEL_0_COMMANDS
$334C: 3F 04 37  call  $3704        ; A = next sound 2 channel 0 data byte
$334F: 68 FA     cmp   a,#$FA       ;\
$3351: D0 00     bne   $3353        ;|
                                    ;} If [A] = F9h:
$3353: 68 F9     cmp   a,#$F9       ;|
$3355: D0 14     bne   $336B        ;/
$3357: 3F 04 37  call  $3704        ;\
$335A: C5 5C 04  mov   $045C,a      ;} Sound 2 channel 0 ADSR settings = next sound 2 channel 0 data byte
$335D: 3F 04 37  call  $3704        ;\
$3360: C5 5D 04  mov   $045D,a      ;} Sound 2 channel 0 ADSR settings |= next sound 2 channel 0 data byte << 8
$3363: E8 FF     mov   a,#$FF       ;\
$3365: C5 60 04  mov   $0460,a      ;} Sound 2 channel 0 update ADSR settings flag = FFh
$3368: 5F 4C 33  jmp   $334C        ; Go to LOOP_CHANNEL_0_COMMANDS

$336B: 68 F5     cmp   a,#$F5       ;\
$336D: D0 05     bne   $3374        ;} If [A] = F5h:
$336F: C5 68 04  mov   $0468,a      ; Enable sound 2 channel 0 pitch slide legato
$3372: 2F 09     bra   $337D

$3374: 68 F8     cmp   a,#$F8       ;\ Else ([A] != F5h):
$3376: D0 19     bne   $3391        ;} If [A] != F8h: go to BRANCH_CHANNEL_0_PITCH_SLIDE_END
$3378: E8 00     mov   a,#$00       ;\
$337A: C5 68 04  mov   $0468,a      ;} Disable sound 2 channel 0 pitch slide legato

$337D: 3F 04 37  call  $3704        ;\
$3380: C5 64 04  mov   $0464,a      ;} Sound 2 channel 0 subnote delta = next sound 2 channel 0 data byte
$3383: 3F 04 37  call  $3704        ;\
$3386: C5 65 04  mov   $0465,a      ;} Sound 2 channel 0 target note = next sound 2 channel 0 data byte
$3389: E8 FF     mov   a,#$FF       ;\
$338B: C5 66 04  mov   $0466,a      ;} Enable sound 2 channel 0 pitch slide = FFh
$338E: 3F 04 37  call  $3704        ; A = next sound 2 channel 0 data byte

; BRANCH_CHANNEL_0_PITCH_SLIDE_END
$3391: 68 FF     cmp   a,#$FF       ;\
$3393: D0 06     bne   $339B        ;} If [A] = FFh:
$3395: 3F 6D 36  call  $366D        ; Reset sound 2 channel 0
$3398: 5F B0 34  jmp   $34B0        ; Go to BRANCH_CHANNEL_0_END

$339B: 68 FE     cmp   a,#$FE       ;\
$339D: D0 0F     bne   $33AE        ;} If [A] = FEh:
$339F: 3F 04 37  call  $3704        ;\
$33A2: C5 58 04  mov   $0458,a      ;} Sound 2 channel 0 repeat counter = next sound 2 channel 0 data byte
$33A5: E5 FA 03  mov   a,$03FA      ;\
$33A8: C5 5A 04  mov   $045A,a      ;} Sound 2 channel 0 repeat point = [sound 2 channel 0 instruction list index]
$33AB: 3F 04 37  call  $3704        ; A = next sound 2 channel 0 data byte

$33AE: 68 FD     cmp   a,#$FD       ;\
$33B0: D0 11     bne   $33C3        ;} If [A] != FDh: go to BRANCH_CHANNEL_0_REPEAT_COMMAND
$33B2: 8C 58 04  dec   $0458        ; Decrement sound 2 channel 0 repeat counter
$33B5: D0 03     bne   $33BA        ; If [sound 2 channel 0 repeat counter] = 0:
$33B7: 5F 4C 33  jmp   $334C        ; Go to LOOP_CHANNEL_0_COMMANDS

; LOOP_CHANNEL_0_REPEAT_COMMAND
$33BA: E5 5A 04  mov   a,$045A      ;\
$33BD: C5 FA 03  mov   $03FA,a      ;} Sound 2 channel 0 instruction list index = [sound 2 channel 0 repeat point]
$33C0: 3F 04 37  call  $3704        ; A = next sound 2 channel 0 data byte

; BRANCH_CHANNEL_0_REPEAT_COMMAND
$33C3: 68 FB     cmp   a,#$FB       ;\
$33C5: D0 03     bne   $33CA        ;} If [A] = FBh:
$33C7: 5F BA 33  jmp   $33BA        ; Go to LOOP_CHANNEL_0_REPEAT_COMMAND

$33CA: 68 FC     cmp   a,#$FC       ;\
$33CC: D0 0A     bne   $33D8        ;} If [A] = FCh:
$33CE: E5 46 04  mov   a,$0446      ;\
$33D1: 04 49     or    a,$49        ;} Noise enable flags |= [sound 2 channel 0 voice bitset]
$33D3: C4 49     mov   $49,a        ;/
$33D5: 5F 4C 33  jmp   $334C        ; Go to LOOP_CHANNEL_0_COMMANDS

$33D8: E9 4B 04  mov   x,$044B      ; X = [sound 2 channel 0 voice index]
$33DB: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$33DE: 3F 04 37  call  $3704        ;\
$33E1: E9 4B 04  mov   x,$044B      ;} Track output volume = next sound 2 channel 0 data byte
$33E4: D5 21 03  mov   $0321+x,a    ;/
$33E7: E8 00     mov   a,#$00       ;\
$33E9: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = 0
$33EC: 3F 04 37  call  $3704        ;\
$33EF: C4 11     mov   $11,a        ;} $10 = (next sound 2 channel 0 data byte) * 100h
$33F1: 8F 00 10  mov   $10,#$00     ;/
$33F4: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled
$33F7: 3F 04 37  call  $3704        ; A = next sound 2 channel 0 data byte
$33FA: 68 F6     cmp   a,#$F6       ;\
$33FC: F0 08     beq   $3406        ;} If [A] != F6h:
$33FE: C5 62 04  mov   $0462,a      ; Sound 2 channel 0 note = [A]
$3401: E8 00     mov   a,#$00       ;\
$3403: C5 63 04  mov   $0463,a      ;} Sound 2 channel 0 subnote = 0

$3406: EC 62 04  mov   y,$0462      ;\
$3409: E5 63 04  mov   a,$0463      ;} $11.$10 = [sound 2 channel 0 note]
$340C: DA 10     movw  $10,ya       ;/
$340E: E9 4B 04  mov   x,$044B      ; X = [sound 2 channel 0 voice index]
$3411: 3F B1 16  call  $16B1        ; Play note
$3414: 3F 04 37  call  $3704        ;\
$3417: C5 FC 03  mov   $03FC,a      ;} Sound 2 channel 0 instruction timer = next sound 2 channel 0 data byte
$341A: E5 60 04  mov   a,$0460      ;\
$341D: F0 18     beq   $3437        ;} If [sound 2 channel 0 update ADSR settings flag] != 0:
$341F: E5 4E 04  mov   a,$044E      ;\
$3422: 08 05     or    a,#$05       ;|
$3424: FD        mov   y,a          ;|
$3425: E5 5C 04  mov   a,$045C      ;|
$3428: 3F 26 17  call  $1726        ;|
$342B: E5 4E 04  mov   a,$044E      ;} DSP sound 2 channel 0 ADSR settings = [sound 2 channel 0 ADSR settings]
$342E: 08 06     or    a,#$06       ;|
$3430: FD        mov   y,a          ;|
$3431: E5 5D 04  mov   a,$045D      ;|
$3434: 3F 26 17  call  $1726        ;/

$3437: E5 67 04  mov   a,$0467      ;\
$343A: D0 07     bne   $3443        ;} If sound 2 channel 0 legato disabled:
$343C: E5 46 04  mov   a,$0446      ;\
$343F: 04 45     or    a,$45        ;} Key on flags |= [sound 2 channel 0 voice bitset]
$3441: C4 45     mov   $45,a        ;/

; BRANCH_PROCESS_CHANNEL_0_INSTRUCTION_END
$3443: 00        nop
$3444: 00        nop
$3445: 00        nop
$3446: 00        nop
$3447: 00        nop
$3448: 00        nop
$3449: E5 66 04  mov   a,$0466      ;\
$344C: 68 FF     cmp   a,#$FF       ;} If sound 2 channel 0 pitch slide disabled: go to BRANCH_CHANNEL_0_END
$344E: D0 60     bne   $34B0        ;/
$3450: E5 68 04  mov   a,$0468      ;\
$3453: F0 05     beq   $345A        ;} If sound 2 channel 0 pitch slide legato enabled:
$3455: E8 FF     mov   a,#$FF       ;\
$3457: C5 67 04  mov   $0467,a      ;} Enable sound 2 channel 0 legato

$345A: E5 62 04  mov   a,$0462      ;\
$345D: 65 65 04  cmp   a,$0465      ;} If [sound 2 channel 0 note] >= [sound 2 channel 0 target note]:
$3460: 90 21     bcc   $3483        ;/
$3462: E5 63 04  mov   a,$0463      ;\
$3465: 80        setc               ;|
$3466: A5 64 04  sbc   a,$0464      ;} Sound 2 channel 0 subnote -= [sound 2 channel 0 subnote delta]
$3469: C5 63 04  mov   $0463,a      ;/
$346C: B0 34     bcs   $34A2        ; If [sound 2 channel 0 subnote] < 0:
$346E: 8C 62 04  dec   $0462        ; Decrement sound 2 channel 0 note
$3471: E5 65 04  mov   a,$0465      ;\
$3474: 65 62 04  cmp   a,$0462      ;} If [sound 2 channel 0 target note] = [sound 2 channel 0 note]:
$3477: D0 29     bne   $34A2        ;/
$3479: E8 00     mov   a,#$00       ;\
$347B: C5 66 04  mov   $0466,a      ;} Disable sound 2 channel 0 pitch slide
$347E: C5 67 04  mov   $0467,a      ; Disable sound 2 channel 0 legato
$3481: 2F 1F     bra   $34A2

$3483: E5 64 04  mov   a,$0464      ;\ Else ([sound 2 channel 0 note] < [sound 2 channel 0 target note]):
$3486: 60        clrc               ;|
$3487: 85 63 04  adc   a,$0463      ;} Sound 2 channel 0 subnote += [sound 2 channel 0 subnote delta]
$348A: C5 63 04  mov   $0463,a      ;/
$348D: 90 13     bcc   $34A2        ; If [sound 2 channel 0 subnote] >= 100h:
$348F: AC 62 04  inc   $0462        ; Increment sound 2 channel 0 note
$3492: E5 65 04  mov   a,$0465      ;\
$3495: 65 62 04  cmp   a,$0462      ;} If [sound 2 channel 0 target note] = [sound 2 channel 0 note]:
$3498: D0 08     bne   $34A2        ;/
$349A: E8 00     mov   a,#$00       ;\
$349C: C5 66 04  mov   $0466,a      ;} Disable sound 2 channel 0 pitch slide
$349F: C5 67 04  mov   $0467,a      ; Disable sound 2 channel 0 legato

$34A2: E5 63 04  mov   a,$0463      ;\
$34A5: EC 62 04  mov   y,$0462      ;} $11.$10 = [sound 2 channel 0 note]
$34A8: DA 10     movw  $10,ya       ;/
$34AA: E9 4B 04  mov   x,$044B      ; X = [sound 2 channel 0 voice index]
$34AD: 3F B1 16  call  $16B1        ; Play note
}

; BRANCH_CHANNEL_0_END
$34B0: E8 FF     mov   a,#$FF       ;\
$34B2: 65 FF 03  cmp   a,$03FF      ;} If [sound 2 channel 1 disable byte] = FFh:
$34B5: D0 03     bne   $34BA        ;/
$34B7: 5F 6C 36  jmp   $366C        ; Return

; Channel 1
{
$34BA: 8C FD 03  dec   $03FD        ; Decrement sound 2 channel 1 instruction timer
$34BD: F0 03     beq   $34C2        ; If [sound 2 channel 1 instruction timer] != 0:
$34BF: 5F FF 35  jmp   $35FF        ; Go to BRANCH_PROCESS_CHANNEL_1_INSTRUCTION_END

$34C2: E5 6E 04  mov   a,$046E      ;\
$34C5: F0 02     beq   $34C9        ;} If sound 2 channel 1 legato flag enabled:
$34C7: 2F 43     bra   $350C        ; Go to LOOP_CHANNEL_1_COMMANDS

$34C9: E8 00     mov   a,#$00       ;\
$34CB: C5 6D 04  mov   $046D,a      ;} Disable sound 2 channel 1 pitch slide
$34CE: C5 6B 04  mov   $046B,a      ; Sound 2 channel 1 subnote delta = 0
$34D1: C5 6C 04  mov   $046C,a      ; Sound 2 channel 1 target note = 0
$34D4: E8 FF     mov   a,#$FF       ;\
$34D6: 65 56 04  cmp   a,$0456      ;} If [sound 2 channel 1 release flag] != FFh:
$34D9: F0 16     beq   $34F1        ;/
$34DB: E5 47 04  mov   a,$0447      ;\
$34DE: 04 46     or    a,$46        ;} Key off flags |= [sound 2 channel 1 voice bitset]
$34E0: C4 46     mov   $46,a        ;/
$34E2: E8 02     mov   a,#$02       ;\
$34E4: C5 57 04  mov   $0457,a      ;} Sound 2 channel 1 release timer = 2
$34E7: E8 01     mov   a,#$01       ;\
$34E9: C5 FD 03  mov   $03FD,a      ;} Sound 2 channel 1 instruction timer = 1
$34EC: E8 FF     mov   a,#$FF       ;\
$34EE: C5 56 04  mov   $0456,a      ;} Sound 2 channel 1 release flag = FFh

$34F1: 8C 57 04  dec   $0457        ; Decrement sound 2 channel 1 release timer
$34F4: F0 03     beq   $34F9        ; If [sound 2 channel 1 release timer] != 0:
$34F6: 5F 6C 36  jmp   $366C        ; Return

$34F9: E8 00     mov   a,#$00       ;\
$34FB: C5 56 04  mov   $0456,a      ;} Sound 2 channel 1 release flag = 0
$34FE: E5 49 04  mov   a,$0449      ;\
$3501: 24 47     and   a,$47        ;} Current music voice bitset &= [sound 2 channel 1 voice mask]
$3503: C4 47     mov   $47,a        ;/
$3505: E5 49 04  mov   a,$0449      ;\
$3508: 24 49     and   a,$49        ;} Noise enable flags &= [sound 2 channel 1 voice mask]
$350A: C4 49     mov   $49,a        ;/

; LOOP_CHANNEL_1_COMMANDS
$350C: 3F 0D 37  call  $370D        ; A = next sound 2 channel 1 data byte
$350F: 68 F9     cmp   a,#$F9       ;\
$3511: D0 14     bne   $3527        ;} If [A] = F9h:
$3513: 3F 0D 37  call  $370D        ;\
$3516: C5 5E 04  mov   $045E,a      ;} Sound 2 channel 1 ADSR settings = next sound 2 channel 1 data byte
$3519: 3F 0D 37  call  $370D        ;\
$351C: C5 5F 04  mov   $045F,a      ;} Sound 2 channel 1 ADSR settings |= next sound 2 channel 1 data byte << 8
$351F: E8 FF     mov   a,#$FF       ;\
$3521: C5 61 04  mov   $0461,a      ;} Sound 2 channel 1 update ADSR settings flag = FFh
$3524: 5F 0C 35  jmp   $350C        ; Go to LOOP_CHANNEL_1_COMMANDS

$3527: 68 F5     cmp   a,#$F5       ;\
$3529: D0 05     bne   $3530        ;} If [A] = F5h:
$352B: C5 6F 04  mov   $046F,a      ; Enable sound 2 channel 1 pitch slide legato
$352E: 2F 09     bra   $3539

$3530: 68 F8     cmp   a,#$F8       ;\ Else ([A] != F5h):
$3532: D0 19     bne   $354D        ;} If [A] != F8h: go to BRANCH_CHANNEL_1_PITCH_SLIDE_END
$3534: E8 00     mov   a,#$00       ;\
$3536: C5 6F 04  mov   $046F,a      ;} Disable sound 2 channel 1 pitch slide legato

$3539: 3F 0D 37  call  $370D        ;\
$353C: C5 6B 04  mov   $046B,a      ;} Sound 2 channel 1 subnote delta = next sound 2 channel 1 data byte
$353F: 3F 0D 37  call  $370D        ;\
$3542: C5 6C 04  mov   $046C,a      ;} Sound 2 channel 1 target note = next sound 2 channel 1 data byte
$3545: E8 FF     mov   a,#$FF       ;\
$3547: C5 6D 04  mov   $046D,a      ;} Enable sound 2 channel 1 pitch slide = FFh
$354A: 3F 0D 37  call  $370D        ; A = next sound 2 channel 1 data byte

; BRANCH_CHANNEL_1_PITCH_SLIDE_END
$354D: 68 FF     cmp   a,#$FF       ;\
$354F: D0 06     bne   $3557        ;} If [A] = FFh:
$3551: 3F B0 36  call  $36B0        ; Reset sound 2 channel 1
$3554: 5F 6C 36  jmp   $366C        ; Return

$3557: 68 FE     cmp   a,#$FE       ;\
$3559: D0 0F     bne   $356A        ;} If [A] = FEh:
$355B: 3F 0D 37  call  $370D        ;\
$355E: C5 59 04  mov   $0459,a      ;} Sound 2 channel 1 repeat counter = next sound 2 channel 1 data byte
$3561: E5 FB 03  mov   a,$03FB      ;\
$3564: C5 5B 04  mov   $045B,a      ;} Sound 2 channel 1 repeat point = [sound 2 channel 1 instruction list index]
$3567: 3F 0D 37  call  $370D        ; A = next sound 2 channel 1 data byte

$356A: 68 FD     cmp   a,#$FD       ;\
$356C: D0 11     bne   $357F        ;} If [A] != FDh: go to BRANCH_CHANNEL_1_REPEAT_COMMAND
$356E: 8C 59 04  dec   $0459        ; Decrement sound 2 channel 1 repeat counter
$3571: D0 03     bne   $3576        ; If [sound 2 channel 1 repeat counter] = 0:
$3573: 5F 0C 35  jmp   $350C        ; Go to LOOP_CHANNEL_1_COMMANDS

; LOOP_CHANNEL_1_REPEAT_COMMAND
$3576: E5 5B 04  mov   a,$045B      ;\
$3579: C5 FB 03  mov   $03FB,a      ;} Sound 2 channel 1 instruction list index = [sound 2 channel 1 repeat point]
$357C: 3F 0D 37  call  $370D        ; A = next sound 2 channel 1 data byte

; BRANCH_CHANNEL_1_REPEAT_COMMAND
$357F: 68 FB     cmp   a,#$FB       ;\
$3581: D0 03     bne   $3586        ;} If [A] = FBh:
$3583: 5F 76 35  jmp   $3576        ; Go to LOOP_CHANNEL_1_REPEAT_COMMAND

$3586: 68 FC     cmp   a,#$FC       ;\
$3588: D0 0A     bne   $3594        ;} If [A] = FCh:
$358A: E5 47 04  mov   a,$0447      ;\
$358D: 04 49     or    a,$49        ;} Noise enable flags |= [sound 2 channel 1 voice bitset]
$358F: C4 49     mov   $49,a        ;/
$3591: 5F 0C 35  jmp   $350C        ; Go to LOOP_CHANNEL_1_COMMANDS

$3594: E9 4C 04  mov   x,$044C      ; X = [sound 2 channel 1 voice index]
$3597: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$359A: 3F 0D 37  call  $370D        ;\
$359D: E9 4C 04  mov   x,$044C      ;} Track output volume = next sound 2 channel 1 data byte
$35A0: D5 21 03  mov   $0321+x,a    ;/
$35A3: E8 00     mov   a,#$00       ;\
$35A5: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = 0
$35A8: 3F 0D 37  call  $370D        ;\
$35AB: C4 11     mov   $11,a        ;} $10 = (next sound 2 channel 1 data byte) * 100h
$35AD: 8F 00 10  mov   $10,#$00     ;/
$35B0: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled
$35B3: 3F 0D 37  call  $370D        ; A = next sound 2 channel 1 data byte
$35B6: 68 F6     cmp   a,#$F6       ;\
$35B8: F0 08     beq   $35C2        ;} If [A] != F6h:
$35BA: C5 69 04  mov   $0469,a      ; Sound 2 channel 1 note = [A]
$35BD: E8 00     mov   a,#$00       ;\
$35BF: C5 6A 04  mov   $046A,a      ;} Sound 2 channel 1 subnote = 0

$35C2: EC 69 04  mov   y,$0469      ;\
$35C5: E5 6A 04  mov   a,$046A      ;} $11.$10 = [sound 2 channel 1 note]
$35C8: DA 10     movw  $10,ya       ;/
$35CA: E9 4C 04  mov   x,$044C      ; X = [sound 2 channel 1 voice index]
$35CD: 3F B1 16  call  $16B1        ; Play note
$35D0: 3F 0D 37  call  $370D        ;\
$35D3: C5 FD 03  mov   $03FD,a      ;} Sound 2 channel 1 instruction timer = next sound 2 channel 1 data byte
$35D6: E5 61 04  mov   a,$0461      ;\
$35D9: F0 18     beq   $35F3        ;} If [sound 2 channel 1 update ADSR settings flag] != 0:
$35DB: E5 4F 04  mov   a,$044F      ;\
$35DE: 08 05     or    a,#$05       ;|
$35E0: FD        mov   y,a          ;|
$35E1: E5 5E 04  mov   a,$045E      ;|
$35E4: 3F 26 17  call  $1726        ;|
$35E7: E5 4F 04  mov   a,$044F      ;} DSP sound 2 channel 1 ADSR settings = [sound 2 channel 1 ADSR settings]
$35EA: 08 06     or    a,#$06       ;|
$35EC: FD        mov   y,a          ;|
$35ED: E5 5F 04  mov   a,$045F      ;|
$35F0: 3F 26 17  call  $1726        ;/

$35F3: E5 6E 04  mov   a,$046E      ;\
$35F6: D0 07     bne   $35FF        ;} If sound 2 channel 1 legato disabled:
$35F8: E5 47 04  mov   a,$0447      ;\
$35FB: 04 45     or    a,$45        ;} Key on flags |= [sound 2 channel 1 voice bitset]
$35FD: C4 45     mov   $45,a        ;/

; BRANCH_PROCESS_CHANNEL_1_INSTRUCTION_END
$35FF: 00        nop
$3600: 00        nop
$3601: 00        nop
$3602: 00        nop
$3603: 00        nop
$3604: 00        nop
$3605: E5 6D 04  mov   a,$046D      ;\
$3608: 68 FF     cmp   a,#$FF       ;} If sound 2 channel 1 pitch slide disabled: return
$360A: D0 60     bne   $366C        ;/
$360C: E5 6F 04  mov   a,$046F      ;\
$360F: F0 05     beq   $3616        ;} If sound 2 channel 1 pitch slide legato enabled:
$3611: E8 FF     mov   a,#$FF       ;\
$3613: C5 6E 04  mov   $046E,a      ;} Enable sound 2 channel 1 legato

$3616: E5 69 04  mov   a,$0469      ;\
$3619: 65 6C 04  cmp   a,$046C      ;} If [sound 2 channel 1 note] >= [sound 2 channel 1 target note]:
$361C: 90 21     bcc   $363F        ;/
$361E: E5 6A 04  mov   a,$046A      ;\
$3621: 80        setc               ;|
$3622: A5 6B 04  sbc   a,$046B      ;} Sound 2 channel 1 subnote -= [sound 2 channel 1 subnote delta]
$3625: C5 6A 04  mov   $046A,a      ;/
$3628: B0 34     bcs   $365E        ; If [sound 2 channel 1 subnote] < 0:
$362A: 8C 69 04  dec   $0469        ; Decrement sound 2 channel 1 note
$362D: E5 6C 04  mov   a,$046C      ;\
$3630: 65 69 04  cmp   a,$0469      ;} If [sound 2 channel 1 target note] = [sound 2 channel 1 note]:
$3633: D0 29     bne   $365E        ;/
$3635: E8 00     mov   a,#$00       ;\
$3637: C5 6D 04  mov   $046D,a      ;} Disable sound 2 channel 1 pitch slide
$363A: C5 6E 04  mov   $046E,a      ; Disable sound 2 channel 1 legato
$363D: 2F 1F     bra   $365E

$363F: E5 6B 04  mov   a,$046B      ;\ Else ([sound 2 channel 1 note] < [sound 2 channel 1 target note]):
$3642: 60        clrc               ;|
$3643: 85 6A 04  adc   a,$046A      ;} Sound 2 channel 1 subnote += [sound 2 channel 1 subnote delta]
$3646: C5 6A 04  mov   $046A,a      ;/
$3649: 90 13     bcc   $365E        ; If [sound 2 channel 1 subnote] >= 100h:
$364B: AC 69 04  inc   $0469        ; Increment sound 2 channel 1 note
$364E: E5 6C 04  mov   a,$046C      ;\
$3651: 65 69 04  cmp   a,$0469      ;} If [sound 2 channel 1 target note] = [sound 2 channel 1 note]:
$3654: D0 08     bne   $365E        ;/
$3656: E8 00     mov   a,#$00       ;\
$3658: C5 6D 04  mov   $046D,a      ;} Disable sound 2 channel 1 pitch slide
$365B: C5 6E 04  mov   $046E,a      ; Disable sound 2 channel 1 legato

$365E: E5 6A 04  mov   a,$046A      ;\
$3661: EC 69 04  mov   y,$0469      ;} $11.$10 = [sound 2 channel 1 note]
$3664: DA 10     movw  $10,ya       ;/
$3666: E9 4C 04  mov   x,$044C      ; X = [sound 2 channel 1 voice index]
$3669: 3F B1 16  call  $16B1        ; Play note
}

$366C: 6F        ret
}


;;; $366D: Reset sound 2 channel 0 ;;;
{
$366D: E8 FF     mov   a,#$FF       ;\
$366F: C5 FE 03  mov   $03FE,a      ;} Sound 2 channel 0 disable byte = FFh
$3672: E8 00     mov   a,#$00       ;\
$3674: C5 60 04  mov   $0460,a      ;} Sound 2 channel 0 update ADSR settings flag = 0
$3677: E5 4D 04  mov   a,$044D      ;\
$367A: 25 48 04  and   a,$0448      ;} Sound 2 enabled voices &= [sound 2 channel 0 mask]
$367D: C5 4D 04  mov   $044D,a      ;/
$3680: E4 1A     mov   a,$1A        ;\
$3682: 25 48 04  and   a,$0448      ;} Enabled sound effect voices &= [sound 2 channel 0 mask]
$3685: C4 1A     mov   $1A,a        ;/
$3687: E4 47     mov   a,$47        ;\
$3689: 05 46 04  or    a,$0446      ;} Current music voice bitset |= [sound 2 channel 0 voice bitset]
$368C: C4 47     mov   $47,a        ;/
$368E: E4 46     mov   a,$46        ;\
$3690: 05 46 04  or    a,$0446      ;} Key off flags |= [sound 2 channel 0 voice bitset]
$3693: C4 46     mov   $46,a        ;/
$3695: E9 4B 04  mov   x,$044B      ; X = [sound 2 channel 0 voice index]
$3698: F5 11 02  mov   a,$0211+x    ; A = [track instrument index]
$369B: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$369E: E9 4B 04  mov   x,$044B      ; X = [sound 2 channel 0 voice index]
$36A1: E5 50 04  mov   a,$0450      ;\
$36A4: D5 21 03  mov   $0321+x,a    ;} Track output volume = [sound 2 channel 0 backup of track output volume]
$36A7: E5 51 04  mov   a,$0451      ;\
$36AA: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = [sound 2 channel 0 backup of track phase inversion options]
$36AD: 5F F3 36  jmp   $36F3        ; Go to reset sound 2 if no enabled voices
}


;;; $36B0: Reset sound 2 channel 1 ;;;
{
$36B0: E8 FF     mov   a,#$FF       ;\
$36B2: C5 FF 03  mov   $03FF,a      ;} Sound 2 channel 1 disable byte = FFh
$36B5: E8 00     mov   a,#$00       ;\
$36B7: C5 61 04  mov   $0461,a      ;} Sound 2 channel 1 update ADSR settings flag = 0
$36BA: E5 4D 04  mov   a,$044D      ;\
$36BD: 25 49 04  and   a,$0449      ;} Sound 2 enabled voices &= [sound 2 channel 1 mask]
$36C0: C5 4D 04  mov   $044D,a      ;/
$36C3: E4 1A     mov   a,$1A        ;\
$36C5: 25 49 04  and   a,$0449      ;} Enabled sound effect voices &= [sound 2 channel 1 mask]
$36C8: C4 1A     mov   $1A,a        ;/
$36CA: E4 47     mov   a,$47        ;\
$36CC: 05 47 04  or    a,$0447      ;} Current music voice bitset |= [sound 2 channel 1 voice bitset]
$36CF: C4 47     mov   $47,a        ;/
$36D1: E4 46     mov   a,$46        ;\
$36D3: 05 47 04  or    a,$0447      ;} Key off flags |= [sound 2 channel 1 voice bitset]
$36D6: C4 46     mov   $46,a        ;/
$36D8: E9 4C 04  mov   x,$044C      ; X = [sound 2 channel 1 voice index]
$36DB: F5 11 02  mov   a,$0211+x    ; A = [track instrument index]
$36DE: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$36E1: E9 4C 04  mov   x,$044C      ; X = [sound 2 channel 1 voice index]
$36E4: E5 52 04  mov   a,$0452      ;\
$36E7: D5 21 03  mov   $0321+x,a    ;} Track output volume = [sound 2 channel 1 backup of track output volume]
$36EA: E5 53 04  mov   a,$0453      ;\
$36ED: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = [sound 2 channel 1 backup of track phase inversion options]
$36F0: 5F F3 36  jmp   $36F3        ; Go to reset sound 2 if no enabled voices
}


;;; $36F3: Reset sound 2 if no enabled voices ;;;
{
; Merge point of reset sound 2 channel routines
$36F3: E5 4D 04  mov   a,$044D      ;\
$36F6: D0 0B     bne   $3703        ;} If [sound 2 enabled voices] = 0:
$36F8: E8 00     mov   a,#$00       ;\
$36FA: C5 F8 03  mov   $03F8,a      ;} Current sound 2 = 0
$36FD: C5 BC 04  mov   $04BC,a      ; Sound 2 priority = 0
$3700: C5 44 04  mov   $0444,a      ; Sound 2 initialisation flag = 0

$3703: 6F        ret
}


;;; $3704: A = next sound 2 channel 0 data byte ;;;
{
$3704: EC FA 03  mov   y,$03FA      ;\
$3707: F7 DC     mov   a,($DC)+y    ;} A = [[$DC] + [$03FA++]]
$3709: AC FA 03  inc   $03FA        ;/
$370C: 6F        ret
}


;;; $370D: A = next sound 2 channel 1 data byte ;;;
{
$370D: EC FB 03  mov   y,$03FB      ;\
$3710: F7 DE     mov   a,($DE)+y    ;} A = [[$DE] + [$03FB++]]
$3712: AC FB 03  inc   $03FB        ;/
$3715: 6F        ret
}


;;; $3716: Sound 2 channel variable pointers ;;;
{
$3716:           dw 0446,0447 ; Sound 2 channel voice bitsets
$371A:           dw 0448,0449 ; Sound 2 channel voice masks
$371E:           dw 044B,044C ; Sound 2 channel voice indices
}


;;; $3722: Sound 2 initialisation ;;;
{
$3722: E8 09     mov   a,#$09       ;\
$3724: C5 45 04  mov   $0445,a      ;} Voice ID = 9
$3727: E4 1A     mov   a,$1A        ;\
$3729: C5 43 04  mov   $0443,a      ;} Remaining enabled sound effect voices = [enabled sound effect voices]
$372C: E8 FF     mov   a,#$FF       ;\
$372E: C5 44 04  mov   $0444,a      ;} Sound 2 initialisation flag = FFh
$3731: E8 00     mov   a,#$00
$3733: C5 4A 04  mov   $044A,a      ; Sound 2 channel index * 2 = 0
$3736: C5 40 04  mov   $0440,a      ; Sound 2 channel index = 0
$3739: C5 46 04  mov   $0446,a      ;\
$373C: C5 47 04  mov   $0447,a      ;} Sound 2 channel voice bitsets = 0
$373F: C5 4B 04  mov   $044B,a      ;\
$3742: C5 4C 04  mov   $044C,a      ;} Sound 2 channel voice indices = 0
$3745: E8 FF     mov   a,#$FF
$3747: C5 48 04  mov   $0448,a      ;\
$374A: C5 49 04  mov   $0449,a      ;} Sound 2 channel voice masks = FFh
$374D: C5 FE 03  mov   $03FE,a      ;\
$3750: C5 FF 03  mov   $03FF,a      ;} Sound 2 channel disable bytes = FFh

; LOOP
$3753: 8C 45 04  dec   $0445        ; Decrement voice ID
$3756: F0 7E     beq   $37D6        ; If [voice ID] = 0: return
$3758: 0C 43 04  asl   $0443        ; Remaining enabled sound effect voices <<= 1
$375B: B0 F6     bcs   $3753        ; If carry set: go to LOOP
$375D: E8 00     mov   a,#$00       ;\
$375F: 65 41 04  cmp   a,$0441      ;} If [number of sound 2 voices to set up] = 0: return
$3762: F0 72     beq   $37D6        ;/
$3764: 8C 41 04  dec   $0441        ; Decrement number of sound 2 voices to set up
$3767: E8 00     mov   a,#$00       ;\
$3769: E9 40 04  mov   x,$0440      ;} Sound 2 channel disable byte = 0
$376C: D5 FE 03  mov   $03FE+x,a    ;/
$376F: AC 40 04  inc   $0440        ; Increment sound 2 channel index
$3772: E5 4A 04  mov   a,$044A      ;\
$3775: 5D        mov   x,a          ;} Y = [sound 2 channel index] * 2
$3776: FD        mov   y,a          ;/
$3777: F5 16 37  mov   a,$3716+x    ;\
$377A: C4 D6     mov   $D6,a        ;} $D6 = sound 2 channel voice bitset
$377C: F5 1A 37  mov   a,$371A+x    ;\
$377F: C4 D8     mov   $D8,a        ;} $D8 = sound 2 channel voice mask
$3781: F5 1E 37  mov   a,$371E+x    ;\
$3784: C4 DA     mov   $DA,a        ;} $DA = sound 2 channel voice index
$3786: 3D        inc   x
$3787: F5 16 37  mov   a,$3716+x
$378A: C4 D7     mov   $D7,a
$378C: F5 1A 37  mov   a,$371A+x
$378F: C4 D9     mov   $D9,a
$3791: F5 1E 37  mov   a,$371E+x
$3794: C4 DB     mov   $DB,a
$3796: AC 4A 04  inc   $044A        ;\
$3799: AC 4A 04  inc   $044A        ;} Sound 2 channel index * 2 += 2
$379C: E5 45 04  mov   a,$0445      ;\
$379F: C5 42 04  mov   $0442,a      ;|
$37A2: 8C 42 04  dec   $0442        ;} Voice index = ([voice ID] - 1) * 2
$37A5: 60        clrc               ;|
$37A6: 0C 42 04  asl   $0442        ;/
$37A9: E9 42 04  mov   x,$0442      ;\
$37AC: F5 21 03  mov   a,$0321+x    ;} Sound 2 channel backup of track output volume = [track output volume]
$37AF: D6 50 04  mov   $0450+y,a    ;/
$37B2: FC        inc   y            ;\
$37B3: F5 51 03  mov   a,$0351+x    ;} Sound 2 channel backup of track phase inversion options = [track phase inversion options]
$37B6: D6 50 04  mov   $0450+y,a    ;/
$37B9: 8D 00     mov   y,#$00       ;\
$37BB: E5 42 04  mov   a,$0442      ;} Sound 2 channel voice index = [voice index]
$37BE: D7 DA     mov   ($DA)+y,a    ;/
$37C0: E5 45 04  mov   a,$0445      ;\
$37C3: 3F 75 28  call  $2875        ;} Go to [$37C6 + [voice index]]
$37C6:           dw 3894, 3879, 385E, 3843, 3828, 380D, 37F2, 37D7

$37D6: 6F        ret

$37D7: E2 1A     set7  $1A          ; Enable voice 7
$37D9: F2 47     clr7  $47          ; Current music voice bitset &= ~80h
$37DB: F2 4A     clr7  $4A          ; Disable echo on voice 7
$37DD: E8 80     mov   a,#$80       ;\
$37DF: 05 4D 04  or    a,$044D      ;} Sound 2 enabled voices |= 80h
$37E2: C5 4D 04  mov   $044D,a      ;/
$37E5: 8D 00     mov   y,#$00
$37E7: E8 80     mov   a,#$80       ;\
$37E9: D7 D6     mov   ($D6)+y,a    ;} Sound 2 channel voice bitset = 80h
$37EB: E8 7F     mov   a,#$7F       ;\
$37ED: D7 D8     mov   ($D8)+y,a    ;} Sound 2 channel voice mask = ~80h
$37EF: 5F 53 37  jmp   $3753        ; Go to LOOP

$37F2: C2 1A     set6  $1A          ; Enable voice 6
$37F4: D2 47     clr6  $47          ; Current music voice bitset &= ~40h
$37F6: D2 4A     clr6  $4A          ; Disable echo on voice 6
$37F8: E8 40     mov   a,#$40       ;\
$37FA: 05 4D 04  or    a,$044D      ;} Sound 2 enabled voices |= 40h
$37FD: C5 4D 04  mov   $044D,a      ;/
$3800: 8D 00     mov   y,#$00
$3802: E8 40     mov   a,#$40       ;\
$3804: D7 D6     mov   ($D6)+y,a    ;} Sound 2 channel voice bitset = 40h
$3806: E8 BF     mov   a,#$BF       ;\
$3808: D7 D8     mov   ($D8)+y,a    ;} Sound 2 channel voice mask = ~40h
$380A: 5F 53 37  jmp   $3753        ; Go to LOOP

$380D: A2 1A     set5  $1A          ; Enable voice 5
$380F: B2 47     clr5  $47          ; Current music voice bitset &= ~20h
$3811: B2 4A     clr5  $4A          ; Disable echo on voice 5
$3813: E8 20     mov   a,#$20       ;\
$3815: 05 4D 04  or    a,$044D      ;} Sound 2 enabled voices |= 20h
$3818: C5 4D 04  mov   $044D,a      ;/
$381B: 8D 00     mov   y,#$00
$381D: E8 20     mov   a,#$20       ;\
$381F: D7 D6     mov   ($D6)+y,a    ;} Sound 2 channel voice bitset = 20h
$3821: E8 DF     mov   a,#$DF       ;\
$3823: D7 D8     mov   ($D8)+y,a    ;} Sound 2 channel voice mask = ~20h
$3825: 5F 53 37  jmp   $3753        ; Go to LOOP

$3828: 82 1A     set4  $1A          ; Enable voice 4
$382A: 92 47     clr4  $47          ; Current music voice bitset &= ~10h
$382C: 92 4A     clr4  $4A          ; Disable echo on voice 4
$382E: E8 10     mov   a,#$10       ;\
$3830: 05 4D 04  or    a,$044D      ;} Sound 2 enabled voices |= 10h
$3833: C5 4D 04  mov   $044D,a      ;/
$3836: 8D 00     mov   y,#$00
$3838: E8 10     mov   a,#$10       ;\
$383A: D7 D6     mov   ($D6)+y,a    ;} Sound 2 channel voice bitset = 10h
$383C: E8 EF     mov   a,#$EF       ;\
$383E: D7 D8     mov   ($D8)+y,a    ;} Sound 2 channel voice mask = ~10h
$3840: 5F 53 37  jmp   $3753        ; Go to LOOP

$3843: 62 1A     set3  $1A          ; Enable voice 3
$3845: 72 47     clr3  $47          ; Current music voice bitset &= ~8
$3847: 72 4A     clr3  $4A          ; Disable echo on voice 3
$3849: E8 08     mov   a,#$08       ;\
$384B: 05 4D 04  or    a,$044D      ;} Sound 2 enabled voices |= 8
$384E: C5 4D 04  mov   $044D,a      ;/
$3851: 8D 00     mov   y,#$00
$3853: E8 08     mov   a,#$08       ;\
$3855: D7 D6     mov   ($D6)+y,a    ;} Sound 2 channel voice bitset = 8
$3857: E8 F7     mov   a,#$F7       ;\
$3859: D7 D8     mov   ($D8)+y,a    ;} Sound 2 channel voice mask = ~8
$385B: 5F 53 37  jmp   $3753        ; Go to LOOP

$385E: 42 1A     set2  $1A          ; Enable voice 2
$3860: 52 47     clr2  $47          ; Current music voice bitset &= ~4
$3862: 52 4A     clr2  $4A          ; Disable echo on voice 2
$3864: E8 04     mov   a,#$04       ;\
$3866: 05 4D 04  or    a,$044D      ;} Sound 2 enabled voices |= 4
$3869: C5 4D 04  mov   $044D,a      ;/
$386C: 8D 00     mov   y,#$00
$386E: E8 04     mov   a,#$04       ;\
$3870: D7 D6     mov   ($D6)+y,a    ;} Sound 2 channel voice bitset = 4
$3872: E8 FB     mov   a,#$FB       ;\
$3874: D7 D8     mov   ($D8)+y,a    ;} Sound 2 channel voice mask = ~4
$3876: 5F 53 37  jmp   $3753        ; Go to LOOP

$3879: 22 1A     set1  $1A          ; Enable voice 1
$387B: 32 47     clr1  $47          ; Current music voice bitset &= ~2
$387D: 32 4A     clr1  $4A          ; Disable echo on voice 1
$387F: E8 02     mov   a,#$02       ;\
$3881: 05 4D 04  or    a,$044D      ;} Sound 2 enabled voices |= 2
$3884: C5 4D 04  mov   $044D,a      ;/
$3887: 8D 00     mov   y,#$00
$3889: E8 02     mov   a,#$02       ;\
$388B: D7 D6     mov   ($D6)+y,a    ;} Sound 2 channel voice bitset = 2
$388D: E8 FD     mov   a,#$FD       ;\
$388F: D7 D8     mov   ($D8)+y,a    ;} Sound 2 channel voice mask = ~2
$3891: 5F 53 37  jmp   $3753        ; Go to LOOP

$3894: 02 1A     set0  $1A          ; Enable voice 0
$3896: 12 47     clr0  $47          ; Current music voice bitset &= ~1
$3898: 12 4A     clr0  $4A          ; Disable echo on voice 0
$389A: E8 01     mov   a,#$01       ;\
$389C: 05 4D 04  or    a,$044D      ;} Sound 2 enabled voices |= 1
$389F: C5 4D 04  mov   $044D,a      ;/
$38A2: 8D 00     mov   y,#$00
$38A4: E8 01     mov   a,#$01       ;\
$38A6: D7 D6     mov   ($D6)+y,a    ;} Sound 2 channel voice bitset = 1
$38A8: E8 FE     mov   a,#$FE       ;\
$38AA: D7 D8     mov   ($D8)+y,a    ;} Sound 2 channel voice mask = ~1
$38AC: 5F 53 37  jmp   $3753        ; Go to LOOP
}


;;; $38AF: A = next sound 2 channel instruction list pointer ;;;
{
$38AF: FC        inc   y            ;\
$38B0: F7 D4     mov   a,($D4)+y    ;} A = [[$D4] + [++Y]]
$38B2: 6F        ret
}


;;; $38B3: A *= 8 ;;;
{
$38B3: 1C        asl   a
$38B4: 1C        asl   a
$38B5: 1C        asl   a
$38B6: 6F        ret
}


;;; $38B7: Sound 2 configurations ;;;
{
;;; $38B7: Sound 2 configuration - sounds 1..5 - number of voices = 1, priority = 1 ;;;
{
; 1: Collected small health drop
; 2: Collected big health drop
; 3: Collected missile drop
; 4: Collected super missile drop
; 5: Collected power bomb drop
$38B7: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$38BA: 6F        ret
}


;;; $38BB: Sound 2 configuration - sounds 6/7 - number of voices = 1, priority = 0 ;;;
{
; 6: Block destroyed by contact damage
; 7: (Super) missile hit wall
$38BB: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$38BE: 6F        ret
}


;;; $38BF: Sound 2 configuration - sound 8 - number of voices = 1, priority = 0 ;;;
{
; 8: Bomb explosion
$38BF: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$38C2: 6F        ret
}


;;; $38C3: Sound 2 configuration - sounds 9..15h - number of voices = 1, priority = 0 ;;;
{
; 9: Enemy killed
; Ah: Block crumbled
; Bh: Enemy killed by contact damage
; Ch: Beam hit wall / torizo statue crumbles
; Dh: Splashed into water
; Eh: Splashed out of water
; Fh: Low pitched air bubbles
; 10h: Lava/acid damaging Samus
; 11h: High pitched air bubbles
; 12h: Lava bubbling 1
; 13h: Lava bubbling 2
; 14h: Lava bubbling 3
; 15h: Maridia elevatube
$38C3: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$38C6: 6F        ret
}


;;; $38C7: Sound 2 configuration - sounds 16h/17h - number of voices = 1, priority = 1 ;;;
{
; 16h: Fake Kraid cry
; 17h: Morph ball eye's ray
$38C7: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$38CA: 6F        ret
}


;;; $38CB: Sound 2 configuration - sound 18h - number of voices = 1, priority = 0 ;;;
{
; 18h: Beacon
$38CB: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$38CE: 6F        ret
}


;;; $38CF: Sound 2 configuration - sound 19h - number of voices = 2, priority = 0 ;;;
{
; 19h: Tourian statue unlocking particle
$38CF: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$38D2: 6F        ret
}


;;; $38D3: Sound 2 configuration - sound 1Ah - number of voices = 2, priority = 0 ;;;
{
; 1Ah: n00b tube shattering
$38D3: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$38D6: 6F        ret
}


;;; $38D7: Sound 2 configuration - sound 1Bh - number of voices = 1, priority = 0 ;;;
{
; 1Bh: Spike platform stops / tatori hits wall
$38D7: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$38DA: 6F        ret
}


;;; $38DB: Sound 2 configuration - sound 1Ch - number of voices = 1, priority = 1 ;;;
{
; 1Ch: Chozo grabs Samus
$38DB: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$38DE: 6F        ret
}


;;; $38DF: Sound 2 configuration - sound 1Dh - number of voices = 1, priority = 0 ;;;
{
; 1Dh: Dachora cry
$38DF: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$38E2: 6F        ret
}


;;; $38E3: Sound 2 configuration - sound 1Eh - number of voices = 2, priority = 1 ;;;
{
; 1Eh: Unused
$38E3: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$38E6: 6F        ret
}


;;; $38E7: Sound 2 configuration - sound 1Fh - number of voices = 1, priority = 1 ;;;
{
; 1Fh: Fune spits
$38E7: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$38EA: 6F        ret
}


;;; $38EB: Sound 2 configuration - sounds 20h..26h - number of voices = 1, priority = 0 ;;;
{
; 20h: Shot fly
; 21h: Shot skree / wall/ninja space pirate
; 22h: Shot pipe bug / choot / Golden Torizo egg hatches
; 23h: Shot zero / sidehopper / zoomer
; 24h: Small explosion
; 25h: Big explosion
; 26h: Bomb Torizo explosive swipe
$38EB: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$38EE: 6F        ret
}


;;; $38EF: Sound 2 configuration - sound 27h - number of voices = 2, priority = 1 ;;;
{
; 27h: Shot torizo
$38EF: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$38F2: 6F        ret
}


;;; $38F3: Sound 2 configuration - sounds 28h..2Bh - number of voices = 1, priority = 0 ;;;
{
; 28h: Unused
; 29h: Mother Brain rising into phase 2 / Crocomire's wall explodes / Spore Spawn gets hard
; 2Ah: Unused
; 2Bh: Ridley's fireball hit surface / Crocomire post-death rumble / Phantoon exploding
$38F3: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$38F6: 6F        ret
}


;;; $38F7: Sound 2 configuration - sound 2Ch - number of voices = 2, priority = 1 ;;;
{
; 2Ch: Shot Spore Spawn / Spore Spawn opens up
$38F7: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$38FA: 6F        ret
}


;;; $38FB: Sound 2 configuration - sound 2Dh - number of voices = 1, priority = 1 ;;;
{
; 2Dh: Kraid's roar / Crocomire dying cry
$38FB: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$38FE: 6F        ret
}


;;; $38FF: Sound 2 configuration - sound 2Eh - number of voices = 2, priority = 1 ;;;
{
; 2Eh: Kraid's dying cry
$38FF: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$3902: 6F        ret
}


;;; $3903: Sound 2 configuration - sounds 2Fh..34h - number of voices = 1, priority = 0 ;;;
{
; 2Fh: Yapping maw
; 30h: Shot super-desgeega / Crocomire destroys wall
; 31h: Brinstar plant chewing
; 32h: Etecoon wall-jump
; 33h: Etecoon cry
; 34h: Cacatac spikes / Golden Torizo egg released
$3903: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$3906: 6F        ret
}


;;; $3907: Sound 2 configuration - sound 35h - number of voices = 1, priority = 1 ;;;
{
; 35h: Etecoon's theme
$3907: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$390A: 6F        ret
}


;;; $390B: Sound 2 configuration - sound 36h - number of voices = 1, priority = 0 ;;;
{
; 36h: Shot rio / squeept / dragon
$390B: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$390E: 6F        ret
}


;;; $390F: Sound 2 configuration - sounds 37h/38h - number of voices = 2, priority = 0 ;;;
{
; 37h: Refill/map station engaged
; 38h: Refill/map station disengaged
$390F: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$3912: 6F        ret
}


;;; $3913: Sound 2 configuration - sounds 39h..45h - number of voices = 1, priority = 0 ;;;
{
; 39h: Dachora speed booster
; 3Ah: Tatori spinning
; 3Bh: Dachora shinespark
; 3Ch: Dachora shinespark ended
; 3Dh: Dachora stored shinespark
; 3Eh: Shot owtch / viola / ripper / tripper / suspensor platform / yard / yapping maw / atomic
; 3Fh: Alcoon spit / fake Kraid lint / ninja pirate spin jump
; 40h: Unused
; 41h: (Empty)
; 42h: Boulder bounces
; 43h: Boulder explodes
; 44h: (Empty)
; 45h: Typewriter stroke - Ceres self destruct sequence
$3913: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$3916: 6F        ret
}


;;; $3917: Sound 2 configuration - sound 46h - number of voices = 1, priority = 1 ;;;
{
; 46h: Lavaquake
$3917: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$391A: 6F        ret
}


;;; $391B: Sound 2 configuration - sounds 47h..4Dh - number of voices = 1, priority = 0 ;;;
{
; 47h: Shot waver
; 48h: Torizo sonic boom
; 49h: Shot skultera / sciser / zoa
; 4Ah: Shot evir
; 4Bh: Chozo / torizo footsteps
; 4Ch: Ki-hunter spit / eye door acid spit / Draygon goop
; 4Dh: Gunship hover
$391B: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$391E: 6F        ret
}


;;; $391F: Sound 2 configuration - sound 4Eh - number of voices = 2, priority = 1 ;;;
{
; 4Eh: Ceres Ridley getaway
$391F: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$3922: 6F        ret
}


;;; $3923: Sound 2 configuration - sound 4Fh - number of voices = 1, priority = 0 ;;;
{
; 4Fh: Unused
$3923: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$3926: 6F        ret
}


;;; $3927: Sound 2 configuration - sounds 50h/51h - number of voices = 2, priority = 1 ;;;
{
; 50h: Metroid draining Samus / random metroid cry
; 51h: Shot coven
$3927: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$392A: 6F        ret
}


;;; $392B: Sound 2 configuration - sounds 52h/53h - number of voices = 1, priority = 0 ;;;
{
; 52h: Shitroid feels remorse
; 53h: Shot mini-Crocomire
$392B: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$392E: 6F        ret
}


;;; $392F: Sound 2 configuration - sound 54h - number of voices = 1, priority = 1 ;;;
{
; 54h: Unused. Shot Crocomire(?)
$392F: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$3932: 6F        ret
}


;;; $3933: Sound 2 configuration - sound 55h - number of voices = 1, priority = 0 ;;;
{
; 55h: Shot beetom
$3933: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$3936: 6F        ret
}


;;; $3937: Sound 2 configuration - sound 56h - number of voices = 2, priority = 0 ;;;
{
; 56h: Acquired suit
$3937: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$393A: 6F        ret
}


;;; $393B: Sound 2 configuration - sound 57h - number of voices = 1, priority = 0 ;;;
{
; 57h: Shot door/gate with dud shot / shot reflec / shot oum
$393B: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$393E: 6F        ret
}


;;; $393F: Sound 2 configuration - sound 58h - number of voices = 2, priority = 0 ;;;
{
; 58h: Shot mochtroid / random metroid cry
$393F: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$3942: 6F        ret
}


;;; $3943: Sound 2 configuration - sound 59h - number of voices = 2, priority = 1 ;;;
{
; 59h: Ridley's roar
$3943: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$3946: 6F        ret
}


;;; $3947: Sound 2 configuration - sound 5Ah - number of voices = 2, priority = 0 ;;;
{
; 5Ah: Shot metroid / random metroid cry
$3947: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$394A: 6F        ret
}


;;; $394B: Sound 2 configuration - sounds 5Bh..62h - number of voices = 1, priority = 0 ;;;
{
; 5Bh: Skree launches attack
; 5Ch: Skree hits the ground
; 5Dh: Sidehopper jumped
; 5Eh: Sidehopper landed / fire arc part spawns / evir spit / alcoon spawns
; 5Fh: Shot holtz / desgeega / viola / alcoon / Botwoon
; 60h: Unused
; 61h: Dragon / magdollite spit / fire pillar
; 62h: Unused
$394B: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$394E: 6F        ret
}


;;; $394F: Sound 2 configuration - sound 63h - number of voices = 2, priority = 0 ;;;
{
; 63h: Mother Brain's death beam
$394F: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$3952: 6F        ret
}


;;; $3953: Sound 2 configuration - sounds 64h..6Dh - number of voices = 1, priority = 0 ;;;
{
; 64h: Holtz cry
; 65h: Rio cry
; 66h: Shot ki-hunter / shot walking space pirate / space pirate attack
; 67h: Space pirate / Mother Brain / torizo / work robot laser
; 68h: Work robot
; 69h: Shot Shaktool
; 6Ah: Shot powamp
; 6Bh: Unused
; 6Ch: Kago bug
; 6Dh: Ceres tiles falling from ceiling
$3953: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$3956: 6F        ret
}


;;; $3957: Sound 2 configuration - sound 6Eh - number of voices = 2, priority = 1 ;;;
{
; 6Eh: Shot Mother Brain phase 1
$3957: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$395A: 6F        ret
}


;;; $395B: Sound 2 configuration - sound 6Fh - number of voices = 2, priority = 1 ;;;
{
; 6Fh: Mother Brain's cry - low pitch
$395B: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$395E: 6F        ret
}


;;; $395F: Sound 2 configuration - sounds 70h/71h - number of voices = 1, priority = 0 ;;;
{
; 70h: Yard bounce
; 71h: Silence
$395F: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$3962: 6F        ret
}


;;; $3963: Sound 2 configuration - sounds 72h..74h - number of voices = 2, priority = 1 ;;;
{
; 72h: Shitroid's cry
; 73h: Phantoon's cry / Draygon's cry
; 74h: Crocomire's cry
$3963: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$3966: 6F        ret
}


;;; $3967: Sound 2 configuration - sound 75h - number of voices = 2, priority = 1 ;;;
{
; 75h: Crocomire's skeleton collapses
$3967: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$396A: 6F        ret
}


;;; $396B: Sound 2 configuration - sound 76h - number of voices = 1, priority = 0 ;;;
{
; 76h: Quake
$396B: 3F 87 39  call  $3987        ; Number of sound 2 voices = 1, sound 2 priority = 0
$396E: 6F        ret
}


;;; $396F: Sound 2 configuration - sound 77h - number of voices = 2, priority = 1 ;;;
{
; 77h: Crocomire melting cry
$396F: 3F A8 39  call  $39A8        ; Number of sound 2 voices = 2, sound 2 priority = 1
$3972: 6F        ret
}


;;; $3973: Sound 2 configuration - sound 78h - number of voices = 2, priority = 0 ;;;
{
; 78h: Shitroid draining
$3973: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$3976: 6F        ret
}


;;; $3977: Sound 2 configuration - sounds 79h..7Bh - number of voices = 2, priority = 0 ;;;
{
; 79h: Phantoon appears 1
; 7Ah: Phantoon appears 2
; 7Bh: Phantoon appears 3
$3977: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$397A: 6F        ret
}


;;; $397B: Sound 2 configuration - sound 7Ch - number of voices = 1, priority = 1 ;;;
{
; 7Ch: Botwoon spit
$397B: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$397E: 6F        ret
}


;;; $397F: Sound 2 configuration - sounds 7Dh/7Eh - number of voices = 1, priority = 1 ;;;
{
; 7Dh: Shitroid feels guilty
; 7Eh: Mother Brain's cry - high pitch / Phantoon's dying cry
$397F: 3F 92 39  call  $3992        ; Number of sound 2 voices = 1, sound 2 priority = 1
$3982: 6F        ret
}


;;; $3983: Sound 2 configuration - sound 7Fh - number of voices = 2, priority = 0 ;;;
{
; 7Fh: Mother Brain charging her rainbow
$3983: 3F 9D 39  call  $399D        ; Number of sound 2 voices = 2, sound 2 priority = 0
$3986: 6F        ret
}
}


;;; $3987: Number of sound 2 voices = 1, sound 2 priority = 0 ;;;
{
; 6: Block destroyed by contact damage
; 7: (Super) missile hit wall
; 8: Bomb explosion
; 9: Enemy killed
; Ah: Block crumbled
; Bh: Enemy killed by contact damage
; Ch: Beam hit wall / torizo statue crumbles
; Dh: Splashed into water
; Eh: Splashed out of water
; Fh: Low pitched air bubbles
; 10h: Lava/acid damaging Samus
; 11h: High pitched air bubbles
; 12h: Lava bubbling 1
; 13h: Lava bubbling 2
; 14h: Lava bubbling 3
; 15h: Maridia elevatube
; 18h: Beacon
; 1Bh: Spike platform stops / tatori hits wall
; 1Dh: Dachora cry
; 20h: Shot fly
; 21h: Shot skree / wall/ninja space pirate
; 22h: Shot pipe bug / choot / Golden Torizo egg hatches
; 23h: Shot zero / sidehopper / zoomer
; 24h: Small explosion
; 25h: Big explosion
; 26h: Bomb Torizo explosive swipe
; 28h: Unused
; 29h: Mother Brain rising into phase 2 / Crocomire's wall explodes / Spore Spawn gets hard
; 2Ah: Unused
; 2Bh: Ridley's fireball hit surface / Crocomire post-death rumble / Phantoon exploding
; 2Fh: Yapping maw
; 30h: Shot super-desgeega / Crocomire destroys wall
; 31h: Brinstar plant chewing
; 32h: Etecoon wall-jump
; 33h: Etecoon cry
; 34h: Cacatac spikes / Golden Torizo egg released
; 36h: Shot rio / squeept / dragon
; 39h: Dachora speed booster
; 3Ah: Tatori spinning
; 3Bh: Dachora shinespark
; 3Ch: Dachora shinespark ended
; 3Dh: Dachora stored shinespark
; 3Eh: Shot owtch / viola / ripper / tripper / suspensor platform / yard / yapping maw / atomic
; 3Fh: Alcoon spit / fake Kraid lint / ninja pirate spin jump
; 40h: Unused
; 41h: (Empty)
; 42h: Boulder bounces
; 43h: Boulder explodes
; 44h: (Empty)
; 45h: Typewriter stroke - Ceres self destruct sequence
; 47h: Shot waver
; 48h: Torizo sonic boom
; 49h: Shot skultera / sciser / zoa
; 4Ah: Shot evir
; 4Bh: Chozo / torizo footsteps
; 4Ch: Ki-hunter spit / eye door acid spit / Draygon goop
; 4Dh: Gunship hover
; 4Fh: Unused
; 52h: Shitroid feels remorse
; 53h: Shot mini-Crocomire
; 55h: Shot beetom
; 57h: Shot door/gate with dud shot / shot reflec / shot oum
; 5Bh: Skree launches attack
; 5Ch: Skree hits the ground
; 5Dh: Sidehopper jumped
; 5Eh: Sidehopper landed / fire arc part spawns / evir spit / alcoon spawns
; 5Fh: Shot holtz / desgeega / viola / alcoon / Botwoon
; 60h: Unused
; 61h: Dragon / magdollite spit / fire pillar
; 62h: Unused
; 64h: Holtz cry
; 65h: Rio cry
; 66h: Shot ki-hunter / shot walking space pirate / space pirate attack
; 67h: Space pirate / Mother Brain / torizo / work robot laser
; 68h: Work robot
; 69h: Shot Shaktool
; 6Ah: Shot powamp
; 6Bh: Unused
; 6Ch: Kago bug
; 6Dh: Ceres tiles falling from ceiling
; 70h: Yard bounce
; 71h: Silence
; 76h: Quake
$3987: E8 01     mov   a,#$01
$3989: C5 41 04  mov   $0441,a
$398C: E8 00     mov   a,#$00
$398E: C5 BC 04  mov   $04BC,a
$3991: 6F        ret
}


;;; $3992: Number of sound 2 voices = 1, sound 2 priority = 1 ;;;
{
; 1: Collected small health drop
; 2: Collected big health drop
; 3: Collected missile drop
; 4: Collected super missile drop
; 5: Collected power bomb drop
; 16h: Fake Kraid cry
; 17h: Morph ball eye's ray
; 1Ch: Chozo grabs Samus
; 1Fh: Fune spits
; 2Dh: Kraid's roar / Crocomire dying cry
; 35h: Etecoon's theme
; 46h: Lavaquake
; 54h: Unused. Shot Crocomire(?)
; 7Ch: Botwoon spit
; 7Dh: Shitroid feels guilty
; 7Eh: Mother Brain's cry - high pitch / Phantoon's dying cry
$3992: E8 01     mov   a,#$01
$3994: C5 41 04  mov   $0441,a
$3997: E8 01     mov   a,#$01
$3999: C5 BC 04  mov   $04BC,a
$399C: 6F        ret
}


;;; $399D: Number of sound 2 voices = 2, sound 2 priority = 0 ;;;
{
; 19h: Tourian statue unlocking particle
; 1Ah: n00b tube shattering
; 37h: Refill/map station engaged
; 38h: Refill/map station disengaged
; 56h: Acquired suit
; 58h: Shot mochtroid / random metroid cry
; 5Ah: Shot metroid / random metroid cry
; 63h: Mother Brain's death beam
; 78h: Shitroid draining
; 79h: Phantoon appears 1
; 7Ah: Phantoon appears 2
; 7Bh: Phantoon appears 3
; 7Fh: Mother Brain charging her rainbow
$399D: E8 02     mov   a,#$02
$399F: C5 41 04  mov   $0441,a
$39A2: E8 00     mov   a,#$00
$39A4: C5 BC 04  mov   $04BC,a
$39A7: 6F        ret
}


;;; $39A8: Number of sound 2 voices = 2, sound 2 priority = 1 ;;;
{
; 1Eh: Unused
; 27h: Shot torizo
; 2Ch: Shot Spore Spawn / Spore Spawn opens up
; 2Eh: Kraid's dying cry
; 4Eh: Ceres Ridley getaway
; 50h: Metroid draining Samus / random metroid cry
; 51h: Shot coven
; 59h: Ridley's roar
; 6Eh: Shot Mother Brain phase 1
; 6Fh: Mother Brain's cry - low pitch
; 72h: Shitroid's cry
; 73h: Phantoon's cry / Draygon's cry
; 74h: Crocomire's cry
; 75h: Crocomire's skeleton collapses
; 77h: Crocomire melting cry
$39A8: E8 02     mov   a,#$02
$39AA: C5 41 04  mov   $0441,a
$39AD: E8 01     mov   a,#$01
$39AF: C5 BC 04  mov   $04BC,a
$39B2: 6F        ret
}


;;; $39B3: Sound 2 instruction lists ;;;
{
$39B3:           dw 3AB1, 3AC3, 3AD5, 3AF1, 3AF3, 3AF5, 3B0C, 3B28, 3B2A, 3B3A, 3B42, 3B5E, 3B73, 3B85, 3B92, 3BA9,
                    3BB4, 3BC1, 3BE7, 3C08, 3C33, 3C3B, 3C43, 3C56, 3C90, 3CFF, 3D46, 3D4E, 3D65, 3D81, 3D9B, 3DA8,
                    3DBF, 3DD6, 3DED, 3E04, 3E20, 3E41, 3E66, 3E94, 3EB0, 3ECC, 3ECE, 3F03, 3F18, 3F20, 3F44, 3F5E,
                    3F75, 3F82, 3F8A, 3F97, 3F9F, 401F, 4036, 4066, 407C, 407E, 4086, 4088, 408A, 408C, 409E, 40A6,
                    40B1, 40B4, 40BC, 40CE, 40D1, 40DE, 40F5, 410C, 412C, 4143, 415A, 4162, 416F, 41A9, 41C7, 41D9,
                    41EE, 421C, 4224, 4236, 424D, 4268, 4288, 429A, 42AF, 42BF, 42D4, 42D6, 42FE, 4310, 4322, 4334,
                    433C, 4347, 4352, 43C1, 43CC, 43E8, 43FA, 4422, 442F, 4441, 4443, 4446, 444E, 446F, 447F, 448F,
                    449C, 44A4, 44B9, 44CE, 44ED, 4598, 459A, 4618, 462D, 4642, 4657, 466C, 4679, 4686, 468E

; Instruction list format:
{
; Commands:
;     F5h dd tt - legato pitch slide with subnote delta = d, target note = t
;     F8h dd tt -        pitch slide with subnote delta = d, target note = t
;     F9h aaaa - voice's ADSR settings = a
;     FBh - repeat
;     FCh - enable noise
;     FDh - decrement repeat counter and repeat if non-zero
;     FEh cc - set repeat pointer with repeat counter = c
;     FFh - end

; Otherwise:
;     ii vv pp nn tt
;     i: Instrument index
;     v: Volume
;     p: Panning
;     n: Note. F6h is a tie
;     t: Length
}

; Sound 1: Collected small health drop
$3AB1:           dw 3AB3
$3AB3:           db 15,80,0A,C7,0A, 15,50,0A,C7,0A, 15,20,0A,C7,0A, FF

; Sound 2: Collected big health drop
$3AC3:           dw 3AC5
$3AC5:           db 15,E0,0A,C7,0A, 15,60,0A,C7,0A, 15,30,0A,C7,0A, FF

; Sound 3: Collected missile drop
$3AD5:           dw 3AD7

; Collected missile / super missile / power bomb drop
$3AD7:           db 0C,60,0A,AF,02, 0C,00,0A,AF,01, 0C,60,0A,AF,02, 0C,00,0A,AF,01, 0C,60,0A,AF,02, FF

; Sound 4: Collected super missile drop
$3AF1:           dw 3AD7

; Sound 5: Collected power bomb drop
$3AF3:           dw 3AD7

; Sound 6: Block destroyed by contact damage
$3AF5:           dw 3AF7
$3AF7:           db 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,8C,03, FF

; Sound 7: (Super) missile hit wall
$3B0C:           dw 3B0E

; (Super) missile hit wall / bomb explosion
$3B0E:           db 08,E0,0A,98,03, 08,E0,0A,95,03, 08,E0,0A,9A,03, 08,E0,0A,8C,03, 08,E0,0A,8C,20, FF

; Sound 8: Bomb explosion
$3B28:           dw 3B0E

; Sound 9: Enemy killed
$3B2A:           dw 3B2C
$3B2C:           db 08,D0,0A,8B,08, F5,D0,BC, 09,D0,0A,98,10, FF

; Sound Ah: Block crumbled
$3B3A:           dw 3B3C
$3B3C:           db 08,70,0A,9D,07, FF

; Sound Bh: Enemy killed by contact damage
$3B42:           dw 3B44
$3B44:           db 08,D0,0A,99,02, 08,D0,0A,9C,03, 0F,D0,0A,8B,03, 0F,E0,0A,8C,03, 0F,D0,0A,8E,0E, FF

; Sound Ch: Beam hit wall / torizo statue crumbles
$3B5E:           dw 3B60
$3B60:           db 08,70,0A,98,03, 08,70,0A,95,03, F5,F0,BC, 09,70,0A,98,06, FF

; Sound Dh: Splashed into water
$3B73:           dw 3B75
$3B75:           db 0F,70,0A,93,03, 0F,E0,0A,90,08, 0F,70,0A,84,15, FF

; Sound Eh: Splashed out of water
$3B85:           dw 3B87
$3B87:           db 0F,60,0A,90,03, 0F,60,0A,84,15, FF

; Sound Fh: Low pitched air bubbles
$3B92:           dw 3B94
$3B94:           db 0E,60,0A,80,05, 0E,60,0A,85,05, 0E,60,0A,91,05, 0E,60,0A,89,05, FF

; Sound 10h: Lava/acid damaging Samus
$3BA9:           dw 3BAB
$3BAB:           db F5,30,BB, 12,10,0A,95,15, FF

; Sound 11h: High pitched air bubbles
$3BB4:           dw 3BB6
$3BB6:           db 0E,60,0A,8C,05, 0E,60,0A,91,05, FF

; Sound 12h: Lava bubbling 1
$3BC1:           dw 3BC3
$3BC3:           db 22,60,0A,84,1C, 22,60,0A,90,19, 0E,60,0A,80,10, 22,60,0A,89,19, 0E,60,0A,80,07, 0E,60,0A,84,10, 22,60,0A,8B,1B, FF

; Sound 13h: Lava bubbling 2
$3BE7:           dw 3BE9
$3BE9:           db 0E,60,0A,80,0A, 0E,60,0A,84,07, 22,60,0A,8B,1F, 22,60,0A,89,16, 0E,60,0A,80,0A, 0E,60,0A,87,10, FF

; Sound 14h: Lava bubbling 3
$3C08:           dw 3C0A
$3C0A:           db 0E,60,0A,80,0A, 0E,60,0A,87,10, 22,60,0A,84,1A, 0E,60,0A,80,0A, 0E,60,0A,84,07, 22,60,0A,91,16, 0E,60,0A,80,0A, 0E,60,0A,87,10, FF

; Sound 15h: Maridia elevatube
$3C33:           dw 3C35
$3C35:           db 25,00,0A,AB,03, FF

; Sound 16h: Fake Kraid cry
$3C3B:           dw 3C3D
$3C3D:           db 25,60,0A,A8,10, FF

; Sound 17h: Morph ball eye's ray
$3C43:           dw 3C45
$3C45:           db F5,70,AA, 06,40,0A,A1,40,
                    FE,00, 06,40,0A,AA,F0, FB,
                    FF

; Sound 18h: Beacon
$3C56:           dw 3C58
$3C58:           db 0B,20,0A,8C,03, 0B,30,0A,8C,03, 0B,40,0A,8C,03, 0B,50,0A,8C,03, 0B,60,0A,8C,03, 0B,70,0A,8C,03, 0B,80,0A,8C,03, 0B,60,0A,8C,03, 0B,50,0A,8C,03, 0B,40,0A,8C,03, 0B,30,0A,8C,03, FF

; Sound 19h: Tourian statue unlocking particle
$3C90:           dw 3C94, 3CE5
$3C94:           db 10,50,0A,C1,03, 10,40,0A,C2,03, 10,30,0A,C3,03, 10,20,0A,C4,03, 10,10,0A,C5,03, 10,10,0A,C6,03, 10,10,0A,C7,03, 10,00,0A,C7,30, 10,60,0A,C7,03, 10,50,0A,C6,03, 10,30,0A,C5,03, 10,30,0A,C4,03, 10,20,0A,C3,03, 10,20,0A,C2,03, 10,10,0A,C1,03, 10,10,0A,C0,03, FF
$3CE5:           db 08,D0,0A,99,03, 08,D0,0A,9C,04, 0F,30,0A,8B,03, 0F,40,0A,8C,03, 0F,50,0A,8E,0E, FF

; Sound 1Ah: n00b tube shattering
$3CFF:           dw 3D03, 3D36
$3D03:           db 08,D0,0A,94,03, 08,D0,0A,97,02, 08,D0,0A,98,03, 08,D0,0A,9A,04, 08,D0,0A,97,03, 08,D0,0A,9A,04, 08,D0,0A,9D,03, 08,D0,0A,9F,03, 08,D0,0A,94,1A, 25,40,0A,8C,26, FF
$3D36:           db 25,D0,0A,98,10, 25,D0,0A,93,16, 25,90,0A,8F,15, FF

; Sound 1Bh: Spike platform stops / tatori hits wall
$3D46:           dw 3D48
$3D48:           db 08,D0,0A,94,19, FF

; Sound 1Ch: Chozo grabs Samus
$3D4E:           dw 3D50
$3D50:           db 0D,40,0C,8B,02, 0D,50,0C,89,02, 0D,60,0C,87,03, 0D,50,0C,85,03, FF

; Sound 1Dh: Dachora cry
$3D65:           dw 3D67
$3D67:           db 14,D0,0A,9F,03, 14,D0,0A,A4,03, 14,90,0A,A4,03, 14,40,0A,A3,03, 14,30,0A,A2,03, FF

; Sound 1Eh: Unused
$3D81:           dw 3D85, 3D8B
$3D85:           db 08,D0,0A,94,59, FF
$3D8B:           db 25,D0,0A,98,10, 25,D0,0A,93,16, 25,90,0A,8F,15, FF

; Sound 1Fh: Fune spits
$3D9B:           dw 3D9D
$3D9D:           db 25,D0,0A,90,09, 00,D8,0A,97,07, FF

; Sound 20h: Shot fly
$3DA8:           dw 3DAA
$3DAA:           db 14,80,0A,9F,03, 14,80,0A,98,0A, 14,40,0A,98,03, 14,30,0A,98,03, FF

; Sound 21h: Shot skree / wall/ninja space pirate
$3DBF:           dw 3DC1

; Shot skree / wall/ninja space pirate / skree launches attack
$3DC1:           db 14,80,0A,98,03, 14,A0,0A,9D,07, 14,50,0A,98,03, 14,30,0A,9D,06, FF

; Sound 22h: Shot pipe bug / choot / Golden Torizo egg hatches
$3DD6:           dw 3DD8
$3DD8:           db 14,D0,0A,90,03, 14,E0,0A,93,03, 14,D0,0A,95,03, 14,50,0A,95,03, FF

; Sound 23h: Shot slug / sidehopper / zoomer
$3DED:           dw 3DEF
$3DEF:           db 14,E0,0C,84,03, 14,D0,0C,89,03, 14,E0,0C,84,03, 14,D0,0C,89,03, FF

; Sound 24h: Small explosion (enemy death)
$3E04:           dw 3E06
$3E06:           db 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,8C,03, 08,D0,0A,8C,25, FF

; Sound 25h: Big explosion
$3E20:           dw 3E22

; Big explosion / quake
$3E22:           db 00,E0,0A,91,08, 08,D0,0A,A1,03, 08,D0,0A,9E,03, 08,D0,0A,A3,03, 08,D0,0A,8E,03, 08,D0,0A,8E,25, FF

; Sound 26h: Bomb Torizo explosive swipe
$3E41:           dw 3E43
$3E43:           db 00,D8,0A,95,05, 01,90,0A,A4,08, F5,F0,80, 0B,A0,0A,B0,0E, F5,F0,80, 0B,70,0A,B0,0E, F5,F0,80, 0B,30,0A,B0,0E, FF

; Sound 27h: Shot torizo
$3E66:           dw 3E6A, 3E7F
$3E6A:           db 14,D0,0A,8B,11, 14,D0,0A,89,20, 14,80,0A,89,05, 14,30,0A,89,05, FF
$3E7F:           db 14,D0,0A,80,09, 14,D0,0A,82,20, 14,80,0A,82,05, 14,30,0A,82,05, FF

; Sound 28h: Unused
$3E94:           dw 3E96

; Sound 28h / 2Ah
$3E96:           db 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,8C,03, 08,D0,0A,8C,25, FF

; Sound 29h: Mother Brain rising into phase 2 / Crocomire's wall explodes / Spore Spawn gets hard
$3EB0:           dw 3EB2
$3EB2:           db 08,40,0A,9F,04, 08,40,0A,9C,03, 08,40,0A,A1,03, 08,40,0A,93,04, 08,40,0A,93,25, FF

; Sound 2Ah: Unused
$3ECC:           dw 3E96

; Sound 2Bh: Ridley's fireball hit surface
$3ECE:           dw 3ED0
$3ED0:           db 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,8C,20, FF

; Sound 2Ch: Shot Spore Spawn / Spore Spawn opens up
$3F03:           dw 3F07, 3F0D
$3F07:           db 25,D0,0A,8E,40, FF
$3F0D:           db 25,00,0A,87,15, 25,D0,0A,87,40, FF

; Sound 2Dh: Kraid's roar / Crocomire dying cry
$3F18:           dw 3F1A
$3F1A:           db 25,D0,0A,95,45, FF

; Sound 2Eh: Kraid's dying cry
$3F20:           dw 3F24, 3F34
$3F24:           db 25,D0,0A,9F,60, 25,D0,0A,9A,30, 25,D0,0A,98,30, FF
$3F34:           db 25,00,0A,9A,45, 25,D0,0A,9C,60, 25,D0,0A,97,50, FF

; Sound 2Fh: Yapping maw
$3F44:           dw 3F46
$3F46:           db 08,50,0A,AD,03, 08,50,0A,AD,04, F5,90,C7, 10,40,0A,BC,07, 10,20,0A,C3,03, FF

; Sound 30h: Shot super-desgeega / Crocomire destroys wall
$3F5E:           dw 3F60
$3F60:           db 25,90,0A,93,06, 25,B0,0A,98,10, 25,40,0A,98,03, 25,30,0A,98,03, FF

; Sound 31h: Brinstar plant chewing
$3F75:           dw 3F77
$3F77:           db 0F,70,0A,8B,0D, 0F,80,0A,92,0D, FF

; Sound 32h: Etecoon wall-jump
$3F82:           dw 3F84
$3F84:           db 1D,70,0A,AC,0B, FF

; Sound 33h: Etecoon cry
$3F8A:           dw 3F8C
$3F8C:           db 1D,70,0A,B4,04, 1D,70,0A,B0,04, FF

; Sound 34h: Cacatac spikes / Golden Torizo egg released
$3F97:           dw 3F99

; Cacatac spikes / Golden Torizo egg released / shot powamp
$3F99:           db 00,D8,0A,90,16, FF

; Sound 35h: Etecoon's theme
$3F9F:           dw 3FA1
$3FA1:           db 1D,70,0A,A9,07, 1D,20,0A,A9,07, 1D,70,0A,AE,07, 1D,20,0A,AE,07, 1D,70,0A,B0,07, 1D,20,0A,B0,07, 1D,70,0A,B2,07, 1D,20,0A,B2,07, 1D,70,0A,B4,07, 1D,20,0A,B4,07, 1D,70,0A,B0,07, 1D,20,0A,B0,07, 1D,70,0A,AB,07, 1D,20,0A,AB,07, 1D,70,0A,B0,07, 1D,20,0A,B0,07, 1D,70,0A,B5,07, 1D,20,0A,B5,07, 1D,70,0A,B2,07, 1D,20,0A,B2,07, 1D,70,0A,AE,07, 1D,20,0A,AE,07, 1D,70,0A,AB,07, 1D,20,0A,AB,07, 1D,70,0A,AD,20, FF

; Sound 36h: Shot rio / squeept / dragon
$401F:           dw 4021
$4021:           db 14,80,0A,8C,03, 14,A0,0A,91,05, 14,50,0A,8C,03, 14,30,0A,91,06, FF

; Sound 37h: Refill/map station engaged
$4036:           dw 403A, 4050
$403A:           db 03,90,0A,89,05, F5,F0,BB, 07,40,0A,B0,20,
                    FE,00, 07,40,0A,BB,0A, FB, FF
$4050:           db 03,90,0A,87,05, F5,F0,C7, 07,40,0A,BC,20,
                    FE,00, 0B,10,0A,B9,07, FB, FF

; Sound 38h: Refill/map station disengaged
$4066:           dw 406A, 4073
$406A:           db F5,F0,B0, 07,90,0A,BB,08, FF
$4073:           db F5,F0,80, 0B,10,0A,B9,08, FF

; Sound 39h: Dachora speed booster
$407C:           dw 4F02

; Sound 3Ah: Tatori spinning
$407E:           dw 4080
$4080:           db 07,60,0A,C7,10, FF

; Sound 3Bh: Dachora shinespark
$4086:           dw 504B

; Sound 3Ch: Dachora shinespark ended
$4088:           dw 50AA

; Sound 3Dh: Dachora stored shinespark
$408A:           dw 4FF9

; Sound 3Eh: Shot owtch / viola / ripper / tripper / suspensor platform / yard / yapping maw / atomic
$408C:           dw 408E
$408E:           db 13,60,0A,95,05, 13,40,0A,95,03, 13,10,0A,95,03, FF

; Sound 3Fh: Alcoon spit / fake Kraid lint / ninja pirate spin jump
$409E:           dw 40A0
$40A0:           db 00,70,0A,95,0C, FF

; Sound 40h: Unused
$40A6:           dw 40A8
$40A8:           db F5,F0,80, 0B,30,0A,C7,08, FF

; Sound 41h: (Empty)
$40B1:           dw 40B3
$40B3:           db FF

; Sound 42h: Boulder bounces
$40B4:           dw 40B6
$40B6:           db 08,D0,0A,94,20, FF

; Sound 43h: Boulder explodes
$40BC:           dw 40BE
$40BE:           db 08,D0,0A,94,03, 08,D0,0A,97,03, 08,D0,0A,99,20, FF

; Sound 44h: (Empty)
$40CE:           dw 40D0
$40D0:           db FF

; Sound 45h: Typewriter stroke - Ceres self destruct sequence
$40D1:           dw 40D3
$40D3:           db 03,50,0A,98,02, 03,50,0A,98,02, FF

; Sound 46h: Lavaquake
$40DE:           dw 40E0
$40E0:           db 08,D0,0A,8E,07, 08,D0,0A,8E,10, 08,D0,0A,8E,09, 08,D0,0A,8E,0E, FF

; Sound 47h: Shot waver
$40F5:           dw 40F7
$40F7:           db 14,D0,0A,98,03, 14,E0,0A,97,03, 14,D0,0A,95,03, 14,50,0A,95,03, FF

; Sound 48h: Torizo sonic boom
$410C:           dw 410E
$410E:           db 00,D8,0A,95,08, F5,F0,8C, 0B,D0,0A,A3,06, F5,F0,8C, 0B,B0,0A,A3,06, F5,F0,8C, 0B,70,0A,A3,06, FF

; Sound 49h: Shot skultera / sciser / zoa
$412C:           dw 412E
$412E:           db 14,80,0A,AB,04, 14,50,0A,AB,04, 14,30,0A,AB,04, 14,20,0A,AB,04, FF

; Sound 4Ah: Shot evir
$4143:           dw 4145
$4145:           db 24,70,0A,9C,03, 24,50,0A,9A,04, 24,40,0A,9A,06, 24,10,0A,9A,06, FF

; Sound 4Bh: Chozo / torizo footsteps
$415A:           dw 415C
$415C:           db 08,A0,0C,98,08, FF

; Sound 4Ch: Ki-hunter spit / eye door acid spit / Draygon goop
$4162:           dw 4164
$4164:           db 00,40,0A,9C,08, 0F,80,0A,93,13, FF

; Sound 4Dh: Gunship hover
$416F:           dw 4171
$4171:           db 0B,20,0A,89,03, 0B,30,0A,89,03, 0B,40,0A,89,03, 0B,50,0A,89,03, 0B,60,0A,89,03, 0B,70,0A,89,03, 0B,80,0A,89,03, 0B,60,0A,89,03, 0B,50,0A,89,03, 0B,40,0A,89,03, 0B,30,0A,89,03, FF

; Sound 4Eh: Ceres Ridley getaway
$41A9:           dw 41AD, 41B6
$41AD:           db F5,B0,C7, 05,D0,0A,98,46, FF
$41B6:           db F5,A0,C7, 09,D0,0F,80,50, F5,50,80, 09,D0,0A,AB,46, FF

; Sound 4Fh: Unused
$41C7:           dw 41C9
$41C9:           db 0F,B0,0A,93,10, 0F,40,0A,93,03, 0F,30,0A,93,03, FF

; Sound 50h: Metroid draining Samus / random Metroid cry
$41D9:           dw 41DD, 41E3
$41DD:           db 24,A0,0A,9A,0E, FF
$41E3:           db 24,00,0A,8C,03, 24,90,0A,98,14, FF

; Sound 51h: Shot coven
$41EE:           dw 41F2, 4207
$41F2:           db 19,60,0A,A4,13, 19,50,0A,A4,13, 19,30,0A,A4,13, 19,10,0A,A4,13, FF
$4207:           db 19,60,0A,9F,16, 19,50,0A,9F,16, 19,30,0A,9F,16, 19,10,0A,9F,16, FF

; Sound 52h: Shitroid feels remorse
$421C:           dw 421E
$421E:           db 22,D0,0A,92,2B, FF

; Sound 53h: Shot mini-Crocomire
$4224:           dw 4226
$4226:           db 0F,B0,0A,93,10, 0F,40,0A,93,03, 0F,30,0A,93,03, FF

; Sound 54h: Unused. Shot Crocomire(?)
$4236:           dw 4238
$4238:           db 14,B0,0A,93,05, 14,80,0A,9C,0A, 14,40,0A,9C,03, 14,30,0A,9C,03, FF

; Sound 55h: Shot beetom
$424D:           dw 424F
$424F:           db F5,F0,80, 0B,40,0A,C5,04, F5,F0,80, 0B,30,0A,F6,03, F5,F0,80, 0B,20,0A,F6,03, FF

; Sound 56h: Acquired suit
$4268:           dw 426C, 427A
$426C:           db 09,D0,0F,87,10, F5,B0,C7, 05,D0,0F,80,60, FF
$427A:           db 09,D0,05,82,30, F5,A0,80, 05,D0,05,C7,60, FF

; Sound 57h: Shot door/gate with dud shot / shot reflec / shot oum
$4288:           dw 428A
$428A:           db 08,70,0A,98,03, 08,50,0A,95,03, 08,40,0A,9A,03, FF

; Sound 58h: Shot mochtroid / random metroid cry
$429A:           dw 429E, 42A4
$429E:           db 24,A0,0A,98,0D, FF
$42A4:           db 24,00,0A,94,03, 24,80,0A,9A,15, FF

; Sound 59h: Ridley's roar
$42AF:           dw 42B3, 42B9
$42B3:           db 25,D0,0A,9D,30, FF
$42B9:           db 25,D0,0A,A1,30, FF

; Sound 5Ah: Shot metroid / random metroid cry
$42BF:           dw 42C3, 42C9
$42C3:           db 24,A0,0A,98,15, FF
$42C9:           db 24,00,0A,96,03, 24,80,0A,95,1D, FF

; Sound 5Bh: Skree launches attack
$42D4:           dw 3DC1

; Sound 5Ch: Skree hits the ground
$42D6:           dw 42D8
$42D8:           db 0F,B0,0A,8B,08, F5,F0,BC, 01,70,0A,98,09, F5,F0,BC, 01,60,0A,98,09, F5,F0,BC, 01,50,0A,98,09, F5,F0,BC, 01,40,0A,98,09, FF

; Sound 5Dh: Sidehopper jumped
$42FE:           dw 4300
$4300:           db 01,B0,0A,80,0F, 01,60,0A,80,03, 01,40,0A,80,03, FF

; Sound 5Eh: Sidehopper landed / fire arc part spawns / evir spit / alcoon spawns
$4310:           dw 4312
$4312:           db 00,A0,0A,84,0F, 00,60,0A,84,03, 00,40,0A,84,03, FF

; Sound 5Fh: Shot holtz / desgeega / viola / alcoon / Botwoon
$4322:           dw 4324
$4324:           db 14,90,0A,82,0A, 14,80,0A,82,03, 14,60,0A,82,03, FF

; Sound 60h: Unused
$4334:           dw 4336
$4336:           db 25,70,0A,AB,20, FF

; Sound 61h: Dragon / magdollite spit / fire pillar
$433C:           dw 433E
$433E:           db F5,50,B0, 09,D0,0A,8C,20, FF

; Sound 62h: Unused
$4347:           dw 4349
$4349:           db F5,F0,B0, 09,D0,0A,8C,10, FF

; Sound 63h: Mother Brain's death beam
$4352:           dw 4356, 438E
$4356:           db 00,E0,0A,95,05, 01,E0,0A,A4,05, 08,E0,0A,9F,04, 08,E0,0A,9C,03, 08,E0,0A,A1,03, 08,E0,0A,93,04, 08,E0,0A,93,08, 08,D0,0A,8B,13, 08,D0,0A,89,13, 08,D0,0A,85,16, 08,D0,0A,82,18, FF
$438E:           db 00,E0,0A,95,05, 18,E0,0A,A4,05, 18,E0,0A,9F,04, 18,E0,0A,9C,03, 18,E0,0A,A1,03, 18,E0,0A,93,04, 18,E0,0A,93,08, 18,E0,0A,8C,05, 18,E0,0A,87,04, 18,E0,0A,84,03, FF

; Sound 64h: Holtz cry
$43C1:           dw 43C3
$43C3:           db F5,50,B0, 09,D0,0A,8C,18, FF

; Sound 65h: Rio cry
$43CC:           dw 43CE
$43CE:           db 14,A0,0A,97,03, 14,A0,0A,97,03, 14,A0,0A,97,03, 14,30,0A,97,03, 14,20,0A,97,03, FF

; Sound 66h: Shot ki-hunter / shot walking space pirate / space pirate attack
$43E8:           dw 43EA
$43EA:           db 14,80,0A,98,0A, 14,40,0A,98,03, 14,30,0A,98,03, FF

; Sound 67h: Space pirate / Mother Brain / torizo / work robot laser
$43FA:           dw 43FC

; Space pirate / Mother Brain / torizo / work robot laser / sound 6Bh
$43FC:           db 00,D8,0A,98,05, F5,F0,C7, 0B,50,0A,B0,03, F5,F0,C7, 0B,50,0A,B0,03, F5,F0,C7, 0B,50,0A,B0,03, F5,F0,BC, 0B,50,0A,B0,03, FF

; Sound 68h: Work robot
$4422:           dw 4424
$4424:           db 1B,A0,0A,94,06, 1B,90,0A,8C,20, FF

; Sound 69h: Shot Shaktool
$442F:           dw 4431
$4431:           db 02,80,0A,89,05, 02,40,0A,89,03, 02,10,0A,89,03, FF

; Sound 6Ah: Shot powamp
$4441:           dw 3F99

; Sound 6Bh: Unused
$4443:           dw 43FC

; Unused byte
$4445:           db FF

; Sound 6Ch: Kago bug
$4446:           dw 4448
$4448:           db 00,40,0A,A8,08, FF

; Sound 6Dh: Ceres tiles falling from ceiling
$444E:           dw 4450
$4450:           db 00,E0,0A,91,08, 08,90,0A,A1,03, 08,90,0A,9E,03, 08,90,0A,A3,03, 08,90,0A,8E,03, 08,90,0A,8E,25, FF

; Sound 6Eh: Shot Mother Brain phase 1
$446F:           dw 4473, 4479
$4473:           db 23,D0,0A,80,20, FF
$4479:           db 23,D0,0A,87,20, FF

; Sound 6Fh: Mother Brain's cry - low pitch
$447F:           dw 4483, 4489
$4483:           db 25,E0,0A,80,C0, FF
$4489:           db 24,E0,0A,8C,C0, FF

; Sound 70h: Yard bounce
$448F:           dw 4491
$4491:           db 1A,60,0A,AB,06, 1A,60,0A,B0,09, FF

; Sound 71h: Silence
$449C:           dw 449E
$449E:           db 09,00,0A,8C,03, FF

; Sound 72h: Shitroid's cry
$44A4:           dw 44A8, 44AE
$44A8:           db 24,A0,0A,8C,30, FF
$44AE:           db 24,00,0A,9D,03, 24,80,0A,87,45, FF

; Sound 73h: Phantoon's cry / Draygon's cry
$44B9:           dw 44BD, 44C3
$44BD:           db 25,E0,0A,A3,40, FF
$44C3:           db 25,00,0A,A6,0C, 25,80,0A,A3,40, FF

; Sound 74h: Crocomire's cry
$44CE:           dw 44D2, 44D8
$44D2:           db 25,90,0A,92,53, FF
$44D8:           db 26,E0,0A,A6,09, 26,E0,0A,A4,0D, 26,E0,0A,A2,0D, 26,E0,0A,A0,0D, FF

; Sound 75h: Crocomire's skeleton collapses
$44ED:           dw 44F1, 4547
$44F1:           db 0D,00,0C,A3,05, 0D,A0,0C,A3,02, 0D,C0,0C,A1,02, 0D,C0,0C,9F,03, 0D,C0,0C,9D,03, 0D,B0,0C,9C,03, 0D,A0,0C,9A,02, 0D,90,0C,A3,02, 0D,90,0C,98,04, 0D,A0,0C,97,02, 0D,C0,0C,95,02, 0D,C0,0C,93,03, 0D,C0,0C,91,03, 0D,B0,0C,90,03, 0D,A0,0C,8E,02, 0D,90,0C,97,02, 0D,90,0C,8C,04, FF
$4547:           db 0D,A0,0C,97,02, 0D,B0,0C,90,03, 0D,C0,0C,91,03, 0D,C0,0C,91,03, 0D,B0,0C,90,03, 0D,90,0C,97,02, 0D,90,0C,97,02, 0D,90,0C,8C,04, 0D,A0,0C,8B,02, 0D,90,0C,8B,02, 0D,C0,0C,87,03, 0D,C0,0C,85,03, 0D,C0,0C,89,02, 0D,B0,0C,84,03, 0D,C0,0C,89,02, 0D,90,0C,80,04, FF

; Sound 76h: Quake (Crocomire moves / Kraid moves / Ridley's tail hits floor)
$4598:           dw 3E22

; Sound 77h: Crocomire melting cry
$459A:           dw 459E, 45D1
$459E:           db 25,D0,0A,A7,15, 25,D0,0A,A3,20, 25,D0,0A,A2,63, 25,00,0A,A2,09, 25,D0,0A,A2,60, 25,00,0A,A2,09, 25,D0,0A,A2,60, 25,00,0A,A2,09, 25,D0,0A,A3,20, 25,D0,0A,A2,33, FF
$45D1:           db 26,D0,0A,A6,0D, 26,D0,0A,A6,0D, 26,D0,0A,A5,0D, 26,D0,0A,A4,0D, 26,D0,0A,A7,0D, 26,D0,0A,A2,0D, 26,00,0A,AA,7B, 26,00,0A,AA,90, 26,D0,0A,A7,0D, 26,D0,0A,A6,0D, 26,D0,0A,A5,0D, 26,D0,0A,A4,0D, 26,D0,0A,A3,0D, 26,D0,0A,A2,0D, FF

; Sound 78h: Shitroid draining
$4618:           dw 461C, 4622
$461C:           db 24,A0,0A,9C,20, FF
$4622:           db 24,00,0A,9D,05, 24,80,0A,95,40, FF

; Sound 79h: Phantoon appears 1
$462D:           dw 4631, 4637
$4631:           db 26,D0,0A,95,38, FF
$4637:           db 26,00,0A,95,0A, 26,D0,0A,9C,38, FF

; Sound 7Ah: Phantoon appears 2
$4642:           dw 4646, 464C
$4646:           db 26,D0,0A,8E,40, FF
$464C:           db 26,00,0A,8E,0A, 26,D0,0A,99,40, FF

; Sound 7Bh: Phantoon appears 3
$4657:           dw 465B, 4661
$465B:           db 26,D0,0A,9E,3D, FF
$4661:           db 26,00,0A,9E,0A, 26,D0,0A,9D,3D, FF

; Sound 7Ch: Botwoon spit
$466C:           dw 466E
$466E:           db 24,90,0A,94,1A, 24,30,0A,94,10, FF

; Sound 7Dh: Shitroid feels guilty
$4679:           dw 467B
$467B:           db 22,D0,0A,88,90, 22,D0,0A,8E,37, FF

; Sound 7Eh: Mother Brain's cry - high pitch / Phantoon's dying cry
$4686:           dw 4688
$4688:           db 25,D0,0A,87,C0, FF

; Sound 7Fh: Mother Brain charging her rainbow
$468E:           dw 4692, 46C8
$4692:           db FE,00, 24,D0,0A,84,0D, 24,D0,0A,85,0D, 24,D0,0A,87,0D, 24,D0,0A,89,0D, 24,D0,0A,8B,0D, 24,D0,0A,8C,0D, 24,D0,0A,8E,0D, 24,D0,0A,90,0D, 24,D0,0A,91,0D, 24,D0,0A,93,0D, FB, FF
$46C8:           db 24,00,0A,80,04,
                    FE,00, 24,D0,0A,84,0D, 24,D0,0A,85,0D, 24,D0,0A,87,0D, 24,D0,0A,89,0D, 24,D0,0A,8B,0D, 24,D0,0A,8C,0D, 24,D0,0A,8E,0D, 24,D0,0A,90,0D, 24,D0,0A,91,0D, 24,D0,0A,93,0D, FB,
                    FF
}
}


;;; $4703..530D: Sound library 3 ;;;
{
;;; $4703: Go to process sound 3 ;;;
{
$4703: 5F D4 47  jmp   $47D4
}


;;; $4706: Handle CPU IO 3 ;;;
{
; BUG: All sound 3 channels are being reset if a new sound effect causes a current sound effect to stop
;      However, some of the channels might have already been reset by reaching the end of their instruction list
;      and in the time since then, the voice may have been allocated to a sound effect in a different library
;      Re-resetting that channel will erroneously mark the voice as available for allocation,
;      allowing two sound effects to have the same voice allocated to them
;      This is the cause of the laser door opening sound glitch

$4706: E5 A9 04  mov   a,$04A9
$4709: F0 00     beq   $470B

$470B: EB 0B     mov   y,$0B        ; Y = [previous value read from CPU IO 3]
$470D: E4 03     mov   a,$03        ;\
$470F: C4 0B     mov   $0B,a        ;} Previous value read from CPU IO 3 = [value read from CPU IO 3]
$4711: C4 07     mov   $07,a        ; Value for CPU IO 3 = [value read from CPU IO 3]
$4713: 7E 03     cmp   y,$03        ;\
$4715: D0 06     bne   $471D        ;} If [Y] != [value read from CPU IO 3]: go to BRANCH_CHANGE

; BRANCH_NO_CHANGE
$4717: E5 70 04  mov   a,$0470      ;\
$471A: D0 E7     bne   $4703        ;} If [current sound 3] != 0: go to process sound 3
$471C: 6F        ret                ; Return

; BRANCH_CHANGE
$471D: 68 00     cmp   a,#$00       ;\
$471F: F0 F6     beq   $4717        ;} If [value read from CPU IO 3] = 0: go to BRANCH_NO_CHANGE
$4721: E4 03     mov   a,$03        ;\
$4723: 68 01     cmp   a,#$01       ;} If [value read from CPU IO 3] != 1 (silence):
$4725: F0 12     beq   $4739        ;/
$4727: E5 BA 04  mov   a,$04BA      ;\
$472A: 68 02     cmp   a,#$02       ;} If [sound 3 low health priority] = 2: go to BRANCH_NO_CHANGE
$472C: F0 E9     beq   $4717        ;/
$472E: E4 03     mov   a,$03        ;\
$4730: 68 02     cmp   a,#$02       ;} If [value read from CPU IO 3] != 2 (low health beep):
$4732: F0 05     beq   $4739        ;/
$4734: E5 BD 04  mov   a,$04BD      ;\
$4737: D0 DE     bne   $4717        ;} If [sound 3 priority] != 0: go to BRANCH_NO_CHANGE

$4739: E5 70 04  mov   a,$0470      ;\
$473C: F0 0B     beq   $4749        ;} If [current sound 3] != 0:
$473E: E8 00     mov   a,#$00       ;\
$4740: C5 85 04  mov   $0485,a      ;} Sound 3 enabled voices = 0
$4743: 3F 86 4B  call  $4B86        ; Reset sound 3 channel 0
$4746: 3F C9 4B  call  $4BC9        ; Reset sound 3 channel 1

$4749: E8 00     mov   a,#$00
$474B: C5 9F 04  mov   $049F,a      ; Sound 3 channel 0 legato flag = 0
$474E: C5 A6 04  mov   $04A6,a      ; Sound 3 channel 1 legato flag = 0
$4751: E4 07     mov   a,$07        ;\
$4753: 9C        dec   a            ;|
$4754: 1C        asl   a            ;} Current sound 3 index = ([value for CPU IO 3] - 1) * 2
$4755: C5 71 04  mov   $0471,a      ;/
$4758: E9 71 04  mov   x,$0471      ;\
$475B: F5 8F 4E  mov   a,$4E8F+x    ;|
$475E: C4 E0     mov   $E0,a        ;|
$4760: 3D        inc   x            ;} Sound 3 instruction list pointer set = [$4E8F + [current sound 3 index]]
$4761: F5 8F 4E  mov   a,$4E8F+x    ;|
$4764: C4 E1     mov   $E1,a        ;/
$4766: E4 07     mov   a,$07        ;\
$4768: C5 70 04  mov   $0470,a      ;} Current sound 3 = [value for CPU IO 3]
$476B: 68 FE     cmp   a,#$FE       ;\
$476D: F0 65     beq   $47D4        ;} If [current sound 3] = FEh: go to process sound 3
$476F: 68 FF     cmp   a,#$FF       ;\
$4771: F0 61     beq   $47D4        ;} If [current sound 3] = FFh: go to process sound 3
$4773: 3F 75 28  call  $2875        ; Go to [$4776 + [current sound 3 index]]

$4776:           dw 4DD0, 4DE0, 4DEB, 4DEF, 4DEF, 4DF3, 4DF7, 4DF7, 4DFB, 4DFF, 4E03, 4E07, 4E07, 4E0B, 4E0F, 4E13,
                    4E17, 4E1B, 4E1F, 4E23, 4E23, 4E27, 4E2B, 4E2F, 4E33, 4E37, 4E3B, 4E3F, 4E3F, 4E3F, 4E3F, 4E43,
                    4E43, 4E47, 4E47, 4E4B, 4E4F, 4E4F, 4E4F, 4E4F, 4E4F, 4E4F, 4E4F, 4E53, 4E57, 4E5B, 4E5F
}


;;; $47D4: Process sound 3 ;;;
{
$47D4: E8 FF     mov   a,#$FF       ;\
$47D6: 65 7C 04  cmp   a,$047C      ;} If [sound 3 initialisation flag] != FFh:
$47D9: F0 3A     beq   $4815        ;/
$47DB: 3F 3B 4C  call  $4C3B        ; Sound 3 initialisation
$47DE: 8D 00     mov   y,#$00       ;\
$47E0: F7 E0     mov   a,($E0)+y    ;|
$47E2: C4 E8     mov   $E8,a        ;} $DC = [[sound 3 channel instruction list pointer set]]
$47E4: 3F C8 4D  call  $4DC8        ;|
$47E7: C4 E9     mov   $E9,a        ;/
$47E9: 3F C8 4D  call  $4DC8        ;\
$47EC: C4 EA     mov   $EA,a        ;|
$47EE: 3F C8 4D  call  $4DC8        ;} $DE = [[sound 3 channel instruction list pointer set] + 2]
$47F1: C4 EB     mov   $EB,a        ;/
$47F3: E5 83 04  mov   a,$0483      ;\
$47F6: 3F CC 4D  call  $4DCC        ;} Sound 3 channel 0 DSP index = [sound 3 channel 0 voice index] * 8
$47F9: C5 86 04  mov   $0486,a      ;/
$47FC: E5 84 04  mov   a,$0484      ;\
$47FF: 3F CC 4D  call  $4DCC        ;} Sound 3 channel 1 DSP index = [sound 3 channel 1 voice index] * 8
$4802: C5 87 04  mov   $0487,a      ;/
$4805: 8D 00     mov   y,#$00       ;\
$4807: CC 72 04  mov   $0472,y      ;} Sound 3 channel instruction list indices = 0
$480A: CC 73 04  mov   $0473,y      ;/
$480D: 8D 01     mov   y,#$01       ;\
$480F: CC 74 04  mov   $0474,y      ;} Sound 3 channel instruction timers = 1
$4812: CC 75 04  mov   $0475,y      ;/

$4815: E8 FF     mov   a,#$FF       ;\
$4817: 65 76 04  cmp   a,$0476      ;} If [sound 3 channel 0 disable byte] = FFh:
$481A: D0 03     bne   $481F        ;/
$481C: 5F CF 49  jmp   $49CF        ; Go to BRANCH_CHANNEL_0_END

; Channel 0
{
$481F: 8C 74 04  dec   $0474        ; Decrement sound 3 channel 0 instruction timer
$4822: F0 03     beq   $4827        ; If [sound 3 channel 0 instruction timer] != 0:
$4824: 5F 68 49  jmp   $4968        ; Go to BRANCH_PROCESS_CHANNEL_0_INSTRUCTION_END

$4827: E5 9F 04  mov   a,$049F      ;\
$482A: F0 02     beq   $482E        ;} If sound 3 channel 0 legato flag enabled:
$482C: 2F 43     bra   $4871        ; Go to LOOP_CHANNEL_0_COMMANDS

$482E: E8 00     mov   a,#$00       ;\
$4830: C5 9E 04  mov   $049E,a      ;} Disable sound 3 channel 0 pitch slide
$4833: C5 9C 04  mov   $049C,a      ; Sound 3 channel 0 subnote delta = 0
$4836: C5 9D 04  mov   $049D,a      ; Sound 3 channel 0 target note = 0
$4839: E8 FF     mov   a,#$FF       ;\
$483B: 65 8C 04  cmp   a,$048C      ;} If [sound 3 channel 0 release flag] != FFh:
$483E: F0 16     beq   $4856        ;/
$4840: E5 7E 04  mov   a,$047E      ;\
$4843: 04 46     or    a,$46        ;} Key off flags |= [sound 3 channel 0 voice bitset]
$4845: C4 46     mov   $46,a        ;/
$4847: E8 02     mov   a,#$02       ;\
$4849: C5 8D 04  mov   $048D,a      ;} Sound 3 channel 0 release timer = 2
$484C: E8 01     mov   a,#$01       ;\
$484E: C5 74 04  mov   $0474,a      ;} Sound 3 channel 0 instruction timer = 1
$4851: E8 FF     mov   a,#$FF       ;\
$4853: C5 8C 04  mov   $048C,a      ;} Sound 3 channel 0 release flag = FFh

$4856: 8C 8D 04  dec   $048D        ; Decrement sound 3 channel 0 release timer
$4859: F0 03     beq   $485E        ; If [sound 3 channel 0 release timer] != 0:
$485B: 5F CF 49  jmp   $49CF        ; Go to BRANCH_CHANNEL_0_END

$485E: E8 00     mov   a,#$00       ;\
$4860: C5 8C 04  mov   $048C,a      ;} Sound 3 channel 0 release flag = 0
$4863: E5 80 04  mov   a,$0480      ;\
$4866: 24 47     and   a,$47        ;} Current music voice bitset &= [sound 3 channel 0 voice mask]
$4868: C4 47     mov   $47,a        ;/
$486A: E5 80 04  mov   a,$0480      ;\
$486D: 24 49     and   a,$49        ;} Noise enable flags &= [sound 3 channel 0 voice mask]
$486F: C4 49     mov   $49,a        ;/

; LOOP_CHANNEL_0_COMMANDS
$4871: 3F 1D 4C  call  $4C1D        ; A = next sound 3 channel 0 data byte
$4874: 68 FA     cmp   a,#$FA       ;\
$4876: D0 00     bne   $4878        ;|
                                    ;} If [A] = F9h:
$4878: 68 F9     cmp   a,#$F9       ;|
$487A: D0 14     bne   $4890        ;/
$487C: 3F 1D 4C  call  $4C1D        ;\
$487F: C5 94 04  mov   $0494,a      ;} Sound 3 channel 0 ADSR settings = next sound 3 channel 0 data byte
$4882: 3F 1D 4C  call  $4C1D        ;\
$4885: C5 95 04  mov   $0495,a      ;} Sound 3 channel 0 ADSR settings |= next sound 3 channel 0 data byte << 8
$4888: E8 FF     mov   a,#$FF       ;\
$488A: C5 98 04  mov   $0498,a      ;} Sound 3 channel 0 update ADSR settings flag = FFh
$488D: 5F 71 48  jmp   $4871        ; Go to LOOP_CHANNEL_0_COMMANDS

$4890: 68 F5     cmp   a,#$F5       ;\
$4892: D0 05     bne   $4899        ;} If [A] = F5h:
$4894: C5 A0 04  mov   $04A0,a      ; Enable sound 3 channel 0 pitch slide legato
$4897: 2F 09     bra   $48A2

$4899: 68 F8     cmp   a,#$F8       ;\ Else ([A] != F5h):
$489B: D0 19     bne   $48B6        ;} If [A] != F8h: go to BRANCH_CHANNEL_0_PITCH_SLIDE_END
$489D: E8 00     mov   a,#$00       ;\
$489F: C5 A0 04  mov   $04A0,a      ;} Disable sound 3 channel 0 pitch slide legato

$48A2: 3F 1D 4C  call  $4C1D        ;\
$48A5: C5 9C 04  mov   $049C,a      ;} Sound 3 channel 0 subnote delta = next sound 3 channel 0 data byte
$48A8: 3F 1D 4C  call  $4C1D        ;\
$48AB: C5 9D 04  mov   $049D,a      ;} Sound 3 channel 0 target note = next sound 3 channel 0 data byte
$48AE: E8 FF     mov   a,#$FF       ;\
$48B0: C5 9E 04  mov   $049E,a      ;} Enable sound 3 channel 0 pitch slide = FFh
$48B3: 3F 1D 4C  call  $4C1D        ; A = next sound 3 channel 0 data byte

; BRANCH_CHANNEL_0_PITCH_SLIDE_END
$48B6: 68 FF     cmp   a,#$FF       ;\
$48B8: D0 06     bne   $48C0        ;} If [A] = FFh:
$48BA: 3F 86 4B  call  $4B86        ; Reset sound 3 channel 0
$48BD: 5F CF 49  jmp   $49CF        ; Go to BRANCH_CHANNEL_0_END

$48C0: 68 FE     cmp   a,#$FE       ;\
$48C2: D0 0F     bne   $48D3        ;} If [A] = FEh:
$48C4: 3F 1D 4C  call  $4C1D        ;\
$48C7: C5 90 04  mov   $0490,a      ;} Sound 3 channel 0 repeat counter = next sound 3 channel 0 data byte
$48CA: E5 72 04  mov   a,$0472      ;\
$48CD: C5 92 04  mov   $0492,a      ;} Sound 3 channel 0 repeat point = [sound 3 channel 0 instruction list index]
$48D0: 3F 1D 4C  call  $4C1D        ; A = next sound 3 channel 0 data byte

$48D3: 68 FD     cmp   a,#$FD       ;\
$48D5: D0 11     bne   $48E8        ;} If [A] != FDh: go to BRANCH_CHANNEL_0_REPEAT_COMMAND
$48D7: 8C 90 04  dec   $0490        ; Decrement sound 3 channel 0 repeat counter
$48DA: D0 03     bne   $48DF        ; If [sound 3 channel 0 repeat counter] = 0:
$48DC: 5F 71 48  jmp   $4871        ; Go to LOOP_CHANNEL_0_COMMANDS

; LOOP_CHANNEL_0_REPEAT_COMMAND
$48DF: E5 92 04  mov   a,$0492      ;\
$48E2: C5 72 04  mov   $0472,a      ;} Sound 3 channel 0 instruction list index = [sound 3 channel 0 repeat point]
$48E5: 3F 1D 4C  call  $4C1D        ; A = next sound 3 channel 0 data byte

; BRANCH_CHANNEL_0_REPEAT_COMMAND
$48E8: 68 FB     cmp   a,#$FB       ;\
$48EA: D0 03     bne   $48EF        ;} If [A] = FBh:
$48EC: 5F DF 48  jmp   $48DF        ; Go to LOOP_CHANNEL_0_REPEAT_COMMAND

$48EF: 68 FC     cmp   a,#$FC       ;\
$48F1: D0 0A     bne   $48FD        ;} If [A] = FCh:
$48F3: E5 7E 04  mov   a,$047E      ;\
$48F6: 04 49     or    a,$49        ;} Noise enable flags |= [sound 3 channel 0 voice bitset]
$48F8: C4 49     mov   $49,a        ;/
$48FA: 5F 71 48  jmp   $4871        ; Go to LOOP_CHANNEL_0_COMMANDS

$48FD: E9 83 04  mov   x,$0483      ; X = [sound 3 channel 0 voice index]
$4900: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$4903: 3F 1D 4C  call  $4C1D        ;\
$4906: E9 83 04  mov   x,$0483      ;} Track output volume = next sound 3 channel 0 data byte
$4909: D5 21 03  mov   $0321+x,a    ;/
$490C: E8 00     mov   a,#$00       ;\
$490E: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = 0
$4911: 3F 1D 4C  call  $4C1D        ;\
$4914: C4 11     mov   $11,a        ;} $10 = (next sound 3 channel 0 data byte) * 100h
$4916: 8F 00 10  mov   $10,#$00     ;/
$4919: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled
$491C: 3F 1D 4C  call  $4C1D        ; A = next sound 3 channel 0 data byte
$491F: 68 F6     cmp   a,#$F6       ;\
$4921: F0 08     beq   $492B        ;} If [A] != F6h:
$4923: C5 9A 04  mov   $049A,a      ; Sound 3 channel 0 note = [A]
$4926: E8 00     mov   a,#$00       ;\
$4928: C5 9B 04  mov   $049B,a      ;} Sound 3 channel 0 subnote = 0

$492B: EC 9A 04  mov   y,$049A      ;\
$492E: E5 9B 04  mov   a,$049B      ;} $11.$10 = [sound 3 channel 0 note]
$4931: DA 10     movw  $10,ya       ;/
$4933: E9 83 04  mov   x,$0483      ; X = [sound 3 channel 0 voice index]
$4936: 3F B1 16  call  $16B1        ; Play note
$4939: 3F 1D 4C  call  $4C1D        ;\
$493C: C5 74 04  mov   $0474,a      ;} Sound 3 channel 0 instruction timer = next sound 3 channel 0 data byte
$493F: E5 98 04  mov   a,$0498      ;\
$4942: F0 18     beq   $495C        ;} If [sound 3 channel 0 update ADSR settings flag] != 0:
$4944: E5 86 04  mov   a,$0486      ;\
$4947: 08 05     or    a,#$05       ;|
$4949: FD        mov   y,a          ;|
$494A: E5 94 04  mov   a,$0494      ;|
$494D: 3F 26 17  call  $1726        ;|
$4950: E5 86 04  mov   a,$0486      ;} DSP sound 3 channel 0 ADSR settings = [sound 3 channel 0 ADSR settings]
$4953: 08 06     or    a,#$06       ;|
$4955: FD        mov   y,a          ;|
$4956: E5 95 04  mov   a,$0495      ;|
$4959: 3F 26 17  call  $1726        ;/

$495C: E5 9F 04  mov   a,$049F      ;\
$495F: D0 07     bne   $4968        ;} If sound 3 channel 0 legato disabled:
$4961: E5 7E 04  mov   a,$047E      ;\
$4964: 04 45     or    a,$45        ;} Key on flags |= [sound 3 channel 0 voice bitset]
$4966: C4 45     mov   $45,a        ;/

; BRANCH_PROCESS_CHANNEL_0_INSTRUCTION_END
$4968: E5 9E 04  mov   a,$049E      ;\
$496B: 68 FF     cmp   a,#$FF       ;} If sound 3 channel 0 pitch slide disabled: go to BRANCH_CHANNEL_0_END
$496D: D0 60     bne   $49CF        ;/
$496F: E5 A0 04  mov   a,$04A0      ;\
$4972: F0 05     beq   $4979        ;} If sound 3 channel 0 pitch slide legato enabled:
$4974: E8 FF     mov   a,#$FF       ;\
$4976: C5 9F 04  mov   $049F,a      ;} Enable sound 3 channel 0 legato

$4979: E5 9A 04  mov   a,$049A      ;\
$497C: 65 9D 04  cmp   a,$049D      ;} If [sound 3 channel 0 note] >= [sound 3 channel 0 target note]:
$497F: 90 21     bcc   $49A2        ;/
$4981: E5 9B 04  mov   a,$049B      ;\
$4984: 80        setc               ;|
$4985: A5 9C 04  sbc   a,$049C      ;} Sound 3 channel 0 subnote -= [sound 3 channel 0 subnote delta]
$4988: C5 9B 04  mov   $049B,a      ;/
$498B: B0 34     bcs   $49C1        ; If [sound 3 channel 0 subnote] < 0:
$498D: 8C 9A 04  dec   $049A        ; Decrement sound 3 channel 0 note
$4990: E5 9D 04  mov   a,$049D      ;\
$4993: 65 9A 04  cmp   a,$049A      ;} If [sound 3 channel 0 target note] = [sound 3 channel 0 note]:
$4996: D0 29     bne   $49C1        ;/
$4998: E8 00     mov   a,#$00       ;\
$499A: C5 9E 04  mov   $049E,a      ;} Disable sound 3 channel 0 pitch slide
$499D: C5 9F 04  mov   $049F,a      ; Disable sound 3 channel 0 legato
$49A0: 2F 1F     bra   $49C1

$49A2: E5 9C 04  mov   a,$049C      ;\ Else ([sound 3 channel 0 note] < [sound 3 channel 0 target note]):
$49A5: 60        clrc               ;|
$49A6: 85 9B 04  adc   a,$049B      ;} Sound 3 channel 0 subnote += [sound 3 channel 0 subnote delta]
$49A9: C5 9B 04  mov   $049B,a      ;/
$49AC: 90 13     bcc   $49C1        ; If [sound 3 channel 0 subnote] >= 100h:
$49AE: AC 9A 04  inc   $049A        ; Increment sound 3 channel 0 note
$49B1: E5 9D 04  mov   a,$049D      ;\
$49B4: 65 9A 04  cmp   a,$049A      ;} If [sound 3 channel 0 target note] = [sound 3 channel 0 note]:
$49B7: D0 08     bne   $49C1        ;/
$49B9: E8 00     mov   a,#$00       ;\
$49BB: C5 9E 04  mov   $049E,a      ;} Disable sound 3 channel 0 pitch slide
$49BE: C5 9F 04  mov   $049F,a      ; Disable sound 3 channel 0 legato

$49C1: E5 9B 04  mov   a,$049B      ;\
$49C4: EC 9A 04  mov   y,$049A      ;} $11.$10 = [sound 3 channel 0 note]
$49C7: DA 10     movw  $10,ya       ;/
$49C9: E9 83 04  mov   x,$0483      ; X = [sound 3 channel 0 voice index]
$49CC: 3F B1 16  call  $16B1        ; Play note
}

; BRANCH_CHANNEL_0_END
$49CF: E8 FF     mov   a,#$FF       ;\
$49D1: 65 77 04  cmp   a,$0477      ;} If [sound 3 channel 1 disable byte] = FFh:
$49D4: D0 03     bne   $49D9        ;/
$49D6: 5F 85 4B  jmp   $4B85        ; Return

; Channel 1
{
$49D9: 8C 75 04  dec   $0475        ; Decrement sound 3 channel 1 instruction timer
$49DC: F0 03     beq   $49E1        ; If [sound 3 channel 1 instruction timer] != 0:
$49DE: 5F 1E 4B  jmp   $4B1E        ; Go to BRANCH_PROCESS_CHANNEL_1_INSTRUCTION_END

$49E1: E5 A6 04  mov   a,$04A6      ;\
$49E4: F0 02     beq   $49E8        ;} If sound 3 channel 1 legato flag enabled:
$49E6: 2F 43     bra   $4A2B        ; Go to LOOP_CHANNEL_1_COMMANDS

$49E8: E8 00     mov   a,#$00       ;\
$49EA: C5 A5 04  mov   $04A5,a      ;} Disable sound 3 channel 1 pitch slide
$49ED: C5 A3 04  mov   $04A3,a      ; Sound 3 channel 1 subnote delta = 0
$49F0: C5 A4 04  mov   $04A4,a      ; Sound 3 channel 1 target note = 0
$49F3: E8 FF     mov   a,#$FF       ;\
$49F5: 65 8E 04  cmp   a,$048E      ;} If [sound 3 channel 1 release flag] != FFh:
$49F8: F0 16     beq   $4A10        ;/
$49FA: E5 7F 04  mov   a,$047F      ;\
$49FD: 04 46     or    a,$46        ;} Key off flags |= [sound 3 channel 1 voice bitset]
$49FF: C4 46     mov   $46,a        ;/
$4A01: E8 02     mov   a,#$02       ;\
$4A03: C5 8F 04  mov   $048F,a      ;} Sound 3 channel 1 release timer = 2
$4A06: E8 01     mov   a,#$01       ;\
$4A08: C5 75 04  mov   $0475,a      ;} Sound 3 channel 1 instruction timer = 1
$4A0B: E8 FF     mov   a,#$FF       ;\
$4A0D: C5 8E 04  mov   $048E,a      ;} Sound 3 channel 1 release flag = FFh

$4A10: 8C 8F 04  dec   $048F        ; Decrement sound 3 channel 1 release timer
$4A13: F0 03     beq   $4A18        ; If [sound 3 channel 1 release timer] != 0:
$4A15: 5F 85 4B  jmp   $4B85        ; Return

$4A18: E8 00     mov   a,#$00       ;\
$4A1A: C5 8E 04  mov   $048E,a      ;} Sound 3 channel 1 release flag = 0
$4A1D: E5 81 04  mov   a,$0481      ;\
$4A20: 24 47     and   a,$47        ;} Current music voice bitset &= [sound 3 channel 1 voice mask]
$4A22: C4 47     mov   $47,a        ;/
$4A24: E5 81 04  mov   a,$0481      ;\
$4A27: 24 49     and   a,$49        ;} Noise enable flags &= [sound 3 channel 1 voice mask]
$4A29: C4 49     mov   $49,a        ;/

; LOOP_CHANNEL_1_COMMANDS
$4A2B: 3F 26 4C  call  $4C26        ; A = next sound 3 channel 1 data byte
$4A2E: 68 F9     cmp   a,#$F9       ;\
$4A30: D0 14     bne   $4A46        ;} If [A] = F9h:
$4A32: 3F 26 4C  call  $4C26        ;\
$4A35: C5 96 04  mov   $0496,a      ;} Sound 3 channel 1 ADSR settings = next sound 3 channel 1 data byte
$4A38: 3F 26 4C  call  $4C26        ;\
$4A3B: C5 97 04  mov   $0497,a      ;} Sound 3 channel 1 ADSR settings |= next sound 3 channel 1 data byte << 8
$4A3E: E8 FF     mov   a,#$FF       ;\
$4A40: C5 99 04  mov   $0499,a      ;} Sound 3 channel 1 update ADSR settings flag = FFh
$4A43: 5F 2B 4A  jmp   $4A2B        ; Go to LOOP_CHANNEL_1_COMMANDS

$4A46: 68 F5     cmp   a,#$F5       ;\
$4A48: D0 05     bne   $4A4F        ;} If [A] = F5h:
$4A4A: C5 A7 04  mov   $04A7,a      ; Enable sound 3 channel 1 pitch slide legato
$4A4D: 2F 09     bra   $4A58

$4A4F: 68 F8     cmp   a,#$F8       ;\ Else ([A] != F5h):
$4A51: D0 19     bne   $4A6C        ;} If [A] != F8h: go to BRANCH_CHANNEL_1_PITCH_SLIDE_END
$4A53: E8 00     mov   a,#$00       ;\
$4A55: C5 A7 04  mov   $04A7,a      ;} Disable sound 3 channel 1 pitch slide legato

$4A58: 3F 26 4C  call  $4C26        ;\
$4A5B: C5 A3 04  mov   $04A3,a      ;} Sound 3 channel 1 subnote delta = next sound 3 channel 1 data byte
$4A5E: 3F 26 4C  call  $4C26        ;\
$4A61: C5 A4 04  mov   $04A4,a      ;} Sound 3 channel 1 target note = next sound 3 channel 1 data byte
$4A64: E8 FF     mov   a,#$FF       ;\
$4A66: C5 A5 04  mov   $04A5,a      ;} Enable sound 3 channel 1 pitch slide = FFh
$4A69: 3F 26 4C  call  $4C26        ; A = next sound 3 channel 1 data byte

; BRANCH_CHANNEL_1_PITCH_SLIDE_END
$4A6C: 68 FF     cmp   a,#$FF       ;\
$4A6E: D0 06     bne   $4A76        ;} If [A] = FFh:
$4A70: 3F C9 4B  call  $4BC9        ; Reset sound 3 channel 1
$4A73: 5F 85 4B  jmp   $4B85        ; Return

$4A76: 68 FE     cmp   a,#$FE       ;\
$4A78: D0 0F     bne   $4A89        ;} If [A] = FEh:
$4A7A: 3F 26 4C  call  $4C26        ;\
$4A7D: C5 91 04  mov   $0491,a      ;} Sound 3 channel 1 repeat counter = next sound 3 channel 1 data byte
$4A80: E5 73 04  mov   a,$0473      ;\
$4A83: C5 93 04  mov   $0493,a      ;} Sound 3 channel 1 repeat point = [sound 3 channel 1 instruction list index]
$4A86: 3F 26 4C  call  $4C26        ; A = next sound 3 channel 1 data byte

$4A89: 68 FD     cmp   a,#$FD       ;\
$4A8B: D0 11     bne   $4A9E        ;} If [A] != FDh: go to BRANCH_CHANNEL_1_REPEAT_COMMAND
$4A8D: 8C 91 04  dec   $0491        ; Decrement sound 3 channel 1 repeat counter
$4A90: D0 03     bne   $4A95        ; If [sound 3 channel 1 repeat counter] = 0:
$4A92: 5F 2B 4A  jmp   $4A2B        ; Go to LOOP_CHANNEL_1_COMMANDS

; LOOP_CHANNEL_1_REPEAT_COMMAND
$4A95: E5 93 04  mov   a,$0493      ;\
$4A98: C5 73 04  mov   $0473,a      ;} Sound 3 channel 1 instruction list index = [sound 3 channel 1 repeat point]
$4A9B: 3F 26 4C  call  $4C26        ; A = next sound 3 channel 1 data byte

; BRANCH_CHANNEL_1_REPEAT_COMMAND
$4A9E: 68 FB     cmp   a,#$FB       ;\
$4AA0: D0 03     bne   $4AA5        ;} If [A] = FBh:
$4AA2: 5F 95 4A  jmp   $4A95        ; Go to LOOP_CHANNEL_1_REPEAT_COMMAND

$4AA5: 68 FC     cmp   a,#$FC       ;\
$4AA7: D0 0A     bne   $4AB3        ;} If [A] = FCh:
$4AA9: E5 7F 04  mov   a,$047F      ;\
$4AAC: 04 49     or    a,$49        ;} Noise enable flags |= [sound 3 channel 1 voice bitset]
$4AAE: C4 49     mov   $49,a        ;/
$4AB0: 5F 2B 4A  jmp   $4A2B        ; Go to LOOP_CHANNEL_1_COMMANDS

$4AB3: E9 84 04  mov   x,$0484      ; X = [sound 3 channel 1 voice index]
$4AB6: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$4AB9: 3F 26 4C  call  $4C26        ;\
$4ABC: E9 84 04  mov   x,$0484      ;} Track output volume = next sound 3 channel 1 data byte
$4ABF: D5 21 03  mov   $0321+x,a    ;/
$4AC2: E8 00     mov   a,#$00       ;\
$4AC4: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = 0
$4AC7: 3F 26 4C  call  $4C26        ;\
$4ACA: C4 11     mov   $11,a        ;} $10 = (next sound 3 channel 1 data byte) * 100h
$4ACC: 8F 00 10  mov   $10,#$00     ;/
$4ACF: 3F 4A 1C  call  $1C4A        ; Calculate and write DSP voice volumes if voice is not sound effect enabled
$4AD2: 3F 26 4C  call  $4C26        ; A = next sound 3 channel 1 data byte
$4AD5: 68 F6     cmp   a,#$F6       ;\
$4AD7: F0 08     beq   $4AE1        ;} If [A] != F6h:
$4AD9: C5 A1 04  mov   $04A1,a      ; Sound 3 channel 1 note = [A]
$4ADC: E8 00     mov   a,#$00       ;\
$4ADE: C5 A2 04  mov   $04A2,a      ;} Sound 3 channel 1 subnote = 0

$4AE1: EC A1 04  mov   y,$04A1      ;\
$4AE4: E5 A2 04  mov   a,$04A2      ;} $11.$10 = [sound 3 channel 1 note]
$4AE7: DA 10     movw  $10,ya       ;/
$4AE9: E9 84 04  mov   x,$0484      ; X = [sound 3 channel 1 voice index]
$4AEC: 3F B1 16  call  $16B1        ; Play note
$4AEF: 3F 26 4C  call  $4C26        ;\
$4AF2: C5 75 04  mov   $0475,a      ;} Sound 3 channel 1 instruction timer = next sound 3 channel 1 data byte
$4AF5: E5 99 04  mov   a,$0499      ;\
$4AF8: F0 18     beq   $4B12        ;} If [sound 3 channel 1 update ADSR settings flag] != 0:
$4AFA: E5 87 04  mov   a,$0487      ;\
$4AFD: 08 05     or    a,#$05       ;|
$4AFF: FD        mov   y,a          ;|
$4B00: E5 96 04  mov   a,$0496      ;|
$4B03: 3F 26 17  call  $1726        ;|
$4B06: E5 87 04  mov   a,$0487      ;} DSP sound 3 channel 1 ADSR settings = [sound 3 channel 1 ADSR settings]
$4B09: 08 06     or    a,#$06       ;|
$4B0B: FD        mov   y,a          ;|
$4B0C: E5 97 04  mov   a,$0497      ;|
$4B0F: 3F 26 17  call  $1726        ;/

$4B12: E5 A6 04  mov   a,$04A6      ;\
$4B15: D0 07     bne   $4B1E        ;} If sound 3 channel 1 legato disabled:
$4B17: E5 7F 04  mov   a,$047F      ;\
$4B1A: 04 45     or    a,$45        ;} Key on flags |= [sound 3 channel 1 voice bitset]
$4B1C: C4 45     mov   $45,a        ;/

; BRANCH_PROCESS_CHANNEL_1_INSTRUCTION_END
$4B1E: E5 A5 04  mov   a,$04A5      ;\
$4B21: 68 FF     cmp   a,#$FF       ;} If sound 3 channel 1 pitch slide disabled: return
$4B23: D0 60     bne   $4B85        ;/
$4B25: E5 A7 04  mov   a,$04A7      ;\
$4B28: F0 05     beq   $4B2F        ;} If sound 3 channel 1 pitch slide legato enabled:
$4B2A: E8 FF     mov   a,#$FF       ;\
$4B2C: C5 A6 04  mov   $04A6,a      ;} Enable sound 3 channel 1 legato

$4B2F: E5 A1 04  mov   a,$04A1      ;\
$4B32: 65 A4 04  cmp   a,$04A4      ;} If [sound 3 channel 1 note] >= [sound 3 channel 1 target note]:
$4B35: 90 21     bcc   $4B58        ;/
$4B37: E5 A2 04  mov   a,$04A2      ;\
$4B3A: 80        setc               ;|
$4B3B: A5 A3 04  sbc   a,$04A3      ;} Sound 3 channel 1 subnote -= [sound 3 channel 1 subnote delta]
$4B3E: C5 A2 04  mov   $04A2,a      ;/
$4B41: B0 34     bcs   $4B77        ; If [sound 3 channel 1 subnote] < 0:
$4B43: 8C A1 04  dec   $04A1        ; Decrement sound 3 channel 1 note
$4B46: E5 A4 04  mov   a,$04A4      ;\
$4B49: 65 A1 04  cmp   a,$04A1      ;} If [sound 3 channel 1 target note] = [sound 3 channel 1 note]:
$4B4C: D0 29     bne   $4B77        ;/
$4B4E: E8 00     mov   a,#$00       ;\
$4B50: C5 A5 04  mov   $04A5,a      ;} Disable sound 3 channel 1 pitch slide
$4B53: C5 A6 04  mov   $04A6,a      ; Disable sound 3 channel 1 legato
$4B56: 2F 1F     bra   $4B77

$4B58: E5 A3 04  mov   a,$04A3      ;\ Else ([sound 3 channel 1 note] < [sound 3 channel 1 target note]):
$4B5B: 60        clrc               ;|
$4B5C: 85 A2 04  adc   a,$04A2      ;} Sound 3 channel 1 subnote += [sound 3 channel 1 subnote delta]
$4B5F: C5 A2 04  mov   $04A2,a      ;/
$4B62: 90 13     bcc   $4B77        ; If [sound 3 channel 1 subnote] >= 100h:
$4B64: AC A1 04  inc   $04A1        ; Increment sound 3 channel 1 note
$4B67: E5 A4 04  mov   a,$04A4      ;\
$4B6A: 65 A1 04  cmp   a,$04A1      ;} If [sound 3 channel 1 target note] = [sound 3 channel 1 note]:
$4B6D: D0 08     bne   $4B77        ;/
$4B6F: E8 00     mov   a,#$00       ;\
$4B71: C5 A5 04  mov   $04A5,a      ;} Disable sound 3 channel 1 pitch slide
$4B74: C5 A6 04  mov   $04A6,a      ; Disable sound 3 channel 1 legato

$4B77: E5 A2 04  mov   a,$04A2      ;\
$4B7A: EC A1 04  mov   y,$04A1      ;} $11.$10 = [sound 3 channel 1 note]
$4B7D: DA 10     movw  $10,ya       ;/
$4B7F: E9 84 04  mov   x,$0484      ; X = [sound 3 channel 1 voice index]
$4B82: 3F B1 16  call  $16B1        ; Play note
}

$4B85: 6F        ret
}


;;; $4B86: Reset sound 3 channel 0 ;;;
{
$4B86: E8 FF     mov   a,#$FF       ;\
$4B88: C5 76 04  mov   $0476,a      ;} Sound 3 channel 0 disable byte = FFh
$4B8B: E8 00     mov   a,#$00       ;\
$4B8D: C5 98 04  mov   $0498,a      ;} Sound 3 channel 0 update ADSR settings flag = 0
$4B90: E5 85 04  mov   a,$0485      ;\
$4B93: 25 80 04  and   a,$0480      ;} Sound 3 enabled voices &= [sound 3 channel 0 mask]
$4B96: C5 85 04  mov   $0485,a      ;/
$4B99: E4 1A     mov   a,$1A        ;\
$4B9B: 25 80 04  and   a,$0480      ;} Enabled sound effect voices &= [sound 3 channel 0 mask]
$4B9E: C4 1A     mov   $1A,a        ;/
$4BA0: E4 47     mov   a,$47        ;\
$4BA2: 05 7E 04  or    a,$047E      ;} Current music voice bitset |= [sound 3 channel 0 voice bitset]
$4BA5: C4 47     mov   $47,a        ;/
$4BA7: E4 46     mov   a,$46        ;\
$4BA9: 05 7E 04  or    a,$047E      ;} Key off flags |= [sound 3 channel 0 voice bitset]
$4BAC: C4 46     mov   $46,a        ;/
$4BAE: E9 83 04  mov   x,$0483      ; X = [sound 3 channel 0 voice index]
$4BB1: F5 11 02  mov   a,$0211+x    ; A = [track instrument index]
$4BB4: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$4BB7: E9 83 04  mov   x,$0483      ; X = [sound 3 channel 0 voice index]
$4BBA: E5 88 04  mov   a,$0488      ;\
$4BBD: D5 21 03  mov   $0321+x,a    ;} Track output volume = [sound 3 channel 0 backup of track output volume]
$4BC0: E5 89 04  mov   a,$0489      ;\
$4BC3: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = [sound 3 channel 0 backup of track phase inversion options]
$4BC6: 5F 0C 4C  jmp   $4C0C        ; Go to reset sound 3 if no enabled voices
}


;;; $4BC9: Reset sound 3 channel 1 ;;;
{
$4BC9: E8 FF     mov   a,#$FF       ;\
$4BCB: C5 77 04  mov   $0477,a      ;} Sound 3 channel 1 disable byte = FFh
$4BCE: E8 00     mov   a,#$00       ;\
$4BD0: C5 99 04  mov   $0499,a      ;} Sound 3 channel 1 update ADSR settings flag = 0
$4BD3: E5 85 04  mov   a,$0485      ;\
$4BD6: 25 81 04  and   a,$0481      ;} Sound 3 enabled voices &= [sound 3 channel 1 mask]
$4BD9: C5 85 04  mov   $0485,a      ;/
$4BDC: E4 1A     mov   a,$1A        ;\
$4BDE: 25 81 04  and   a,$0481      ;} Enabled sound effect voices &= [sound 3 channel 1 mask]
$4BE1: C4 1A     mov   $1A,a        ;/
$4BE3: E4 47     mov   a,$47        ;\
$4BE5: 05 7F 04  or    a,$047F      ;} Current music voice bitset |= [sound 3 channel 1 voice bitset]
$4BE8: C4 47     mov   $47,a        ;/
$4BEA: E4 46     mov   a,$46        ;\
$4BEC: 05 7F 04  or    a,$047F      ;} Key off flags |= [sound 3 channel 1 voice bitset]
$4BEF: C4 46     mov   $46,a        ;/
$4BF1: E9 84 04  mov   x,$0484      ; X = [sound 3 channel 1 voice index]
$4BF4: F5 11 02  mov   a,$0211+x    ; A = [track instrument index]
$4BF7: 3F FC 18  call  $18FC        ; Set voice's instrument settings
$4BFA: E9 84 04  mov   x,$0484      ; X = [sound 3 channel 1 voice index]
$4BFD: E5 8A 04  mov   a,$048A      ;\
$4C00: D5 21 03  mov   $0321+x,a    ;} Track output volume = [sound 3 channel 1 backup of track output volume]
$4C03: E5 8B 04  mov   a,$048B      ;\
$4C06: D5 51 03  mov   $0351+x,a    ;} Track phase inversion options = [sound 3 channel 1 backup of track phase inversion options]
$4C09: 5F 0C 4C  jmp   $4C0C        ; Go to reset sound 3 if no enabled voices
}


;;; $4C0C: Reset sound 3 if no enabled voices ;;;
{
; Merge point of reset sound 3 channel routines
$4C0C: E5 85 04  mov   a,$0485      ;\
$4C0F: D0 0B     bne   $4C1C        ;} If [sound 3 enabled voices] = 0:
$4C11: E8 00     mov   a,#$00       ;\
$4C13: C5 70 04  mov   $0470,a      ;} Current sound 3 = 0
$4C16: C5 BD 04  mov   $04BD,a      ; Sound 3 priority = 0
$4C19: C5 7C 04  mov   $047C,a      ; Sound 3 initialisation flag = 0

$4C1C: 6F        ret
}


;;; $4C1D: A = next sound 3 channel 0 data byte ;;;
{
$4C1D: EC 72 04  mov   y,$0472      ;\
$4C20: F7 E8     mov   a,($E8)+y    ;} A = [[$E8] + [$0472++]]
$4C22: AC 72 04  inc   $0472        ;/
$4C25: 6F        ret
}


;;; $4C26: A = next sound 3 channel 1 data byte ;;;
{
$4C26: EC 73 04  mov   y,$0473      ;\
$4C29: F7 EA     mov   a,($EA)+y    ;} A = [[$EA] + [$0473++]]
$4C2B: AC 73 04  inc   $0473        ;/
$4C2E: 6F        ret
}


;;; $4C2F: Sound 3 channel variable pointers ;;;
{
$4C2F:           dw 047E,047F ; Sound 3 channel voice bitsets
$4C33:           dw 0480,0481 ; Sound 3 channel voice masks
$4C37:           dw 0483,0484 ; Sound 3 channel voice indices
}


;;; $4C3B: Sound 3 initialisation ;;;
{
$4C3B: E8 09     mov   a,#09        ;\
$4C3D: C5 7D 04  mov   $047D,a      ;} Voice ID = 9
$4C40: E4 1A     mov   a,$1A        ;\
$4C42: C5 7B 04  mov   $047B,a      ;} Remaining enabled sound effect voices = [enabled sound effect voices]
$4C45: E8 FF     mov   a,#$FF       ;\
$4C47: C5 7C 04  mov   $047C,a      ;} Sound 3 initialisation flag = FFh
$4C4A: E8 00     mov   a,#$00
$4C4C: C5 82 04  mov   $0482,a      ; Sound 3 channel index * 2 = 0
$4C4F: C5 78 04  mov   $0478,a      ; Sound 3 channel index = 0
$4C52: C5 7E 04  mov   $047E,a      ;\
$4C55: C5 7F 04  mov   $047F,a      ;} Sound 3 channel voice bitsets = 0
$4C58: C5 83 04  mov   $0483,a      ;\
$4C5B: C5 84 04  mov   $0484,a      ;} Sound 3 channel voice indices = 0
$4C5E: E8 FF     mov   a,#$FF
$4C60: C5 80 04  mov   $0480,a      ;\
$4C63: C5 81 04  mov   $0481,a      ;} Sound 3 channel voice masks = FFh
$4C66: C5 76 04  mov   $0476,a      ;\
$4C69: C5 77 04  mov   $0477,a      ;} Sound 3 channel disable bytes = FFh

; LOOP
$4C6C: 8C 7D 04  dec   $047D        ; Decrement voice ID
$4C6F: F0 7E     beq   $4CEF        ; If [voice ID] = 0: return
$4C71: 0C 7B 04  asl   $047B        ; Remaining enabled sound effect voices <<= 1
$4C74: B0 F6     bcs   $4C6C        ; If carry set: go to LOOP
$4C76: E8 00     mov   a,#$00       ;\
$4C78: 65 79 04  cmp   a,$0479      ;} If [number of sound 3 voices to set up] = 0: return
$4C7B: F0 72     beq   $4CEF        ;/
$4C7D: 8C 79 04  dec   $0479        ; Decrement number of sound 3 voices to set up
$4C80: E8 00     mov   a,#$00       ;\
$4C82: E9 78 04  mov   x,$0478      ;} Sound 3 channel disable byte = 0
$4C85: D5 76 04  mov   $0476+x,a    ;/
$4C88: AC 78 04  inc   $0478        ; Increment sound 3 channel index
$4C8B: E5 82 04  mov   a,$0482      ;\
$4C8E: 5D        mov   x,a          ;} Y = [sound 3 channel index] * 2
$4C8F: FD        mov   y,a          ;/
$4C90: F5 2F 4C  mov   a,$4C2F+x    ;\
$4C93: C4 E2     mov   $E2,a        ;} $E2 = sound 3 channel voice bitset
$4C95: F5 33 4C  mov   a,$4C33+x    ;\
$4C98: C4 E4     mov   $E4,a        ;} $E4 = sound 3 channel voice mask
$4C9A: F5 37 4C  mov   a,$4C37+x    ;\
$4C9D: C4 E6     mov   $E6,a        ;} $E6 = sound 3 channel voice index
$4C9F: 3D        inc   x
$4CA0: F5 2F 4C  mov   a,$4C2F+x
$4CA3: C4 E3     mov   $E3,a
$4CA5: F5 33 4C  mov   a,$4C33+x
$4CA8: C4 E5     mov   $E5,a
$4CAA: F5 37 4C  mov   a,$4C37+x
$4CAD: C4 E7     mov   $E7,a
$4CAF: AC 82 04  inc   $0482        ;\
$4CB2: AC 82 04  inc   $0482        ;} Sound 3 channel index * 2 += 2
$4CB5: E5 7D 04  mov   a,$047D      ;\
$4CB8: C5 7A 04  mov   $047A,a      ;|
$4CBB: 8C 7A 04  dec   $047A        ;} Voice index = ([voice ID] - 1) * 2
$4CBE: 60        clrc               ;|
$4CBF: 0C 7A 04  asl   $047A        ;/
$4CC2: E9 7A 04  mov   x,$047A      ;\
$4CC5: F5 21 03  mov   a,$0321+x    ;} Sound 3 channel backup of track output volume = [track output volume]
$4CC8: D6 88 04  mov   $0488+y,a    ;/
$4CCB: FC        inc   y            ;\
$4CCC: F5 51 03  mov   a,$0351+x    ;} Sound 3 channel backup of track phase inversion options = [track phase inversion options]
$4CCF: D6 88 04  mov   $0488+y,a    ;/
$4CD2: 8D 00     mov   y,#$00       ;\
$4CD4: E5 7A 04  mov   a,$047A      ;} Sound 3 channel voice index = [voice index]
$4CD7: D7 E6     mov   ($E6)+y,a    ;/
$4CD9: E5 7D 04  mov   a,$047D      ;\
$4CDC: 3F 75 28  call  $2875        ;} Go to [$4CDF + [voice index]]
$4CDF:           dw 4DAD, 4D92, 4D77, 4D5C, 4D41, 4D26, 4D0B, 4CF0

$4CEF: 6F        ret

$4CF0: E2 1A     set7  $1A          ; Enable voice 7
$4CF2: F2 47     clr7  $47          ; Current music voice bitset &= ~80h
$4CF4: F2 4A     clr7  $4A          ; Disable echo on voice 7
$4CF6: E8 80     mov   a,#$80       ;\
$4CF8: 05 85 04  or    a,$0485      ;} Sound 3 enabled voices |= 80h
$4CFB: C5 85 04  mov   $0485,a      ;/
$4CFE: 8D 00     mov   y,#$00
$4D00: E8 80     mov   a,#$80       ;\
$4D02: D7 E2     mov   ($E2)+y,a    ;} Sound 3 channel voice bitset = 80h
$4D04: E8 7F     mov   a,#$7F       ;\
$4D06: D7 E4     mov   ($E4)+y,a    ;} Sound 3 channel voice mask = ~80h
$4D08: 5F 6C 4C  jmp   $4C6C        ; Go to LOOP

$4D0B: C2 1A     set6  $1A          ; Enable voice 6
$4D0D: D2 47     clr6  $47          ; Current music voice bitset &= ~40h
$4D0F: D2 4A     clr6  $4A          ; Disable echo on voice 6
$4D11: E8 40     mov   a,#$40       ;\
$4D13: 05 85 04  or    a,$0485      ;} Sound 3 enabled voices |= 40h
$4D16: C5 85 04  mov   $0485,a      ;/
$4D19: 8D 00     mov   y,#$00
$4D1B: E8 40     mov   a,#$40       ;\
$4D1D: D7 E2     mov   ($E2)+y,a    ;} Sound 3 channel voice bitset = 40h
$4D1F: E8 BF     mov   a,#$BF       ;\
$4D21: D7 E4     mov   ($E4)+y,a    ;} Sound 3 channel voice mask = ~40h
$4D23: 5F 6C 4C  jmp   $4C6C        ; Go to LOOP

$4D26: A2 1A     set5  $1A          ; Enable voice 5
$4D28: B2 47     clr5  $47          ; Current music voice bitset &= ~20h
$4D2A: B2 4A     clr5  $4A          ; Disable echo on voice 5
$4D2C: E8 20     mov   a,#$20       ;\
$4D2E: 05 85 04  or    a,$0485      ;} Sound 3 enabled voices |= 20h
$4D31: C5 85 04  mov   $0485,a      ;/
$4D34: 8D 00     mov   y,#$00
$4D36: E8 20     mov   a,#$20       ;\
$4D38: D7 E2     mov   ($E2)+y,a    ;} Sound 3 channel voice bitset = 20h
$4D3A: E8 DF     mov   a,#$DF       ;\
$4D3C: D7 E4     mov   ($E4)+y,a    ;} Sound 3 channel voice mask = ~20h
$4D3E: 5F 6C 4C  jmp   $4C6C        ; Go to LOOP

$4D41: 82 1A     set4  $1A          ; Enable voice 4
$4D43: 92 47     clr4  $47          ; Current music voice bitset &= ~10h
$4D45: 92 4A     clr4  $4A          ; Disable echo on voice 4
$4D47: E8 10     mov   a,#$10       ;\
$4D49: 05 85 04  or    a,$0485      ;} Sound 3 enabled voices |= 10h
$4D4C: C5 85 04  mov   $0485,a      ;/
$4D4F: 8D 00     mov   y,#$00
$4D51: E8 10     mov   a,#$10       ;\
$4D53: D7 E2     mov   ($E2)+y,a    ;} Sound 3 channel voice bitset = 10h
$4D55: E8 EF     mov   a,#$EF       ;\
$4D57: D7 E4     mov   ($E4)+y,a    ;} Sound 3 channel voice mask = ~10h
$4D59: 5F 6C 4C  jmp   $4C6C        ; Go to LOOP

$4D5C: 62 1A     set3  $1A          ; Enable voice 3
$4D5E: 72 47     clr3  $47          ; Current music voice bitset &= ~8
$4D60: 72 4A     clr3  $4A          ; Disable echo on voice 3
$4D62: E8 08     mov   a,#$08       ;\
$4D64: 05 85 04  or    a,$0485      ;} Sound 3 enabled voices |= 8
$4D67: C5 85 04  mov   $0485,a      ;/
$4D6A: 8D 00     mov   y,#$00
$4D6C: E8 08     mov   a,#$08       ;\
$4D6E: D7 E2     mov   ($E2)+y,a    ;} Sound 3 channel voice bitset = 8
$4D70: E8 F7     mov   a,#$F7       ;\
$4D72: D7 E4     mov   ($E4)+y,a    ;} Sound 3 channel voice mask = ~8
$4D74: 5F 6C 4C  jmp   $4C6C        ; Go to LOOP

$4D77: 42 1A     set2  $1A          ; Enable voice 2
$4D79: 52 47     clr2  $47          ; Current music voice bitset &= ~4
$4D7B: 52 4A     clr2  $4A          ; Disable echo on voice 2
$4D7D: E8 04     mov   a,#$04       ;\
$4D7F: 05 85 04  or    a,$0485      ;} Sound 3 enabled voices |= 4
$4D82: C5 85 04  mov   $0485,a      ;/
$4D85: 8D 00     mov   y,#$00
$4D87: E8 04     mov   a,#$04       ;\
$4D89: D7 E2     mov   ($E2)+y,a    ;} Sound 3 channel voice bitset = 4
$4D8B: E8 FB     mov   a,#$FB       ;\
$4D8D: D7 E4     mov   ($E4)+y,a    ;} Sound 3 channel voice mask = ~4
$4D8F: 5F 6C 4C  jmp   $4C6C        ; Go to LOOP

$4D92: 22 1A     set1  $1A          ; Enable voice 1
$4D94: 32 47     clr1  $47          ; Current music voice bitset &= ~2
$4D96: 32 4A     clr1  $4A          ; Disable echo on voice 1
$4D98: E8 02     mov   a,#$02       ;\
$4D9A: 05 85 04  or    a,$0485      ;} Sound 3 enabled voices |= 2
$4D9D: C5 85 04  mov   $0485,a      ;/
$4DA0: 8D 00     mov   y,#$00
$4DA2: E8 02     mov   a,#$02       ;\
$4DA4: D7 E2     mov   ($E2)+y,a    ;} Sound 3 channel voice bitset = 2
$4DA6: E8 FD     mov   a,#$FD       ;\
$4DA8: D7 E4     mov   ($E4)+y,a    ;} Sound 3 channel voice mask = ~2
$4DAA: 5F 6C 4C  jmp   $4C6C        ; Go to LOOP

$4DAD: 02 1A     set0  $1A          ; Enable voice 0
$4DAF: 12 47     clr0  $47          ; Current music voice bitset &= ~1
$4DB1: 12 4A     clr0  $4A          ; Disable echo on voice 0
$4DB3: E8 01     mov   a,#$01       ;\
$4DB5: 05 85 04  or    a,$0485      ;} Sound 3 enabled voices |= 1
$4DB8: C5 85 04  mov   $0485,a      ;/
$4DBB: 8D 00     mov   y,#$00
$4DBD: E8 01     mov   a,#$01       ;\
$4DBF: D7 E2     mov   ($E2)+y,a    ;} Sound 3 channel voice bitset = 1
$4DC1: E8 FE     mov   a,#$FE       ;\
$4DC3: D7 E4     mov   ($E4)+y,a    ;} Sound 3 channel voice mask = ~1
$4DC5: 5F 6C 4C  jmp   $4C6C        ; Go to LOOP
}


;;; $4DC8: A = next sound 2 channel instruction list pointer ;;;
{
$4DC8: FC        inc   y            ;\
$4DC9: F7 E0     mov   a,($E0)+y    ;} A = [[$E0] + [++Y]]
$4DCB: 6F        ret
}


;;; $4DCC: A *= 8 ;;;
{
$4DCC: 1C        asl   a
$4DCD: 1C        asl   a
$4DCE: 1C        asl   a
$4DCF: 6F        ret
}


;;; $4DD0: Sound 3 configurations ;;;
{
;;; $4DD0: Sound 3 configuration - sound 1 - number of sound 3 voices = 1, sound 3 priority = 1, sound 3 low health priority = 0 ;;;
{
; 1: Silence
$4DD0: E8 01     mov   a,#$01
$4DD2: C5 79 04  mov   $0479,a
$4DD5: E8 00     mov   a,#$00
$4DD7: C5 BA 04  mov   $04BA,a
$4DDA: E8 01     mov   a,#$01
$4DDC: C5 BD 04  mov   $04BD,a
$4DDF: 6F        ret
}


;;; $4DE0: Sound 3 configuration - sound 2 - number of sound 3 voices = 1, sound 3 low health priority = 2 ;;;
{
; 2: Low health beep
$4DE0: E8 01     mov   a,#$01
$4DE2: C5 79 04  mov   $0479,a
$4DE5: E8 02     mov   a,#$02
$4DE7: C5 BA 04  mov   $04BA,a
$4DEA: 6F        ret
}


;;; $4DEB: Sound 3 configuration - sound 3 - number of voices = 1, priority = 0 ;;;
{
; 3: Speed booster
$4DEB: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4DEE: 6F        ret
}


;;; $4DEF: Sound 3 configuration - sounds 4/5 - number of voices = 2, priority = 0 ;;;
{
; 4: Samus landed hard
; 5: Samus landed / wall-jumped
$4DEF: 3F 6E 4E  call  $4E6E        ; Number of sound 3 voices = 2, sound 3 priority = 0
$4DF2: 6F        ret
}


;;; $4DF3: Sound 3 configuration - sound 6 - number of voices = 1, priority = 0 ;;;
{
; 6: Samus' footsteps
$4DF3: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4DF6: 6F        ret
}


;;; $4DF7: Sound 3 configuration - sounds 7/8 - number of voices = 2, priority = 1 ;;;
{
; 7: Door opened
; 8: Door closed
$4DF7: 3F 79 4E  call  $4E79        ; Number of sound 3 voices = 2, sound 3 priority = 1
$4DFA: 6F        ret
}


;;; $4DFB: Sound 3 configuration - sound 9 - number of voices = 1, priority = 0 ;;;
{
; 9: Missile door shot with missile / shot zebetite
$4DFB: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4DFE: 6F        ret
}


;;; $4DFF: Sound 3 configuration - sound Ah - number of voices = 1, priority = 1 ;;;
{
; Ah: Enemy frozen
$4DFF: 3F 84 4E  call  $4E84        ; Number of sound 3 voices = 1, sound 3 priority = 1
$4E02: 6F        ret
}


;;; $4E03: Sound 3 configuration - sound Bh - number of voices = 2, priority = 0 ;;;
{
; Bh: Elevator
$4E03: 3F 6E 4E  call  $4E6E        ; Number of sound 3 voices = 2, sound 3 priority = 0
$4E06: 6F        ret
}


;;; $4E07: Sound 3 configuration - sounds Ch/Dh - number of voices = 1, priority = 0 ;;;
{
; Ch: Stored shinespark
; Dh: Typewriter stroke - intro
$4E07: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E0A: 6F        ret
}


;;; $4E0B: Sound 3 configuration - sound Eh - number of voices = 2, priority = 1 ;;;
{
; Eh: Gate opening/closing
$4E0B: 3F 79 4E  call  $4E79        ; Number of sound 3 voices = 2, sound 3 priority = 1
$4E0E: 6F        ret
}


;;; $4E0F: Sound 3 configuration - sound Fh - number of voices = 2, priority = 0 ;;;
{
; Fh: Shinespark
$4E0F: 3F 6E 4E  call  $4E6E        ; Number of sound 3 voices = 2, sound 3 priority = 0
$4E12: 6F        ret
}


;;; $4E13: Sound 3 configuration - sound 10h - number of voices = 1, priority = 0 ;;;
{
; 10h: Shinespark ended
$4E13: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E16: 6F        ret
}


;;; $4E17: Sound 3 configuration - sound 11h - number of voices = 1, priority = 0 ;;;
{
; 11h: (shorter version of shinespark ended)
$4E17: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E1A: 6F        ret
}


;;; $4E1B: Sound 3 configuration - sound 12h - number of voices = 1, priority = 1 ;;;
{
; 12h: (Empty)
$4E1B: 3F 84 4E  call  $4E84        ; Number of sound 3 voices = 1, sound 3 priority = 1
$4E1E: 6F        ret
}


;;; $4E1F: Sound 3 configuration - sound 13h - number of voices = 1, priority = 0 ;;;
{
; 13h: Mother Brain's / torizo's projectile hits surface / Shitroid exploding / Mother Brain exploding
$4E1F: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E22: 6F        ret
}


;;; $4E23: Sound 3 configuration - sounds 14h/15h - number of voices = 2, priority = 1 ;;;
{
; 14h: Gunship elevator activated
; 15h: Gunship elevator deactivated
$4E23: 3F 79 4E  call  $4E79        ; Number of sound 3 voices = 2, sound 3 priority = 1
$4E26: 6F        ret
}


;;; $4E27: Sound 3 configuration - sound 16h - number of voices = 1, priority = 0 ;;;
{
; 16h: Unused. Crunchy footstep
$4E27: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E2A: 6F        ret
}


;;; $4E2B: Sound 3 configuration - sound 17h - number of voices = 1, priority = 0 ;;;
{
; 17h: Mother Brain's blue rings
$4E2B: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E2E: 6F        ret
}


;;; $4E2F: Sound 3 configuration - sound 18h - number of voices = 1, priority = 0 ;;;
{
; 18h: (Empty)
$4E2F: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E32: 6F        ret
}


;;; $4E33: Sound 3 configuration - sound 19h - number of voices = 2, priority = 1 ;;;
{
; 19h: Shitroid dies
$4E33: 3F 79 4E  call  $4E79        ; Number of sound 3 voices = 2, sound 3 priority = 1
$4E36: 6F        ret
}


;;; $4E37: Sound 3 configuration - sound 1Ah - number of voices = 1, priority = 0 ;;;
{
; 1Ah: (Empty)
$4E37: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E3A: 6F        ret
}


;;; $4E3B: Sound 3 configuration - sound 1Bh - number of voices = 2, priority = 1 ;;;
{
; 1Bh: Draygon dying cry
$4E3B: 3F 79 4E  call  $4E79        ; Number of sound 3 voices = 2, sound 3 priority = 1
$4E3E: 6F        ret
}


;;; $4E3F: Sound 3 configuration - sounds 1Ch..1Fh - number of voices = 1, priority = 0 ;;;
{
; 1Ch: Crocomire spit
; 1Dh: Phantoon's flame
; 1Eh: Kraid's earthquake
; 1Fh: Kraid fires lint
$4E3F: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E42: 6F        ret
}


;;; $4E43: Sound 3 configuration - sounds 20h/21h - number of voices = 1, priority = 1 ;;;
{
; 20h: (Empty)
; 21h: Ridley whips its tail
$4E43: 3F 84 4E  call  $4E84        ; Number of sound 3 voices = 1, sound 3 priority = 1
$4E46: 6F        ret
}


;;; $4E47: Sound 3 configuration - sounds 22h/23h - number of voices = 1, priority = 0 ;;;
{
; 22h: Crocomire acid damage
; 23h: Baby metroid cry 1
$4E47: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E4A: 6F        ret
}


;;; $4E4B: Sound 3 configuration - sound 24h - number of voices = 1, priority = 1 ;;;
{
; 24h: Baby metroid cry - Ceres
$4E4B: 3F 84 4E  call  $4E84        ; Number of sound 3 voices = 1, sound 3 priority = 1
$4E4E: 6F        ret
}


;;; $4E4F: Sound 3 configuration - sounds 25h..2Bh - number of voices = 1, priority = 0 ;;;
{
; 25h: Silence (clear speed booster / elevator sound)
; 26h: Baby metroid cry 2
; 27h: Baby metroid cry 3
; 28h: Phantoon materialises attack
; 29h: Phantoon's super missiled attack
; 2Ah: Pause menu ambient beep
; 2Bh: Resume speed booster / shinespark
$4E4F: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E52: 6F        ret
}


;;; $4E53: Sound 3 configuration - sound 2Ch - number of voices = 2, priority = 1 ;;;
{
; 2Ch: Ceres door opening
$4E53: 3F 79 4E  call  $4E79        ; Number of sound 3 voices = 2, sound 3 priority = 1
$4E56: 6F        ret
}


;;; $4E57: Sound 3 configuration - sound 2Dh - number of voices = 1, priority = 0 ;;;
{
; 2Dh: Gaining/losing incremental health
$4E57: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E5A: 6F        ret
}


;;; $4E5B: Sound 3 configuration - sound 2Eh - number of voices = 2, priority = 1 ;;;
{
; 2Eh: Mother Brain's glass shattering
$4E5B: 3F 79 4E  call  $4E79        ; Number of sound 3 voices = 2, sound 3 priority = 1
$4E5E: 6F        ret
}


;;; $4E5F: Sound 3 configuration - sound 2Fh - number of voices = 1, priority = 0 ;;;
{
; 2Fh: (Empty)
$4E5F: 3F 63 4E  call  $4E63        ; Number of sound 3 voices = 1, sound 3 priority = 0
$4E62: 6F        ret
}
}


;;; $4E63: Number of sound 3 voices = 1, sound 3 priority = 0 ;;;
{
; 3: Speed booster
; 6: Samus' footsteps
; 9: Missile door shot with missile / shot zebetite
; Ch: Stored shinespark
; Dh: Typewriter stroke - intro
; 10h: Shinespark ended
; 11h: (shorter version of shinespark ended)
; 13h: Mother Brain's / torizo's projectile hits surface / Shitroid exploding / Mother Brain exploding
; 16h: Unused. Crunchy footstep
; 17h: Mother Brain's blue rings
; 18h: (Empty)
; 1Ah: (Empty)
; 1Ch: Crocomire spit
; 1Dh: Phantoon's flame
; 1Eh: Kraid's earthquake
; 1Fh: Kraid fires lint
; 22h: Crocomire acid damage
; 23h: Baby metroid cry 1
; 25h: Silence (clear speed booster / elevator sound)
; 26h: Baby metroid cry 2
; 27h: Baby metroid cry 3
; 28h: Phantoon materialises attack
; 29h: Phantoon's super missiled attack
; 2Ah: Pause menu ambient beep
; 2Bh: Resume speed booster / shinespark
; 2Dh: Gaining/losing incremental health
; 2Fh: (Empty)
$4E63: E8 01     mov   a,#$01
$4E65: C5 79 04  mov   $0479,a
$4E68: E8 00     mov   a,#$00
$4E6A: C5 BD 04  mov   $04BD,a
$4E6D: 6F        ret
}


;;; $4E6E: Number of sound 3 voices = 2, sound 3 priority = 0 ;;;
{
; 4: Samus landed hard
; 5: Samus landed / wall-jumped
; Bh: Elevator
; Fh: Shinespark
$4E6E: E8 02     mov   a,#$02
$4E70: C5 79 04  mov   $0479,a
$4E73: E8 00     mov   a,#$00
$4E75: C5 BD 04  mov   $04BD,a
$4E78: 6F        ret
}


;;; $4E79: Number of sound 3 voices = 2, sound 3 priority = 1 ;;;
{
; 7: Door opened
; 8: Door closed
; Eh: Gate opening/closing
; 14h: Gunship elevator activated
; 15h: Gunship elevator deactivated
; 19h: Shitroid dies
; 1Bh: Draygon dying cry
; 2Ch: Ceres door opening
; 2Eh: Mother Brain's glass shattering
$4E79: E8 02     mov   a,#$02
$4E7B: C5 79 04  mov   $0479,a
$4E7E: E8 01     mov   a,#$01
$4E80: C5 BD 04  mov   $04BD,a
$4E83: 6F        ret
}


;;; $4E84: Number of sound 3 voices = 1, sound 3 priority = 1 ;;;
{
; Ah: Enemy frozen
; 12h: (Empty)
; 20h: (Empty)
; 21h: Ridley whips its tail
; 24h: Baby metroid cry - Ceres
$4E84: E8 01     mov   a,#$01
$4E86: C5 79 04  mov   $0479,a
$4E89: E8 01     mov   a,#$01
$4E8B: C5 BD 04  mov   $04BD,a
$4E8E: 6F        ret
}


;;; $4E8F: Sound 3 instruction lists ;;;
{
$4E8F:           dw 4EED, 4EF5, 4F00, 4F4B, 4F5B, 4F6B, 4F73, 4F89, 4F9F, 4FB6, 4FE1, 4FF7, 4FFF, 500C, 5047, 50A8,
                    50C4, 50D1, 50D4, 50F0, 511A, 5130, 5142, 5175, 5178, 5188, 518B, 51B9, 51C1, 51D4, 51F0, 51FD,
                    5200, 5208, 5229, 5231, 5239, 5241, 524E, 5256, 5277, 5293, 52A5, 52B0, 52C6, 52F1, 530B

; Instruction list format:
{
; Commands:
;     F5h dd tt - legato pitch slide with subnote delta = d, target note = t
;     F8h dd tt -        pitch slide with subnote delta = d, target note = t
;     F9h aaaa - voice's ADSR settings = a
;     FBh - repeat
;     FCh - enable noise
;     FDh - decrement repeat counter and repeat if non-zero
;     FEh cc - set repeat pointer with repeat counter = c
;     FFh - end

; Otherwise:
;     ii vv pp nn tt
;     i: Instrument index
;     v: Volume
;     p: Panning
;     n: Note. F6h is a tie
;     t: Length
}

; Sound 1: Silence
$4EED:           dw 4EEF
$4EEF:           db 11,00,0A,BC,03, FF

; Sound 2: Low health beep
$4EF5:           dw 4EF7
$4EF7:           db FE,00, 15,90,0A,BC,F0, FB, FF

; Sound 3: Speed booster
$4F00:           dw 4F02

; Speed booster / Dachora speed booster (sound library 2)
$4F02:           db F5,E0,C7, 05,60,0A,98,12, F5,E0,C7, 05,70,0A,A4,11, F5,E0,C7, 05,80,0A,B0,10, F5,E0,C7, 05,80,0A,B4,08, F5,E0,C7, 05,80,0A,B9,07, F5,E0,C7, 05,80,0A,BC,06, F5,E0,C1, 05,80,0A,BC,06, F5,E0,C7, 05,80,0A,C5,06,
                    FE,00, 05,60,0A,C7,10, FB,
                    FF

; Sound 4: Samus landed hard
$4F4B:           dw 4F4F, 4F55
$4F4F:           db 03,90,0A,80,03, FF
$4F55:           db 03,A0,0A,84,05, FF

; Sound 5: Samus landed / wall-jumped
$4F5B:           dw 4F5F, 4F65
$4F5F:           db 03,40,0A,80,03, FF
$4F65:           db 03,50,0A,84,05, FF

; Sound 6: Samus' footsteps
$4F6B:           dw 4F6D
$4F6D:           db 09,80,0A,82,03, FF

; Sound 7: Door opened
$4F73:           dw 4F77, 4F80
$4F77:           db F5,F0,A9, 06,80,0A,91,18, FF
$4F80:           db F5,F0,A8, 02,80,0A,90,18, FF

; Sound 8: Door closed
$4F89:           dw 4F8D, 4F96
$4F8D:           db F5,F0,89, 06,80,0A,A1,15, FF
$4F96:           db F5,F0,87, 02,80,0A,9F,15, FF

; Sound 9: Missile door shot with missile / shot zebetite
$4F9F:           dw 4FA1
$4FA1:           db 02,B0,0A,8C,03, 02,D0,0A,90,03, 02,D0,0A,8C,03, 02,D0,0A,90,03, FF

; Sound Ah: Enemy frozen
$4FB6:           dw 4FB8
$4FB8:           db 0D,70,0C,A3,01, 0D,80,0C,A1,01, 0D,80,0C,9F,02, 0D,80,0C,9D,02, 0D,70,0C,9C,02, 0D,50,0C,9A,01, 0D,60,0C,97,01, 0D,60,0C,98,03, FF

; Sound Bh: Elevator
$4FE1:           dw 4FE5, 4FEE
$4FE5:           db FE,00, 0B,90,0A,80,70, FB, FF
$4FEE:           db FE,00, 06,40,0A,98,13, FB, FF

; Sound Ch: Stored shinespark
$4FF7:           dw 4FF9

; Stored shinespark / Dachora stored shinespark (sound library 2)
$4FF9:           db 05,A0,0A,C7,B0, FF

; Sound Dh: Typewriter stroke - intro
$4FFF:           dw 5001
$5001:           db 03,50,0A,98,02, 03,50,0A,98,02, FF

; Sound Eh: Gate opening/closing
$500C:           dw 5010, 503E
$5010:           db 03,50,0C,85,05, 03,50,0C,85,05, 03,50,0C,85,05, 03,50,0C,85,05, 03,50,0C,85,05, 03,50,0C,85,05, 03,50,0C,85,05, 03,50,0C,85,05, 03,50,0C,85,05, FF
$503E:           db F5,60,A9, 06,90,0A,91,20, FF

; Sound Fh: Shinespark
$5047:           dw 504B, 5083

; Shinespark / Dachora shinespark (sound library 2)
$504B:           db 01,00,0A,90,0C, 01,D0,0A,91,0C, 01,D0,0A,93,0C, 01,D0,0A,95,0A, 01,D0,0A,95,0A, 01,D0,0A,97,08, 01,D0,0A,97,08, 01,D0,0A,98,06, 01,D0,0A,98,06, 01,D0,0A,9A,04, 01,D0,0A,9A,04, FF
$5083:           db F5,90,C7, 05,C0,0A,98,10, F5,F0,C7, 05,C0,0A,F6,30, 05,C0,0A,C1,03, 05,C0,0A,C3,03, 05,C0,0A,C5,03, 05,C0,0A,C7,03, FF

; Sound 10h: Shinespark ended
$50A8:           dw 50AA

; Shinespark ended / Dachora shinespark ended (sound library 2)
$50AA:           db 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,8C,03, 08,D0,0A,8C,15, FF

; Sound 11h: (shorter version of shinespark ended)
$50C4:           dw 50C6
$50C6:           db 08,D0,0A,8C,03, 08,D0,0A,8C,15, FF

; Sound 12h: (Empty)
$50D1:           dw 50D3
$50D3:           db FF

; Sound 13h: Mother Brain's / torizo's projectile hits surface / Shitroid exploding / Mother Brain exploding
$50D4:           dw 50D6
$50D6:           db 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,8C,03, 08,D0,0A,8C,25, FF

; Sound 14h: Gunship elevator activated
$50F0:           dw 50F4, 5107
$50F4:           db 06,00,0A,91,23, 06,A0,0A,91,18, F5,F0,A9, 06,A0,0A,91,18, FF
$5107:           db 02,00,0A,90,23, 02,20,0A,90,18, F5,F0,A8, 02,20,0A,90,18, FF

; Sound 15h: Gunship elevator deactivated
$511A:           dw 511E, 5127
$511E:           db F5,F0,89, 06,80,0A,A1,15, FF
$5127:           db F5,F0,87, 02,10,0A,9F,15, FF

; Sound 16h: Unused. Crunchy footstep that's supposed to play when Mother Brain is being attacked by Shitroid
$5130:           dw 5132
$5132:           db 08,D0,0A,A3,03, 08,D0,0A,8E,03, 08,D0,0A,8E,25, FF

; Sound 17h: Mother Brain's blue rings
$5142:           dw 5144
$5144:           db F5,F0,C3, 0B,90,0A,A6,03, F5,F0,C3, 0B,90,0A,A6,03, F5,F0,C3, 0B,90,0A,A6,03, F5,F0,C3, 0B,90,0A,A6,03, F5,F0,C3, 0B,90,0A,A6,03, F5,F0,C3, 0B,90,0A,A6,03, FF

; Sound 18h: (Empty)
$5175:           dw 5177
$5177:           db FF

; Sound 19h: Shitroid dies
$5178:           dw 517C, 5182
$517C:           db 25,D0,0A,93,26, FF
$5182:           db 25,A0,0A,8C,3B, FF

; Sound 1Ah: (Empty)
$5188:           dw 518A
$518A:           db FF

; Sound 1Bh: Draygon dying cry
$518B:           dw 518F, 519F
$518F:           db 25,D0,0A,8E,30, 25,D0,0A,8E,30, 25,D0,0A,8E,40, FF
$519F:           db 25,00,0A,A6,0C, 25,80,0A,98,30, 25,80,0A,98,30, 25,80,0A,9A,10, 25,80,0A,98,40, FF

; Sound 1Ch: Crocomire spit
$51B9:           dw 51BB
$51BB:           db 00,D0,0A,9C,20, FF

; Sound 1Dh: Phantoon's flame
$51C1:           dw 51C3
$51C3:           db F5,F0,B5, 09,D0,0A,93,08, F5,F0,B5, 09,D0,0A,93,08, FF

; Sound 1Eh: Kraid's earthquake
$51D4:           dw 51D6
$51D6:           db 08,D0,0A,98,03, 08,D0,0A,95,03, 08,D0,0A,9A,03, 08,D0,0A,8C,03, 08,D0,0A,8C,25, FF

; Sound 1Fh: Kraid fires lint
$51F0:           dw 51F2
$51F2:           db 00,D0,0A,90,08, 01,D0,0A,8C,20, FF

; Sound 20h: (Empty)
$51FD:           dw 51FF
$51FF:           db FF

; Sound 21h: Ridley whips its tail
$5200:           dw 5202
$5202:           db 07,D0,0A,C7,10, FF

; Sound 22h: Crocomire acid damage
$5208:           dw 520A
$520A:           db 09,B0,0A,8C,05, 0E,B0,0A,91,05, 09,B0,0A,8C,05, 0E,B0,0A,91,05, 09,B0,0A,8C,05, 0E,B0,0A,91,05, FF

; Sound 23h: Baby metroid cry 1
$5229:           dw 522B
$522B:           db 25,20,0A,95,40, FF

; Sound 24h: Baby metroid cry - Ceres
$5231:           dw 5233
$5233:           db 24,20,0A,95,40, FF

; Sound 25h: Silence (clear speed booster / elevator sound)
$5239:           dw 523B
$523B:           db 07,00,0A,C7,03, FF

; Sound 26h: Baby metroid cry 2
$5241:           dw 5243
$5243:           db 25,20,0A,92,09, 25,30,0A,92,40, FF

; Sound 27h: Baby metroid cry 3
$524E:           dw 5250
$5250:           db 25,30,0A,91,40, FF

; Sound 28h: Phantoon materialises attack
$5256:           dw 5258
$5258:           db 00,D0,0A,91,08, 00,D0,0A,91,08, 00,D0,0A,91,08, 00,D0,0A,91,08, 00,D0,0A,91,08, 00,D0,0A,91,08, FF

; Sound 29h: Phantoon's super missiled attack
$5277:           dw 5279
$5279:           db 00,D0,0A,91,06, 00,D0,0A,91,06, 00,D0,0A,91,06, 00,D0,0A,91,06, 00,D0,0A,91,06, FF

; Sound 2Ah: Pause menu ambient beep
$5293:           dw 5295
$5295:           db 0B,20,0A,C7,03, 0B,20,0A,C7,03, 0B,10,0A,C7,03, FF

; Sound 2Bh: Resume speed booster / shinespark
$52A5:           dw 52A7
$52A7:           db FE,00, 05,60,0A,C7,10, FB, FF

; Sound 2Ch: Ceres door opening
$52B0:           dw 52B4, 52BD
$52B4:           db F5,F0,A9, 06,70,0A,91,18, FF
$52BD:           db F5,F0,A4, 06,70,0A,8C,18, FF

; Sound 2Dh: Gaining/losing incremental health
$52C6:           dw 52C8
$52C8:           db 06,70,0A,A8,01, 06,00,0A,A8,01, 06,70,0A,A8,01, 06,00,0A,A8,01, 06,70,0A,A8,01, 06,00,0A,A8,01, 06,70,0A,A8,01, 06,00,0A,A8,01, FF

; Sound 2Eh: Mother Brain's glass shattering
$52F1:           dw 52F5, 52FB
$52F5:           db 08,D0,0A,94,59, FF
$52FB:           db 25,D0,0A,98,10, 25,D0,0A,93,16, 25,90,0A,8F,15, FF

; Sound 2Fh: (Empty)
$530B:           dw 530D
$530D:           db FF
}
}


;;; $530E..5696: Shared trackers ;;;
{
;;; $530E: Music track 1 - Samus fanfare ;;;
{
$530E:           dw 5312, 0000

$5312:           dw 5322, 535D, 53AF, 53C6, 53E0, 53F5, 5431, 0000

$5322:           db FA, 26, E7, 12, E5, B4, F5, 0F, 0A, 0A, F7, 02, 0A, 00, E0, 0B,
                    EA, F4, F4, 46, E1, 0A, ED, AA, EE, 18, DC, 18, 7F, B2, ED, AA,
                    EE, 18, DC, B5, ED, AA, EE, 18, DC, B2, ED, AA, EE, 18, DC, B0,
                    30, AD, ED, AA, EE, 30, 82, AD, 0F, C9, 00

$535D:           db E0, 0B, EA, F4, F4, 46, E1, 0A, 03, C9, ED, 64, EE, 28, FA, 04,
                    7F, A6, A6, A6, A6, A6, A6, A9, A9, A9, A9, A9, A9, ED, FA, EE,
                    28, 64, A6, A6, A6, A6, A6, A6, A4, A4, A4, A4, A4, A4, ED, 3C,
                    EE, 0A, E6, AD, AD, AD, AD, AD, AD, AD, AD, AD, AD, AD, AD, AD,
                    AD, AD, AD, ED, E6, EE, 1E, 3C, AD, AD, AD, AD, AD, AD, AD, AD,
                    0C, C9

$53AF:           db E0, 0B, EA, 00, F4, 46, ED, C8, E1, 03, 18, 7F, 8F, 8E, ED, B4,
                    8C, 89, 30, 82, 82, 0F, C9

$53C6:           db E0, 0B, EA, 00, F4, 1E, ED, C8, E1, 11, 03, C9, 18, 7F, 8F, 8E,
                    8C, 89, F4, 00, 30, 82, 2D, 82, 0F, C9

$53E0:           db E0, 0B, EA, F4, F4, 46, ED, BE, E1, 0A, 18, 7F, A2, A1, 9F, A1,
                    30, 8E, 8E, 0F, C9

$53F5:           db E0, 02, F4, 00, E1, 06, ED, 3C, EE, 32, 82, 05, C9, 05, 7F, 93,
                    0A, C9, 0F, 97, 14, C9, 08, 93, E1, 08, 04, C9, 95, C9, 97, 0B,
                    C9, E1, 03, 05, C9, 93, 0A, C9, 0F, 93, 1E, C9, 08, 90, E1, 0D,
                    04, C9, 93, C9, ED, AA, EE, 1E, 14, 93, 1C, C9

$5431:           db E0, 02, F4, 00, E1, 0E, ED, 3C, EE, 32, 82, 05, 7F, 91, 1E, C9,
                    06, C9, 93, 10, C9, E1, 0C, 04, 93, C9, 90, C9, 09, 93, E1, 11,
                    05, 91, 24, C9, 06, 93, 10, C9, E1, 07, 04, 93, 0E, C9, 04, 97,
                    C9, 06, 97, ED, AA, EE, 1E, 14, 04, 93, 14, C9, 00
}


;;; $546E: Music track 2 - item fanfare ;;;
{
$546E:           dw 5472, 0000

$5472:           dw 5482, 54AA, 54BD, 54D8, 54EB, 550B, 553B, 0000

$5482:           db FA, 26, E7, 2D, E5, 96, F5, 0F, 0A, 0A, F7, 02, 0A, 00, E0, 0B,
                    F4, 46, EA, 00, ED, E6, E1, 03, 60, 7F, 8A, 89, 87, 2A, 82, E5,
                    AA, E6, 28, 3C, C8, 04, C9, 00

$54AA:           db E0, 0B, F4, 46, EA, 00, ED, E6, E1, 11, 60, 7F, 8A, 89, 87, 54,
                    82, 04, C9

$54BD:           db E0, 0B, F4, 46, EA, 00, ED, DC, E1, 06, 30, 7F, 9A, 18, 9D, A2,
                    30, A1, 9C, A2, 18, 9D, 9A, 54, 9A, 04, C9

$54D8:           db E0, 0B, F4, 46, EA, 00, ED, DC, E1, 0E, 60, 7F, 9D, 9C, 9A, 54,
                    95, 04, C9

$54EB:           db E0, 0B, F4, 46, EA, 00, ED, D2, E1, 0A, 04, C9, 18, 7F, 9D, A2,
                    A4, A6, A8, A4, 9F, A4, ED, A0, A9, ED, BD, A6, A2, 9F, 54, C9

$550B:           db E0, 0B, F4, 46, EA, 00, ED, DC, E1, 08, 1C, 7F, 9D, 14, C9, 1C,
                    A4, 14, C9, 1C, A8, 14, C9, 1C, 9F, 14, C9, ED, C8, 1C, A9, E8,
                    A0, 0A, ED, E5, 14, C9, 1C, A2, 14, C9, E1, 0A, 54, A1, 04, C9

$553B:           db E0, 0B, F4, 46, EA, 00, ED, AA, E1, 0C, 18, C9, 1C, 7F, A2, 14,
                    C9, 18, A6, 04, C8, 14, C9, 1C, A4, 14, C9, 18, A4, 04, C8, 14,
                    C9, 1C, A6, 14, C9, 18, 9F, 04, C8, 50, C9, 04, C9, 00
}


;;; $5569: Music track 3 - elevator ;;;
{
$5569:           dw 5583
$556B:           dw 5573, 00FF,556B, 0000

$5573:           dw 5593, 55C8, 55EE, 5630, 0000, 0000, 0000, 0000

$5583:           dw 5649, 5665, 566D, 5675, 567D, 0000, 0000, 0000

$5593:           db E5, DC, E7, 10, E0, 0C, F4, 28, ED, 46, E1, 07, F5, 0F, 0A, 0A,
                    F7, 02, 0A, 00, 30, C9, 18, 2F, BA, B5, B9, B1, 48, C9, 18, B0,
                    B6, BB, C9, C9, 18, 1F, B5, 0C, C9, 18, B2, 0C, C9, 24, C9, 60,
                    C9, C9, 0C, C9, 00

$55C8:           db E0, 0C, F4, 28, ED, 32, E1, 0A, 30, C9, 18, 2F, A6, A1, A5, 9D,
                    48, C9, 18, 9C, A2, A7, C9, C9, AD, 0C, C9, 18, AA, 0C, C9, 21,
                    C9, 60, C9, C9, 0F, C9

$55EE:           db E0, 0C, F4, 28, ED, 3C, E1, 0D, 20, C9, 06, 0F, BA, B5, B9, B1,
                    B0, B6, BB, 2A, C9, 06, BA, B5, B9, B1, B0, B6, BB, 36, C9, 06,
                    B9, B1, BA, B5, B0, B6, BB, 3E, C9, 06, BA, B5, B9, B1, B0, B6,
                    BB, 20, C9, 06, B5, BA, B9, B1, B0, B6, BB, 11, C9, 60, C9, C9,
                    07, C9

$5630:           db E0, 0B, F4, 46, E1, 0A, ED, 3C, EE, 3C, C8, 3C, 7F, 80, ED, C8,
                    EE, 30, 3C, 30, C8, EF, 88, 56, 05

$5649:           db FA, 26, E7, 10, E5, C8, E0, 0C, F4, 28, ED, 46, F5, 0F, 0A, 0A,
                    F7, 02, 0A, 00, E1, 07, E2, C0, 0D, 0C, C9, 00

$5665:           db E0, 0C, F4, 28, ED, 32, 0C, C9

$566D:           db E0, 0C, F4, 28, ED, 3C, 0C, C9

$5675:           db E0, 0B, F4, 46, E1, 0A, 0C, C9

$567D:           db E0, 0C, F4, 28, ED, 28, E1, 0D, 0C, C9, 00, ED, 3C, EE, 3C, C8,
                    3C, 80, ED, C8, EE, 30, 3C, 30, C8, 00
}


;;; $5697: Music track 4 - pre-statue hall ;;;
{
$5697:           dw 569F, 00FF,5697, 0000

$569F:           dw 56AF, 0000, 0000, 0000, 0000, 0000, 0000, 0000

$56AF:           db FA, 26, E7, 10, E5, E6, F5, 01, 00, 00, F7, 02, 00, 00, E0, 0B,
                    F4, 46, E1, 0A, ED, 32, EE, 3C, B4, 3C, 7F, 80, ED, B4, EE, 30,
                    32, 30, C8, ED, 32, EE, 3C, B4, 3C, 80, ED, B4, EE, 30, 32, 30,
                    C8, 00, 00
}
}
