/**
 * @file
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "pp.h"

#define BAUD           19200
#define MAX_ARGS       2
#define RINGBUF_RXSIZE 16
#define RINGBUF_TXSIZE 64
#define LINE_SIZE      80

#define ARRAY_SIZE(array)      (sizeof (array) / sizeof (array[0]))
#define RISING_EDGE(name)      (!last_in.name && in.name)
#define DEF_PSTR(name, string) static const char PSTR_##name[] PROGMEM = string;
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
        const char *name, *alias, port[2];
} port_t;

INLINE int   bitfield_get(const uint8_t* bitfield, size_t i);
INLINE void  bitfield_set(uint8_t* bitfield, size_t i, uint8_t set);

INLINE void  ports_init();
void         ports_reset();
INLINE void  ports_read();
INLINE void  ports_write();
void         ports_print(const port_t* ports, const uint8_t* bitfield, size_t n);

uint8_t      state_update();
const char*  state_str(uint8_t state);

INLINE ringbuf_t* ringbuf_init(void* buf, uint8_t size);
INLINE int   ringbuf_full(ringbuf_t* rb);
INLINE int   ringbuf_empty(ringbuf_t* rb);
int          ringbuf_putc(ringbuf_t* rb, char c);
int          ringbuf_getc(ringbuf_t* rb);

INLINE void  uart_init();
int          uart_putchar(char c, FILE* fp);
char*        uart_gets();

void         backspace();
int          check_usage(int wrong, int argc, char* argv[]);
int          check_manual();
void         print_version();
void         cmd_handler();
INLINE void  cmd_exec(char*);
const cmd_t* cmd_find(const char* name, cmd_t* cmd);
void         cmd_usage(const char* name);
void         cmd_in(int argc, char* argv[]);
void         cmd_out(int argc, char* argv[]);
void         cmd_on_off(int argc, char* argv[]);
void         cmd_mode(int argc, char* argv[]);
void         cmd_reset(int argc, char* argv[]);
void         cmd_help(int argc, char* argv[]);
void         cmd_version(int argc, char* argv[]);

#define COMMAND(name, fn, args, help) \
        DEF_PSTR(cmd_##name##_name, #name) \
        DEF_PSTR(cmd_##name##_args, args) \
        DEF_PSTR(cmd_##name##_help, help)
#define OUT(name, port, bit, alias) \
        DEF_PSTR(out_##name##_name, #name) \
        IF_EMPTY(alias,, DEF_PSTR(out_##name##_alias, #alias))
#define IN(name, port, bit, alias) \
        DEF_PSTR(in_##name##_name, #name) \
        IF_EMPTY(alias,, DEF_PSTR(in_##name##_alias, #alias))
#include "generate.h"

enum {
#define STATE(name, attrs) STATE_##name,
#include "generate.h"
};

typedef union {
        struct {
#define IN(name, port, bit, alias) uint8_t name  : 1;
#include "generate.h"
        };
        struct {
#define IN(name, port, bit, alias) uint8_t alias : 1;
#include "generate.h"
        };
        uint8_t bitfield[0];
} in_t;

typedef union {
        struct {
#define OUT(name, port, bit, alias) uint8_t name  : 1;
#include "generate.h"
        };
        struct {
#define OUT(name, port, bit, alias) uint8_t alias : 1;
#include "generate.h"
        };
        uint8_t bitfield[0];
} out_t;

const port_t PROGMEM in_list[] = {
#define IN(name, port, bit, alias) \
        { PSTR_in_##name##_name, IF_EMPTY(alias, 0, PSTR_in_##name##_alias), #port#bit },
#include "generate.h"
};

const port_t PROGMEM out_list[] = {
#define OUT(name, port, bit, alias) \
        { PSTR_out_##name##_name, IF_EMPTY(alias, 0, PSTR_out_##name##_alias), #port#bit },
#include "generate.h"
};

const cmd_t PROGMEM cmd_list[] = {
#define COMMAND(name, fn, args, help) { cmd_##fn, PSTR_cmd_##name##_name, PSTR_cmd_##name##_args, PSTR_cmd_##name##_help },
#include "generate.h"
};

ringbuf_t *uart_rxbuf, *uart_txbuf;

in_t  in, last_in;
out_t out;

struct {
        uint8_t manual            : 1;
        uint8_t prompt_active     : 1;
        uint8_t fehler_einkuppeln : 1;
        uint8_t fehler_auskuppeln : 1;
} flag;

uint8_t state = 0;

#define ACTION(name, code) INLINE void action_##name() { code }
#include "generate.h"

int main() {
        OSCCAL = 0xA1;
        ports_init();
        uart_init();
        sei();
        print_version();
        for (;;) {
                ports_read();
                uint8_t new_state = state_update();
                if (new_state != state) {
                        if (flag.prompt_active) {
                                putchar('\n');
                                flag.prompt_active = 0;
                        }
                        printf_P(PSTR("%S -> %S\n"), state_str(state), state_str(new_state));
                        state = new_state;
                } else {
                        cmd_handler();
                }
                ports_write();
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
#include "generate.h"
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
        last_in = in;
#define IN(name, port, bit, alias) in.name = (PIN ## port >> bit) & 1;
#include "generate.h"
}

INLINE void ports_write() {
#define OUT(name, port, bit, alias) \
        if (out.name) { PORT ## port |= (1 << bit); } \
        else { PORT ## port &= ~(1 << bit); }
#include "generate.h"
}

const char* state_str(uint8_t state) {
        switch (state) {
#define STATE(name, attrs) case STATE_##name: return PSTR(#name);
#include "generate.h"
        }
        return 0;
}

uint8_t state_update() {
        if (flag.manual)
                return state;

        out.led_parkbremse = !in.parkbremse_gezogen;
        out.led_kappvorrichtung = in.kappvorrichtung_falsch;
        out.led_gangwarnung = in.gang_falsch;
        out.led_power = !in.motor_an;
        out.drehlampe = out.einkuppeln_links | out.einkuppeln_rechts;

        if (state != STATE_bremse_getreten) {
                if (RISING_EDGE(schalter_einkuppeln_links) || RISING_EDGE(schalter_einkuppeln_rechts))
                        flag.fehler_einkuppeln = 1;
                else if (!in.schalter_einkuppeln_links && !in.schalter_einkuppeln_rechts)
                        flag.fehler_einkuppeln = 0;
        } else {
                flag.fehler_einkuppeln = 0;
        }

        if (state != STATE_links_eingekuppelt && state != STATE_rechts_eingekuppelt &&
            state != STATE_schlepp_links && state != STATE_schlepp_rechts) {
                if (RISING_EDGE(schalter_auskuppeln))
                        flag.fehler_auskuppeln = 1;
                else if (!in.schalter_auskuppeln)
                        flag.fehler_auskuppeln = 0;
        } else {
                flag.fehler_auskuppeln = 0;
        }

        uint8_t fehler_state = state == STATE_fehler_motor_an || state == STATE_fehler_motor_aus;
        out.buzzer = flag.fehler_einkuppeln | flag.fehler_auskuppeln | fehler_state;

#define EVENT(name, condition) uint8_t name = (condition);
#include "generate.h"

#define TRANSITION(initial, event, final, act, attrs) \
        if (state == STATE_##initial && (event)) { IF_EMPTY(act,, action_##act()); return STATE_##final; }
#include "generate.h"

        return state;
}

void backspace() {
        putchar('\b');
        putchar(' ');
        putchar('\b');
}

int check_usage(int wrong, int argc, char* argv[]) {
        if (wrong || (argc == 2 && !strcmp_P(argv[1], PSTR("--help")))) {
                cmd_usage(argv[0]);
                return 0;
        }
        return 1;
}

void cmd_usage(const char* name) {
        cmd_t cmd;
        if (cmd_find(name, &cmd))
                printf_P(PSTR("Usage: %S %S\n%S\n"), cmd.name, cmd.args, cmd.help);
}

int check_manual() {
        if (!flag.manual)
                puts_P(PSTR("Enable manual mode first with command 'mode --manual'."));
        return flag.manual;
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
        for (size_t i = 0; i < ARRAY_SIZE(cmd_list); ++i) {
                memcpy_P(cmd, cmd_list + i, sizeof (cmd_t));
                if (!strcmp_P(name, cmd->name))
                        return cmd_list + i;
        }
        printf_P(PSTR("Command not found: %s\n"), name);
        return 0;
}

void cmd_handler() {
        if (!flag.prompt_active) {
                printf_P(PSTR("%S $ "), flag.manual ? PSTR("MANUAL") : state_str(state));
                flag.prompt_active = 1;
        }
        char* line = uart_gets();
        if (line) {
                cmd_exec(line);
                flag.prompt_active = 0;
        }
}

void ports_print(const port_t* port_list, const uint8_t* bitfield, size_t n) {
        printf_P(PSTR("%-18S | %-28S | Port | Active\n"), PSTR("Name"), PSTR("Alias"));
        for (size_t i = 0; i < n; ++i) {
                port_t port;
                memcpy_P(&port, port_list + i, sizeof (port_t));
                printf_P(PSTR("%-18S | %-28S |   %c%c | %c\n"),
                         port.name, port.alias ? port.alias : PSTR(""),
                         port.port[0], port.port[1],
                         bitfield_get(bitfield, i) ? 'X' : ' ');
        }
        putchar('\n');
}

void cmd_in(int argc, char* argv[]) {
        if (check_usage(argc != 1, argc, argv)) {
                puts_P(PSTR("Inputs:"));
                ports_print(in_list, in.bitfield, ARRAY_SIZE(in_list));
        }
}

void cmd_out(int argc, char* argv[]) {
        if (check_usage(argc != 1, argc, argv)) {
                puts_P(PSTR("Outputs:"));
                ports_print(out_list, out.bitfield, ARRAY_SIZE(out_list));
        }
}

void cmd_on_off(int argc, char* argv[]) {
        if (check_usage(argc != 2, argc, argv) && check_manual()) {
                for (size_t i = 0; i < ARRAY_SIZE(out_list); ++i) {
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
        if (!check_usage(0, argc, argv)) {
                // nothing
        } else if (argc == 2 && !strcmp_P(argv[1], PSTR("--manual"))) {
                flag.manual = 1;
                state = 0;
        } else if (argc == 2 && !strcmp_P(argv[1], PSTR("--auto"))) {
                flag.manual = 0;
                state = 0;
                ports_reset();
        } else if (argc == 1) {
                printf_P(PSTR("%S mode is active\n"), flag.manual ? PSTR("Manual") : PSTR("Automatic"));
        } else {
                cmd_usage(argv[0]);
        }
}

void cmd_reset(int argc, char* argv[]) {
        if (check_usage(argc != 1, argc, argv) && check_manual())
                ports_reset();
}

void cmd_help(int argc, char* argv[]) {
        if (!check_usage(0, argc, argv)) {
                // nothing
        } else if (argc == 1) {
                puts_P(PSTR("List of commands:"));
                cmd_t cmd;
                for (size_t i = 0; i < ARRAY_SIZE(cmd_list); ++i) {
                        memcpy_P(&cmd, cmd_list + i, sizeof (cmd_t));
                        printf_P(PSTR("  %-16S %S\n"), cmd.name, cmd.help);
                }
                putchar('\n');
        } else if (argc == 2) {
                cmd_usage(argv[1]);
        } else {
                cmd_usage(argv[0]);
        }
}

void cmd_version(int argc, char* argv[]) {
        if (check_usage(argc != 1, argc, argv))
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
        while (ringbuf_full(uart_txbuf)) {
                // wait, do nothing
        }
        ringbuf_putc(uart_txbuf, c);
        UCSR0B |= (1 << UDRIE);
        return 0;
}

char* uart_gets() {
        static char line[LINE_SIZE];
        static uint8_t size = 0;
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
