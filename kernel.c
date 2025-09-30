#include <stdint.h>

#define COM1_BASE 0x3F8
#define COM1_DATA (COM1_BASE + 0)
#define COM1_IER  (COM1_BASE + 1)
#define COM1_LCR  (COM1_BASE + 3)
#define COM1_LSR  (COM1_BASE + 5)

#define FLOPPY_DRIVE_A 0x00

#define APP_LOAD_ADDR 0x2000
#define APP_MAX_SIZE  4096

struct __attribute__((packed)) fat12_boot_sector {
    unsigned char  jump[3];
    unsigned char  oem[8];
    uint16_t       bytes_per_sector;
    uint8_t        sectors_per_cluster;
    uint16_t       reserved_sectors;
    uint8_t        num_fats;
    uint16_t       root_entries;
    uint16_t       total_sectors_short;
    uint8_t        media_descriptor;
    uint16_t       sectors_per_fat;
    uint16_t       sectors_per_track;
    uint16_t       num_heads;
    uint32_t       hidden_sectors;
    uint32_t       total_sectors_long;
};

struct __attribute__((packed)) fat12_dir_entry {
    unsigned char name[8];           // Offset 0-7: Filename (8 bytes)
    unsigned char ext[3];            // Offset 8-10: Extension (3 bytes)
    uint8_t       attr;              // Offset 11: Attributes
    uint8_t       reserved;          // Offset 12: Reserved (Windows NT)
    uint8_t       ctime_ms;          // Offset 13: Creation time, fine resolution
    uint16_t      ctime;             // Offset 14-15: Creation time
    uint16_t      cdate;             // Offset 16-17: Creation date
    uint16_t      adate;             // Offset 18-19: Last access date
    uint16_t      cluster_high;      // Offset 20-21: High 16 bits of cluster (FAT32, 0 for FAT12)
    uint16_t      mtime;             // Offset 22-23: Last modification time
    uint16_t      mdate;             // Offset 24-25: Last modification date
    uint16_t      start_cluster;     // Offset 26-27: Starting cluster (LOW 16 bits)
    uint32_t      size;              // Offset 28-31: File size in bytes
};


static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

