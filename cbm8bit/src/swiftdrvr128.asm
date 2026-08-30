; SwiftDriver 128 Main
;
;
;   Copyright 2026-2026 Bo Zimmerman, Kelly Fox
;
;   Licensed under the Apache License, Version 2.0 (the "License");
;   you may not use this file except in compliance with the License.
;   You may obtain a copy of the License at
;
;	   http://www.apache.org/licenses/LICENSE-2.0
;
;   Unless required by applicable law or agreed to in writing, software
;   distributed under the License is distributed on an "AS IS" BASIS,
;   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
;   See the License for the specific language governing permissions and
;   limitations under the License.
;        

* = $1500
        ;.D SWIFTDRVR.BIN
        ; KERNAL BAUD RATES
        ; B50=1
        ; B75=2
        ; B110=3
        ; B135=4
        ; B150=5
        ; B300=6
        ; B600=7
        ; B1200=8
        ; B1800=9
        ; B2400=10
        ; B3600=11
        ; B4800=12
        ; B7200=13
        ; B9600=14
        ; B19200=15
        ; B38400=16
        ; B57600=17
        ; B115200=18
        ; B230400=19
;;;;;;;;;;;

BASE = $DE00
DATAPORT = $DE00
STATUS = $DE01
COMMAND = $DE02
CONTROL = $DE03
ESR232 = $DE07
RHEAD = $0A19; KERNAL RIDBS -- THE ROM'S OWN RS232
RTAIL = $0A18; RIDBE. $E7CE IS OUR DEQUEUE.
RBUFF = $C8
RECVBUF = $1300
SENDBUF = $1400
VECS = $1900; SAVED KERNAL VECTORS LIVE JOYELSEINSTRERR$RIGHT$STR$VAL THE IMAGE,
OLDNMI = $1918; OR BLOAD WIPES THEM AND CLOSE RESTORES JUNK
NMINV = $0318
        JMP INIT
INIT
        SEI
        LDA $031B
        CMP #>DOOPEN
        BEQ NOSAVE;ALREADY HOOKED, VECS VALID
        LDY #20
INSAV
        LDA $0319,Y
        STA VECS,Y
        DEY
        BNE INSAV
NOSAVE
        LDA VECS+1
        STA OLDOPN+1
        LDA VECS+2
        STA OLDOPN+2
        LDA VECS+3
        STA DOCLOSE+1
        LDA VECS+4
        STA DOCLOSE+2
        LDA VECS+5
        STA OLDCHK+1
        LDA VECS+6
        STA OLDCHK+2
        LDA VECS+7
        STA OLDCKO+1
        LDA VECS+8
        STA OLDCKO+2
        LDA VECS+11
        STA DOGETS+1
        LDA VECS+12
        STA DOGETS+2
        LDA VECS+17
        STA DOGETI+1
        LDA VECS+18
        STA DOGETI+2
        LDA VECS+13
        STA DOPUT2+1
        LDA VECS+14
        STA DOPUT2+2
        LDA VECS+19
        STA DOCLAL+1
        LDA VECS+20
        STA DOCLAL+2
NOINIT
        LDA #<DOOPEN
        STA $031A
        LDA #>DOOPEN
        STA $031B
        CLI
        RTS
DOOPEN
        LDA $BA
        CMP #$02
        BEQ DOOP2
OLDOPN
        JMP $EFBD; NOT OURS, CHAIN TO ROM IOPEN
DOOP2
        LDX $B8
        JSR $F202
        BNE DOOP2A;ALREADY OPEN?
        JMP $F67F;?FILE OPEN ERROR
DOOP2A
        LDX $98
        CPX #$0A
        BCC DOOP2B;TABLE FULL?
        JMP $F67C;?TOO MANY FILES
DOOP2B
        INC $98
        LDA $B8
        STA $0362,X;LAT
        LDA $B9
        ORA #$60
        STA $B9
        STA $0376,X;SAT
        LDA $BA
        STA $036C,X;FAT
        LDY #$00
        STY $0A0F
        STY $0A14;KERNAL RS232 IDLE, STATUS CLEAR
        LDA #$7F
        STA $DD0D;AND NO CIA2 INTERRUPTS
        STY RHEAD
        STY RTAIL
        STY RCOUNT
        STY ERRORS
        STY RXERRS
        STY SHEAD
        STY STAIL
        STY STOPPED
        STY STATUS
        LDA STATUS;RESET ACIA, CLEAR IRQ
        DEY
        STY SFREE
        INY;SEND BUFFER EMPTY
        STY RBUFF
        LDA #>RECVBUF
        STA RBUFF+1
