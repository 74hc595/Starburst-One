/**
 * Single alphanumeric display message scroller
 * Matt Sarnoff (msarnoff.org)
 * December 26, 2016
 *
 * Drives an LTP-587G common-anode 16-segment display.
 * All segments are multiplexed; the only external component needed is a
 * current limiting resistor on the common anode.
 *
 * A hard-coded message is spelled out on the display, one character at a
 * time, with a brief flicker so consecutive identical characters can be
 * distinguished. The message repeats in an infinite loop.
 *
 * Since the ATtiny461A only has 15 usable I/Os (PB7 must be used for /RESET
 * to support in-circuit programming), the top two horizontal segments are
 * tied together. There is no apparent effect on brightness.
 *
 * The decimal point is not driven.
 */

#include <avr/sfr_defs.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>

struct segment {
  union {
    struct {
      uint8_t ddra;
      uint8_t ddrb;
    };
    uint16_t val;
  };
};


/*        a
 *    ---------
 *   |\k  |m  /|
 * h | \  |  / | c
 *   |  \ | /n |
 *   |   \|/   |
 *    ---- ----
 *   |u  /|\  p|
 * g |  / | \  | d
 *   | /t |  \ |
 *   |/  s|  r\|
 *    ---- ----
 *     f    e
 */
enum seg {
  SEG_A, /* A and B are combined */
  SEG_C,
  SEG_D,
  SEG_E,
  SEG_F,
  SEG_G,
  SEG_H,
  SEG_K,
  SEG_M,
  SEG_N,
  SEG_P,
  SEG_R,
  SEG_S,
  SEG_T,
  SEG_U,
  NUM_SEGS
};

static const struct segment segments[15] PROGMEM = {
  [SEG_A] = { 0b00000001, 0b00000000 },
  [SEG_M] = { 0b00000010, 0b00000000 },
  [SEG_K] = { 0b00000100, 0b00000000 },
  [SEG_H] = { 0b00001000, 0b00000000 },
  [SEG_G] = { 0b00010000, 0b00000000 },
  [SEG_T] = { 0b00100000, 0b00000000 },
  [SEG_E] = { 0b01000000, 0b00000000 },
  [SEG_F] = { 0b10000000, 0b00000000 },
  [SEG_N] = { 0b00000000, 0b00000001 },
  [SEG_C] = { 0b00000000, 0b00000010 },
  [SEG_P] = { 0b00000000, 0b00000100 },
  [SEG_U] = { 0b00000000, 0b00001000 },
  [SEG_D] = { 0b00000000, 0b00010000 },
  [SEG_R] = { 0b00000000, 0b00100000 },
  [SEG_S] = { 0b00000000, 0b01000000 }
};

/* Character definitions */
enum segbit {
  sa = (1 << SEG_A),
  sc = (1 << SEG_C),
  sd = (1 << SEG_D),
  se = (1 << SEG_E),
  sf = (1 << SEG_F),
  sg = (1 << SEG_G),
  sh = (1 << SEG_H),
  sk = (1 << SEG_K),
  sm = (1 << SEG_M),
  sn = (1 << SEG_N),
  sp = (1 << SEG_P),
  sr = (1 << SEG_R),
  ss = (1 << SEG_S),
  st = (1 << SEG_T),
  su = (1 << SEG_U)
};

