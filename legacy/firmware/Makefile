APPVER = 1.8.0

FIRMWARE_BUILD_VERSION=$(shell cat version.h | grep "ONEKEY_VERSION " | awk -F "[\"\"]" '{print $$2}')
FIRMWARE_BUILD_DATE=$(shell date "+%m%d")
ifeq ($(EMULATOR),1)
NAME = onekey_emu
else
ifndef BUILD_ID
BUILD_COMMIT=$(shell git rev-parse HEAD | cut -c1-7)
ifneq ($(BITCOIN_ONLY),1)
NAME=classic1s.$(FIRMWARE_BUILD_VERSION)-Stable-$(FIRMWARE_BUILD_DATE)-$(BUILD_COMMIT)
else
NAME=classic1s.$(FIRMWARE_BUILD_VERSION)-Stable-bc-only-$(FIRMWARE_BUILD_DATE)-$(BUILD_COMMIT)
endif
else
NAME=classic1s.$(FIRMWARE_BUILD_VERSION)-Alpha-$(FIRMWARE_BUILD_DATE)-$(BUILD_ID)
endif
endif

SECP256K1_ZKP ?= 1


ifeq ($(SECP256K1_ZKP),1)
CFLAGS   += -DSECP256K1_ZKP
CFLAGS   += -DUSE_SECP256K1_ZKP
CFLAGS   += -DUSE_SECP256K1_ZKP_ECDSA
ifeq ($(EMULATOR),1)
CFLAGS += -DSECP256K1_CONTEXT_SIZE=208
else
CFLAGS += -DSECP256K1_CONTEXT_SIZE=180
OBJS += field_10x26_arm.o
endif
ZKP_CFLAGS = \
	-DUSE_ASM_ARM \
	-DUSE_EXTERNAL_ASM \
	-DUSE_EXTERNAL_DEFAULT_CALLBACKS \
	-DECMULT_GEN_PREC_BITS=2 \
	-DECMULT_WINDOW_SIZE=8 \
	-DENABLE_MODULE_GENERATOR \
	-DENABLE_MODULE_RECOVERY \
	-DENABLE_MODULE_SCHNORRSIG \
	-DENABLE_MODULE_EXTRAKEYS

OBJS += secp256k1-zkp.o
OBJS += precomputed_ecmult.o
OBJS += precomputed_ecmult_gen.o
OBJS += ../vendor/trezor-crypto/zkp_bip340.o
OBJS += ../vendor/trezor-crypto/zkp_context.o
OBJS += ../vendor/trezor-crypto/zkp_ecdsa.o
endif

ifeq ($(EMULATOR),1)
OBJS += udp.o
else
OBJS += usb.o
OBJS += bl_check.o
OBJS += otp.o
OBJS += header.o
OBJS += startup.o
endif

OBJS += font.o
OBJS += font_ex.o
OBJS += chinese.o
OBJS += gettext.o
OBJS += i18n/i18n.o
OBJS += menu_para.o
OBJS += menu_core.o
OBJS += menu_list.o

OBJS += messages.o
ifeq ($(EMULATOR),1)
OBJS += config_emu.o
else
OBJS += config.o
endif
OBJS += trezor.o
OBJS += pinmatrix.o
OBJS += fsm.o
OBJS += coins.o
OBJS += coin_info.o
OBJS += transaction.o
OBJS += protect.o
OBJS += layout2.o
OBJS += recovery.o
OBJS += reset.o
OBJS += signing.o
OBJS += crypto.o
OBJS += se_chip.o
OBJS += psbt/psbt.o
OBJS += bip322_simple/bip322_simple.o

