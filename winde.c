#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#define VERSION        "0"
#define UART_BAUD_RATE 9600

typedef struct {
	size_t read;
        size_t write;
	size_t size;
	char buf[0];
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

void cmd_exec(char*);

ringbuf_t* ringbuf_init(void* buf, size_t size);
void       ringbuf_reset(ringbuf_t* rb);
int        ringbuf_full(ringbuf_t* rb);
int        ringbuf_empty(ringbuf_t* rb);
int        ringbuf_putc(ringbuf_t* rb, char c);
int        ringbuf_getc(ringbuf_t* rb);

void uart_init(uint32_t baud);
int  uart_putchar(char c, FILE* fp);
int  uart_getc();

void cmd_handler();
void cmd_help(int argc, char* argv[]);
void cmd_in(int argc, char* argv[]);
void cmd_version(int argc, char* argv[]);

ringbuf_t *uart_rx_ringbuf, *uart_tx_ringbuf;
char       uart_rx_buf[32], uart_tx_buf[32];

FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, 0, _FDEV_SETUP_WRITE);

cmd_t cmd_list[] = {
        { "in",      cmd_in      },
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
#define OUTPUT(name, port, bit) DDR ## port |= (1 << bit);
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
        out.led_eingekuppelt1 = in.schalter_trommel1;
}

void cmd_handler() {
        static char line[64];
        static int size = -1;
        if (size < 0) {
                printf("> ");
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
        printf("Inputs:\n"
#define INPUT(name, port, bit)        #name" = %d\n"
#define INPUT_WITH_ALIAS(name, port, bit, alias) #name" ("#alias") = %d\n"
#include "ports.h"
               "%c",
#define INPUT(name, port, bit) in.name,
#include "ports.h"
               '\n');
}

void cmd_help(int argc, char* argv[]) {
        printf("List of available commands:\n");
        for (cmd_t* cmd = cmd_list; cmd->name; ++cmd)
                printf("%s\n", cmd->name);
        putchar('\n');
}

void cmd_version(int argc, char* argv[]) {
        printf("Steuersoftware Winde Version " VERSION "\n"
               "  Elektronikentwicklung: Christian 'Paule' Schreiber\n"
               "  Softwareentwicklung:   Daniel 'Teilchen' Mendler\n\n");
}

void cmd_exec(char* line) {
        const int MAX_ARGS = 10;
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

ringbuf_t* ringbuf_init(void* buf, size_t size) {
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
