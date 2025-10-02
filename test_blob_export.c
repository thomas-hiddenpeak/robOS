/*
 * Test blob data export/import functionality
 * This is a simple test to demonstrate blob data handling
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Mock data to simulate real blob content
const uint8_t test_blob_data[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20,
                                  0x57, 0x6f, 0x72, 0x6c, 0x64, // "Hello World"
                                  0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                  0x06, 0x07, 0x08, 0x09, 0x0a, 0xff,
                                  0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9,
                                  0xf8, 0xf7, 0xf6, 0xf5};

int main() {
  printf("Test blob data for export/import verification:\n");
  printf("Size: %zu bytes\n", sizeof(test_blob_data));
  printf("Content: ");
  for (size_t i = 0; i < sizeof(test_blob_data); i++) {
    printf("%02x ", test_blob_data[i]);
  }
  printf("\n");

  // This demonstrates the type of binary data that our
  // base64 encoding/decoding system should handle correctly
  printf(
      "\nThis data should be preserved exactly through export/import cycle\n");

  return 0;
}