ifneq ($(BITCOIN_ONLY),1)
OBJS += fido2/ctap_trans.o
OBJS += fido2/ctap.o
OBJS += fido2/ctap_parse.o
OBJS += fido2/resident_credential.o
OBJS += ethereum.o
OBJS += ethereum_definitions.o
OBJS += ethereum_networks.o
OBJS += ethereum_tokens.o
OBJS += ethereum_onekey.o
OBJS += ethereum_tokens_onekey.o
OBJS += nem2.o
OBJS += nem_mosaics.o
OBJS += solana.o
OBJS += starcoin.o
OBJS += stellar.o
OBJS += tron.o
OBJS += tron_tokens.o
OBJS += aptos.o
OBJS += near.o
OBJS += conflux.o
OBJS += conflux_tokens.o
OBJS += algorand.o
OBJS += algo/algo_asa.o
OBJS += algo/base64.o
OBJS += algo/parser_encoding.o
OBJS += algo/parser_impl.o
OBJS += algo/parser.o
OBJS += algo/tx.o
OBJS += ripple.o
OBJS += filecoin.o
OBJS += filecoin/parser_impl.o
OBJS += filecoin/tx.o
OBJS += filecoin/parser.o
OBJS += jsmn.o
OBJS += cosmos/json_parser.o
OBJS += cosmos/tx_validate.o
OBJS += cosmos/tx_parser.o
OBJS += cosmos/tx_display.o
OBJS += cosmos/parser.o
OBJS += cosmos_networks.o
OBJS += cosmos.o
OBJS += polkadot.o
OBJS += polkadot/crypto_helper.o
OBJS += polkadot/bignum.o
OBJS += polkadot/substrate/substrate_types.o
# OBJS += polkadot/substrate/substrate_dispatch_V25.o
OBJS += polkadot/substrate/substrate_dispatch_V26.o
OBJS += polkadot/substrate/substrate_dispatch.o
OBJS += polkadot/parser_impl_common.o
OBJS += polkadot/parser_impl.o
OBJS += polkadot/parser.o
OBJS += polkadot/tx.o
OBJS += ada.o
OBJS += cbor.o
OBJS += sui.o
OBJS += kaspa.o
OBJS += nexa.o
OBJS += nervos.o
OBJS += alephium.o
OBJS += alephium/alph_address.o
OBJS += alephium/alph_decode.o
OBJS += alephium/alph_layout.o
OBJS += nostr.o
OBJS += scdo.o
OBJS += scdo_tokens.o
OBJS += base64.o
OBJS += lnurl.o
OBJS += ton.o
OBJS += ton_bits.o
OBJS += ton_cell.o
OBJS += ton_tokens.o
OBJS += ton_address.o
OBJS += ton_layout.o
OBJS += benfen.o
OBJS += neo.o
OBJS += neo_tokens.o
endif

OBJS += debug.o

OBJS += ../vendor/trezor-crypto/address.o
OBJS += ../vendor/trezor-crypto/bignum.o
OBJS += ../vendor/trezor-crypto/ecdsa.o
OBJS += ../vendor/trezor-crypto/curves.o
OBJS += ../vendor/trezor-crypto/secp256k1.o
OBJS += ../vendor/trezor-crypto/nist256p1.o
OBJS += ../vendor/trezor-crypto/hmac_drbg.o
OBJS += ../vendor/trezor-crypto/rfc6979.o
OBJS += ../vendor/trezor-crypto/rand.o
OBJS += ../vendor/trezor-crypto/memzero.o

OBJS += ../vendor/trezor-crypto/ed25519-donna/curve25519-donna-32bit.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/curve25519-donna-helpers.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/modm-donna-32bit.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/ed25519-donna-basepoint-table.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/ed25519-donna-32bit-tables.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/ed25519-donna-impl-base.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/ed25519.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/curve25519-donna-scalarmult-base.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/ed25519-sha3.o
OBJS += ../vendor/trezor-crypto/ed25519-donna/ed25519-keccak.o

OBJS += ../vendor/trezor-crypto/hmac.o
OBJS += ../vendor/trezor-crypto/bip32.o
OBJS += ../vendor/trezor-crypto/bip39.o
OBJS += ../vendor/trezor-crypto/bip39_english.o
OBJS += ../vendor/trezor-crypto/pbkdf2.o
OBJS += ../vendor/trezor-crypto/base32.o
OBJS += ../vendor/trezor-crypto/base58.o
OBJS += ../vendor/trezor-crypto/segwit_addr.o

OBJS += ../vendor/trezor-crypto/ripemd160.o
OBJS += ../vendor/trezor-crypto/sha2.o
OBJS += ../vendor/trezor-crypto/sha3.o
OBJS += ../vendor/trezor-crypto/blake256.o
OBJS += ../vendor/trezor-crypto/blake2b.o
OBJS += ../vendor/trezor-crypto/blake2s.o
OBJS += ../vendor/trezor-crypto/groestl.o
OBJS += ../vendor/trezor-crypto/hasher.o

OBJS += ../vendor/trezor-crypto/aes/aescrypt.o
OBJS += ../vendor/trezor-crypto/aes/aeskey.o
OBJS += ../vendor/trezor-crypto/aes/aestab.o
OBJS += ../vendor/trezor-crypto/aes/aes_modes.o

OBJS += ../vendor/trezor-crypto/chacha20poly1305/chacha20poly1305.o
OBJS += ../vendor/trezor-crypto/chacha20poly1305/chacha_merged.o
OBJS += ../vendor/trezor-crypto/chacha20poly1305/poly1305-donna.o
OBJS += ../vendor/trezor-crypto/chacha20poly1305/rfc7539.o

OBJS += ../vendor/trezor-crypto/nem.o

OBJS += ../vendor/trezor-crypto/cardano.o

OBJS += ../vendor/trezor-crypto/sha512.o
OBJS += ../vendor/trezor-crypto/schnorr_bch.o

OBJS += ../vendor/QR-Code-generator/c/qrcodegen.o

OBJS += ../vendor/tiny_cbor/src/cborencoder.o
OBJS += ../vendor/tiny_cbor/src/cborparser.o
OBJS += ../vendor/tiny_cbor/src/cborvalidation.o
OBJS += ../vendor/tiny_cbor/src/cborerrorstrings.o

