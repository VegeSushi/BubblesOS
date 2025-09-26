%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 20
%endif

BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [BootDrive], dl
    mov si, msg_boot
    call puts
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov dl, [BootDrive]
    mov ah, 0x02
    mov al, KERNEL_SECTORS
    mov ch, 0x00
    mov dh, 0x00
    mov cl, 0x02
    int 0x13
    jc disk_error
    jmp 0x1000:0x0000

disk_error:
    mov si, msg_err
    call puts
    jmp $

puts:
    pusha
.print_next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .print_next
.done:
    popa
    ret

msg_boot db "Switching to Bubbles kernel...", 13, 10, 0
msg_err  db "Disk read error!", 13, 10, 0
BootDrive db 0
times 510-($-$$) db 0
dw 0xAA55
