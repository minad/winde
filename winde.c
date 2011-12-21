#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#define VERSION                 "0"
#define UART_BAUD_RATE          9600

// Output port
#define PORT(x)                 _PORT(x)
// Data direction register
#define DDR(x)                  _DDR(x)
// Input port
#define PIN(x)                  _PIN(x)
#define BIT(x)                  _BIT(x)
#define RESET(x)                _RESET(x)
#define SET(x)                  _SET(x)
#define TOGGLE(x)               _TOGGLE(x)
#define SET_OUTPUT(x)           _SET_OUTPUT(x)
#define SET_INPUT(x)            _SET_INPUT(x)
#define SET_INPUT_PULLUP(x)     do { _SET_INPUT(x); _SET(x); } while (0)
#define IS_SET(x)               _IS_SET(x)

#define _PORT(x,y)              PORT ## x
#define _DDR(x,y)               DDR ## x
#define _PIN(x,y)               PIN ## x
#define _BIT(x,y)               y
#define _RESET(x,y)             _PORT(x,y) &= ~(1 << y)
#define _SET(x,y)               _PORT(x,y) |= (1 << y)
#define _TOGGLE(x,y)            _PORT(x,y) ^= (1 << y)
#define _SET_OUTPUT(x,y)        _DDR(x,y) |= (1 << y)
#define _SET_INPUT(x,y)         _DDR(x,y) &= ~(1 << y)
#define _IS_SET(x,y)            ((_PIN(x,y) & (1 << y)) != 0)

#define OUT_LED1                A,7
#define OUT_LED2                A,6
#define OUT_LED3                A,5
#define OUT_LED4                A,4
#define OUT_LED5                A,3
#define OUT_LED6                A,2
#define OUT_LED7                A,1
#define OUT_LED8                A,0
#define OUT_BUZZER              F,2

#define OUT_SYSTEM1             C,2
#define OUT_SYSTEM2             C,3
#define OUT_SYSTEM3             C,4
#define OUT_SYSTEM4             C,5
#define OUT_SYSTEM5             C,6
#define OUT_SYSTEM6             C,7

#define OUT_ZUENDUNGSBRUECKE    C,1
#define OUT_ZUENDUNG_AN         C,0
#define OUT_LATCH_DISABLE       B,6

#define IN_SCHALTER1            E,2
#define IN_SCHALTER2            E,3
#define IN_SCHALTER3            E,4
#define IN_SCHALTER4            E,5
#define IN_SCHALTER5            E,6
#define IN_SCHALTER6            B,5

#define IN_SYSTEM1              D,7 // Schließer
#define IN_SYSTEM2              D,6
#define IN_SYSTEM3              D,5
#define IN_SYSTEM4              D,4
#define IN_SYSTEM5              D,3
#define IN_SYSTEM6              D,2
#define IN_SYSTEM7              D,1
#define IN_SYSTEM8              D,0 // Öffner
#define IN_SYSTEM9              B,7 // Öffner

#define OUT_LED_EINGEKUPPELT1   OUT_LED1
#define OUT_LED_EINGEKUPPELT2   OUT_LED1
#define OUT_LED_HANDBREMSE      OUT_LED3
#define OUT_LED_KAPPVORRICHTUNG OUT_LED4
#define OUT_LED_TEMPERATUR      OUT_LED5
#define OUT_LED_POWER           OUT_LED6

#define OUT_GASSPERRE           OUT_SYSTEM4
#define OUT_TROMMELBREMSE       OUT_SYSTEM5
#define OUT_AUSZUGSBREMSE       OUT_SYSTEM6

#define IN_SCHALTER_TROMMEL1    IN_SCHALTER1
#define IN_SCHALTER_TROMMEL2    IN_SCHALTER2
#define IN_SCHALTER_BREMSE_AUF  IN_SCHALTER3
#define IN_SCHALTER_AUSKUPPELN  IN_SCHALTER5

#define IN_BREMSE_GETRETEN      IN_SYSTEM1
#define IN_GANGWARNUNG          IN_SYSTEM2
#define IN_DREHZAHL1            IN_SYSTEM3
#define IN_DREHZAHL2            IN_SYSTEM4
#define IN_TEMPERATUR_MOTOR     IN_SYSTEM5
#define IN_TEMPERATUR_WANDLER   IN_SYSTEM6
#define IN_HANDBREMSE           IN_SYSTEM8

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

void cmd_exec(char*);

ringbuf_t* ringbuf_init(void* buf, size_t size);
void ringbuf_reset(ringbuf_t* rb);
int ringbuf_full(ringbuf_t* rb);
int ringbuf_empty(ringbuf_t* rb);
int ringbuf_putc(ringbuf_t* rb, char c);
int ringbuf_getc(ringbuf_t* rb);

void   uart_init(uint32_t baud);
int    uart_putchar(char c, FILE* fp);
int    uart_getc();

void cmd_handler();
void cmd_help(int argc, char* argv[]);
void cmd_version(int argc, char* argv[]);

ringbuf_t *uart_rx_ringbuf, *uart_tx_ringbuf;
char       uart_rx_buf[256], uart_tx_buf[256];

FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, 0, _FDEV_SETUP_WRITE);

cmd_t cmd_list[] = {
        { "help",    cmd_help    },
        { "version", cmd_version },
        { 0,         0           },
};

int main() {
        system_init();
        printf("\n--------------------\n"
               "Steuersoftware Winde"
               "\n--------------------\n");
        for (;;) {
                cmd_handler();
        }
        return 0;
}

void system_init() {
        OSCCAL = 0xA1;

        SET_OUTPUT(OUT_LED1);
        SET_OUTPUT(OUT_LED2);
        SET_OUTPUT(OUT_LED3);
        SET_OUTPUT(OUT_LED4);
        SET_OUTPUT(OUT_LED5);
        SET_OUTPUT(OUT_LED6);
        SET_OUTPUT(OUT_LED7);
        SET_OUTPUT(OUT_LED8);
        SET_OUTPUT(OUT_BUZZER);

        SET_OUTPUT(OUT_SYSTEM1);
        SET_OUTPUT(OUT_SYSTEM2);
        SET_OUTPUT(OUT_SYSTEM3);
        SET_OUTPUT(OUT_SYSTEM4);
        SET_OUTPUT(OUT_SYSTEM5);
        SET_OUTPUT(OUT_SYSTEM6);

        SET_OUTPUT(OUT_ZUENDUNGSBRUECKE);
        SET_OUTPUT(OUT_ZUENDUNG_AN);
        SET_OUTPUT(OUT_LATCH_DISABLE);

        uart_init(UART_BAUD_RATE);
        sei();
}

void cmd_handler() {
        static char line[64];
        static int size = -1;
        if (size < 0) {
                printf("> ");
                size = 0;
        }
        int c = uart_getc();
        if (c == EOF) {
                if (c == '\n') {
                        line[size] = 0;
                        cmd_exec(line);
                        size = -1;
                } else if (size + 1 < sizeof (line)) {
                        line[size++] = c;
                }
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
