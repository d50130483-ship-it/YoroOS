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

unsigned int rand_seed = 12345;
unsigned int rand() {
    rand_seed = (rand_seed * 1103515245 + 12345);
    return (unsigned int)(rand_seed / 65536) % 32768;
}

unsigned char get_rtc_register(int reg) {
    outb(0x70, reg);
    return inb(0x71);
}

#define VGA_WIDTH 34
#define VGA_HEIGHT 15

char term_chars[VGA_HEIGHT][VGA_WIDTH];
unsigned char term_colors[VGA_HEIGHT][VGA_WIDTH];
int terminal_row = 0;
int terminal_column = 0;
unsigned char terminal_color = 0;

unsigned char vga_buffer[64000];
unsigned char* vga_mem = (unsigned char*) 0xA0000;

// WINDOW MANAGER STRUCTURES
typedef struct {
    int x, y, w, h;
    char title[32];
    int open;
    int type; // 0 = Terminal, 1 = About
} Window;

Window windows[2];
int z_order[2] = {0, 1};
int drag_win = -1;
int drag_off_x = 0;
int drag_off_y = 0;
int prev_mouse_b = 0;

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
        if (terminal_column >= VGA_WIDTH) {
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

const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

char check_key() {
    if ((inb(0x64) & 1) && !(inb(0x64) & 0x20)) { 
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
    mouse_wait(1); outb(0x64, 0xD4);
    mouse_wait(1); outb(0x60, a);
}

unsigned char mouse_read() {
    mouse_wait(0); return inb(0x60);
}

void mouse_init() {
    mouse_wait(1); outb(0x64, 0xA8); 
    mouse_wait(1); outb(0x64, 0x20); 
    mouse_wait(0); unsigned char status = inb(0x60) | 2; 
    mouse_wait(1); outb(0x64, 0x60); 
    mouse_wait(1); outb(0x60, status);
    mouse_write(0xF6); mouse_read(); 
    mouse_write(0xF4); mouse_read(); 
}

void handle_mouse() {
    while ((inb(0x64) & 0x21) == 0x21) {
        mouse_byte[mouse_cycle++] = inb(0x60);
        if (mouse_cycle == 3) {
            mouse_cycle = 0;
            int state = mouse_byte[0];
            char dx = mouse_byte[1];
            char dy = mouse_byte[2];
            
            mouse_x += dx;
            mouse_y += dy; 
            
            if(mouse_x < 0) mouse_x = 0;
            if(mouse_x > 319) mouse_x = 319;
            if(mouse_y < 0) mouse_y = 0;
            if(mouse_y > 199) mouse_y = 199;
            
            int mb = state & 0x07;

            // Window Management Logic
            if ((mb & 1) && !prev_mouse_b) { // Clicked
                int clicked_win = -1;
                for (int i=1; i>=0; i--) {
                    int wid = z_order[i];
                    if (!windows[wid].open) continue;
                    if (mouse_x >= windows[wid].x && mouse_x <= windows[wid].x + windows[wid].w &&
                        mouse_y >= windows[wid].y && mouse_y <= windows[wid].y + windows[wid].h) {
                        
                        clicked_win = wid;
                        // Bring to front
                        if (i == 0) { 
                            int temp = z_order[0]; z_order[0] = z_order[1]; z_order[1] = temp;
                        }

                        // Close button or Drag
                        if (mouse_y <= windows[wid].y + 14) {
                            if (mouse_x >= windows[wid].x + 4 && mouse_x <= windows[wid].x + 12) {
                                windows[wid].open = 0; // Close window
                            } else {
                                drag_win = wid;
                                drag_off_x = mouse_x - windows[wid].x;
                                drag_off_y = mouse_y - windows[wid].y;
                            }
                        }
                        break;
                    }
                }
                
                // Dock clicks
                if (clicked_win == -1) {
                    if (mouse_y >= 180 && mouse_y <= 198) {
                        if (mouse_x >= 95 && mouse_x <= 113) windows[1].open = 1; // Finder -> About
                        if (mouse_x >= 135 && mouse_x <= 153) windows[0].open = 1; // Terminal
                    }
                }
            }
            if (!(mb & 1)) {
                drag_win = -1; // Stop dragging
            }
            if (drag_win != -1) {
                windows[drag_win].x = mouse_x - drag_off_x;
                windows[drag_win].y = mouse_y - drag_off_y;
            }
            prev_mouse_b = mb & 1;
        }
    }
}

// Drawing Primitives
void draw_rect(int x, int y, int w, int h, unsigned char color) {
    for(int i=0; i<h; i++) {
        for(int j=0; j<w; j++) {
            if (x+j >= 0 && x+j < 320 && y+i >= 0 && y+i < 200) {
                vga_buffer[(y+i)*320 + (x+j)] = color;
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
                if (x+col >= 0 && x+col < 320 && y+r >= 0 && y+r < 200) vga_buffer[(y + r) * 320 + (x + col)] = fg;
            } else if (bg != 255) { // 255 means transparent
                if (x+col >= 0 && x+col < 320 && y+r >= 0 && y+r < 200) vga_buffer[(y + r) * 320 + (x + col)] = bg;
            }
        }
    }
}

void draw_string(const char* str, int x, int y, unsigned char fg, unsigned char bg) {
    int cur_x = x;
    for (int i=0; str[i] != '\0'; i++) {
        draw_char(str[i], cur_x, y, fg, bg);
        cur_x += 8;
    }
}

// Mac Arrow Cursor
unsigned char cursor_img[10][10] = {
    {1,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,1,1,1,0,0,0},
    {1,2,1,2,1,0,0,0,0,0},
    {1,1,0,1,2,1,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,0}
};

void draw_mouse() {
    for(int i=0; i<10; i++) {
        for(int j=0; j<10; j++) {
            if (cursor_img[i][j] == 1) {
                if(mouse_x+j >=0 && mouse_x+j < 320 && mouse_y+i >=0 && mouse_y+i < 200) vga_buffer[(mouse_y+i)*320 + (mouse_x+j)] = 0;
            } else if (cursor_img[i][j] == 2) {
                if(mouse_x+j >=0 && mouse_x+j < 320 && mouse_y+i >=0 && mouse_y+i < 200) vga_buffer[(mouse_y+i)*320 + (mouse_x+j)] = 15;
            }
        }
    }
}

void get_time_str(char* buf) {
    unsigned char hour = get_rtc_register(0x04);
    unsigned char minute = get_rtc_register(0x02);
    hour = (hour & 0x0F) + ((hour / 16) * 10);
    minute = (minute & 0x0F) + ((minute / 16) * 10);
    buf[0] = (hour / 10) + '0'; buf[1] = (hour % 10) + '0';
    buf[2] = ':';
    buf[3] = (minute / 10) + '0'; buf[4] = (minute % 10) + '0';
    buf[5] = '\0';
}

void render() {
    // 1. Desktop background
    draw_rect(0, 0, 320, 200, 54); 

    // 2. Menu Bar
    draw_rect(0, 0, 320, 12, 15); // White bar
    draw_rect(0, 12, 320, 1, 7);  // Shadow border
    draw_string(" * YoroOS   File   Edit   View   Help", 5, 2, 0, 15);
    
    char time_str[6]; get_time_str(time_str);
    draw_string(time_str, 275, 2, 0, 15);
    
    // 3. Dock
    draw_rect(90, 178, 140, 22, 7); // Dock background
    draw_rect(95, 180, 18, 18, 1); // Finder icon
    draw_rect(115, 180, 18, 18, 2); // Safari icon
    draw_rect(135, 180, 18, 18, 0); // Terminal icon
    draw_rect(155, 180, 18, 18, 4); // Music icon
    draw_rect(175, 180, 18, 18, 14); // Notes icon

    // 4. Windows
    for(int i=0; i<2; i++) {
        int wid = z_order[i];
        if (!windows[wid].open) continue;
        int wx = windows[wid].x, wy = windows[wid].y, ww = windows[wid].w, wh = windows[wid].h;

        draw_rect(wx, wy, ww, wh, 0); // border
        draw_rect(wx+1, wy+1, ww-2, wh-2, 15); // window bg
        draw_rect(wx+1, wy+1, ww-2, 14, 7); // title bar
        
        draw_rect(wx+5, wy+4, 6, 6, 4); // red close btn
        draw_rect(wx+15, wy+4, 6, 6, 14); // yellow
        draw_rect(wx+25, wy+4, 6, 6, 10); // green
        
        draw_string(windows[wid].title, wx + ww/2 - 32, wy+4, 0, 7);

        if (windows[wid].type == 0) {
            for(int r=0; r < VGA_HEIGHT; r++) {
                for(int c=0; c < VGA_WIDTH; c++) {
                    draw_char(term_chars[r][c], wx + 4 + c * 8, wy + 18 + r * 8, term_colors[r][c], 15);
                }
            }
            unsigned int t = (unsigned int)rdtsc();
            if ((t / 50000000) % 2 == 0) {
                draw_char('_', wx + 4 + terminal_column * 8, wy + 18 + terminal_row * 8, terminal_color, 15);
            }
        } else if (windows[wid].type == 1) {
            draw_string("YoroOS Mac Edition", wx + 10, wy + 30, 0, 15);
            draw_string("Version 1.2", wx + 10, wy + 45, 0, 15);
            draw_string("Made with C & ASM", wx + 10, wy + 60, 0, 15);
        }
    }

    // 5. Draw Arrow Mouse
    draw_mouse();

    for(int i=0; i<64000; i++) vga_mem[i] = vga_buffer[i];
}

char buffer[256];
int pos = 0;

void process_command() {
    if (pos == 0) return;
    
    if (strcmp(buffer, "help") == 0) {
        print("Commands: clear, hello, color, time, rand, reboot, poweroff\n");
    } 
    else if (strcmp(buffer, "clear") == 0) { clear(); } 
    else if (strcmp(buffer, "hello") == 0) { print("Hello from Mac Edition!\n"); }
    else if (strncmp(buffer, "color ", 6) == 0) {
        int col = 0; int i = 6;
        while(buffer[i] >= '0' && buffer[i] <= '9') {
            col = col * 10 + (buffer[i] - '0'); i++;
        }
        if (col >= 0 && col <= 15) { terminal_color = col; print("Color changed!\n"); }
    }
    else if (strcmp(buffer, "time") == 0) {
        char t[6]; get_time_str(t); print(t); print("\n");
    }
    else if (strcmp(buffer, "rand") == 0) {
        print("Rand: "); print_int(rand()); print("\n");
    }
    else if (strcmp(buffer, "reboot") == 0) {
        outb(0x64, 0xFE); while(1);
    }
    else if (strcmp(buffer, "poweroff") == 0) {
        outw(0x604, 0x2000); outw(0x4004, 0x3400); while(1);
    }
    else {
        print("-bash: "); print(buffer); print(": command not found\n");
    }
    pos = 0;
}

void init_windows() {
    windows[0].x = 20; windows[0].y = 25; windows[0].w = 280; windows[0].h = 142;
    windows[0].open = 1; windows[0].type = 0;
    const char* t1 = "Terminal"; for(int i=0; i<9; i++) windows[0].title[i] = t1[i];

    windows[1].x = 100; windows[1].y = 50; windows[1].w = 160; windows[1].h = 80;
    windows[1].open = 0; windows[1].type = 1;
    const char* t2 = "About Mac"; for(int i=0; i<10; i++) windows[1].title[i] = t2[i];
}

void main() {
    mouse_init();
    init_windows();
    clear();
    
    print("Last login: Today on console\n");
    terminal_color = 12; // red prompt
    print("macintosh:~ user$ ");
    terminal_color = 0;

    rand_seed = (unsigned int)rdtsc();

    while (1) {
        handle_mouse();
        
        char c = check_key();
        if (c && windows[0].open) { // Only type if terminal is open
            if (c == '\n') {
                buffer[pos] = '\0';
                print("\n");
                process_command();
                terminal_color = 12;
                print("macintosh:~ user$ ");
                terminal_color = 0;
                pos = 0;
            } else if (c == '\b') {
                if (pos > 0) { pos--; putchar('\b'); }
            } else {
                if (pos < 255) { buffer[pos++] = c; putchar(c); }
            }
        }
        render();
    }
}