/*******************************************************************************
 *   Ledger Eos App
 *   (c) 2018-2019 Ledger
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 ********************************************************************************/
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
//#include <byteswap.h>

#include "eos_types.h"
#include "eos_varint.h"
#include "eos_api.h"

#ifndef TEST
#include "os.h"
#endif

// #define bswap_16(value) \
// ((((value) & 0xff) << 8) | ((value) >> 8))

// #define bswap_32(value) \
// (((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | \
// (uint32_t)bswap_16((uint16_t)((value) >> 16)))

// #define bswap_64(value) \
// (((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) \
// << 32) | \
// (uint64_t)bswap_32((uint32_t)((value) >> 32)))


static const char hexAlphabet[] = "0123456789ABCDEF";
static const char base32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
//static const char base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char charmap[] = ".12345abcdefghijklmnopqrstuvwxyz";
//static int base64ModTable[] = {0, 2, 1};




unsigned short crc16(char *ptr, int count) {
   int  crc;
   char i;
   crc = 0;
   while (--count >= 0) {
      crc = crc ^ (int) *ptr++ << 8;
      i = 8;
      do
      {
         if (crc & 0x8000)
            crc = crc << 1 ^ 0x1021;
         else
            crc = crc << 1;
      } while(--i);
   }
   return (crc);
}

/**
 * adapted from https://stash.forgerock.org/projects/OPENAM/repos/forgerock-authenticator-ios/browse/ForgeRock-Authenticator/base32.c
 */
int base32_encode(const uint8_t *data, int length, char *result, int bufSize) {
    int count = 0;
    int quantum = 8;

    if (length < 0 || length > (1 << 28)) {
        return -1;
    }

    if (length > 0) {
        int buffer = data[0];
        int next = 1;
        int bitsLeft = 8;

        while (count < bufSize && (bitsLeft > 0 || next < length)) {
            if (bitsLeft < 5) {
                if (next < length) {
                    buffer <<= 8;
                    buffer |= data[next++] & 0xFF;
                    bitsLeft += 8;
                } else {
                    int pad = 5 - bitsLeft;
                    buffer <<= pad;
                    bitsLeft += pad;
                }
            }

            int index = 0x1F & (buffer >> (bitsLeft - 5));
            bitsLeft -= 5;
            result[count++] = base32Alphabet[index];

            // Track the characters which make up a single quantum of 8 characters
            quantum--;
            if (quantum == 0) {
                quantum = 8;
            }
        }

        // If the number of encoded characters does not make a full quantum, insert padding
        if (quantum != 8) {
            while (quantum > 0 && count < bufSize) {
                result[count++] = '=';
                quantum--;
            }
        }
    }

    // Finally check if we exceeded buffer size.
    if (count < bufSize) {
        result[count] = '\000';
        return count;
    } else {
        return -1;
    }
}

void encode_key(uint8_t *in, char *out, uint8_t versionByte) {
    uint8_t buffer[35];
    buffer[0] = versionByte;
    int i;
    for (i = 0; i < 32; i++) {
        buffer[i+1] = in[i];
    }
    short crc = crc16((char *)buffer, 33); // checksum
    buffer[33] = crc;
    buffer[34] = crc >> 8;
    base32_encode(buffer, 35, out, 56);
    out[56] = '\0';
}

void encode_public_key(uint8_t *in, char *out) {
    encode_key(in, out, 6 << 3);
}

void encode_pre_auth_key(uint8_t *in, char *out) {
    encode_key(in, out, 19 << 3);
}

void encode_hash_x_key(uint8_t *in, char *out) {
    encode_key(in, out, 23 << 3);
}

char* rtrim(char* string, char junk) {
    char* original = string + strlen(string) - 1;
    while(*--original == junk);
    *(original + 1) = '\0';
    return string;
}

uint32_t parse_name(uint64_t value, char* out) {
    char str[13];
    memset(str, '.', 13);
    uint64_t tmp = value;
    uint32_t i;
    for(i = 0; i <= 12; i++ ) {
        char c = charmap[tmp & (i == 0 ? 0x0f : 0x1f)];
        str[12-i] = c;
        tmp >>= (i == 0 ? 4 : 5);
    }

    memcpy(out, rtrim(str, '.'), 13);
    return strlen(out);
}   

uint32_t parse_varint32(uint8_t* buf, unsigned char* bytes) {
    return varint_decode(buf, 4, bytes);
}

void print_summary(char *in, char *out, uint8_t numCharsL, uint8_t numCharsR) {
    uint8_t outLength = numCharsL + numCharsR + 2;
    uint16_t inLength = strlen(in);
    if (inLength > outLength) {
        memcpy(out, in, numCharsL);
        out[numCharsL] = '.';
        out[numCharsL+1] = '.';
        memcpy(out + numCharsL+2, in + inLength - numCharsR, numCharsR);
        out[outLength] = '\0';
    } else {
        memcpy(out, in, inLength);
    }
}

void print_binary(uint8_t *in, char *out, uint8_t len) {
    out[0] = '0';
    out[1] = 'x';
    uint8_t i, j;
    for (i = 0, j = 2; i < len; i+=1, j+=2) {
        out[j] = hexAlphabet[in[i] / 16];
        out[j+1] = hexAlphabet[in[i] % 16];
    }
    out[j] = '\0';
}

