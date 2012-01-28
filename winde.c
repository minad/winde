#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#define UART_BAUD_RATE 9600
#define MAX_ARGS       4
#define TABLE_FORMAT   "%-20s | %-24s | %-4s | %s\n"

typedef struct {
        int8_t read;
        int8_t write;
        int8_t size;
        char   buf[0];
} ringbuf_t;

typedef struct {
        char* name;
        char* usage;
        void (*fn)(int, char*[]);
        char* help;
} cmd_t;

void ports_init();
void ports_reset();
void ports_read();
void ports_write();
void state_update();
const char* state_str(char);
void state_set(char);

ringbuf_t* ringbuf_init(void* buf, int8_t size);
void       ringbuf_reset(ringbuf_t* rb);
int        ringbuf_full(ringbuf_t* rb);
int        ringbuf_empty(ringbuf_t* rb);
int        ringbuf_putc(ringbuf_t* rb, char c);
int        ringbuf_getc(ringbuf_t* rb);

void uart_init(uint32_t baud);
int  uart_putchar(char c, FILE* fp);
int  uart_getc();

void usage();
int  check_manual();
void print_version();
void cmd_handler();
void cmd_exec(char*);
const cmd_t* cmd_find(const char*);
void cmd_in(int argc, char* argv[]);
void cmd_out(int argc, char* argv[]);
void cmd_on_off(int argc, char* argv[]);
void cmd_mode(int argc, char* argv[]);
void cmd_reset(int argc, char* argv[]);
void cmd_help(int argc, char* argv[]);
void cmd_version(int argc, char* argv[]);

ringbuf_t *uart_rx_buf, *uart_tx_buf;

const cmd_t cmd_list[] = {
        { "in",      0,        cmd_in,      "Print list of input ports"  },
        { "out",     0,        cmd_out,     "Print list of output ports" },
        { "on",      "<port>", cmd_on_off,  "Set port on"                },
        { "off",     "<port>", cmd_on_off,  "Set port off"               },
        { "mode",    "[a|m]",  cmd_mode,    "Set automatic/manual mode"  },
        { "reset",   0,        cmd_reset,   "Reset system"               },
        { "help",    "[cmd]",  cmd_help,    "Print this help"            },
        { "version", 0,        cmd_version, "Print version"              },
        { 0,                                                             },
}, *current_cmd = 0;

struct {
#define IN(name, port, bit)              int name : 1;
#define IN_ALIAS(name, port, bit, alias) union { int name : 1; int alias : 1; };
#include "config.h"
} in;

struct {
#define OUT(name, port, bit)              int name : 1;
#define OUT_ALIAS(name, port, bit, alias) union { int name : 1; int alias : 1; };
#include "config.h"
} out;

char manual = 1, state = 0, show_prompt = 1;

enum {
#define STATE(name, attrs) STATE_##name,
#include "config.h"
};

#define ACTION(name, code) static inline void action_##name() code
#include "config.h"

int main() {
        OSCCAL = 0xA1;
        ports_init();
        uart_init(UART_BAUD_RATE);
        sei();
        print_version();

        for (;;) {
                ports_read();
                state_update();
                ports_write();
                cmd_handler();
        }
        return 0;
}

void ports_init() {
        ports_reset();

#define OUT(name, port, bit) DDR ## port |= (1 << bit);
#include "config.h"
}

void ports_reset() {
#define OUT(name, port, bit) out.name = 0;
#include "config.h"
}

void ports_read() {
#define IN(name, port, bit) in.name = (PIN ## port >> bit) & 1;
#include "config.h"
}

