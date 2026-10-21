#include "font.h"

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void outb(unsigned short port, unsigned char val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline void outw(unsigned short port, unsigned short val) {
    asm volatile ( "outw %0, %1" : : "a"(val), "Nd"(port) );
}

static inline unsigned long long rdtsc() {
    unsigned int lo, hi;
    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((unsigned long long)hi << 32) | lo;
}

static inline void cpuid(int code, unsigned int *a, unsigned int *d) {
    asm volatile("cpuid":"=a"(*a),"=d"(*d):"a"(code):"ecx","ebx");
}

#define VGA_WIDTH 40
#define VGA_HEIGHT 25

char term_chars[VGA_HEIGHT][VGA_WIDTH];
unsigned char term_colors[VGA_HEIGHT][VGA_WIDTH];
int terminal_row = 0;
int terminal_column = 0;
unsigned char terminal_color = 15; // White

unsigned char vga_buffer[64000];
unsigned char* vga_mem = (unsigned char*) 0xA0000;

void clear() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            term_chars[y][x] = ' ';
            term_colors[y][x] = terminal_color;
        }
    }
    terminal_row = 0;
    terminal_column = 0;
}

void scroll() {
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            term_chars[y - 1][x] = term_chars[y][x];
            term_colors[y - 1][x] = term_colors[y][x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        term_chars[VGA_HEIGHT - 1][x] = ' ';
        term_colors[VGA_HEIGHT - 1][x] = terminal_color;
    }
    terminal_row--;
}

void putchar(char c) {
    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
    } else if (c == '\b') {
        if (terminal_column > 0) {
            terminal_column--;
            term_chars[terminal_row][terminal_column] = ' ';
        } else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
            term_chars[terminal_row][terminal_column] = ' ';
        }
    } else {
        term_chars[terminal_row][terminal_column] = c;
        term_colors[terminal_row][terminal_column] = terminal_color;
        terminal_column++;
        if (terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            terminal_row++;
        }
    }
    if (terminal_row >= VGA_HEIGHT) {
        scroll();
    }
}

void print(const char* data) {
    for (int i = 0; data[i] != '\0'; i++) putchar(data[i]);
}

void print_int(unsigned int n) {
    if (n == 0) { putchar('0'); return; }
    char buf[16]; int i = 0;
    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    for (int j = i - 1; j >= 0; j--) putchar(buf[j]);
}

void print_hex(unsigned int n) {
    print("0x");
    if (n == 0) { putchar('0'); return; }
    char buf[16]; int i = 0;
    while (n > 0) {
        int rem = n % 16;
        if (rem < 10) buf[i++] = rem + '0';
        else buf[i++] = rem - 10 + 'A';
        n /= 16;
    }
    for (int j = i - 1; j >= 0; j--) putchar(buf[j]);
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

unsigned int rand_seed = 12345;
unsigned int rand() {
    rand_seed = (rand_seed * 1103515245 + 12345);
    return (unsigned int)(rand_seed / 65536) % 32768;
}

unsigned char get_rtc_register(int reg) {
    outb(0x70, reg);
    return inb(0x71);
}

void print_time() {
    unsigned char second = get_rtc_register(0x00);
    unsigned char minute = get_rtc_register(0x02);
    unsigned char hour = get_rtc_register(0x04);
    
    // Convert BCD to binary
    second = (second & 0x0F) + ((second / 16) * 10);
    minute = (minute & 0x0F) + ((minute / 16) * 10);
    hour = (hour & 0x0F) + ((hour / 16) * 10);
    
    print_int(hour); print(":");
    if (minute < 10) print("0"); print_int(minute); print(":");
    if (second < 10) print("0"); print_int(second); print("\n");
}

void print_date() {
    unsigned char day = get_rtc_register(0x07);
    unsigned char month = get_rtc_register(0x08);
    unsigned char year = get_rtc_register(0x09);
    
    day = (day & 0x0F) + ((day / 16) * 10);
    month = (month & 0x0F) + ((month / 16) * 10);
    year = (year & 0x0F) + ((year / 16) * 10);
    
    if (day < 10) print("0"); print_int(day); print("/");
    if (month < 10) print("0"); print_int(month); print("/20");
    print_int(year); print("\n");
}

const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

char check_key() {
    if ((inb(0x64) & 1) && !(inb(0x64) & 0x20)) { // Data available AND from keyboard
        unsigned char scancode = inb(0x60);
        if (!(scancode & 0x80)) {
            if (scancode < 128 && scancode_to_ascii[scancode] != 0) {
                return scancode_to_ascii[scancode];
            }
        }
    }
    return 0;
}

// Mouse Logic
int mouse_x = 160;
int mouse_y = 100;
int mouse_b = 0;
unsigned char mouse_cycle = 0;
char mouse_byte[3];

void mouse_wait(unsigned char type) {
    int time_out = 100000;
    if (type == 0) {
        while (time_out--) if ((inb(0x64) & 1) == 1) return;
    } else {
        while (time_out--) if ((inb(0x64) & 2) == 0) return;
    }
}

void mouse_write(unsigned char a) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, a);
}