NEWVEC
        SEI
        LDA NMINV+1
        CMP #>NEWNMI
        BEQ NONVEC
        STA OLDNMI+1
        LDA NMINV
        STA OLDNMI
        LDA #<NEWNMI
        STA NMINV
        LDA #>NEWNMI
        STA NMINV+1
NONVEC
        CLI
        STY ESR232
        LDA #$BB
        LDX $C7
        JSR $FF74
        TAY
        LDA BAUDS,Y
        BPL DOOP3
        LDY #16
        STY CONTROL
        AND #$7F
        STA ESR232
        LDA #$00
DOOP3
        ORA #16
        STA CONTROL
        LDA #9
        STA COMMAND
        STA RTSON
        AND #$F0; KEEP PARITY/ECHO
        ORA #1; DTR ONLY, RTS OFF
        STA RTSOFF
        LDA STATUS;CLEAR IRQ FROM DTR ENABLE
        LDA DATAPORT
        LDA #<DOCLOSE
        STA $031C
        LDA #>DOCLOSE
        STA $031D
        LDA #<DOCHKN
        STA $031E
        LDA #>DOCHKN
        STA $031F
        LDA #<DOCKOT
        STA $0320
        LDA #>DOCKOT
        STA $0321
        LDA #<DOGETS
        STA $0324
        LDA #>DOGETS
        STA $0325
        LDA #<DOGETI
        STA $032A
        LDA #>DOGETI
        STA $032B
        LDA #<DOPUT
        STA $0326
        LDA #>DOPUT
        STA $0327
        LDA #<DOCLAL
        STA $032C
        LDA #>DOCLAL
        STA $032D
EOPEN
        JSR UPDCD
        CLC
        RTS
NEWNMI
        CLD
        LDA STATUS
        BPL NREVD;ACIA IRQ FLAG CLEAR, NOT OURS
        TAX;SNAPSHOT. X IS PUSHED BY THE ROM NMI ENTRY
        AND #8
        BEQ NMDONE
        TXA
        AND #6
        BNE NMBAD
        LDY RTAIL
        LDA DATAPORT;ALWAYS READ, OR THE IRQ STICKS
        INY
        CPY RHEAD
        BEQ NMFULL
        DEY
        STA (RBUFF),Y
        INC RTAIL
        INC RCOUNT
        LDA RTAIL
        SEC
        SBC RHEAD
        CMP #223
        BCC NMDONE
        LDA STOPPED
        BNE NMDONE
        LDA RTSOFF
        STA COMMAND
        STA STOPPED;CHOKE THE SENDER
NMDONE
        JMP $FF33
NMBAD
        LDA DATAPORT;MUST READ, OR THE IRQ STICKS
        INC RXERRS
        JMP $FF33;BAD BYTE, DISCARDED NOT QUEUED
NMFULL
        INC ERRORS
        JMP $FF33;NO ROOM, BYTE DROPPED
NREVD
        JMP (OLDNMI);NOT OURS, CHAIN ON
SAVBYTE
        BYTE 0
UPDCD
        PHP
        PHA
        LDA $DD03
        ORA #$10
        STA $DD03
        LDA STATUS
        AND #32
        BEQ CLRDCD
        LDA $DD01
        ORA #$10
        STA $DD01
        PLA
        PLP
        RTS
CLRDCD
        LDA $DD01
        AND #$EF
        STA $DD01
        PLA
        PLP
        RTS
DOPUT
        PHA
        LDA $9A
        CMP #$02
        BEQ DOPUT3
        PLA
DOPUT2
        JMP $EF79
DOPUT3
        PLA
        STA SAVBYTE
        TYA
        PHA
        JSR PUTQ
        PLA
        TAY
        LDA SAVBYTE
        RTS
PUTQ
        JSR DOXMIT
        LDA SFREE
        BNE PUTQ2
        LDY #0
        STY PUTCNT
PUTQ1
        JSR DOXMIT
        LDA SFREE
        BNE PUTQ2
        DEY
        BNE PUTQ1
        DEC PUTCNT
        BNE PUTQ1
        INC ERRORS
        SEC
        RTS;BUFFER JAMMED, BYTE LOST
