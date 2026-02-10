/******************************************************************************

 @file  crypto_port.h

 @brief This file contains the data structures and wrapper APIs for BLE
        LL crypto operations to abstract the underlying implementations,
        which can be based on SimpleLink APIs or Zephyr APIs.

 Group: WCS, BTS
 Target Device: cc23xx

 ******************************************************************************
 
 Copyright (c) 2025-2026, Texas Instruments Incorporated

 All rights reserved not granted herein.
 Limited License.

 Texas Instruments Incorporated grants a world-wide, royalty-free,
 non-exclusive license under copyrights and patents it now or hereafter
 owns or controls to make, have made, use, import, offer to sell and sell
 ("Utilize") this software subject to the terms herein. With respect to the
 foregoing patent license, such license is granted solely to the extent that
 any such patent is necessary to Utilize the software alone. The patent
 license shall not apply to any combinations which include this software,
 other than combinations with devices manufactured by or for TI ("TI
 Devices"). No hardware patent is licensed hereunder.

 Redistributions must preserve existing copyright notices and reproduce
 this license (including the above copyright notice and the disclaimer and
 (if applicable) source code license limitations below) in the documentation
 and/or other materials provided with the distribution.

 Redistribution and use in binary form, without modification, are permitted
 provided that the following conditions are met:

   * No reverse engineering, decompilation, or disassembly of this software
     is permitted with respect to any software provided in binary form.
   * Any redistribution and use are licensed by TI for use only with TI Devices.
   * Nothing shall obligate TI to provide you with source code for the software
     licensed and provided to you in object code.

 If software source code is provided to you, modification and redistribution
 of the source code are permitted provided that the following conditions are
 met:

   * Any redistribution and use of the source code, including any resulting
     derivative works, are licensed by TI for use only with TI Devices.
   * Any redistribution and use of any object code compiled from the source
     code and any resulting derivative works, are licensed by TI for use
     only with TI Devices.

 Neither the name of Texas Instruments Incorporated nor the names of its
 suppliers may be used to endorse or promote products derived from this
 software without specific prior written permission.

 DISCLAIMER.

 THIS SOFTWARE IS PROVIDED BY TI AND TI'S LICENSORS "AS IS" AND ANY EXPRESS
 OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL TI AND TI'S LICENSORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 
 
 *****************************************************************************/

/*********************************************************************
 *
 * WARNING!!!
 *
 * THE API'S FOUND IN THIS FILE ARE FOR INTERNAL STACK USE ONLY!
 * FUNCTIONS SHOULD NOT BE CALLED DIRECTLY FROM APPLICATIONS, AND ANY
 * CALLS TO THESE FUNCTIONS FROM OUTSIDE OF THE STACK MAY RESULT IN
 * UNEXPECTED BEHAVIOR.
 *
 */

#ifndef PORT_CRYPTO_PORT
#define PORT_CRYPTO_PORT

#include <stdint.h>

typedef enum
{
    CRYPTO_P_STATUS_SUCCESS = 0,
    CRYPTO_P_STATUS_ERROR_FAIL,
    CRYPTO_P_STATUS_ERROR_INVALID_PARAM,
    CRYPTO_P_STATUS_ERROR_NOT_SUPPORTED,
} CryptoP_Status_t;

typedef enum
{
    CRYPTO_P_ECC_SECP_R1_256 = 0,
} CryptoP_ECCCurve_t;

typedef enum
{
    CRYPTO_P_KEY_TYPE_ID,
    CRYPTO_P_KEY_TYPE_PLAINTEXT,
} CryptoP_KeyType_t;

typedef enum
{
    CRYPTO_P_AES_OP_ENCRYPT = 0,
    CRYPTO_P_AES_OP_DECRYPT,
} CryptoP_AESOperation_t;

typedef struct
{
    /** Type of the key whether it in plaintext or an ID */
    CryptoP_KeyType_t type;
    /** Buffer containing the plaintext key or key ID */
    uint8_t *keyData;
    /** Length of the plaintext key or key ID*/
    uint32_t *keyDataLen;
    /** Key attributes reserved for PSA */
    void *keyAttributes;
} CryptoP_Key_t;

typedef struct
{
    /** Key used to encrypt/decrypt */
    CryptoP_Key_t *key;
    /** Buffer containing the data to be encrypted/decrypted */
    uint8_t *input;
    /** Buffer to store the encrypted/decrypted data */
    uint8_t *output;
    /** Length of the input/output buffers */
    uint32_t length;
    /** Operation to perform, whether encryption/decryption */
    CryptoP_AESOperation_t operation;
} CryptoP_AESECBParams_t;

typedef struct
{
    /** Input key information. */
    CryptoP_Key_t *key;
    /** Buffer containing a nonce. */
    uint8_t *nonce;
    /** Length of the nonce buffer. */
    uint32_t nonceLen;
    /** Buffer containing additional authentication data. */
    uint8_t *aad;
    /** Length of aad in bytes. */
    uint32_t aadLen;
    /**
     * - Encryption: the plaintext buffer to be encrypted and authenticated in
     * the CCM operation.
     * - Decryption: the ciphertext buffer to be decrypted and verified in the
     * CCM operation.
     */
    uint8_t *input;
    /** Length of the input buffer in bytes. */
    uint32_t inputLen;
    /**
     * - Encryption: the ciphertext buffer that encrypted plaintext and MAC/tag
     * are copied to (this buffer must be long enough to also contain the
     * MAC/tag).
     * - Decryption: the plaintext buffer that decrypted ciphertext is copied
     * to.
     */
    uint8_t *output;
    /** Length of the output buffer in bytes. */
    uint32_t outputLen;
    /** Buffer for the MAC/tag */
    uint8_t *tag;
    /** Length of the MAC/tag in bytes. */
    uint32_t tagLen;
    /** Operation to be done on the input */
    CryptoP_AESOperation_t operation;
} CryptoP_AESCCMParams_t;

typedef struct
{
    /** Curve function to use */
    CryptoP_ECCCurve_t curve;
    /** Output key where the generated private key will be stored */
    CryptoP_Key_t *privateKey;
    /** Output key where the generated public key will be stored */
    CryptoP_Key_t *publicKey;
} CryptoP_ECDHKeyPairGenParams_t;

typedef struct
{
    /** Curve function to use */
    CryptoP_ECCCurve_t curve;
    /** Input key containing own private key information */
    CryptoP_Key_t *myPrivateKey;
    /** Input key containing peer's private key information */
    CryptoP_Key_t *peerPublicKey;
    /** Output key where generated DH shared key will be stored */
    CryptoP_Key_t *dhSharedKey;
} CryptoP_ECDHSharedSecretGenParams_t;

CryptoP_Status_t CryptoP_AESInit(void);
CryptoP_Status_t CryptoP_AESECB(CryptoP_AESECBParams_t *params);
CryptoP_Status_t CryptoP_AESCCM(CryptoP_AESCCMParams_t *params);

CryptoP_Status_t CryptoP_ECDHInit(void);
CryptoP_Status_t CryptoP_ECDHGenerateKeyPair(CryptoP_ECDHKeyPairGenParams_t *params);
CryptoP_Status_t CryptoP_ECDHGenerateDHKey(CryptoP_ECDHSharedSecretGenParams_t *params);

CryptoP_Status_t CryptoP_RNGInit(void);
CryptoP_Status_t CryptoP_RNGGenerateTrueRandomBytes(uint8_t *buf, uint8_t len);
CryptoP_Status_t CryptoP_RNGGeneratePseudoRandomBytes(uint8_t *buf, uint8_t len);

#endif /* PORT_CRYPTO_PORT */