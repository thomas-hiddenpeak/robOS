/**
 * @file base64.c
 * @brief Simple Base64 encoding/decoding implementation for config manager
 */

#include "base64.h"
#include <string.h>

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char base64_pad = '=';

size_t base64_encode_len(size_t input_len) {
  return ((input_len + 2) / 3) * 4 + 1; // +1 for null terminator
}

size_t base64_decode_len(size_t base64_len) { return (base64_len * 3) / 4; }

int base64_encode(const uint8_t *input, size_t input_len, char *output,
                  size_t output_len) {
  if (input == NULL || output == NULL) {
    return -1;
  }

  size_t needed_len = base64_encode_len(input_len);
  if (output_len < needed_len) {
    return -1;
  }

  size_t output_pos = 0;
  size_t i;

  for (i = 0; i < input_len; i += 3) {
    uint32_t triple = 0;
    int padding = 0;

    // Build 24-bit value from up to 3 bytes
    triple = (uint32_t)input[i] << 16;
    if (i + 1 < input_len) {
      triple |= (uint32_t)input[i + 1] << 8;
    } else {
      padding++;
    }
    if (i + 2 < input_len) {
      triple |= (uint32_t)input[i + 2];
    } else {
      padding++;
    }

    // Extract 4 6-bit values
    output[output_pos++] = base64_chars[(triple >> 18) & 0x3F];
    output[output_pos++] = base64_chars[(triple >> 12) & 0x3F];
    output[output_pos++] =
        (padding >= 2) ? base64_pad : base64_chars[(triple >> 6) & 0x3F];
    output[output_pos++] =
        (padding >= 1) ? base64_pad : base64_chars[triple & 0x3F];
  }

  output[output_pos] = '\0';
  return 0;
}

static int base64_char_to_value(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  } else if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  } else if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  } else if (c == '+') {
    return 62;
  } else if (c == '/') {
    return 63;
  } else if (c == base64_pad) {
    return -1; // Padding
  }
  return -2; // Invalid character
}

int base64_decode(const char *input, size_t input_len, uint8_t *output,
                  size_t output_len) {
  if (input == NULL || output == NULL) {
    return -1;
  }

  if (input_len % 4 != 0) {
    return -1; // Invalid base64 length
  }

  size_t max_output_len = base64_decode_len(input_len);
  if (output_len < max_output_len) {
    return -1;
  }

  size_t output_pos = 0;
  size_t i;

  for (i = 0; i < input_len; i += 4) {
    int values[4];
    int padding = 0;

    // Decode 4 characters
    for (int j = 0; j < 4; j++) {
      values[j] = base64_char_to_value(input[i + j]);
      if (values[j] == -1) { // Padding
        padding++;
      } else if (values[j] == -2) { // Invalid character
        return -1;
      }
    }

    // Combine into 24-bit value
    uint32_t triple = 0;
    if (values[0] >= 0)
      triple |= (uint32_t)values[0] << 18;
    if (values[1] >= 0)
      triple |= (uint32_t)values[1] << 12;
    if (values[2] >= 0)
      triple |= (uint32_t)values[2] << 6;
    if (values[3] >= 0)
      triple |= (uint32_t)values[3];

    // Extract bytes
    if (padding < 3)
      output[output_pos++] = (triple >> 16) & 0xFF;
    if (padding < 2)
      output[output_pos++] = (triple >> 8) & 0xFF;
    if (padding < 1)
      output[output_pos++] = triple & 0xFF;
  }

  return output_pos;
}