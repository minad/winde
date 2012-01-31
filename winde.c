#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include "pp.h"

#define BAUD           9600
#define MAX_ARGS       4
#define RINGBUF_RXSIZE 32
#define RINGBUF_TXSIZE 64
#define LINE_SIZE      64

#define NELEM(a) (sizeof (a) / sizeof (a[0]))

typedef struct {
        int8_t read, write, size;
        char   buf[0];
} ringbuf_t;

typedef struct {
        void (*fn)(int, char*[]);
        const char *name, *args, *help;
} cmd_t;

inline void  ports_init();
void         ports_reset();
inline void  ports_read();
inline void  ports_write();

void         state_update();
const char*  state_str(char);
void         state_transition(char);

ringbuf_t*   ringbuf_init(void* buf, int8_t size);
inline int   ringbuf_full(ringbuf_t* rb);
inline int   ringbuf_empty(ringbuf_t* rb);
int          ringbuf_putc(ringbuf_t* rb, char c);
int          ringbuf_getc(ringbuf_t* rb);

inline void  uart_init();
int          uart_putchar(char c, FILE* fp);
inline int   uart_getc();

void         usage();
int          check_manual();
void         print_version();
void         cmd_handler();
inline void  cmd_exec(char*);
const cmd_t* cmd_find(const char*, cmd_t*);
void         cmd_in(int argc, char* argv[]);
void         cmd_out(int argc, char* argv[]);
void         cmd_on_off(int argc, char* argv[]);
void         cmd_mode(int argc, char* argv[]);
void         cmd_reset(int argc, char* argv[]);
void         cmd_help(int argc, char* argv[]);
void         cmd_version(int argc, char* argv[]);

ringbuf_t *uart_rxbuf, *uart_txbuf;

#define DEF_PSTR(name, str) const prog_char PSTR_##name[] = str;

DEF_PSTR(TABLE_FORMAT, "%-20S | %-24S | %-4S | %S\n")
DEF_PSTR(X,            "X")
DEF_PSTR(EMPTY,        "")
DEF_PSTR(NAME,         "Name")
DEF_PSTR(ALIAS,        "Alias")
DEF_PSTR(PORT,         "Port")
DEF_PSTR(ACTIVE,       "Active")

