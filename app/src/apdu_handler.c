/*******************************************************************************
*   (c) 2018, 2019 Zondax GmbH
*   (c) 2016 Ledger
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

#include "app_main.h"

#include <string.h>
#include <os_io_seproxyhal.h>
#include <os.h>
#include <ux.h>

#include "view.h"
#include "actions.h"
#include "tx.h"
#include "addr.h"
#include "crypto.h"
#include "coin.h"
#include "zxmacros.h"
#include "app_mode.h"

#include "key.h"
#include "parser.h"
#include "nvdata.h"

__Z_INLINE void handleExtractSpendSignature(volatile uint32_t *flags,
                                            volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleExtractSpendSignature]\n");

    *tx = 0;
    if (rx != APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }
    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != 0) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }
    zxerr_t err = crypto_extract_spend_signature(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2);

    if (err == zxerr_ok) {
        *tx = 64;
        THROW(APDU_CODE_OK);
    } else {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
}

__Z_INLINE void handleExtractTransparentSignature(volatile uint32_t *flags,
                                                  volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleExtractTransparentSignature]\n");

    *tx = 0;
    if (rx != APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != 0) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }
    zxerr_t err = crypto_extract_transparent_signature(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2);
    if (err == zxerr_ok) {
        *tx = 64;
        THROW(APDU_CODE_OK);
    } else {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
}

__Z_INLINE void handleExtractSpendData(volatile uint32_t *flags,
                                       volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleExtractSpendData]\n");

    *tx = 0;
    if (rx != APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != 0) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }
    zxerr_t err = crypto_extract_spend_proofkeyandrnd(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2);
    view_tx_state();
    if (err == zxerr_ok) {
        *tx = 128;
        THROW(APDU_CODE_OK);
    } else {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }

}

__Z_INLINE void handleExtractOutputData(volatile uint32_t *flags,
                                        volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleExtractOutputData]\n");

    *tx = 0;
    if (rx != APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != 0) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }
    uint16_t replyLen = 0;
    zxerr_t err = crypto_extract_output_rnd(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2, &replyLen);
    view_tx_state();
    if (err == zxerr_ok) {
        *tx = replyLen;
        THROW(APDU_CODE_OK);
    } else {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
}

__Z_INLINE void handleInitTX(volatile uint32_t *flags,
                             volatile uint32_t *tx, uint32_t rx) {
    if (!process_chunk(tx, rx)) {
        THROW(APDU_CODE_OK);
    }

    zemu_log("----[handleInitTX]\n");

    *tx = 0;
    const uint8_t *message = tx_get_buffer();
    const uint16_t messageLength = tx_get_buffer_length();

    if (messageLength > FLASH_BUFFER_SIZE) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        *tx = 0;
        THROW(APDU_CODE_DATA_TOO_LONG);
    }

    zxerr_t err;
    err = crypto_extracttx_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        transaction_reset();
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        *tx = 0;
        THROW(APDU_CODE_EXTRACT_TRANSACTION_FAIL);
    }

    err = crypto_hash_messagebuffer(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        transaction_reset();
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        *tx = 0;
        THROW(APDU_CODE_HASH_MSG_BUF_FAIL);
    }

////////////

    view_review_init(tx_getItem, tx_getNumItems, app_reply_hash);

    view_review_show();
    *flags |= IO_ASYNCH_REPLY;
}

__Z_INLINE void handleGetKeyIVK(volatile uint32_t *flags,
                                volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleGetKeyIVK]\n");

    *tx = 0;
    if (rx < APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (rx - APDU_MIN_LENGTH != DATA_LENGTH_GET_IVK) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != DATA_LENGTH_GET_IVK) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint8_t requireConfirmation = G_io_apdu_buffer[OFFSET_P1];

    if (!requireConfirmation) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint32_t zip32path = 0;
    parser_error_t prserr = parser_sapling_path(G_io_apdu_buffer + OFFSET_DATA, DATA_LENGTH_GET_IVK,
                                                &zip32path);
    MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
    if (prserr != parser_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    key_state.kind = key_ivk;
    uint16_t replyLen = 0;

    zxerr_t err = crypto_ivk_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2, zip32path, &replyLen);
    if (err != zxerr_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    key_state.len = replyLen;

    view_review_init(key_getItem, key_getNumItems, app_reply_key);
    view_review_show();
    *flags |= IO_ASYNCH_REPLY;
}

__Z_INLINE void handleGetKeyOVK(volatile uint32_t *flags,
                                volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleGetKeyOVK]\n");

    *tx = 0;
    if (rx < APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (rx - APDU_MIN_LENGTH != DATA_LENGTH_GET_OVK) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != DATA_LENGTH_GET_OVK) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint8_t requireConfirmation = G_io_apdu_buffer[OFFSET_P1];

    if (!requireConfirmation) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint32_t zip32path = 0;
    parser_error_t prserr = parser_sapling_path(G_io_apdu_buffer + OFFSET_DATA, DATA_LENGTH_GET_IVK,
                                                &zip32path);
    MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
    if (prserr != parser_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    key_state.kind = key_ovk;
    uint16_t replyLen = 0;

    zxerr_t err = crypto_ovk_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2, zip32path, &replyLen);
    if (err != zxerr_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    key_state.len = replyLen;

    view_review_init(key_getItem, key_getNumItems, app_reply_key);
    view_review_show();
    *flags |= IO_ASYNCH_REPLY;
}

__Z_INLINE void handleCheckandSign(volatile uint32_t *flags,
                                   volatile uint32_t *tx, uint32_t rx) {
    if (!process_chunk(tx, rx)) {
        THROW(APDU_CODE_OK);
    }
    *tx = 0;

    zemu_log("----[handleCheckandSign]\n");

//    zxerr_t err = check_and_sign_tx();
//
//    if (err != zxerr_ok) {
//        *tx = 0;
//        view_idle_show(0, NULL);
//        transaction_reset();
//        THROW(APDU_CODE_DATA_INVALID);
//    }
    const uint8_t *message = tx_get_buffer();
    const uint16_t messageLength = tx_get_buffer_length();
    zxerr_t err;

    if (get_state() != STATE_PROCESSED_ALL_EXTRACTIONS) {
        zemu_log("[handleCheckandSign] not STATE_PROCESSED_ALL_EXTRACTIONS\n");
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_UNPROCESSED_TX);
    }

    // TODO: check this
    // tx_reset_state();

    set_state(STATE_CHECKING_ALL_TXDATA);
    view_tx_state();

    err = crypto_check_prevouts(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_PREVOUT_INVALID);
    }

    err = crypto_check_sequence(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_SEQUENCE_INVALID);
    }

    err = crypto_check_outputs(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_OUTPUTS_INVALID);
    }

    err = crypto_check_joinsplits(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_JOINSPLIT_INVALID);
    }

    // /!\ the valuebalance is different to the total value
    err = crypto_check_valuebalance(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);

    if(err != zxerr_ok){
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_BAD_VALUEBALANCE);
    }


    err = crypto_checkspend_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_SPEND_INVALID);
    }

    err = crypto_checkoutput_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_OUTPUT_CONTENT_INVALID);
    }

    err = crypto_checkencryptions_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_ENCRYPTION_INVALID);
    }

    set_state(STATE_VERIFIED_ALL_TXDATA);
    view_tx_state();

    err = crypto_sign_and_check_transparent(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_CHECK_SIGN_TR_FAIL);
    }

    err = crypto_signspends_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_SIGN_SPEND_FAIL);
    }

    err = crypto_hash_messagebuffer(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3, message, messageLength);
    if (err != zxerr_ok) {
        MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
        view_idle_show(0, NULL);
        transaction_reset();
        THROW(APDU_CODE_HASH_MSG_BUF_FAIL);
    }

    set_state(STATE_SIGNED_TX);
    view_tx_state();

    *tx = 32;
    THROW(APDU_CODE_OK);
}

__Z_INLINE void handleGetAddrSecp256K1(volatile uint32_t *flags,
                                       volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleGetAddrSecp256K1]\n");

    extractHDPath(rx, OFFSET_DATA);
    *tx = 0;
    uint8_t requireConfirmation = G_io_apdu_buffer[OFFSET_P1];
    uint16_t replyLen = 0;

    zxerr_t err = crypto_fillAddress_secp256k1(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2, &replyLen);
    if (err != zxerr_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }

    action_addrResponse.kind = addr_secp256k1;
    action_addrResponse.len = replyLen;

    if (requireConfirmation) {
        view_review_init(addr_getItem, addr_getNumItems, app_reply_address);
        view_review_show();
        *flags |= IO_ASYNCH_REPLY;
        return;
    }
    *tx = replyLen;
    THROW(APDU_CODE_OK);
}


__Z_INLINE void handleGetAddrSaplingDiv(volatile uint32_t *flags,
                                        volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleGetAddrSaplingDiv]\n");

    *tx = 0;
    if (rx < APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (rx - APDU_MIN_LENGTH != DATA_LENGTH_GET_ADDR_DIV) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != DATA_LENGTH_GET_ADDR_DIV) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint8_t requireConfirmation = G_io_apdu_buffer[OFFSET_P1];

    uint16_t replyLen = 0;

    zemu_log_stack("handleGetAddrSapling_withdiv");

    parser_addr_div_t parser_addr;
    MEMZERO(&parser_addr, sizeof(parser_addr_div_t));

    parser_error_t prserr = parser_sapling_path_with_div(G_io_apdu_buffer + OFFSET_DATA, DATA_LENGTH_GET_ADDR_DIV,
                                                         &parser_addr);
    MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
    if (prserr != parser_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    zxerr_t err = crypto_fillAddress_with_diversifier_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 3,
                                                              parser_addr.path, parser_addr.div, &replyLen);
    if (err != zxerr_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    action_addrResponse.kind = addr_sapling_div;
    action_addrResponse.len = replyLen;

    if (requireConfirmation) {
        view_review_init(addr_getItem, addr_getNumItems, app_reply_address);
        view_review_show();
        *flags |= IO_ASYNCH_REPLY;
        return;
    }
    *tx = replyLen;
    THROW(APDU_CODE_OK);
}

__Z_INLINE void handleGetDiversifierList(volatile uint32_t *flags,
                                         volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleGetDiversifierList]\n");

    *tx = 0;
    if (rx < APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (rx - APDU_MIN_LENGTH != DATA_LENGTH_GET_DIV_LIST) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != DATA_LENGTH_GET_DIV_LIST) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint16_t replyLen = 0;

    zemu_log_stack("handleGetAddrSapling_divlist");

    parser_addr_div_t parser_addr;
    MEMZERO(&parser_addr, sizeof(parser_addr_div_t));

    parser_error_t prserr = parser_sapling_path_with_div(G_io_apdu_buffer + OFFSET_DATA, DATA_LENGTH_GET_DIV_LIST,
                                                         &parser_addr);
    MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
    if (prserr != parser_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    zxerr_t err = crypto_diversifier_with_startindex(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2, parser_addr.path,
                                                     parser_addr.div, &replyLen);

    if (err == zxerr_ok) {
        *tx = replyLen;
        THROW(APDU_CODE_OK);
    } else {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
}

__Z_INLINE void handleGetAddrSapling(volatile uint32_t *flags,
                                     volatile uint32_t *tx, uint32_t rx) {
    zemu_log("----[handleGetAddrSapling]\n");

    *tx = 0;
    if (rx < APDU_MIN_LENGTH) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (rx - APDU_MIN_LENGTH != DATA_LENGTH_GET_ADDR_SAPLING) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    if (G_io_apdu_buffer[OFFSET_DATA_LEN] != DATA_LENGTH_GET_ADDR_SAPLING) {
        THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
    }

    uint8_t requireConfirmation = G_io_apdu_buffer[OFFSET_P1];

    zemu_log_stack("handleGetAddrSapling");

    uint32_t zip32path = 0;
    parser_error_t prserr = parser_sapling_path(G_io_apdu_buffer + OFFSET_DATA, DATA_LENGTH_GET_ADDR_SAPLING,
                                                &zip32path);
    MEMZERO(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE);
    if (prserr != parser_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    uint16_t replyLen = 0;
    zxerr_t err = crypto_fillAddress_sapling(G_io_apdu_buffer, IO_APDU_BUFFER_SIZE - 2, zip32path, &replyLen);
    if (err != zxerr_ok) {
        *tx = 0;
        THROW(APDU_CODE_DATA_INVALID);
    }
    action_addrResponse.kind = addr_sapling;
    action_addrResponse.len = replyLen;

    if (requireConfirmation) {
        view_review_init(addr_getItem, addr_getNumItems, app_reply_address);
        view_review_show();
        *flags |= IO_ASYNCH_REPLY;
        return;
    }

    *tx = replyLen;
    THROW(APDU_CODE_OK);
}

__Z_INLINE void handleSignSapling(volatile uint32_t *flags,
                                  volatile uint32_t *tx, uint32_t rx) {
    THROW(APDU_CODE_COMMAND_NOT_ALLOWED);
}

#if defined(APP_TESTING)
#include <zxmacros.h>
#include "cx.h"
#include "rslib.h"
#include "jubjub.h"

void handleTest(volatile uint32_t *flags, volatile uint32_t *tx, uint32_t rx) {

    uint8_t point[32] = {    48, 181, 242, 170, 173, 50, 86, 48, 188, 221, 219, 206, 77, 103, 101, 109, 5, 253, 28, 194,
    208, 55, 187, 83, 117, 182, 233, 109, 158, 1, 161, 215};

    uint8_t scalar[32] = {            0x66, 0x5e, 0xd6, 0xf7, 0xb7, 0x93, 0xaf, 0xa1, 0x82, 0x21, 0xe1, 0x57, 0xba, 0xd5,
            0x43, 0x3c, 0x54, 0x23, 0xf4, 0xfe, 0xc9, 0x46, 0xe0, 0x8e, 0xd6, 0x30, 0xa0, 0xc6,
            0x0a, 0x1f, 0xac, 0x02,};

    jubjub_extendedpoint p;
    jubjub_fq scal;
    jubjub_field_frombytes(scal,scalar);

    jubjub_extendedpoint_tobytes(point,JUBJUB_GEN);
    zxerr_t err = jubjub_extendedpoint_frombytes(&p, point);
    if(err!=zxerr_ok){
        *tx = 0;
        MEMZERO(point, 32);
        THROW(APDU_CODE_OK);
    }
    //MEMCPY(&p, &JUBJUB_GEN, 32);
    //jubjub_extendedpoint_scalarmult(&p, scal);
    jubjub_extendedpoint_tobytes(point,p);

    MEMCPY(G_io_apdu_buffer, point,32);
    *tx = 32;
    THROW(APDU_CODE_OK);
}
#endif

void handleApdu(volatile uint32_t *flags, volatile uint32_t *tx, uint32_t rx) {
    uint16_t sw = 0;

    BEGIN_TRY
    {
        TRY
        {
            if (G_io_apdu_buffer[OFFSET_CLA] != CLA) {
                THROW(APDU_CODE_CLA_NOT_SUPPORTED);
            }

            if (rx < APDU_MIN_LENGTH) {
                THROW(APDU_CODE_WRONG_LENGTH);
            }

            switch (G_io_apdu_buffer[OFFSET_INS]) {
                case INS_GET_VERSION: {
                    handle_getversion(flags, tx, rx);
                    break;
                }

                case INS_GET_ADDR_SECP256K1: {
                    handleGetAddrSecp256K1(flags, tx, rx);
                    break;
                }

                case INS_GET_IVK: {
                    handleGetKeyIVK(flags, tx, rx);
                    break;
                }

                case INS_GET_OVK: {
                    handleGetKeyOVK(flags, tx, rx);
                    break;
                }

                case INS_INIT_TX: {
                    handleInitTX(flags, tx, rx);
                    break;
                }

                case INS_EXTRACT_SPEND: {
                    handleExtractSpendData(flags, tx, rx);
                    break;
                }

                case INS_EXTRACT_OUTPUT: {
                    handleExtractOutputData(flags, tx, rx);
                    break;
                }

                case INS_CHECKANDSIGN: {
                    handleCheckandSign(flags, tx, rx);
                    break;
                }

                case INS_EXTRACT_SPENDSIG: {
                    handleExtractSpendSignature(flags, tx, rx);
                    break;
                }

                case INS_EXTRACT_TRANSSIG: {
                    handleExtractTransparentSignature(flags, tx, rx);
                    break;
                }

                case INS_GET_ADDR_SAPLING: {
                    handleGetAddrSapling(flags, tx, rx);
                    break;
                }
                case INS_GET_DIV_LIST: {
                    handleGetDiversifierList(flags, tx, rx);
                    break;
                }

                case INS_GET_ADDR_SAPLING_DIV: {
                    handleGetAddrSaplingDiv(flags, tx, rx);
                    break;
                }

                case INS_SIGN_SAPLING: {
                    handleSignSapling(flags, tx, rx);
                    break;
                }

#if defined(APP_TESTING)
                    case INS_TEST: {
                        handleTest(flags, tx, rx);
                        /*
                        G_io_apdu_buffer[0] = 0xCA;
                        G_io_apdu_buffer[1] = 0xFE;
                        *tx = 3;
                        */
                        THROW(APDU_CODE_OK);
                        break;
                    }
#endif
                default:
                    THROW(APDU_CODE_INS_NOT_SUPPORTED);
            }
        }
        CATCH(EXCEPTION_IO_RESET)
        {
            THROW(EXCEPTION_IO_RESET);
        }
        CATCH_OTHER(e)
        {
            switch (e & 0xF000) {
                case 0x6000:
                case APDU_CODE_OK:
                    sw = e;
                    break;
                default:
                    sw = 0x6800 | (e & 0x7FF);
                    break;
            }
            G_io_apdu_buffer[*tx] = sw >> 8;
            G_io_apdu_buffer[*tx + 1] = sw;
            *tx += 2;
        }
        FINALLY
        {
        }
    }
    END_TRY;
}