void print_binary_summary(uint8_t *in, char *out, uint8_t len) {
    out[0] = '0';
    out[1] = 'x';
    if (2+len*2 > 18) {
        uint8_t i, j;
        for (i = 0, j = 2; i < 4; i+=1, j+=2) {
            out[j] = hexAlphabet[in[i] / 16];
            out[j+1] = hexAlphabet[in[i] % 16];
        }
        out[j++] = '.';
        out[j++] = '.';
        for (i = len - 4; i < len; i+=1, j+=2) {
            out[j] = hexAlphabet[in[i] / 16];
            out[j+1] = hexAlphabet[in[i] % 16];
        }
        out[j] = '\0';
    } else {
        print_binary(in, out, len);
        return;
    }
}

void print_public_key(uint8_t *in, char *out, uint8_t numCharsL, uint8_t numCharsR) {
    if (numCharsL > 0) {
        char buffer[57];
        encode_public_key(in, buffer);
        print_summary(buffer, out, numCharsL, numCharsR);
    } else {
        encode_public_key(in, out);
    }
}

void print_amount(uint64_t amount, char *asset, char *out) {
    char buffer[AMOUNT_MAX_SIZE];
    uint64_t dVal = amount;
    int i, j;

    memset(buffer, 0, AMOUNT_MAX_SIZE);
    for (i = 0; dVal > 0 || i < 9; i++) {
        if (dVal > 0) {
            buffer[i] = (dVal % 10) + '0';
            dVal /= 10;
        } else {
            buffer[i] = '0';
        }
        if (i == 6) { // stroops to xlm: 1 xlm = 10000000 stroops
            i += 1;
            buffer[i] = '.';
        }
        if (i >= AMOUNT_MAX_SIZE) {
            THROW(0x6700);
        }
    }
    // reverse order
    for (i -= 1, j = 0; i >= 0 && j < AMOUNT_MAX_SIZE-1; i--, j++) {
        out[j] = buffer[i];
    }
    // strip trailing 0s
    for (j -= 1; j > 0; j--) {
        if (out[j] != '0') break;
    }
    j += 1;

    // strip trailing .
    if (out[j-1] == '.') j -= 1;

    if (asset) {
        // qualify amount
        out[j++] = ' ';
        strcpy(out + j, asset);
        out[j+strlen(asset)] = '\0';
    } else {
        out[j] = '\0';
    }

}

void print_int(int64_t l, char *out) {
    if (l == 0) {
        strcpy(out, "0");
        return;
    }

    char buffer[AMOUNT_MAX_SIZE];
    int64_t dVal = l < 0 ? -l : l;
    int i;

    memset(buffer, 0, AMOUNT_MAX_SIZE);
    for (i = 0; dVal > 0; i++) {
        buffer[i] = (dVal % 10) + '0';
        dVal /= 10;
        if (i >= AMOUNT_MAX_SIZE) {
            THROW(0x6700);
        }
    }
    int j = 0;
    if (l < 0) {
        out[j++] = '-';
    }
    // reverse order
    for (i -= 1; i >= 0 && j < AMOUNT_MAX_SIZE-1; i--, j++) {
        out[j] = buffer[i];
    }
    out[j] = '\0';
}

void print_uint(uint64_t l, char *out) {
    if (l == 0) {
        strcpy(out, "0");
        return;
    }

    char buffer[AMOUNT_MAX_SIZE];
    uint64_t dVal = l;
    int i, j;

    memset(buffer, 0, AMOUNT_MAX_SIZE);
    for (i = 0; dVal > 0; i++) {
        buffer[i] = (dVal % 10) + '0';
        dVal /= 10;
        if (i >= AMOUNT_MAX_SIZE) {
            THROW(0x6700);
        }
    }
    // reverse order
    for (i -= 1, j = 0; i >= 0 && j < AMOUNT_MAX_SIZE-1; i--, j++) {
        out[j] = buffer[i];
    }
    out[j] = '\0';
}

void print_asset_t(asset_t *asset, char *out) {
    //char issuer[12];
    //print_public_key(asset->issuer, issuer, 3, 4);
    //print_asset(asset->code, issuer, out);
}

 void print_asset(char *code, char *issuer, char *out) {
    uint8_t offset = strlen(code);
    strcpy(out, code);
    out[offset] = '@';
    strcpy(out+offset+1, issuer);
}
 void print_flag(char *flag, char *out, char prefix) {
    uint8_t len = strlen(out);
    if (len) {
        strcpy(out+len, ", ");
        len += 2;
    }
    if (prefix) {
        out[len] = prefix;
        len += 1;
    }
    strcpy(out+len, flag);
}
 void print_flags(uint32_t flags, char *out, char prefix) {
    if (flags & 0x01) {
        print_flag("Auth required", out, prefix);
    }
    if (flags & 0x02) {
        print_flag("Auth revocable", out, prefix);
    }
    if (flags & 0x04) {
        print_flag("Auth immutable", out, prefix);
    }
}
 void print_native_asset_code(uint8_t network, char *out) {
}
