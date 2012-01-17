#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#define _STR(x) #x
#define STR(x) _STR(x)

#define VERSION        0.1
#define UART_BAUD_RATE 9600
#define MAX_ARGS       4
#define TABLE_FORMAT   "%-20s | %-24s | %-4s | %s\n"

typedef struct {
        int8_t read;
        int8_t write;
        int8_t size;
        char   buf[0];
} ringbuf_t;

typedef struct cmd_s {
        char* name;
        char* args;
        void (*fn)(const struct cmd_s*, int, char*[]);
        char* help;
} cmd_t;

void system_init();
void ports_init();
void ports_reset();
void ports_read();
void ports_write();
void state_machine();

ringbuf_t* ringbuf_init(void* buf, int8_t size);
void       ringbuf_reset(ringbuf_t* rb);
int        ringbuf_full(ringbuf_t* rb);
int        ringbuf_empty(ringbuf_t* rb);
int        ringbuf_putc(ringbuf_t* rb, char c);
int        ringbuf_getc(ringbuf_t* rb);

void uart_init(uint32_t baud);
int  uart_putchar(char c, FILE* fp);
int  uart_getc();

void prompt();
void usage(const cmd_t* cmd);
int  check_manual();
void cmd_handler();
void cmd_exec(char*);
void cmd_in(const cmd_t*, int argc, char* argv[]);
void cmd_out(const cmd_t*, int argc, char* argv[]);
void cmd_on_off(const cmd_t*, int argc, char* argv[]);
void cmd_mode(const cmd_t*, int argc, char* argv[]);
void cmd_reset(const cmd_t*, int argc, char* argv[]);
void cmd_help(const cmd_t*, int argc, char* argv[]);
void cmd_version(const cmd_t*, int argc, char* argv[]);

ringbuf_t *uart_rx_ringbuf, *uart_tx_ringbuf;
char       uart_rx_buf[32], uart_tx_buf[32];

FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, 0, _FDEV_SETUP_WRITE);

const cmd_t cmd_list[] = {
        { "in",      0,        cmd_in,      "Print list of input ports"  },
        { "out",     0,        cmd_out,     "Print list of output ports" },
        { "on",      "<port>", cmd_on_off,  "Set port on"                },
        { "off",     "<port>", cmd_on_off,  "Set port off"               },
        { "mode",    "[a|m]",  cmd_mode,    "Set automatic/manual mode"  },
        { "reset",   0,        cmd_reset,   "Reset system"               },
        { "help",    0,        cmd_help,    "Print this help"            },
        { "version", 0,        cmd_version, "Print version"              },
        { 0,                                                             },
};

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

char manual = 0, state = 0;

#define ACTION(name, code)     void action_##name() code
#define EVENT(name, condition) int event_##name() { return (condition); }
#include "config.h"

enum {
#define STATE(name) STATE_##name,
#include "config.h"
};

int main() {
        system_init();
        printf("\n--------------------\n"
               "Steuersoftware Winde"
               "\n--------------------\n");
        for (;;) {
                ports_read();
                state_machine();
                ports_write();
                cmd_handler();
        }
        return 0;
}