ifeq ($(EMULATOR),1)
OBJS += ../vendor/trezor-storage/storage.o
OBJS += ../vendor/trezor-storage/norcow.o
endif

OBJS += ../vendor/nanopb/pb_common.o
OBJS += ../vendor/nanopb/pb_decode.o
OBJS += ../vendor/nanopb/pb_encode.o

OBJS += protob/messages.pb.o
OBJS += protob/messages-bitcoin.pb.o
OBJS += protob/messages-common.pb.o
OBJS += protob/messages-crypto.pb.o
OBJS += protob/messages-debug.pb.o
OBJS += protob/messages-management.pb.o

ifneq ($(BITCOIN_ONLY),1)
OBJS += ../vendor/trezor-crypto/cash_addr.o
OBJS += protob/messages-ethereum.pb.o
OBJS += protob/messages-ethereum-definitions.pb.o
OBJS += protob/messages-ethereum-onekey.pb.o
OBJS += protob/messages-nem.pb.o
OBJS += protob/messages-solana.pb.o
OBJS += protob/messages-starcoin.pb.o
OBJS += protob/messages-stellar.pb.o
OBJS += protob/messages-tron.pb.o
OBJS += protob/messages-aptos.pb.o
OBJS += protob/messages-near.pb.o
OBJS += protob/messages-conflux.pb.o
OBJS += protob/messages-algorand.pb.o
OBJS += protob/messages-ripple.pb.o
OBJS += protob/messages-filecoin.pb.o
OBJS += protob/messages-cosmos.pb.o
OBJS += protob/messages-polkadot.pb.o
OBJS += protob/messages-cardano.pb.o
OBJS += protob/messages-sui.pb.o
OBJS += protob/messages-kaspa.pb.o
OBJS += protob/messages-nexa.pb.o
OBJS += protob/messages-nervos.pb.o
OBJS += protob/messages-nostr.pb.o
OBJS += protob/messages-scdo.pb.o
OBJS += protob/messages-lnurl.pb.o
OBJS += protob/messages-alephium.pb.o
OBJS += protob/messages-ton.pb.o
OBJS += protob/messages-webauthn.pb.o
OBJS += protob/messages-benfen.pb.o
OBJS += protob/messages-neo.pb.o
endif

OPTFLAGS ?= -Os

../vendor/trezor-crypto/bip32.o: OPTFLAGS = -Os
../vendor/trezor-crypto/bip39.o: OPTFLAGS = -Os
../vendor/trezor-crypto/ecdsa.o: OPTFLAGS = -Os
../vendor/trezor-crypto/sha2.o: OPTFLAGS = -Os
../vendor/trezor-crypto/secp256k1.o: OPTFLAGS = -Os

include ../Makefile.include


ifeq ($(EMULATOR),1)
CFLAGS += --warn-no-unused-parameter
CFLAGS += -fstack-protector-all
else
#CFLAGS += -fstack-protector-strong
endif
CFLAGS += -Wno-sequence-point
CFLAGS += -I../vendor/nanopb -Iprotob -DPB_FIELD_16BIT=1 -DPB_ENCODE_ARRAYS_UNPACKED=1 -DPB_VALIDATE_UTF8=1
CFLAGS += -DSCM_REVISION='"$(shell git rev-parse HEAD | sed 's:\(..\):\\x\1:g')"'
CFLAGS += -DBUILD_ID='"$(NAME)"'
CFLAGS += -DUSE_MONERO=0
CFLAGS += -I./
ifneq ($(BITCOIN_ONLY),1)
CFLAGS += -DUSE_ETHEREUM=1
CFLAGS += -DUSE_NEM=1
MAKO_RENDER_FLAG =
else
CFLAGS += -DUSE_ETHEREUM=0
CFLAGS += -DUSE_NEM=0
MAKO_RENDER_FLAG = --bitcoin-only
endif

# disable certain optimizations and features when small footprint is required
CFLAGS += -DUSE_PRECOMPUTED_CP=0

CFLAGS += -DCBOR_NO_FLOATING_POINT=l

CFLAGS += -DUSE_CARDANO=1

%:: %.mako defs
	@printf "  MAKO    $@\n"
	$(Q)$(PYTHON) ../vendor/trezor-common/tools/cointool.py render $(MAKO_RENDER_FLAG) $@.mako

ifeq ($(BOOTLOADER_QA), 0)
bl_data.h: bl_data.py bootloader.dat
	@printf "  PYTHON  bl_data.py bootloader.dat\n"
	$(Q)$(PYTHON) bl_data.py bootloader.dat
else
bl_data.h: bl_data.py bootloader_qa.dat
	@printf "  PYTHON  bl_data.py bootloader_qa.dat\n"
	$(Q)$(PYTHON) bl_data.py bootloader_qa.dat
endif

header.o: version.h

clean::
	rm -f bl_data.h
	find -maxdepth 1 -name "*.mako" | sed 's/.mako$$//' | xargs rm -f
