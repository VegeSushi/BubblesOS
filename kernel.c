static void bios_putc(char c) {
    __asm__ __volatile__ (
        "movb $0x0E, %%ah \n\t"
        "movb %0, %%al    \n\t"
        "int  $0x10       \n\t"
        :
        : "r"(c)
        : "ax"
    );
}

static void bios_puts(const char* s) {
    while (*s) bios_putc(*s++);
}

static void bios_newline(void) {
    bios_putc('\r');
    bios_putc('\n');
}

static unsigned char bios_getkey(void) {
    unsigned char ch;
    __asm__ __volatile__ (
        "xor %%ah, %%ah \n\t"  // AH=0
        "int $0x16      \n\t"  // BIOS keyboard service
        "mov %%al, %0   \n\t"  // return ASCII in ch
        : "=r"(ch)
        :
        : "ax"
    );
    return ch;
}

void read_command(char *buf, unsigned int maxlen) {
    unsigned int i = 0;

    while (i < maxlen - 1) {
        unsigned char c = bios_getkey();

        if (c == '\r') {            // Enter
            break;
        } else if (c == 0x08) {     // Backspace
            if (i > 0) {
                i--;                        // remove from buffer
                bios_putc(0x08);            // move cursor back
                bios_putc(' ');             // overwrite with space
                bios_putc(0x08);            // move cursor back again
            }
        } else {
            buf[i++] = c;           // add to buffer
            bios_putc(c);           // echo
        }
    }

    buf[i] = 0; // null-terminate
    bios_newline(); // move to next line after Enter
}


static void bios_putdec(unsigned int val) {
    char buf[6];
    int i = 0;

    // handle 0 explicitly
    if (val == 0) {
        bios_putc('0');
        return;
    }

    // convert backwards
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    // print in reverse
    while (i--) {
        bios_putc(buf[i]);
    }
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

void print_banner(void) {
    bios_puts("  ____        _     _     _                  _  __                    _ ");
    bios_newline();
    bios_puts(" |  _ \\      | |   | |   | |                | |/ /                   | |");
    bios_newline();
    bios_puts(" | |_) |_   _| |__ | |__ | | ___  ___ ______| ' / ___ _ __ _ __   ___| |");
    bios_newline();
    bios_puts(" |  _ <| | | | '_ \\| '_ \\| |/ _ \\/ __|______|  < / _ \\ '__| '_ \\ / _ \\ |");
    bios_newline();
    bios_puts(" | |_) | |_| | |_) | |_) | |  __/\\__ \\      | . \\  __/ |  | | | |  __/ |");
    bios_newline();
    bios_puts(" |____/ \\__,_|_.__/|_.__/|_|\\___||___/      |_|\\_\\___|_|  |_| |_|\\___|_|");
    bios_newline();
    bios_newline();
}

void kmain(void) {
    print_banner();
    bios_newline();
    bios_puts("CS: ");
    unsigned short cs;
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    bios_putdec(cs);
    bios_newline();
    bios_puts("Conventional RAM: ");
    unsigned short kb;
    __asm__ __volatile__("int $0x12" : "=a"(kb) : : );
    bios_putdec(kb);
    bios_puts("KB");
    bios_newline();
    unsigned short ext_kb;
    __asm__ __volatile__(
        "movb $0x88, %%ah \n\t"
        "int $0x15        \n\t"
        : "=a"(ext_kb)   // 'a' is okay for 16-bit ax in ia16-elf-gcc
    );
    bios_puts("Extended RAM: ");
    bios_putdec(ext_kb);
    bios_puts("KB");
    bios_newline();
    for (;;) {
        char cmd[80];
        bios_puts(">");
        read_command(cmd, sizeof(cmd));
        bios_newline();
        if (!strcmp(cmd, "reboot")) {
            __asm__ __volatile__("int $0x19");
        } else if (!strcmp(cmd, "halt")) {
            bios_puts("Halting...");
            for (;;) __asm__ __volatile__("hlt");
        } else {
            bios_puts("Owhno, Unknwon command!");
        }
        bios_newline();
    }
}

__attribute__((naked, section(".text._start")))
void _start(void) {
    __asm__ __volatile__ (
        "cli                \n\t"
        "mov  $0x1000, %ax  \n\t"
        "mov  %ax, %ds      \n\t"
        "mov  %ax, %es      \n\t"
        "mov  %ax, %ss      \n\t"
        "mov  $0x8000, %sp  \n\t"
        "sti                \n\t"
        "call kmain         \n\t"
        "hlt                \n\t"
        "jmp .              \n\t"
    );
}