void system_init() {
        OSCCAL = 0xA1;
        ports_init();
        uart_init(UART_BAUD_RATE);
        sei();
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

void state_machine() {
        if (manual)
                return;
#define TRANSITION(initial, ename, aname, final) \
        if (state == STATE_##initial && event_##ename()) \
        { action_##aname(); state = STATE_##final; return; }
#include "config.h"
}

void prompt() {
        printf("%c> ", manual ? 'm' : 'a');
}

void usage(const cmd_t* cmd) {
        printf("Usage: %s %s\n", cmd->name, cmd->args ? cmd->args : "");
}

int check_manual() {
        if (!manual)
                printf("Enable manual mode first!\n");
        return manual;
}

void cmd_exec(char* line) {
        char *argv[MAX_ARGS];
        int argc;
        for (argc = 0; argc < MAX_ARGS; ++argc) {
                if (!(argv[argc] = strsep(&line, " \t")) || *argv[argc] == '\0')
                        break;
        }
        if (argc > 0) {
                for (const cmd_t* cmd = cmd_list; cmd->name; ++cmd) {
                        if (!strcmp(cmd->name, argv[0])) {
                                cmd->fn(cmd, argc, argv);
                                return;
                        }
                }
                printf("Command not found: %s\n", argv[0]);
        }
}

void cmd_handler() {
        static char line[64];
        static int size = -1;
        if (size < 0) {
                prompt();
                size = 0;
        }
        int c = uart_getc();
        if (c != EOF) {
                if (c == '\r') {
                        putchar('\n');
                        line[size] = 0;
                        cmd_exec(line);
                        size = -1;
                } else if (size + 1 < sizeof (line)) {
                        putchar(c);
                        line[size++] = c;
                }
        }
}

void cmd_in(const cmd_t* cmd, int argc, char* argv[]) {
        if (argc != 1)
                return usage(cmd);
        printf("Inputs:\n");
        printf(TABLE_FORMAT, "Name", "Alias", "Port", "Active");
#define IN(name, port, bit) \
        printf(TABLE_FORMAT, #name, "", #port#bit, in.name ? "X" : "");
#define IN_ALIAS(name, port, bit, alias) \
        printf(TABLE_FORMAT, #name, #alias, #port#bit, in.name ? "X" : "");
#include "config.h"
}

void cmd_out(const cmd_t* cmd, int argc, char* argv[]) {
        if (argc != 1)
                return usage(cmd);
        printf("Outputs:\n");
        printf(TABLE_FORMAT, "Name", "Alias", "Port", "Active");
#define OUT(name, port, bit) \
        printf(TABLE_FORMAT, #name, "", #port#bit, out.name ? "X" : "");
#define OUT_ALIAS(name, port, bit, alias) \
        printf(TABLE_FORMAT, #name, #alias, #port#bit, out.name ? "X" : "");
#include "config.h"
}

void cmd_on_off(const cmd_t* cmd, int argc, char* argv[]) {
        if (argc != 2)
                return usage(cmd);
        if (check_manual()) {
                int on = strcmp(argv[0], "on") ? 0 : 1;
#define OUT(name, port, bit) \
                if (!strcmp(argv[1], #name)) { out.name = on; return; }
#define OUT_ALIAS(name, port, bit, alias)                               \
                if (!strcmp(argv[1], #name) || !strcmp(argv[1], #alias)) { out.name = on; return; }
#include "config.h"
        }
}

void cmd_mode(const cmd_t* cmd, int argc, char* argv[]) {
        if (argc == 2 && !strcmp(argv[1], "m"))
                manual = 1;
        else if (argc == 2 && !strcmp(argv[1], "a"))
                manual = 0;
        else if (argc != 1)
                usage(cmd);
}

void cmd_reset(const cmd_t* cmd, int argc, char* argv[]) {
        if (argc != 1)
                return usage(cmd);
        if (check_manual()) {
                state = 0;
                ports_reset();
        }
}

void cmd_help(const cmd_t* cmd, int argc, char* argv[]) {
        if (argc != 1)
                return usage(cmd);
        printf("List of available commands:\n");
        for (const cmd_t* cmd = cmd_list; cmd->name; ++cmd)
                printf("%s\n", cmd->name);
        putchar('\n');
}

void cmd_version(const cmd_t* cmd, int argc, char* argv[]) {
        if (argc != 1)
                return usage(cmd);
        printf("Steuersoftware Winde Version " STR(VERSION) "\n"
               "  Elektronikentwicklung: Christian 'Paule' Schreiber\n"
               "  Softwareentwicklung:   Daniel 'Teilchen' Mendler\n\n");
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

        uart_rx_ringbuf = ringbuf_init(uart_rx_buf, sizeof (uart_rx_buf));
        uart_tx_ringbuf = ringbuf_init(uart_tx_buf, sizeof (uart_tx_buf));

        stdout = &uart_stdout;
}

int uart_putchar(char c, FILE* fp) {
        if (c == '\n')
                uart_putchar('\r', fp);
        while (ringbuf_full(uart_tx_ringbuf)) {} // wait
        ringbuf_putc(uart_tx_ringbuf, c);
        UCSR0B |= (1 << UDRIE);
        return 0;
}

int uart_getc() {
        return ringbuf_getc(uart_rx_ringbuf);
}

ISR(USART0_RX_vect) {
        ringbuf_putc(uart_rx_ringbuf, UDR0);
}

ISR(USART0_UDRE_vect) {
        if (!ringbuf_empty(uart_tx_ringbuf))
                UDR0 = ringbuf_getc(uart_tx_ringbuf);
        else
                UCSR0B &= ~(1 << UDRIE);
}