int str_to_int(const char *s) {
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

unsigned int div32_16(unsigned long n, unsigned int d) {
    unsigned int q = 0;
    while (n >= d) {
        n -= d;
        q++;
    }
    return q;
}

void speaker_on(unsigned int freq) {
    unsigned int divisor = div32_16(1193180UL, freq);

    // set PIT channel 2, mode 3
    outb(0x43, 0xB6);
    outb(0x42, divisor & 0xFF);
    outb(0x42, (divisor >> 8) & 0xFF);

    // enable speaker
    unsigned char tmp = inb(0x61);
    tmp |= 3; // bits 0+1
    outb(0x61, tmp);
}

void speaker_off(void) {
    unsigned char tmp = inb(0x61);
    tmp &= 0xFC; // clear bits 0+1
    outb(0x61, tmp);
}

static void com1_init(void) {
    outb(COM1_IER, 0x00);        // Disable interrupts
    outb(COM1_LCR, 0x80);        // Enable DLAB
    outb(COM1_BASE + 0, 0x0C);
    outb(COM1_BASE + 1, 0x00);   // DLM = 0
    outb(COM1_LCR, 0x03);        // 8N1, disable DLAB
    outb(COM1_BASE + 2, 0xC7);   // Enable FIFO, clear
    outb(COM1_IER, 0x0B);
}

static void com1_putc(char c) {
    while (!(inb(COM1_LSR) & 0x20)); // Wait for THR empty
    outb(COM1_DATA, c);
}

static void com1_newline(void) {
    com1_putc('\r');
    com1_putc('\n');
}


static void com1_puts(const char *s) {
    while (*s) {
        com1_putc(*s++);
    }
}

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

extern unsigned char bios_read_sector(
    unsigned char drive,
    unsigned char head,
    unsigned char track,
    unsigned char sector,
    void* buffer
);

static struct fat12_boot_sector boot_sector;
static unsigned char fat_buffer[512 * 9];
static unsigned char root_dir_buffer[512 * 14];
static unsigned char file_buffer[512];
static unsigned char fat12_initialized = 0;

// Helper function to read the FAT12 boot sector from floppy A:
static int fat12_read_boot_sector(void) {
    if (bios_read_sector(FLOPPY_DRIVE_A, 0, 0, 1, &boot_sector)) {
        return -1;
    }
    return 0;
}

// Helper function to read the FAT from floppy A:
static int fat12_read_fat(void) {
    struct fat12_boot_sector *bs = &boot_sector;
    unsigned int fat_start_sector = bs->reserved_sectors;

    for (unsigned int i = 0; i < bs->sectors_per_fat; i++) {
        unsigned int lba = fat_start_sector + i;
        unsigned char cyl = lba / (bs->sectors_per_track * bs->num_heads);
        unsigned char temp = lba % (bs->sectors_per_track * bs->num_heads);
        unsigned char head = temp / bs->sectors_per_track;
        unsigned char sector = (temp % bs->sectors_per_track) + 1;

        if (bios_read_sector(FLOPPY_DRIVE_A, head, cyl, sector, &fat_buffer[i * 512])) {
            return -1;
        }
    }
    return 0;
}

// Helper function to read root directory from floppy A:
static int fat12_read_root_dir(void) {
    struct fat12_boot_sector *bs = &boot_sector;
    unsigned int root_start = bs->reserved_sectors + (bs->num_fats * bs->sectors_per_fat);
    unsigned int root_sectors = (bs->root_entries * 32) / bs->bytes_per_sector;

    for (unsigned int i = 0; i < root_sectors; i++) {
        unsigned int lba = root_start + i;
        unsigned char cyl = lba / (bs->sectors_per_track * bs->num_heads);
        unsigned char temp = lba % (bs->sectors_per_track * bs->num_heads);
        unsigned char head = temp / bs->sectors_per_track;
        unsigned char sector = (temp % bs->sectors_per_track) + 1;

        if (bios_read_sector(FLOPPY_DRIVE_A, head, cyl, sector, &root_dir_buffer[i * 512])) {
            return -1;
        }
    }
    return 0;
}

static unsigned short fat12_get_next_cluster(unsigned short cluster) {
    unsigned int fat_offset = cluster + (cluster / 2);  // cluster * 1.5

    // Fix: Use memcpy or manual byte access to avoid strict-aliasing
    unsigned short next_cluster;
    unsigned char *ptr = &fat_buffer[fat_offset];
    next_cluster = ptr[0] | (ptr[1] << 8);  // Manual little-endian read

    if (cluster & 1) {
        next_cluster >>= 4;  // Odd cluster, use high 12 bits
    } else {
        next_cluster &= 0x0FFF;  // Even cluster, use low 12 bits
    }

    return next_cluster;
}

// Convert 8.3 filename to padded format (keep as-is)
static void format_filename(const char *input, char *output) {
    int i, j;

    for (i = 0; i < 11; i++) {
        output[i] = ' ';
    }
    output[11] = 0;

    for (i = 0; i < 8 && input[i] && input[i] != '.'; i++) {
        output[i] = input[i];
        if (output[i] >= 'a' && output[i] <= 'z') {
            output[i] -= 32;
        }
    }

    const char *ext = input;
    while (*ext && *ext != '.') ext++;
    if (*ext == '.') {
        ext++;
        for (j = 0; j < 3 && ext[j]; j++) {
            output[8 + j] = ext[j];
            if (output[8 + j] >= 'a' && output[8 + j] <= 'z') {
                output[8 + j] -= 32;
            }
        }
    }
}

static void fat12_list_files(void) {
    if (!fat12_initialized) {
        bios_puts("Error: FAT12 not mounted! Use 'mount' first.");
        bios_newline();
        return;
    }

    struct fat12_boot_sector *bs = &boot_sector;
    struct fat12_dir_entry *entries = (struct fat12_dir_entry*)root_dir_buffer;

    bios_puts("Files on A:");
    bios_newline();

    unsigned int file_count = 0;

    for (unsigned int i = 0; i < bs->root_entries; i++) {
        struct fat12_dir_entry *entry = &entries[i];

        // CRITICAL: Check for end of directory FIRST
        if (entry->name[0] == 0x00) {
            break;  // End of directory - stop immediately
        }

        // Skip deleted files
        if ((unsigned char)entry->name[0] == 0xE5) {
            continue;
        }

        // Skip volume labels
        if (entry->attr & 0x08) {
            continue;
        }

        // Skip entries with invalid attributes (like 0x0F for LFN)
        if (entry->attr == 0x0F) {
            continue;  // Long filename entry
        }

        // Additional validation: check if name has printable characters
        int valid = 0;
        for (int j = 0; j < 8; j++) {
            unsigned char c = entry->name[j];
            // Check for printable ASCII or space
            if ((c >= 0x20 && c <= 0x7E) || c == 0x05) {
                valid = 1;
                break;
            }
        }

        if (!valid) {
            continue;  // Skip entries with non-printable names
        }

        file_count++;

        // Print filename (handle 0x05 special case - should be 0xE5)
        for (int j = 0; j < 8; j++) {
            unsigned char c = entry->name[j];
            if (c == ' ') break;
            if (c == 0x05) c = 0xE5;  // Special case for Japanese characters
            bios_putc(c);
        }

        // Print extension if present
        if (entry->ext[0] != ' ' && entry->ext[0] != 0) {
            bios_putc('.');
            for (int j = 0; j < 3; j++) {
                unsigned char c = entry->ext[j];
                if (c == ' ' || c == 0) break;
                bios_putc(c);
            }
        }

        // Print info
        if (entry->attr & 0x10) {
            bios_puts(" <DIR>");
        } else {
            bios_puts(" ");
            bios_putdec(entry->size);
            bios_puts(" bytes");
        }

        bios_newline();
    }

    bios_newline();
    bios_putdec(file_count);
    bios_puts(" file(s)");
    bios_newline();
}

// FIXED: Initialize FAT12 with proper error handling
static int fat12_init(void) {
    bios_puts("Mounting A:...");
    bios_newline();

    if (fat12_read_boot_sector()) {
        bios_puts("Error: Cannot read boot sector!");
        bios_newline();
        fat12_initialized = 0;
        return -1;
    }

    // Validate it's a proper FAT12 floppy
    if (boot_sector.bytes_per_sector != 512) {
        bios_puts("Error: Invalid sector size!");
        bios_newline();
        fat12_initialized = 0;
        return -1;
    }

    if (fat12_read_fat()) {
        bios_puts("Error: Cannot read FAT!");
        bios_newline();
        fat12_initialized = 0;
        return -1;
    }

    if (fat12_read_root_dir()) {
        bios_puts("Error: Cannot read root directory!");
        bios_newline();
        fat12_initialized = 0;
        return -1;
    }

    fat12_initialized = 1;
    bios_puts("A: mounted successfully!");
    bios_newline();
    return 0;
}

static struct fat12_dir_entry* fat12_find_file(const char *filename) {
    if (!fat12_initialized) return 0;

    char formatted[12];
    format_filename(filename, formatted); // Produces 8+3 padded string

    struct fat12_dir_entry *entries = (struct fat12_dir_entry*)root_dir_buffer;

    for (unsigned int i = 0; i < boot_sector.root_entries; i++) {
        struct fat12_dir_entry *entry = &entries[i];

        if (entry->name[0] == 0x00) break;   // End of directory
        if ((unsigned char)entry->name[0] == 0xE5) continue; // Deleted
        if (entry->attr & 0x08) continue;   // Volume label
        if (entry->attr == 0x0F) continue;  // LFN entry

        // Compare name ignoring trailing spaces
        int match = 1;
        for (int j = 0; j < 8; j++) {
            unsigned char a = entry->name[j];
            unsigned char b = formatted[j];

            // Treat spaces as "padding"
            if (a == ' ') a = 0;
            if (b == ' ') b = 0;

            if (a != b) {
                match = 0;
                break;
            }
        }

        // Compare extension ignoring trailing spaces
        if (match) {
            for (int j = 0; j < 3; j++) {
                unsigned char a = entry->ext[j];
                unsigned char b = formatted[8 + j];

                if (a == ' ') a = 0;
                if (b == ' ') b = 0;

                if (a != b) {
                    match = 0;
                    break;
                }
            }
        }

        if (match) return entry;
    }

    return 0; // Not found
}

static int fat12_read_cluster(unsigned short cluster, void *buffer) {
    if (!fat12_initialized) return -1;

    unsigned int first_data_sector =
    boot_sector.reserved_sectors + (boot_sector.num_fats * boot_sector.sectors_per_fat) +
    ((boot_sector.root_entries * 32) / boot_sector.bytes_per_sector);

    unsigned int sector = first_data_sector + (cluster - 2) * boot_sector.sectors_per_cluster;

    for (unsigned int i = 0; i < boot_sector.sectors_per_cluster; i++) {
        unsigned int lba = sector + i;
        unsigned char cyl = lba / (boot_sector.sectors_per_track * boot_sector.num_heads);
        unsigned char temp = lba % (boot_sector.sectors_per_track * boot_sector.num_heads);
        unsigned char head = temp / boot_sector.sectors_per_track;
        unsigned char sec = (temp % boot_sector.sectors_per_track) + 1;

        if (bios_read_sector(FLOPPY_DRIVE_A, head, cyl, sec, (unsigned char*)buffer + i * 512)) {
            return -1;
        }
    }
    return 0;
}

static int fat12_read_file(const char *filename, void *buffer, unsigned int max_size) {
    struct fat12_dir_entry *file = fat12_find_file(filename);
    if (!file) return -1;

    unsigned int remaining = file->size;
    unsigned short cluster = file->start_cluster;
    unsigned char *buf = (unsigned char*)buffer;

    while (cluster < 0xFF8) { // FAT12 end-of-chain >= 0xFF8
        if (remaining == 0) break;

        unsigned int to_read = remaining > boot_sector.bytes_per_sector * boot_sector.sectors_per_cluster ?
        boot_sector.bytes_per_sector * boot_sector.sectors_per_cluster : remaining;

        if (to_read > max_size) return -1; // Buffer too small

        if (fat12_read_cluster(cluster, buf)) return -1;

        buf += to_read;
        remaining -= to_read;
        max_size -= to_read;

        cluster = fat12_get_next_cluster(cluster);
    }

    return file->size; // Return bytes read
}

int split_command_arg(char *input, char **cmd, char **arg) {
    // skip leading spaces
    while (*input == ' ' || *input == '\t') input++;

    *cmd = input;

    while (*input && *input != ' ' && *input != '\t') input++;

    if (*input) {
        *input = 0;
        input++;

        while (*input == ' ' || *input == '\t') input++;

        if (*input) {
            *arg = input;
            return 1;
        }
    }

    *arg = "";
    return 0;
}

typedef void (*user_app_t)(void);

void run_app(const char *filename) {
    unsigned char *app_memory = (unsigned char *)APP_LOAD_ADDR;
    bios_puts("Loading into memory...");
    bios_newline();
    int size = fat12_read_file(filename, app_memory, APP_MAX_SIZE);

    if (size <= 0) {
        bios_puts("Failed to load app!");
        bios_newline();
        return;
    }

    bios_puts("Running app...");
    bios_newline();
    bios_newline();

    // Jump to app code
    user_app_t app = (user_app_t)APP_LOAD_ADDR;
    app();  // Transfer control to the app
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
        bios_putc('>');
        read_command(cmd, sizeof(cmd));
        char *command, *arg;
        split_command_arg(cmd, &command, &arg);
        bios_newline();
        if (!strcmp(command, "reboot")) {
            __asm__ __volatile__("int $0x19");
        } else if (!strcmp(command, "halt")) {
            bios_puts("Halting...");
            for (;;) __asm__ __volatile__("hlt");
        } else if (!strcmp(command, "com")) {
            bios_puts("Initializing COM1");
            com1_init();
            bios_newline();
            bios_puts("Type !q to quit");
            for (;;) {
                bios_newline();
                char cmd[80];
                bios_puts("COM1>");
                read_command(cmd, sizeof(cmd));
                if (!strcmp(cmd, "!q")) {
                    break;
                }
                com1_puts(cmd);
                com1_newline();
                bios_puts("Send!");
            }
        } else if (!strcmp(command, "ls")) {
            fat12_list_files();
        } else if (!strcmp(command, "mount")) {
            fat12_init();
        } else if (!strcmp(command, "cat")) {
            if (fat12_initialized) {
                if (fat12_find_file(arg)) {
                    char buffer[4096];
                    int size = fat12_read_file(arg, buffer, sizeof(buffer));
                    if (size > 0) {
                        for (int i = 0; i < size; i++) bios_putc(buffer[i]);
                    }
                } else {
                    bios_puts("File not found!");
                }
            } else {
                bios_puts("Error: FAT12 not mounted! Use 'mount' first.");
            }
        } else if (!strcmp(command, "beepon")) {
            if (arg[0] != 0) {
                speaker_on(str_to_int(arg));
            } else {
                bios_puts("Missing argument: frequency");
            }
        } else if (!strcmp(command, "beepoff")) {
            speaker_off();
        } else if (!strcmp(command, "run")) {
            if (fat12_initialized) {
                if (fat12_find_file(arg)) {
                    run_app(arg);
                } else {
                    bios_puts("File not found!");
                }
            } else {
                bios_puts("Error: FAT12 not mounted! Use 'mount' first.");
            }
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
