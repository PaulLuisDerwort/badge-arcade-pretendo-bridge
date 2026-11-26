// Minimal stand-in for libctru's 3ds.h, only for testing spotpass.cpp on a PC.
#pragma once
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t Result;
#define R_FAILED(r) (((Result)(r)) < 0)
typedef enum { PS_ALGORITHM_CBC_ENC, PS_ALGORITHM_CBC_DEC, PS_ALGORITHM_CTR_ENC, PS_ALGORITHM_CTR_DEC,
               PS_ALGORITHM_CCM_ENC, PS_ALGORITHM_CCM_DEC } PS_AESAlgorithm;
typedef enum { PS_KEYSLOT_0D, PS_KEYSLOT_2D, PS_KEYSLOT_31, PS_KEYSLOT_38, PS_KEYSLOT_32,
               PS_KEYSLOT_39_DLP, PS_KEYSLOT_2E, PS_KEYSLOT_INVALID, PS_KEYSLOT_36, PS_KEYSLOT_39_NFC } PS_AESKeyType;
extern "C" Result PS_EncryptDecryptAes(u32 size, u8* in, u8* out, PS_AESAlgorithm aes_algo, PS_AESKeyType key_type, u8* iv);
