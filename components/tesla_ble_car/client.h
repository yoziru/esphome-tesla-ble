#ifndef TESLA_BLE_CLIENT_H_INCLUDED
#define TESLA_BLE_CLIENT_H_INCLUDED
#define MBEDTLS_CONFIG_FILE "mbedtls/esp_config.h"
#include <string>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha1.h"

#include "vcsec.pb.h"

namespace TeslaBLE {
class Client {
 private:
  mbedtls_pk_context private_key_context_{};
  mbedtls_ecp_keypair tesla_key_{};
  mbedtls_ecdh_context ecdh_context_{};
  mbedtls_ctr_drbg_context drbg_context_{};
  unsigned char shared_secret_[MBEDTLS_ECP_MAX_BYTES]{};
  unsigned char key_id_[4]{};
  unsigned char public_key_[MBEDTLS_ECP_MAX_BYTES]{};
  size_t public_key_size_ = 0;
  uint32_t counter_ = 1;

  static void PrependLength(const unsigned char *input_buffer,
                            size_t input_buffer_length,
                            unsigned char *output_buffer,
                            size_t *output_buffer_length);

  int GeneratePublicKey();

  int GenerateKeyId();

  int Encrypt(unsigned char *input_buffer, size_t input_buffer_length,
              unsigned char *output_buffer, size_t output_buffer_length,
              size_t *output_length, unsigned char *signature_buffer);

 public:
  int CreatePrivateKey();

  void SetCounter(const uint32_t *counter);

  int LoadPrivateKey(const uint8_t *private_key_buffer, size_t key_size);

  int GetPrivateKey(unsigned char *output_buffer, size_t buffer_length,
                    size_t *output_length);

  int LoadTeslaKey(const uint8_t *public_key_buffer, size_t key_size);

  void Cleanup();

  int BuildWhiteListMessage(unsigned char *output_buffer,
                            size_t *output_length);

  static int ParseFromVCSECMessage(unsigned char *input_buffer,
                                   size_t input_size,
                                   VCSEC_FromVCSECMessage *output);

  int BuildEphemeralKeyMessage(unsigned char *output_buffer,
                               size_t *output_length);

  int BuildUnsignedToMessage(VCSEC_UnsignedMessage *message,
                             unsigned char *output_buffer,
                             size_t *output_length);

  int BuildActionMessage(const VCSEC_RKEAction_E *action,
                         unsigned char *output_buffer, size_t *output_length);

  int BuildSignedToMessage(VCSEC_UnsignedMessage *message,
                           unsigned char *output, size_t *output_length);

  int BuildAuthenticationResponse(const VCSEC_AuthenticationLevel_E *level,
                                  unsigned char *output_buffer,
                                  size_t *output_length);

  int BuildInformationRequestMessage(
      const VCSEC_InformationRequestType *information_request_type,
      unsigned char *output_buffer, size_t *output_length);
};
}  // namespace TeslaBLE
#endif  // TESLA_BLE_CLIENT_H_INCLUDED
