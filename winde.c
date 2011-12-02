#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#define VERSION "0"

// Set/get output/input pins
#define PORT(x)              _PORT(x)
#define DDR(x)               _DDR(x)
#define PIN(x)               _PIN(x)
#define BIT(x)               _BIT(x)
#define RESET(x)             _RESET(x)
#define SET(x)               _SET(x)
#define TOGGLE(x)            _TOGGLE(x)
#define SET_OUTPUT(x)        _SET_OUTPUT(x)
#define SET_INPUT(x)         _SET_INPUT(x)
#define SET_INPUT_PULLUP(x)  do { _SET_INPUT(x); _SET(x); } while (0)
#define IS_SET(x)            _IS_SET(x)

#define _PORT(x,y)           PORT ## x
#define _DDR(x,y)            DDR ## x
#define _PIN(x,y)            PIN ## x
#define _BIT(x,y)            y
#define _RESET(x,y)          _PORT(x,y) &= ~(1 << y)
#define _SET(x,y)            _PORT(x,y) |= (1 << y)
#define _TOGGLE(x,y)         _PORT(x,y) ^= (1 << y)
#define _SET_OUTPUT(x,y)     _DDR(x,y) |= (1 << y)
#define _SET_INPUT(x,y)      _DDR(x,y) &= ~(1 << y)
#define _IS_SET(x,y)         ((_PIN(x,y) & (1 << y)) != 0)

typedef struct {
	char *read;
	char *write;
	size_t size;
	char buf[0];
} ringbuf_t;

typedef struct {
        char* name;
        void (*fn)(int, char*[]);
} cmd_t;

void system_init();

void cmd_exec(char*);

ringbuf_t* ringbuf_init(void* buf, size_t size);
void ringbuf_reset(ringbuf_t* rb);
int ringbuf_putc(ringbuf_t* rb, char c);
int ringbuf_getc(ringbuf_t* rb);

void   uart_init(uint32_t bps);
int    uart_putchar(char c, FILE* fp);
size_t uart_gets(char* s, size_t size);

void cmd_handler();
void cmd_help(int argc, char* argv[]);
void cmd_version(int argc, char* argv[]);

ringbuf_t* uart_ringbuf;
char       uart_buf[512];

FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, 0, _FDEV_SETUP_WRITE);

cmd_t cmd_list[] = {
        { "help",    cmd_help    },
        { "version", cmd_version },
        { 0,         0           },
};

int main() {
        system_init();
        printf("Steuersoftware Winde\n");
        printf("> ");
        for (;;) {
                cmd_handler();
        }
        return 0;
}

void system_init() {
        uart_init(9600);
        sei();
}

void cmd_handler() {
        char line[64];
        if (uart_gets(line, sizeof (line)) > 0) {
                cmd_exec(line);
                printf("> ");
        }
}

void cmd_help(int argc, char* argv[]) {
        printf("List of available commands:\n");
        for (cmd_t* cmd = cmd_list; cmd->name; ++cmd)
                printf("%s\n", cmd->name);
        printf("\n");
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
                                break;
                        }
                }
        }
}

ringbuf_t* ringbuf_init(void* buf, size_t size) {
	ringbuf_t *rb = (ringbuf_t*)buf;
	rb->size = size - sizeof(ringbuf_t);
	ringbuf_reset(rb);
	return rb;
}

void ringbuf_reset(ringbuf_t* rb) {
	rb->read = rb->write = rb->buf;
        memset(rb->buf, 0, rb->size);
}

int ringbuf_putc(ringbuf_t* rb, char c) {
        if (rb->read == rb->write + 1)
                return EOF;
        *rb->write++ = c;
        if (rb->write == rb->buf + rb->size)
                rb->write = 0;
        return c;
}

int ringbuf_getc(ringbuf_t* rb) {
        if (rb->read == rb->write)
                return EOF;
        char c = *rb->read++;
        if (rb->read == rb->buf + rb->size)
                rb->read = 0;
        return c;
}

void uart_init(uint32_t bps) {
        uint32_t baud = F_CPU / (bps * 16) - 1;

        // Set baud rate
        UBRR0H = baud >> 8;
        UBRR0L = baud & 0xFF;

        // set frame format: 8 bit, no parity, 1 stop bit
        UCSR0C = (1 << UMSEL) | (1 << UCSZ1) | (1 << UCSZ0);
        // enable serial receiver and transmitter
        UCSR0B = (1 << RXEN) | (1 << TXEN) | (1 << RXCIE);

        uart_ringbuf = ringbuf_init(uart_buf, sizeof (uart_buf));

        stdout = &uart_stdout;

}

int uart_putchar(char c, FILE* fp) {
        // wait until transmit buffer is empty
        loop_until_bit_is_set(UCSR0A, UDRE);
        UDR0 = c;
        return 0;
}

size_t uart_gets(char* s, size_t size) {
        char *p = s, *end = s + size - 1;
        while (p < end) {
                int c = ringbuf_getc(uart_ringbuf);
                if (c == EOF || c == '\n')
                        break;
                *p++ = c;
        }
        *p = '\0';
        return p - s;
}

ISR(USART0_RX_vect) {
        ringbuf_putc(uart_ringbuf, UDR0);
}
