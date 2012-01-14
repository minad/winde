#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#define VERSION        "0.1"
#define UART_BAUD_RATE 9600
#define MAX_ARGS       4

typedef struct {
        int8_t read;
        int8_t write;
        int8_t size;
        char   buf[0];
} ringbuf_t;

typedef struct {
        char* name;
        void (*fn)(int, char*[]);
} cmd_t;

void system_init();
void ports_init();
void ports_read();
void ports_write();
void ports_update();

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
void usage(const char*, ...);
void cmd_handler();
void cmd_exec(char*);
void cmd_in(int argc, char* argv[]);
void cmd_out(int argc, char* argv[]);
void cmd_on(int argc, char* argv[]);
void cmd_off(int argc, char* argv[]);
void cmd_mode(int argc, char* argv[]);
void cmd_help(int argc, char* argv[]);
void cmd_version(int argc, char* argv[]);

ringbuf_t *uart_rx_ringbuf, *uart_tx_ringbuf;
char       uart_rx_buf[32], uart_tx_buf[32];

FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, 0, _FDEV_SETUP_WRITE);

cmd_t cmd_list[] = {
        { "in",      cmd_in      },
        { "out",     cmd_out     },
        { "on",      cmd_on      },
        { "off",     cmd_off     },
        { "mode"  ,  cmd_mode    },
        { "help",    cmd_help    },
        { "version", cmd_version },
        { 0,         0           },
};

struct {
#define INPUT(name, port, bit) int name : 1;
#define INPUT_WITH_ALIAS(name, port, bit, alias) union { int name : 1; int alias : 1; };
#include "ports.h"
} in;

struct {
#define OUTPUT(name, port, bit) int name : 1;
#define OUTPUT_WITH_ALIAS(name, port, bit, alias) union { int name : 1; int alias : 1; };
#include "ports.h"
} out;

int ports_manual = 0;

int main() {
        system_init();
        printf("\n--------------------\n"
               "Steuersoftware Winde"
               "\n--------------------\n");
        for (;;) {
                ports_read();
                ports_update();
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
#define OUTPUT(name, port, bit) DDR ## port |= (1 << bit); out.name = 0;
#include "ports.h"
}

void ports_read() {
#define INPUT(name, port, bit) in.name = (PIN ## port >> bit) & 1;
#include "ports.h"
}

void ports_write() {
#define OUTPUT(name, port, bit) if (out.name) { PORT ## port |= (1 << bit); } else { PORT ## port &= ~(1 << bit); }
#include "ports.h"
}

void ports_update() {
        if (ports_manual)
                return;

        out.led_eingekuppelt1 = in.schalter_trommel1;
}

void prompt() {
        printf("%c> ", ports_manual ? 'm' : 'a');
}

void usage(const char* fmt, ...) {
        printf("Usage: ");

        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);

        putchar('\n');
}

void cmd_exec(char* line) {
        char *argv[MAX_ARGS];
        int argc;
        for (argc = 0; argc < MAX_ARGS; ++argc) {
                if (!(argv[argc] = strsep(&line, " \t")) || *argv[argc] == '\0')
                        break;
        }
        if (argc > 0) {
                for (cmd_t* cmd = cmd_list; cmd->name; ++cmd) {
                        if (!strcmp(cmd->name, argv[0])) {
                                cmd->fn(argc, argv);
                                return;
                        }
                }
                printf("Command not found %s\n", argv[0]);
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

void cmd_in(int argc, char* argv[]) {
        if (argc != 1)
                return usage(argv[0]);

        printf("Inputs:\n"
#define INPUT(name, port, bit)        #name" = %d\n"
#define INPUT_WITH_ALIAS(name, port, bit, alias) #name" ("#alias") = %d\n"
#include "ports.h"
               "%c",
#define INPUT(name, port, bit) in.name,
#include "ports.h"
               '\n');
}

void cmd_out(int argc, char* argv[]) {
        if (argc != 1)
                return usage(argv[0]);

        printf("Outputs:\n"
#define OUTPUT(name, port, bit)        #name" = %d\n"
#define OUTPUT_WITH_ALIAS(name, port, bit, alias) #name" ("#alias") = %d\n"
#include "ports.h"
               "%c",
#define OUTPUT(name, port, bit) out.name,
#include "ports.h"
               '\n');
}

void cmd_on(int argc, char* argv[]) {
        if (argc != 2)
                return usage("%s <port>", argv[0]);

        if (!ports_manual) {
                printf("Enable manual mode first!\n");
                return;
        }

#define OUTPUT(name, port, bit) if (!strcmp(argv[1], #name)) { out.name = 1; return; }
#define OUTPUT_WITH_ALIAS(name, port, bit, alias) if (!strcmp(argv[1], #name) || !strcmp(argv[1], #name)) { out.name = 1; return; }
#include "ports.h"
}

void cmd_off(int argc, char* argv[]) {
        if (argc != 2)
                return usage("%s <port>", argv[0]);

        if (!ports_manual) {
                printf("Enable manual mode first!\n");
                return;
        }

#define OUTPUT(name, port, bit) if (!strcmp(argv[1], #name)) { out.name = 0; return; }
#define OUTPUT_WITH_ALIAS(name, port, bit, alias) if (!strcmp(argv[1], #name) || !strcmp(argv[1], #alias)) { out.name = 0; return; }
#include "ports.h"
}

void cmd_mode(int argc, char* argv[]) {
        if (argc == 2 && !strcmp(argv[1], "m"))
                ports_manual = 1;
        else if (argc == 2 && !strcmp(argv[1], "a"))
                ports_manual = 0;
        else if (argc != 1)
                usage("%s [m|a]", argv[0]);
}

void cmd_help(int argc, char* argv[]) {
        if (argc != 1)
                return usage(argv[0]);

        printf("List of available commands:\n");
        for (cmd_t* cmd = cmd_list; cmd->name; ++cmd)
                printf("%s\n", cmd->name);
        putchar('\n');
}

void cmd_version(int argc, char* argv[]) {
        if (argc != 1)
                return usage(argv[0]);

        printf("Steuersoftware Winde Version " VERSION "\n"
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
        while (ringbuf_full(uart_tx_ringbuf)) {
                // wait
        }
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