void ports_write() {
#define OUT(name, port, bit) \
        if (out.name) { PORT ## port |= (1 << bit); } \
        else { PORT ## port &= ~(1 << bit); }
#include "config.h"
}

const char* state_str(char state) {
        switch (state) {
#define STATE(name, attrs) case STATE_##name: return #name;
#include "config.h"
        default: return "invalid";
        }
}

void state_set(char s) {
        state = s;
        printf("\nState %s\n", state_str(state));
        show_prompt = 1;
}

void state_update() {
        if (manual)
                return;

#define EVENT(name, condition) int name = (condition);
#include "config.h"

        out.led_handbremse = in.handbremse_angezogen;
        out.led_kappung = in.kappung_gespannt;
        out.led_temperatur = !temp_ok;
        out.led_power = 1;
        out.buzzer = (state != STATE_bremse_getreten && (in.schalter_einkuppeln_links || in.schalter_einkuppeln_rechts)) ||
                     (state != STATE_bereit && in.schalter_auszugsbremse_auf) ||
                     (state != STATE_links_eingekuppelt && state != STATE_rechts_eingekuppelt && in.schalter_auskuppeln); 

#define TRANS_ACTION(initial, event, final, attrs, act) \
        if (state == STATE_##initial && event) { state_set(STATE_##final); action_##act(); return; }
#define TRANS(initial, event, final, attrs) \
        if (state == STATE_##initial && event) { state_set(STATE_##final); return; }
#include "config.h"
}

void usage() {
        printf("Usage: %s %s\n", current_cmd->name, current_cmd->usage ? current_cmd->usage : "");
}

int check_manual() {
        if (!manual)
                printf("Enable manual mode first!\n");
        return manual;
}

void print_version() {
        printf("\nSteuersoftware der Winde AFK-3\n"
               "  Version:       " VERSION "\n"
               "  Git-Version:   " GIT_VERSION "\n"
               "  Kompiliert am: " __DATE__ " " __TIME__ "\n"
               "  Elektronik:    Christian 'Paule' Schreiber\n"
               "  Software:      Daniel 'Teilchen' Mendler\n\n");
}

void cmd_exec(char* line) {
        char *argv[MAX_ARGS];
        int argc;
        for (argc = 0; argc < MAX_ARGS; ++argc) {
                if (!(argv[argc] = strsep(&line, " \t")) || *argv[argc] == '\0')
                        break;
        }
        if (argc > 0) {
                const cmd_t* cmd = cmd_find(argv[0]);
                if (cmd) {
                        current_cmd = cmd;
                        cmd->fn(argc, argv);
                        current_cmd = 0;
                }
        }
}

const cmd_t* cmd_find(const char* name) {
        for (const cmd_t* cmd = cmd_list; cmd->name; ++cmd) {
                if (!strcmp(cmd->name, name))
                        return cmd;
        }
        printf("Command not found: %s\n", name);
        return 0;
}

void cmd_handler() {
        static char line[64];
        static int size = 0;
        if (show_prompt) {
                printf("%s> ", manual ? "manual" : state_str(state));
                show_prompt = 0;
        }
        int c = uart_getc();
        if (c != EOF) {
                if (c == '\r') {
                        putchar('\n');
                        line[size] = 0;
                        cmd_exec(line);
                        size = 0;
                        show_prompt = 1;
                } else if (size + 1 < sizeof (line)) {
                        putchar(c);
                        line[size++] = c;
                }
        }
}

void cmd_in(int argc, char* argv[]) {
        if (argc != 1)
                return usage();
        printf("Inputs:\n");
        printf(TABLE_FORMAT, "Name", "Alias", "Port", "Active");
#define IN(name, port, bit) \
        printf(TABLE_FORMAT, #name, "", #port#bit, in.name ? "X" : "");
#define IN_ALIAS(name, port, bit, alias) \
        printf(TABLE_FORMAT, #name, #alias, #port#bit, in.name ? "X" : "");
#include "config.h"
        putchar('\n');
}

void cmd_out(int argc, char* argv[]) {
        if (argc != 1)
                return usage();
        printf("Outputs:\n");
        printf(TABLE_FORMAT, "Name", "Alias", "Port", "Active");
#define OUT(name, port, bit) \
        printf(TABLE_FORMAT, #name, "", #port#bit, out.name ? "X" : "");
#define OUT_ALIAS(name, port, bit, alias) \
        printf(TABLE_FORMAT, #name, #alias, #port#bit, out.name ? "X" : "");
#include "config.h"
        putchar('\n');
}

void cmd_on_off(int argc, char* argv[]) {
        if (argc != 2)
                return usage();
        if (check_manual()) {
                int on = strcmp(argv[0], "on") ? 0 : 1;
#define OUT(name, port, bit) \
                if (!strcmp(argv[1], #name)) { out.name = on; return; }
#define OUT_ALIAS(name, port, bit, alias)                               \
                if (!strcmp(argv[1], #name) || !strcmp(argv[1], #alias)) { out.name = on; return; }
#include "config.h"
        }
}

void cmd_mode(int argc, char* argv[]) {
        if (argc == 2 && !strcmp(argv[1], "m")) {
                manual = 1;
        } else if (argc == 2 && !strcmp(argv[1], "a")) {
                manual = 0;
                state = 0;
                ports_reset();
        } else if (argc != 1) {
                usage();
        }
}

void cmd_reset(int argc, char* argv[]) {
        if (argc != 1)
                return usage();
        if (check_manual())
                ports_reset();
}

void cmd_help(int argc, char* argv[]) {
        if (argc == 1) {
                printf("List of commands:\n");
                for (const cmd_t* cmd = cmd_list; cmd->name; ++cmd)
                        printf("  %20s %s\n", cmd->name, cmd->help);
                putchar('\n');
        } else if (argc == 2) {
                const cmd_t* cmd = cmd_find(argv[1]);
                if (cmd)
                        printf("Usage: %s %s\n%s\n", cmd->name, cmd->usage, cmd->help);
        } else {
                usage();
        }
}

void cmd_version(int argc, char* argv[]) {
        if (argc != 1)
                return usage();
        print_version();
}

ringbuf_t* ringbuf_init(void* buf, int8_t size) {
	ringbuf_t *rb = (ringbuf_t*)buf;
	rb->size = size - sizeof(ringbuf_t);
	ringbuf_reset(rb);
	return rb;
}

void ringbuf_reset(ringbuf_t* rb) {
	rb->read = rb->write = 0;
        memset(rb->buf, 0, rb->size);
}

int ringbuf_full(ringbuf_t* rb) {
        return (rb->read == (rb->write + 1) % rb->size);
}

int ringbuf_empty(ringbuf_t* rb) {
        return (rb->read == rb->write);
}

int ringbuf_putc(ringbuf_t* rb, char c) {
        if (ringbuf_full(rb))
                return EOF;
        rb->buf[rb->write++] = c;
        if (rb->write == rb->size)
                rb->write = 0;
        return c;
}

int ringbuf_getc(ringbuf_t* rb) {
        if (ringbuf_empty(rb))
                return EOF;
        char c = rb->buf[rb->read++];
        if (rb->read == rb->size)
                rb->read = 0;
        return c;
}

void uart_init(uint32_t baud) {
        baud = F_CPU / (baud * 16) - 1;

        // Set baud rate
        UBRR0H = baud >> 8;
        UBRR0L = baud & 0xFF;

        // set frame format: 8 bit, no parity, 1 stop bit
        UCSR0C = (1 << UCSZ1) | (1 << UCSZ0);
        // enable serial receiver and transmitter
        UCSR0B = (1 << RXEN) | (1 << TXEN) | (1 << RXCIE);

        static char rx_buf[32], tx_buf[32];
        uart_rx_buf = ringbuf_init(rx_buf, sizeof (rx_buf));
        uart_tx_buf = ringbuf_init(tx_buf, sizeof (tx_buf));

        static FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, 0, _FDEV_SETUP_WRITE);
        stdout = &uart_stdout;
}

int uart_putchar(char c, FILE* fp) {
        if (c == '\n')
                uart_putchar('\r', fp);
        while (ringbuf_full(uart_tx_buf)) {} // wait
        ringbuf_putc(uart_tx_buf, c);
        UCSR0B |= (1 << UDRIE);
        return 0;
}

int uart_getc() {
        return ringbuf_getc(uart_rx_buf);
}

ISR(USART0_RX_vect) {
        ringbuf_putc(uart_rx_buf, UDR0);
}

ISR(USART0_UDRE_vect) {
        if (!ringbuf_empty(uart_tx_buf))
                UDR0 = ringbuf_getc(uart_tx_buf);
        else
                UCSR0B &= ~(1 << UDRIE);
}
