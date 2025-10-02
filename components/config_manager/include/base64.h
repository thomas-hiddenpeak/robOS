/**
 * @file base64.h
 * @brief Simple Base64 encoding/decoding functions for config manager
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate the length needed for base64 encoded string
 * @param input_len Length of input binary data
 * @return Length needed for base64 string (including null terminator)
 */
size_t base64_encode_len(size_t input_len);

/**
 * @brief Calculate the length needed for decoded binary data
 * @param base64_len Length of base64 string
 * @return Length needed for decoded binary data
 */
size_t base64_decode_len(size_t base64_len);

/**
 * @brief Encode binary data to base64 string
 * @param input Binary data to encode
 * @param input_len Length of input data
 * @param output Buffer to store base64 string
 * @param output_len Size of output buffer
 * @return 0 on success, -1 on error
 */
int base64_encode(const uint8_t *input, size_t input_len, char *output,
                  size_t output_len);

/**
 * @brief Decode base64 string to binary data
 * @param input Base64 string to decode
 * @param input_len Length of base64 string
 * @param output Buffer to store decoded data
 * @param output_len Size of output buffer
 * @return Length of decoded data on success, -1 on error
 */
int base64_decode(const char *input, size_t input_len, uint8_t *output,
                  size_t output_len);

#ifdef __cplusplus
}
#endif