unsigned char mouse_read() {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_init() {
    mouse_wait(1);
    outb(0x64, 0xA8); // Enable aux
    mouse_wait(1);
    outb(0x64, 0x20); // Get compaq status byte
    mouse_wait(0);
    unsigned char status = inb(0x60) | 2; // Enable IRQ12
    mouse_wait(1);
    outb(0x64, 0x60); // Set compaq status
    mouse_wait(1);
    outb(0x60, status);
    mouse_write(0xF6); // Set defaults
    mouse_read(); // Acknowledge
    mouse_write(0xF4); // Enable data reporting
    mouse_read(); // Acknowledge
}

void handle_mouse() {
    while ((inb(0x64) & 0x21) == 0x21) {
        mouse_byte[mouse_cycle++] = inb(0x60);
        if (mouse_cycle == 3) {
            mouse_cycle = 0;
            char dx = mouse_byte[1];
            char dy = mouse_byte[2];
            
            // Apply mouse movement
            mouse_x += dx;
            mouse_y += dy; // Changed from -= to += based on your feedback!
            
            // Keep mouse inside screen bounds
            if(mouse_x < 0) mouse_x = 0;
            if(mouse_x > 319) mouse_x = 319;
            if(mouse_y < 0) mouse_y = 0;
            if(mouse_y > 199) mouse_y = 199;
            mouse_b = mouse_byte[0] & 0x07;
            
            // Paint if left clicked!
            if (mouse_b & 1) { 
                int grid_x = mouse_x / 8;
                int grid_y = mouse_y / 8;
                if (grid_x >= 0 && grid_x < VGA_WIDTH && grid_y >= 0 && grid_y < VGA_HEIGHT) {
                    term_colors[grid_y][grid_x] = (rand() % 14) + 1; // Random color
                }
            }
            // Erase if right clicked!
            if (mouse_b & 2) { 
                int grid_x = mouse_x / 8;
                int grid_y = mouse_y / 8;
                if (grid_x >= 0 && grid_x < VGA_WIDTH && grid_y >= 0 && grid_y < VGA_HEIGHT) {
                    term_colors[grid_y][grid_x] = terminal_color;
                    term_chars[grid_y][grid_x] = ' ';
                }
            }
        }
    }
}

void draw_char(char c, int x, int y, unsigned char fg, unsigned char bg) {
    if (c < 0 || c >= 128) c = '?';
    char *bitmap = font8x8_basic[(int)c];
    for (int r = 0; r < 8; r++) {
        for (int col = 0; col < 8; col++) {
            if (bitmap[r] & (1 << col)) {
                vga_buffer[(y + r) * 320 + (x + col)] = fg;
            } else {
                vga_buffer[(y + r) * 320 + (x + col)] = bg;
            }
        }
    }
}

void draw_mouse() {
    // Draw a simple 5x5 cursor
    for (int i=0; i<6; i++) {
        if(mouse_x+i < 320 && mouse_y+i < 200) vga_buffer[(mouse_y+i)*320 + mouse_x+i] = 15;
        if(mouse_x < 320 && mouse_y+i < 200) vga_buffer[(mouse_y+i)*320 + mouse_x] = 15;
        if(mouse_x+i < 320 && mouse_y < 200) vga_buffer[mouse_y*320 + mouse_x+i] = 15;
    }
}

void render() {
    // Clear buffer to dark gray
    for(int i=0; i<64000; i++) vga_buffer[i] = 0;

    // Draw terminal
    for(int r=0; r < VGA_HEIGHT; r++) {
        for(int c=0; c < VGA_WIDTH; c++) {
            draw_char(term_chars[r][c], c * 8, r * 8, term_colors[r][c], 0);
        }
    }
    
    // Draw blinking cursor
    unsigned int t = (unsigned int)rdtsc();
    if ((t / 50000000) % 2 == 0) {
        draw_char('_', terminal_column * 8, terminal_row * 8, terminal_color, 0);
    }

    draw_mouse();

    // Copy to VGA memory
    for(int i=0; i<64000; i++) {
        vga_mem[i] = vga_buffer[i];
    }
}

char buffer[256];
int pos = 0;

void process_command() {
    if (pos == 0) return;
    
    if (strcmp(buffer, "help") == 0) {
        print("Available commands:\n");
        print("  help      - Show this message\n");
        print("  clear     - Clear the screen\n");
        print("  hello     - Print a greeting\n");
        print("  echo      - Print arguments\n");
        print("  whoami    - Print current user\n");
        print("  time      - Show current time (RTC)\n");
        print("  date      - Show current date (RTC)\n");
        print("  sysinfo   - Show system information\n");
        print("  lscpu     - Show CPU info (CPUID)\n");
        print("  free      - Show memory info\n");
        print("  uptime    - Show CPU cycles elapsed\n");
        print("  ping      - Ping... pong!\n");
        print("  color     - Change text color (usage: color <1-15>)\n");
        print("  rand      - Generate random number\n");
        print("  ls        - List directory contents\n");
        print("  pwd       - Print working directory\n");
        print("  cd        - Change directory\n");
        print("  cat       - Concatenate files\n");
        print("  reboot    - Reboot system\n");
        print("  poweroff  - ACPI/QEMU Poweroff\n");
        print("  exit      - Exit shell\n");
        print("\nHINT: Try drawing by clicking your MOUSE!\n");
    } 
    else if (strcmp(buffer, "clear") == 0) {
        clear();
    } 
    else if (strcmp(buffer, "hello") == 0) {
        print("Hello! This is YoroOS GUI working properly!\n");
    } 
    else if (strncmp(buffer, "echo ", 5) == 0) {
        print(&buffer[5]);
        print("\n");
    } 
    else if (strcmp(buffer, "echo") == 0) {
        print("\n");
    }
    else if (strcmp(buffer, "whoami") == 0) {
        print("user (root privileges)\n");
    }
    else if (strcmp(buffer, "time") == 0) {
        print_time();
    }
    else if (strcmp(buffer, "date") == 0) {
        print_date();
    }
    else if (strcmp(buffer, "sysinfo") == 0) {
        print("OS: YoroOS v1.1 (GUI Edition)\n");
        print("Architecture: 32-bit x86\n");
        print("Video: VGA Mode 13h (320x200x256)\n");
    }
    else if (strcmp(buffer, "lscpu") == 0) {
        unsigned int eax = 0, edx = 0;
        cpuid(1, &eax, &edx);
        print("CPU Family/Model: ");
        print_hex(eax);
        print("\nCPU Features (EDX): ");
        print_hex(edx);
        print("\n");
    }
    else if (strcmp(buffer, "free") == 0) {
        print("Total memory: 640K ought to be enough for anybody.\n");
        print("Used memory:  A little bit\n");
        print("Free memory:  The rest\n");
    }
    else if (strcmp(buffer, "uptime") == 0) {
        unsigned long long cycles = rdtsc();
        print("CPU cycles elapsed: ");
        print_int((unsigned int)(cycles >> 32));
        print_int((unsigned int)(cycles & 0xFFFFFFFF));
        print("\n");
    }
    else if (strcmp(buffer, "ping") == 0) {
        print("pong!\n");
    }
    else if (strncmp(buffer, "color ", 6) == 0) {
        int col = 0;
        int i = 6;
        while(buffer[i] >= '0' && buffer[i] <= '9') {
            col = col * 10 + (buffer[i] - '0');
            i++;
        }
        if (col >= 1 && col <= 15) {
            terminal_color = col;
            print("Color changed!\n");
        } else {
            print("Usage: color <1-15>\n");
        }
    }
    else if (strcmp(buffer, "color") == 0) {
        print("Usage: color <1-15>\n");
    }
    else if (strcmp(buffer, "rand") == 0) {
        print("Random number: ");
        print_int(rand());
        print("\n");
    }
    else if (strcmp(buffer, "ls") == 0) {
        print(".\n..\nboot.bin\nkernel.bin\nsecret.txt\n");
    }
    else if (strcmp(buffer, "pwd") == 0) {
        print("/root\n");
    }
    else if (strncmp(buffer, "cd ", 3) == 0) {
        print("cd: ");
        print(&buffer[3]);
        print(": No such file or directory\n");
    }
    else if (strcmp(buffer, "cd") == 0) {
        print("\n");
    }
    else if (strncmp(buffer, "cat ", 4) == 0) {
        if (strcmp(&buffer[4], "secret.txt") == 0) {
            print("The secret is: 42\n");
        } else {
            print("cat: ");
            print(&buffer[4]);
            print(": No such file or directory\n");
        }
    }
    else if (strcmp(buffer, "cat") == 0) {
        print("Usage: cat <file>\n");
    }
    else if (strcmp(buffer, "reboot") == 0) {
        print("Rebooting...\n");
        outb(0x64, 0xFE); 
        while(1);
    }
    else if (strcmp(buffer, "poweroff") == 0) {
        print("Powering off (QEMU)...\n");
        outw(0x604, 0x2000); // QEMU specific poweroff
        outw(0x4004, 0x3400); // VirtualBox specific
        while(1);
    }
    else if (strcmp(buffer, "exit") == 0) {
        print("Where would you go? This is the only OS you need.\n");
    }
    else {
        print("Unknown command: ");
        print(buffer);
        print("\n");
    }
    pos = 0;
}

void main() {
    mouse_init();
    clear();
    terminal_color = 11; // Cyan
    print("Welcome to YoroOS GUI!\n");
    terminal_color = 15; // White
    print("VGA Mode 13h (320x200 256 colors)\n");
    print("Mouse is ENABLED! Click to paint.\n\n");
    
    terminal_color = 10; // Light green
    print("user@yoroos:~$ ");
    terminal_color = 15;

    rand_seed = (unsigned int)rdtsc();

    while (1) {
        handle_mouse();
        
        char c = check_key();
        if (c) {
            if (c == '\n') {
                buffer[pos] = '\0';
                print("\n");
                process_command();
                terminal_color = 10;
                print("user@yoroos:~$ ");
                terminal_color = 15;
                pos = 0;
            } else if (c == '\b') {
                if (pos > 0) {
                    pos--;
                    putchar('\b');
                }
            } else {
                if (pos < 255) {
                    buffer[pos++] = c;
                    putchar(c);
                }
            }
        }
        
        render();
    }
}
