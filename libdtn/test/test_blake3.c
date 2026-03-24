#include <stdio.h>
#include <string.h>
#include "blake3.h"

int main() {
    // Input data
    const char *data = "Hello, World!";
    
    // Output hash (BLAKE3 outputs 32 bytes by default)
    uint8_t hash[BLAKE3_OUT_LEN];
    
    // Hash the data
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, strlen(data));
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);
    
    // Print the hash in hex
    for (int i = 0; i < BLAKE3_OUT_LEN; i++) {
        printf("%02x", hash[i]);
    }
    printf("\n");
    
    return 0;
}
