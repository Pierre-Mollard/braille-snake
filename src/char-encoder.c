#include "braille-snake.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/*
 * A braille character shows 8 dots, so 256 different patterns
 * Braille char in unicode are encoded in 3 bytes utf8
 */
int encode_grid_to_braille(bool in_grid[4][2], unsigned char out_utf8[4],
                           uint32_t *out_hex) {

  if (!in_grid || !out_utf8)
    return -1;

  // Unicode braille is like this:
  // (1) (4)
  // (2) (5)
  // (3) (6)
  // (7) (8)
  // so 256 patterns on one byte
  // with an base at 0x2800

  uint8_t offset = 0;
  offset |= (uint8_t)in_grid[0][0];
  offset |= (uint8_t)in_grid[1][0] << 1;
  offset |= (uint8_t)in_grid[2][0] << 2;
  offset |= (uint8_t)in_grid[0][1] << 3;
  offset |= (uint8_t)in_grid[1][1] << 4;
  offset |= (uint8_t)in_grid[2][1] << 5;
  offset |= (uint8_t)in_grid[3][0] << 6;
  offset |= (uint8_t)in_grid[3][1] << 7;

  // UTF-8 for a 3-byte code point: 1110xxxx 10xxxxxx 10xxxxxx
  //                                bit#0    bit#1    bit#2
  // out_utf8[0] = leading byte
  // out_utf8[1] = continuation byte
  // out_utf8[2] = continuation byte

  int braille_unicode = 0x2800 + offset;
  *out_hex = braille_unicode;
  out_utf8[0] = 0xE0 | (0x0F & (braille_unicode >> 12));
  out_utf8[1] = 0x80 | (0x3F & (braille_unicode >> 6));
  out_utf8[2] = 0x80 | (0x3F & braille_unicode);
  out_utf8[3] = '\0';
  return 0;
}