PUTQ2
        LDY STAIL
        LDA SAVBYTE
        STA SENDBUF,Y
        INC STAIL
        DEC SFREE
        LDY #0
        STY PUTCNT
PUTQ3
        JSR DOXMIT
        LDA SFREE
        CMP #$FF
        BEQ PUTQ4;QUEUE DRAINED
        LDA STOPPED
        BNE PUTQ4;OUR OWN RTS IS DOWN
        DEY
        BNE PUTQ3
        DEC PUTCNT
        BNE PUTQ3
        INC ERRORS;CTS STUCK, BYTE STAYS QUEUED
PUTQ4
        CLC
        RTS
DOXMIT
        LDA SFREE
        CMP #$FF
        BEQ DOXMIT2
        LDA STOPPED
        BNE DOXMIT2
        LDA STATUS
        AND #16
        BEQ DOXMIT2
        TYA
        PHA
        LDY SHEAD
        LDA SENDBUF,Y
        STA DATAPORT
        PLA
        TAY
        INC SHEAD
        INC SFREE
DOXMIT2
        RTS
DOCLOSE
        JSR $F188
        PHP
        PHA
        LDA $BA
        CMP #$02
        BNE NOCLOS
DOCLOS2
        LDA #$0A; DROP DTR, NO INTS
        STA COMMAND
        LDA NMINV+1
        CMP #>NEWNMI
        BNE NOCLOS
        SEI
        LDA OLDNMI
        STA NMINV
        LDA OLDNMI+1
        STA NMINV+1
        TYA
        PHA
        LDY #20
INCLO
        LDA VECS,Y
        STA $0319,Y
        DEY
        BNE INCLO
        PLA
        TAY
        JSR NOINIT; REHOOK DOOPEN, AND CLI!
NOCLOS
        JSR UPDCD
        PLA
        PLP
        RTS
DOCLAL
        JSR $F222
        PHP
        PHA
        JMP DOCLOS2
DOGETS
        JSR $EF06
        JMP RXFLOW; CALL IBASIN
DOGETI
        JSR $EEEB; CALL IGETIN ($EEEB, INSTR $EF0F)
RXFLOW
        PHP
        PHA
        LDA $99
        CMP #$02
        BNE NOGETS
        LDA STOPPED
        BEQ GETSX
        LDA RTAIL
        SEC
        SBC RHEAD
        CMP #193
        BCS GETSX;STILL TOO FULL TO RESUME
        LDA RTSON
        STA COMMAND
        LDA #0
        STA STOPPED
GETSX
        JSR DOXMIT;KEEP THE SEND QUEUE MOVING
        PLA
        PLP
        CLC
        RTS;OURS: VALID BYTE, CARRY CLEAR
NOGETS
        PLA
        PLP
        RTS;NOT OURS: PASS CARRY THROUGH
DOCHKN
        TXA
        PHA
        JSR $F202
        BNE CHKCHN;LOOKUP LA, X=INDEX
        JSR $F212
        CMP #$02
        BEQ CHKMIN;A=DEVICE
CHKCHN
        PLA
        TAX
OLDCHK
        JMP $F106
CHKMIN
        PLA
        LDA #$02
        STA $99
        CLC
        RTS
DOCKOT
        TXA
        PHA
        JSR $F202
        BNE CKOCHN
        JSR $F212
        CMP #$02
        BEQ CKOMIN
CKOCHN
        PLA
        TAX
OLDCKO
        JMP $F14C
CKOMIN
        PLA
        LDA #$02
        STA $9A
        CLC
        RTS
BAUDS
        BYTE 9,0,0,1,2,2,5,6,7,8,8,9,10,11,12,14,15,130,129,128,0,0,0,0,0,0
RCOUNT
        BYTE 0
ERRORS
        BYTE 0
RTSON
        BYTE 0
RTSOFF
        BYTE 0
SHEAD
        BYTE 0
STAIL
        BYTE 0
SFREE
        BYTE 0
STOPPED
        BYTE 0
PUTCNT
        BYTE 0
RXERRS
        BYTE 0
PRINT
        NOP;:OPEN1,8,15,"S0:SWIFTDRVR1*":CLOSE1:SAVE"SWIFTDRVR128.BAS",8
PRINT2
        NOP;:VERIFY"SWIFTDRVR128.BAS",8
PRINT3
        NOP;"“SWIFTDRVR128.BASQQQQ":PRINT"%LADSQQQQ":PRINT"SYS11000‘‘‘‘‘‘‘‘"