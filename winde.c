#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "pp.h"

#define BAUD           19200
#define MAX_ARGS       4
#define RINGBUF_RXSIZE 16
#define RINGBUF_TXSIZE 64
#define LINE_SIZE      80

#define NELEM(a) (sizeof (a) / sizeof (a[0]))
// inline can be commented out to check function size with avr-nm
#define INLINE   inline

typedef volatile struct {
        uint8_t read, write, size;
        char   buf[0];
} ringbuf_t;

typedef struct {
        void (*fn)(int, char*[]);
        const char *name, *args, *help;
} cmd_t;

typedef struct {
        const char *name, *alias, *port;
} port_t;

INLINE int   bitfield_get(const uint8_t* bitfield, size_t i);
INLINE void  bitfield_set(uint8_t* bitfield, size_t i, uint8_t set);

INLINE void  ports_init();
void         ports_reset();
INLINE void  ports_read();
INLINE void  ports_write();
void         ports_print(const port_t* ports, const uint8_t* bitfield, size_t n);

void         state_update();
const char*  state_str(uint8_t);
void         state_transition(uint8_t);

INLINE ringbuf_t* ringbuf_init(void* buf, uint8_t size);
INLINE int   ringbuf_full(ringbuf_t* rb);
INLINE int   ringbuf_empty(ringbuf_t* rb);
int          ringbuf_putc(ringbuf_t* rb, char c);
int          ringbuf_getc(ringbuf_t* rb);

INLINE void  uart_init();
int          uart_putchar(char c, FILE* fp);
char*        uart_gets();

void         backspace();
int          usage(int, int, char*[]);
int          check_manual();
void         print_version();
void         cmd_handler();
INLINE void  cmd_exec(char*);
const cmd_t* cmd_find(const char*, cmd_t*);
void         cmd_in(int argc, char* argv[]);
void         cmd_out(int argc, char* argv[]);
void         cmd_on_off(int argc, char* argv[]);
void         cmd_mode(int argc, char* argv[]);
void         cmd_reset(int argc, char* argv[]);
void         cmd_help(int argc, char* argv[]);
void         cmd_version(int argc, char* argv[]);

ringbuf_t *uart_rxbuf, *uart_txbuf;

#define DEF_PSTR(name, str) const char PSTR_##name[] PROGMEM = str;

DEF_PSTR(PORT_FORMAT, "%-18S | %-28S | %-4S | %S\n")
DEF_PSTR(X,           "X")
DEF_PSTR(EMPTY,       "")
DEF_PSTR(NAME,        "Name")
DEF_PSTR(ALIAS,       "Alias")
DEF_PSTR(PORT,        "Port")
DEF_PSTR(ACTIVE,      "Active")