#define COMMAND(name, fn, args, help) \
        DEF_PSTR(cmd_##name##_name, #name) \
        DEF_PSTR(cmd_##name##_args, args)  \
        DEF_PSTR(cmd_##name##_help, help)
#define OUT(name, port, bit, alias) \
        DEF_PSTR(out_##name##_name, #name) \
        IF_EMPTY(alias, , DEF_PSTR(out_##name##_alias, #alias))
#include "config.h"

const cmd_t PROGMEM cmd_list[] = {
#define COMMAND(name, fn, args, help) { cmd_##fn, PSTR_cmd_##name##_name, PSTR_cmd_##name##_args, PSTR_cmd_##name##_help },
#include "config.h"
};
const cmd_t* current_cmd;

union {
        struct {
#define IN(name, port, bit, alias) int name : 1;
#include "config.h"
        };
        struct {
#define IN(name, port, bit, alias) int alias : 1;
#include "config.h"
        };
} in;

union {
        struct {
#define OUT(name, port, bit, alias) int name : 1;
#include "config.h"
        };
        struct {
#define OUT(name, port, bit, alias) int alias : 1;
#include "config.h"
        };
} out;

char manual = 1, state = 0, show_prompt = 1;

enum {
#define STATE(name, attrs) STATE_##name,
#include "config.h"
};

inline void action_() {}
#define ACTION(name, code) inline void action_##name() { code }
#include "config.h"

int main() {
        OSCCAL = 0xA1;
        ports_init();
        uart_init();
        wdt_enable(WDTO_15MS);
        sei();
        print_version();
        for (;;) {
                ports_read();
                state_update();
                ports_write();
                cmd_handler();
                wdt_reset();
        }
        return 0;
}

inline void ports_init() {
#define OUT(name, port, bit, alias) DDR ## port |= (1 << bit);
#include "config.h"
}

void ports_reset() {
#define OUT(name, port, bit, alias) out.name = 0;
#include "config.h"
}

inline void ports_read() {
#define IN(name, port, bit, alias) in.name = (PIN ## port >> bit) & 1;
#include "config.h"
}

inline void ports_write() {
#define OUT(name, port, bit, alias) \
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

void state_transition(char new_state) {
        printf_P(PSTR("\n%S -> %S\n"), state_str(state), state_str(new_state));
        state = new_state;
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

#define TRANSITION(initial, event, final, act, attrs) \
        if (state == STATE_##initial && (event)) { state_transition(STATE_##final); action_##act(); return; }
#include "config.h"
}

void usage() {
        cmd_t cmd;
        memcpy_P(&cmd, current_cmd, sizeof (cmd_t));
        printf_P(PSTR("Usage: %S %S\n"), cmd.name, cmd.args);
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

inline void cmd_exec(char* line) {
        char *argv[MAX_ARGS];
        int argc;
        cmd_t cmd;
        for (argc = 0; argc < MAX_ARGS; ++argc) {
                if (!(argv[argc] = strsep_P(&line, PSTR(" \t"))) || *argv[argc] == '\0')
                        break;
        }
        if (argc > 0 && (current_cmd = cmd_find(argv[0], &cmd)))
                cmd.fn(argc, argv);
}

const cmd_t* cmd_find(const char* name, cmd_t* cmd) {
        for (int i = 0; i < NELEM(cmd_list); ++i) {
                memcpy_P(cmd, cmd_list + i, sizeof (cmd_t));
                if (!strcmp_P(name, cmd->name))
                        return cmd_list + i;
        }
        printf_P(PSTR("Command not found: %s\n"), name);
        return 0;
}

void cmd_handler() {
        static char line[LINE_SIZE];
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
        printf_P(PSTR_TABLE_FORMAT, PSTR_NAME, PSTR_ALIAS, PSTR_PORT, PSTR_ACTIVE);
#define IN(name, port, bit, alias) \
        printf_P(PSTR_TABLE_FORMAT, PSTR(#name), IF_EMPTY(alias, PSTR_EMPTY, PSTR(#alias)), PSTR(#port#bit), in.name ? PSTR_X : PSTR_EMPTY);
#include "config.h"
        putchar('\n');
}

void cmd_out(int argc, char* argv[]) {
        if (argc != 1)
                return usage();
        printf_P(PSTR("Outputs:\n"));
        printf_P(PSTR_TABLE_FORMAT, PSTR_NAME, PSTR_ALIAS, PSTR_PORT, PSTR_ACTIVE);
#define OUT(name, port, bit, alias) \
        printf_P(PSTR_TABLE_FORMAT, PSTR_out_##name##_name, IF_EMPTY(alias, PSTR_EMPTY, PSTR_out_##name##_alias), PSTR(#port#bit), out.name ? PSTR_X : PSTR_EMPTY);
#include "config.h"
        putchar('\n');
}

void cmd_on_off(int argc, char* argv[]) {
        if (argc != 2)
                return usage();
        if (check_manual()) {
                int on = strcmp_P(argv[0], PSTR("on")) ? 0 : 1;
#define OUT(name, port, bit, alias) \
                if (!strcmp_P(argv[1], PSTR_out_##name##_name) || \
                    IF_EMPTY(alias, 0, !strcmp_P(argv[1], PSTR_out_##name##_alias))) \
                { out.name = on; return; }
#include "config.h"
        }
}

void cmd_mode(int argc, char* argv[]) {
        if (argc == 2 && !strcmp_P(argv[1], PSTR("m"))) {
                manual = 1;
        } else if (argc == 2 && !strcmp_P(argv[1], PSTR("a"))) {
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
        cmd_t cmd;
        if (argc == 1) {
                printf_P(PSTR("List of commands:\n"));
                for (int i = 0; i < NELEM(cmd_list); ++i) {
                        memcpy_P(&cmd, cmd_list + i, sizeof (cmd_t));
                        printf_P(PSTR("  %20S %S\n"), cmd.name, cmd.help);
                }
                putchar('\n');
        } else if (argc == 2) {
                if (cmd_find(argv[1], &cmd))
                        printf_P(PSTR("Usage: %S %S\n%S\n"), cmd.name, cmd.args, cmd.help);
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

inline int ringbuf_full(ringbuf_t* rb) {
        return (rb->read == (rb->write + 1) % rb->size);
}

inline int ringbuf_empty(ringbuf_t* rb) {
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

void uart_init() {
#include <util/setbaud.h>
        UBRR0H = UBRRH_VALUE;
        UBRR0L = UBRRL_VALUE;
#if USE_2X
        UCSR0A |= (1 << U2X);
#else
        UCSR0A &= ~(1 << U2X);
#endif

        // set frame format: 8 bit, no parity, 1 stop bit
        UCSR0C = (1 << UCSZ1) | (1 << UCSZ0);
        // enable serial receiver and transmitter
        UCSR0B = (1 << RXEN) | (1 << TXEN) | (1 << RXCIE);

        static char rxbuf[RINGBUF_RXSIZE], txbuf[RINGBUF_TXSIZE];
        uart_rxbuf = ringbuf_init(rxbuf, sizeof (rxbuf));
        uart_txbuf = ringbuf_init(txbuf, sizeof (txbuf));

        static FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, 0, _FDEV_SETUP_WRITE);
        stdout = &uart_stdout;
}

int uart_putchar(char c, FILE* fp) {
        if (c == '\n')
                uart_putchar('\r', fp);
        while (ringbuf_full(uart_txbuf)) {
                wdt_reset(); // wait, reset watchdog
        }
        ringbuf_putc(uart_txbuf, c);
        UCSR0B |= (1 << UDRIE);
        return 0;
}

inline int uart_getc() {
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
