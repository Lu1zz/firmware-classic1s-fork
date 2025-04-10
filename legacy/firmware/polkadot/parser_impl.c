#include "parser_impl.h"
#include "bignum.h"
#include "coin.h"
#include "crypto_helper.h"
#include "parser_common.h"
#include "parser_txdef.h"
#include "substrate/substrate_dispatch.h"
#include "substrate/substrate_types.h"

parser_error_t _polkadot_readTx(parser_context_t *c, parser_tx_t *v,
                                bool mode_enabled) {
  CHECK_INPUT()

  // Now forward parse
  CHECK_ERROR(_readCallIndex(c, &v->callIndex))
  CHECK_ERROR(
      _readMethod(c, v->callIndex.moduleIdx, v->callIndex.idx, &v->method))
  CHECK_ERROR(_readEra(c, &v->era))
  CHECK_ERROR(_readCompactIndex(c, &v->nonce))
  CHECK_ERROR(_readCompactBalance(c, &v->tip))
  if (mode_enabled) {
    CHECK_ERROR(_readu8(c, &v->mode))
  }
  CHECK_ERROR(_readu32(c, &v->specVersion))
  CHECK_ERROR(_readu32(c, &v->transactionVersion))
  CHECK_ERROR(_readHash(c, &v->genesisHash))
  CHECK_ERROR(_readHash(c, &v->blockHash))
  if (mode_enabled) {
    uint8_t optMetadataHash = 0;
    CHECK_ERROR(_readu8(c, &optMetadataHash));
    // Reject the transaction if Mode=Enabled or MetadataDigest is present
    if (v->mode == 1 || optMetadataHash == 1) {
      return parser_unexpected_value;
    }
  }
  if (c->offset < c->bufferLen) {
    return parser_unexpected_unparsed_bytes;
  }

  if (c->offset > c->bufferLen) {
    return parser_unexpected_buffer_end;
  }
  return parser_ok;
}