#define COMMAND(name, fn, args, help) \
        DEF_PSTR(cmd_##name##_name, #name) \
        DEF_PSTR(cmd_##name##_args, args) \
        DEF_PSTR(cmd_##name##_help, help)
#define OUT(name, port, bit, alias) \
        DEF_PSTR(out_##name##_name, #name) \
        DEF_PSTR(out_##name##_port, #port#bit) \
        IF_EMPTY(alias,, DEF_PSTR(out_##name##_alias, #alias))
#define IN(name, port, bit, alias) \
        DEF_PSTR(in_##name##_name, #name) \
        DEF_PSTR(in_##name##_port, #port#bit) \
        IF_EMPTY(alias,, DEF_PSTR(in_##name##_alias, #alias))
#include "config.h"

const cmd_t PROGMEM cmd_list[] = {
#define COMMAND(name, fn, args, help) { cmd_##fn, PSTR_cmd_##name##_name, PSTR_cmd_##name##_args, PSTR_cmd_##name##_help },
#include "config.h"
};

union {
        struct {
#define IN(name, port, bit, alias) uint8_t name  : 1;
#include "config.h"
        };
        struct {
#define IN(name, port, bit, alias) uint8_t alias : 1;
#include "config.h"
        };
        uint8_t bitfield[0];
} in;

union {
        struct {
#define IN(name, port, bit, alias) uint8_t name  : 1;
#include "config.h"
        };
        struct {
#define IN(name, port, bit, alias) uint8_t alias : 1;
#include "config.h"
        };
        uint8_t bitfield[0];
} last_in;

union {
        struct {
#define OUT(name, port, bit, alias) uint8_t name  : 1;
#include "config.h"
        };
        struct {
#define OUT(name, port, bit, alias) uint8_t alias : 1;
#include "config.h"
        };
        uint8_t bitfield[0];
} out;

const port_t PROGMEM in_list[] = {
#define IN(name, port, bit, alias) \
        { PSTR_in_##name##_name, IF_EMPTY(alias, 0, PSTR_in_##name##_alias), PSTR_in_##name##_port },
#include "config.h"
};

const port_t PROGMEM out_list[] = {
#define OUT(name, port, bit, alias) \
        { PSTR_out_##name##_name, IF_EMPTY(alias, 0, PSTR_out_##name##_alias), PSTR_out_##name##_port },
#include "config.h"
};

uint8_t manual = 0, state = 0, last_state = 0, prompt_active = 0;

enum {
#define STATE(name, attrs) STATE_##name,
#include "config.h"
};

#define ACTION(name, code) INLINE void action_##name() { code }
#include "config.h"

int main() {
        OSCCAL = 0xA1;
        ports_init();
        uart_init();
        sei();
        wdt_enable(WDTO_500MS);
        print_version();
        for (;;) {
                ports_read();
                state_update();
                ports_write();
                if (state == last_state)
                        cmd_handler();
                wdt_reset();
        }
        return 0;
}

INLINE int bitfield_get(const uint8_t* bitfield, size_t i) {
        return (bitfield[i >> 3] >> (i & 7)) & 1;
}

INLINE void bitfield_set(uint8_t* bitfield, size_t i, uint8_t set) {
        if (set)
                bitfield[i >> 3] |= (1 << (i & 7));
        else
                bitfield[i >> 3] &= ~(1 << (i & 7));
}

INLINE void ports_init() {
        ports_reset();

#define OUT(name, port, bit, alias) DDR ## port |= (1 << bit);
#include "config.h"
}

void ports_reset() {
        // Hack: Latch anschalten
        // Vorgaukeln, dass auskuppeln gedrückt und Bremse getreten wird
        DDRD |= (1 << 7);
        DDRE |= (1 << 6);
        PORTD |= (1 << 7);
        PORTE |= (1 << 6);
        PORTB &= ~(1 << 6);
        _delay_ms(50);
        PORTD &= ~(1 << 7);
        PORTE &= ~(1 << 6);
        DDRD &= ~(1 << 7);
        DDRE &= ~(1 << 6);

        memset(&out, 0, sizeof (out));
}

INLINE void ports_read() {
        memcpy(&last_in, &in, sizeof (in));
#define IN(name, port, bit, alias) in.name = (PIN ## port >> bit) & 1;
#include "config.h"
}

INLINE void ports_write() {
#define OUT(name, port, bit, alias) \
        if (out.name) { PORT ## port |= (1 << bit); } \
        else { PORT ## port &= ~(1 << bit); }
#include "config.h"
}

const char* state_str(uint8_t state) {
        switch (state) {
#define STATE(name, attrs) case STATE_##name: return PSTR(#name);
#include "config.h"
        default: return PSTR("invalid");
        }
}

void state_transition(uint8_t new_state) {
        if (prompt_active) {
                putchar('\n');
                prompt_active = 0;
        }
        printf_P(PSTR("%S -> %S\n"), state_str(state), state_str(new_state));
        state = new_state;
}

void state_update() {
        if (manual)
                return;

        last_state = state;

#define EVENT(name, condition) int name = (condition);
#include "config.h"

        out.led_parkbremse = !in.parkbremse_gezogen;
        out.led_kappvorrichtung = in.kappvorrichtung_falsch;
        out.led_gangwarnung = in.gang_falsch;
        out.led_power = in.motor_aus;
        out.drehlampe = out.einkuppeln_links || out.einkuppeln_rechts;
        out.drehlampe = out.einkuppeln_links || out.einkuppeln_rechts;

#define RISING_EDGE(name)  (!last_in.name && in.name)

        if (state != STATE_bremse_getreten) {
                if (RISING_EDGE(schalter_einkuppeln_links) || RISING_EDGE(schalter_einkuppeln_rechts))
                        out.buzzer = 1;
                else if (!in.schalter_einkuppeln_links || !in.schalter_einkuppeln_rechts)
                        out.buzzer = 0;
        } else if (state != STATE_links_eingekuppelt && state != STATE_rechts_eingekuppelt) {
                if (RISING_EDGE(schalter_auskuppeln))
                        out.buzzer = 1;
                else if (!in.schalter_auskuppeln)
                        out.buzzer = 0;
        } else {
                out.buzzer = 0;
        }

#define TRANSITION(initial, event, final, act, attrs) \
        if (state == STATE_##initial && (event)) { state_transition(STATE_##final); IF_EMPTY(act,, action_##act()); return; }
#include "config.h"
}

void backspace() {
        putchar('\b');
        putchar(' ');
        putchar('\b');
}

int usage(int show, int argc, char* argv[]) {
        if (show || (argc == 2 && !strcmp_P(argv[1], PSTR("--help")))) {
                cmd_t cmd;
                cmd_find(argv[0], &cmd);
                printf_P(PSTR("Usage: %S %S\n"), cmd.name, cmd.args);
                return 1;
        }
        return 0;
}

int check_manual() {
        if (!manual)
                puts_P(PSTR("Enable manual mode first with command 'mode --manual'."));
        return manual;
}

void print_version() {
        puts_P(PSTR("\nSteuersoftware der Winde AFK-3\n"
                    "  Version:       " VERSION "\n"
                    "  Git-Version:   " GIT_VERSION "\n"
                    "  Kompiliert am: " __DATE__ " " __TIME__ "\n"
                    "  Elektronik:    Christian 'Paule' Schreiber\n"
                    "  Software:      Daniel 'Teilchen' Mendler\n"));
}

INLINE void cmd_exec(char* line) {
        char *argv[MAX_ARGS];
        int argc;
        cmd_t cmd;
        for (argc = 0; argc < MAX_ARGS; ++argc) {
                if (!(argv[argc] = strsep_P(&line, PSTR(" "))) || *argv[argc] == '\0')
                        break;
        }
        if (argc > 0 && cmd_find(argv[0], &cmd))
                cmd.fn(argc, argv);
}

const cmd_t* cmd_find(const char* name, cmd_t* cmd) {
        for (size_t i = 0; i < NELEM(cmd_list); ++i) {
                memcpy_P(cmd, cmd_list + i, sizeof (cmd_t));
                if (!strcmp_P(name, cmd->name))
                        return cmd_list + i;
        }
        printf_P(PSTR("Command not found: %s\n"), name);
        return 0;
}

void cmd_handler() {
        if (!prompt_active) {
                printf_P(PSTR("%S $ "), manual ? PSTR("MANUAL") : state_str(state));
                prompt_active = 1;
        }
        char* line = uart_gets();
        if (line) {
                cmd_exec(line);
                prompt_active = 0;
        }
}

void ports_print(const port_t* port_list, const uint8_t* bitfield, size_t n) {
        printf_P(PSTR_PORT_FORMAT, PSTR_NAME, PSTR_ALIAS, PSTR_PORT, PSTR_ACTIVE);
        for (size_t i = 0; i < n; ++i) {
                port_t port;
                memcpy_P(&port, port_list + i, sizeof (port_t));
                printf_P(PSTR_PORT_FORMAT, port.name, port.alias ? port.alias : PSTR_EMPTY,
                         port.port, bitfield_get(bitfield, i) ? PSTR_X : PSTR_EMPTY);
        }
        putchar('\n');
}

void cmd_in(int argc, char* argv[]) {
        if (usage(argc != 1, argc, argv))
                return;
        puts_P(PSTR("Inputs:"));
        ports_print(in_list, in.bitfield, NELEM(in_list));
}

void cmd_out(int argc, char* argv[]) {
        if (usage(argc != 1, argc, argv))
                return;
        puts_P(PSTR("Outputs:"));
        ports_print(out_list, out.bitfield, NELEM(out_list));
}

void cmd_on_off(int argc, char* argv[]) {
        if (usage(argc != 2, argc, argv))
                return;
        if (check_manual()) {
                for (size_t i = 0; i < NELEM(out_list); ++i) {
                        port_t port;
                        memcpy_P(&port, out_list + i, sizeof (port_t));
                        if (!strcmp_P(argv[1], port.name) ||
                            (port.alias && !strcmp_P(argv[1], port.alias))) {
                                bitfield_set(out.bitfield, i, !strcmp_P(argv[0], PSTR_cmd_on_name));
                                return;
                        }
                }
                printf_P(PSTR("Output not found: %s\n"), argv[1]);
        }
}

void cmd_mode(int argc, char* argv[]) {
        if (usage(0, argc, argv)) {
                // nothing
        } else if (argc == 2 && !strcmp_P(argv[1], PSTR("--manual"))) {
                manual = 1;
                last_state = state = 0;
        } else if (argc == 2 && !strcmp_P(argv[1], PSTR("--auto"))) {
                manual = last_state = state = 0;
                ports_reset();
        } else if (argc == 1) {
                printf_P(PSTR("%S mode is active\n"), manual ? PSTR("Manual") : PSTR("Automatic"));
        } else {
                usage(1, argc, argv);
        }
}

void cmd_reset(int argc, char* argv[]) {
        if (usage(argc != 1, argc, argv))
                return;
        if (check_manual())
                ports_reset();
}

void cmd_help(int argc, char* argv[]) {
        cmd_t cmd;
        if (usage(0, argc, argv)) {
                // nothing
        } else if (argc == 1) {
                puts_P(PSTR("List of commands:"));
                for (size_t i = 0; i < NELEM(cmd_list); ++i) {
                        memcpy_P(&cmd, cmd_list + i, sizeof (cmd_t));
                        printf_P(PSTR("  %-16S %S\n"), cmd.name, cmd.help);
                }
                putchar('\n');
        } else if (argc == 2) {
                if (cmd_find(argv[1], &cmd))
                        printf_P(PSTR("Usage: %S %S\n%S\n"), cmd.name, cmd.args, cmd.help);
        } else {
                usage(1, argc, argv);
        }
}

void cmd_version(int argc, char* argv[]) {
        if (usage(argc != 1, argc, argv))
                return;
        print_version();
}

INLINE ringbuf_t* ringbuf_init(void* buf, uint8_t size) {
	ringbuf_t *rb = (ringbuf_t*)buf;
	rb->size = size - sizeof(ringbuf_t);
	rb->read = rb->write = 0;
	return rb;
}

INLINE int ringbuf_full(ringbuf_t* rb) {
        return rb->read == (rb->write + 1) % rb->size;
}

INLINE int ringbuf_empty(ringbuf_t* rb) {
        return rb->read == rb->write;
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
        while (ringbuf_full(uart_txbuf))
                wdt_reset(); // wait, reset watchdog
        ringbuf_putc(uart_txbuf, c);
        UCSR0B |= (1 << UDRIE);
        return 0;
}

char* uart_gets() {
        static char line[LINE_SIZE];
        static size_t size = 0;
        int c = ringbuf_getc(uart_rxbuf);
        switch (c) {
        case EOF:
                break;
        case '\b':   // backspace deletes the last character
        case '\x7f': // DEL
                if (size > 0) {
                        backspace();
                        --size;
                } else {
                        putchar('\a');
                }
                break;
        case '\r':
        case '\n':
                putchar('\n');
                line[size] = 0;
                size = 0;
                return line;
        case 'c' & 0x1F: // ^c prints newline and clears buffer
                putchar('\n');
                size = 0;
                break;
        case 'w' & 0x1F: // ^w kills the last word
                for (; size > 0 && line[size-1] != ' '; --size)
                        backspace();
                break;
        case 'u' & 0x1F: // ^u kills the entire buffer
                for (; size > 0; --size)
                        backspace();
                break;
        case '\t': // tab is replaced by space
                c = ' ';
                // fall through
        default:
                if (size + 1 < sizeof (line)) {
                        putchar(c);
                        line[size++] = c;
                } else {
                        putchar('\a');
                }
                break;
        }
        return 0;
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
