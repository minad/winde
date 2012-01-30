#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#define UART_BAUD_RATE 9600
#define MAX_ARGS       4

typedef struct {
        int8_t read, write, size;
        char   buf[0];
} ringbuf_t;

typedef struct {
        void (*fn)(int, char*[]);
        char *name, *args, *help;
} cmd_t;

void ports_init();
void ports_reset();
void ports_read();
void ports_write();
void state_update();
const char* state_str(char);
void state_set(char);

ringbuf_t* ringbuf_init(void* buf, int8_t size);
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

ringbuf_t *uart_rxbuf, *uart_txbuf;

const prog_char S_TABLE_FORMAT[] = "%-20S | %-24S | %-4S | %S\n";
const prog_char S_X[]            = "X";
const prog_char S_EMPTY[]        = "";
const prog_char S_NAME[]         = "Name";
const prog_char S_ALIAS[]        = "Alias";
const prog_char S_PORT[]         = "Port";
const prog_char S_ACTIVE[]       = "Active";

const cmd_t cmd_list[] = {
        { cmd_in,      "in",      "",       "Print list of input ports"  },
        { cmd_out,     "out",     "",       "Print list of output ports" },
        { cmd_on_off,  "on",      "<port>", "Set port on"                },
        { cmd_on_off,  "off",     "<port>", "Set port off"               },
        { cmd_mode,    "mode",    "[a|m]",  "Set automatic/manual mode"  },
        { cmd_reset,   "reset",   "",       "Reset system"               },
        { cmd_help,    "help",    "[cmd]",  "Print this help"            },
        { cmd_version, "version", "",       "Print version"              },
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

#define ACTION(name, code) static inline void action_##name() { code }
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
#define STATE(name, attrs) case STATE_##name: return PSTR(#name);
#include "config.h"
        default: return PSTR("invalid");
        }
}

void state_set(char s) {
        state = s;
        printf_P(PSTR("\nState %S\n"), state_str(state));
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
                     (state != STATE_aufbau_ok && in.schalter_auszugsbremse_auf) ||
                     (state != STATE_links_eingekuppelt && state != STATE_rechts_eingekuppelt && in.schalter_auskuppeln);

#define TRANS_ACTION(initial, event, final, attrs, act) \
        if (state == STATE_##initial && (event)) { state_set(STATE_##final); action_##act(); return; }
#define TRANS(initial, event, final, attrs) \
        if (state == STATE_##initial && (event)) { state_set(STATE_##final); return; }
#include "config.h"
}

void usage() {
        printf_P(PSTR("Usage: %s %s\n"), current_cmd->name, current_cmd->args);
}

int check_manual() {
        if (!manual)
                printf_P(PSTR("Enable manual mode first!\n"));
        return manual;
}

void print_version() {
        printf_P(PSTR("\nSteuersoftware der Winde AFK-3\n"
                      "  Version:       " VERSION "\n"
                      "  Git-Version:   " GIT_VERSION "\n"
                      "  Kompiliert am: " __DATE__ " " __TIME__ "\n"
                      "  Elektronik:    Christian 'Paule' Schreiber\n"
                      "  Software:      Daniel 'Teilchen' Mendler\n\n"));
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
        for (const cmd_t* cmd = cmd_list; cmd->fn; ++cmd) {
                if (!strcmp(cmd->name, name))
                        return cmd;
        }
        printf_P(PSTR("Command not found: %s\n"), name);
        return 0;
}

void cmd_handler() {
        static char line[64];
        static int size = 0;
        if (show_prompt) {
                printf_P(PSTR("%S> "), manual ? PSTR("manual") : state_str(state));
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
        printf_P(PSTR("Inputs:\n"));
        printf_P(S_TABLE_FORMAT, S_NAME, S_ALIAS, S_PORT, S_ACTIVE);
#define IN(name, port, bit) \
        printf_P(S_TABLE_FORMAT, PSTR(#name), S_EMPTY, PSTR(#port#bit), in.name ? S_X : S_EMPTY);
#define IN_ALIAS(name, port, bit, alias) \
        printf_P(S_TABLE_FORMAT, PSTR(#name), PSTR(#alias), PSTR(#port#bit), in.name ? S_X : S_EMPTY);
#include "config.h"
        putchar('\n');
}

void cmd_out(int argc, char* argv[]) {
        if (argc != 1)
                return usage();
        printf_P(PSTR("Outputs:\n"));
        printf_P(S_TABLE_FORMAT, S_NAME, S_ALIAS, S_PORT, S_ACTIVE);
#define OUT(name, port, bit) \
        printf_P(S_TABLE_FORMAT, PSTR(#name), S_EMPTY, PSTR(#port#bit), out.name ? S_X : S_EMPTY);
#define OUT_ALIAS(name, port, bit, alias) \
        printf_P(S_TABLE_FORMAT, PSTR(#name), PSTR(#alias), PSTR(#port#bit), out.name ? S_X : S_EMPTY);
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
                printf_P(PSTR("List of commands:\n"));
                for (const cmd_t* cmd = cmd_list; cmd->fn; ++cmd)
                        printf_P(PSTR("  %20s %s\n"), cmd->name, cmd->help);
                putchar('\n');
        } else if (argc == 2) {
                const cmd_t* cmd = cmd_find(argv[1]);
                if (cmd)
                        printf_P(PSTR("Usage: %s %s\n%s\n"), cmd->name, cmd->args, cmd->help);
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
	rb->read = rb->write = 0;
	return rb;
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

        static char rxbuf[32], txbuf[32];
        uart_rxbuf = ringbuf_init(rxbuf, sizeof (rxbuf));
        uart_txbuf = ringbuf_init(txbuf, sizeof (txbuf));

        static FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, 0, _FDEV_SETUP_WRITE);
        stdout = &uart_stdout;
}

int uart_putchar(char c, FILE* fp) {
        if (c == '\n')
                uart_putchar('\r', fp);
        while (ringbuf_full(uart_txbuf)) {} // wait
        ringbuf_putc(uart_txbuf, c);
        UCSR0B |= (1 << UDRIE);
        return 0;
}

int uart_getc() {
        return ringbuf_getc(uart_rxbuf);
}

ISR(USART0_RX_vect) {
        ringbuf_putc(uart_rxbuf, UDR0);
}

ISR(USART0_UDRE_vect) {
        if (!ringbuf_empty(uart_txbuf))
                UDR0 = ringbuf_getc(uart_txbuf);
        else
                UCSR0B &= ~(1 << UDRIE);
}