static const uint16_t patterns[256] PROGMEM = {
  [' '] = 0,
  ['!'] = sh|sm|sf|se|sd, /* smiley face */
  ['"'] = sh|sm,
  ['#'] = sc|sd|se|sf|sm|sp|ss|su,
  ['$'] = sa|sd|se|sf|sh|sm|sp|ss|su,
  ['%'] = sd|sh|sk|sn|sp|sr|st|su,
  ['&'] = sa|sd|se|sf|sg|sk|sn|sr|su,
  ['\'']= sm,
  ['('] = sn|sr,
  [')'] = sk|sr,
  ['*'] = sk|sm|sn|sp|sr|ss|st|su,
  ['+'] = sm|sp|ss|su,
  [','] = st,
  ['-'] = sp|su,
  ['.'] = sf,
  ['/'] = sn|st,
  ['0'] = sa|sc|sd|se|sf|sg|sh,
  ['1'] = sc|sd,
  ['2'] = sa|sc|se|sf|sg|sp|su,
  ['3'] = sa|sc|sd|se|sf|sp|su,
  ['4'] = sc|sd|sh|sp|su,
  ['5'] = sa|sd|se|sf|sh|sp|su,
  ['6'] = sa|sd|se|sf|sg|sh|sp|su,
  ['7'] = sa|sc|sd,
  ['8'] = sa|sc|sd|se|sf|sg|sh|sp|su,
  ['9'] = sa|sc|sd|se|sf|sh|sp|su,
  [':'] = sf|su,
  [';'] = sf|sp,
  ['<'] = se|sf|sn|st,
  ['='] = se|sf|sp|su,
  ['>'] = se|sf|sk|sr,
  ['?'] = sa|sc|sh|sp|ss,
  ['@'] = sa|sc|sd|se|sf|sg|ss|su,
  ['A'] = sa|sc|sd|sg|sh|sp|su,
  ['B'] = sa|sc|sd|se|sf|sp|sm|ss,
  ['C'] = sa|se|sf|sg|sh,
  ['D'] = sa|sc|sd|se|sf|sm|ss,
  ['E'] = sa|se|sf|sg|sh|su,
  ['F'] = sa|sg|sh|su,
  ['G'] = sa|sd|se|sf|sg|sh|sp,
  ['H'] = sc|sd|sg|sh|sp|su,
  ['I'] = sa|se|sf|sm|ss,
  ['J'] = sc|sd|se|sf|sg,
  ['K'] = sg|sh|sn|sr|su,
  ['L'] = se|sf|sg|sh,
  ['M'] = sc|sd|sg|sh|sk|sn,
  ['N'] = sc|sd|sg|sh|sk|sr,
  ['O'] = sa|sc|sd|se|sf|sg|sh,
  ['P'] = sa|sc|sg|sh|sp|su,
  ['Q'] = sa|sc|sd|se|sf|sg|sh|sr,
  ['R'] = sa|sc|sg|sh|sr|sp|su,
  ['S'] = sa|sd|se|sf|sh|sp|su,
  ['T'] = sa|sm|ss,
  ['U'] = sc|sd|se|sf|sg|sh,
  ['V'] = sg|sh|sn|st,
  ['W'] = sc|sd|sg|sh|sr|st,
  ['X'] = sk|sn|sr|st,
  ['Y'] = sc|sh|sp|ss|su,
  ['Z'] = sa|se|sf|sn|st,
  ['['] = sa|sf|sg|sh,
  ['\\']= sk|sr,
  [']'] = sa|sc|sd|se,
  ['^'] = sr|st,
  ['_'] = sf|se,
  ['{'] = se|sn|ss|su,
  ['|'] = sm|ss,
  ['}'] = sf|sk|sp|ss,
  ['~'] = sd|se|sg|ss|su,
};

static const char *message = "HELLO WORLD ";

#define SPEED 200

int main(void)
{
  /* all pins at hi-z */
  DDRA = 0;
  DDRB = 0;
  /* disable pullups */
  PORTA = 0;
  PORTB = 0;

  const char *c = message;

  while (1) {
    uint16_t pattern = pgm_read_word(patterns+(*c));
    for (uint16_t count = 0; count < SPEED; count++) {
      uint16_t segmask = sa;
      for (enum seg i = 0; i < NUM_SEGS; i++) {
        struct segment s = {.val = pgm_read_word(segments+i)};
        if (pattern & segmask) {
          DDRA = s.ddra;
          DDRB = s.ddrb;
        } else {
          DDRA = 0;
          DDRB = 0;
        }
        segmask <<= 1;
        _delay_us(100);
      }
    }
    c++;
    if (*c == '\0') { c = message; }
    /* blank out for a bit after each character */
    DDRA = 0;
    DDRB = 0;
    _delay_ms(30);
  }
}
