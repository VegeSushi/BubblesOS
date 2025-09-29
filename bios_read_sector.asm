; bios_read_sector for 16-bit NASM
; Function signature:
; unsigned char bios_read_sector(unsigned char drive, unsigned char head,
;                                 unsigned char track, unsigned char sector, void* buffer)
;
; Returns: 0 = success, 1 = error

BITS 16

section .text
global bios_read_sector

bios_read_sector:
    push bp
    mov  bp, sp
    push bx
    push cx
    push dx
    push di
    push es

    ; Stack layout (16-bit):
    ; [bp+0]  = old BP
    ; [bp+2]  = return address
    ; [bp+4]  = drive (byte)
    ; [bp+6]  = head (byte)
    ; [bp+8]  = track/cylinder (byte)
    ; [bp+10] = sector (byte)
    ; [bp+12] = buffer pointer (word)

    ; Get drive number
    mov  dl, [bp+4]     ; DL = drive (0x00 for floppy A:)

    ; Get head number
    mov  dh, [bp+6]     ; DH = head

    ; Get cylinder (track)
    mov  ch, [bp+8]     ; CH = cylinder

    ; Get sector number
    mov  cl, [bp+10]    ; CL = sector (1-based)

    ; Get buffer address
    mov  bx, [bp+12]    ; BX = buffer offset

    ; Set ES to current DS (assume same segment)
    push ds
    pop  es

    ; Set up BIOS read parameters
    ; AH = 0x02 (read sectors)
    ; AL = 0x01 (number of sectors to read)
    ; CH = cylinder/track
    ; CL = sector
    ; DH = head
    ; DL = drive
    ; ES:BX = buffer address

    mov  ah, 0x02       ; BIOS function: read sectors
    mov  al, 0x01       ; Read 1 sector

    ; Retry up to 3 times
    mov  di, 3
.retry:
    pusha               ; Save all registers
    int  0x13           ; Call BIOS disk interrupt
    jnc  .success       ; If carry clear, read succeeded

    ; Reset disk system on error
    popa                ; Restore registers
    pusha               ; Save again for reset
    xor  ah, ah         ; Function 0x00: reset disk system
    int  0x13
    popa                ; Restore registers

    dec  di
    jnz  .retry         ; Retry if attempts remaining

    ; All retries failed
    mov  al, 1          ; Return error code
    jmp  .done

.success:
    popa                ; Clean up saved registers
    xor  al, al         ; Return 0 (success)

.done:
    pop  es
    pop  di
    pop  dx
    pop  cx
    pop  bx
    pop  bp
    ret
