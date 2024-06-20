#include "client.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha1.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include "utils.h"
#include "vcsec.pb.h"

namespace TeslaBLE {

/*
 * This sets the counter to the last known counter by
 * the car and increments the value
 *
 * @param counter Last known counter sent by the carz
 * @return void
 */
void Client::SetCounter(const u_int32_t *counter) {
  this->counter_ = *counter + 1;
}

/*
 * This will create a new private key, public key
 * and generates the key_id
 *
 * @return int result code 0 for successful
 */
int Client::CreatePrivateKey() {
  mbedtls_entropy_context entropy_context;
  mbedtls_entropy_init(&entropy_context);
  mbedtls_pk_init(&this->private_key_context_);
  mbedtls_ctr_drbg_init(&this->drbg_context_);

  int return_code = mbedtls_ctr_drbg_seed(&drbg_context_, mbedtls_entropy_func,
                                          &entropy_context, nullptr, 0);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code = mbedtls_pk_setup(
      &this->private_key_context_,
      mbedtls_pk_info_from_type((mbedtls_pk_type_t)MBEDTLS_PK_ECKEY));

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code = mbedtls_ecp_gen_key(
      MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(this->private_key_context_),
      mbedtls_ctr_drbg_random, &this->drbg_context_);

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return this->GeneratePublicKey();
}

/*
 * This will load an existing private key
 * and generates the public key and key_id
 *
 * @return int result code 0 for successful
 */
int Client::LoadPrivateKey(const uint8_t *private_key_buffer,
                           size_t private_key_length) {
  mbedtls_entropy_context entropy_context;
  mbedtls_entropy_init(&entropy_context);
  mbedtls_pk_init(&this->private_key_context_);
  mbedtls_ctr_drbg_init(&this->drbg_context_);

  int return_code = mbedtls_ctr_drbg_seed(&drbg_context_, mbedtls_entropy_func,
                                          &entropy_context, nullptr, 0);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  unsigned char password[0];
  return_code = mbedtls_pk_parse_key(
      &this->private_key_context_, private_key_buffer, private_key_length,
      password, 0, mbedtls_ctr_drbg_random, &this->drbg_context_);

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return this->GeneratePublicKey();
}

/*
 * This will return the private key in the pem format
 *
 * @param output_buffer Pointer of the buffer where should be written to
 * @param output_buffer_length Size of the output buffer
 * @param output_length Pointer to size_t that will store the written length
 * @return int result code 0 for successful
 */
int Client::GetPrivateKey(unsigned char *output_buffer,
                          size_t output_buffer_length, size_t *output_length) {
  int return_code = mbedtls_pk_write_key_pem(
      &this->private_key_context_, output_buffer, output_buffer_length);

  if (return_code != 0) {
    printf("Failed to write private key");
    return 1;
  }

  *output_length = strlen((char *)output_buffer) + 1;
  return 0;
}

/*
 * This generates the public key from the private key
 *
 * @return int result code 0 for successful
 */
int Client::GeneratePublicKey() {
  int return_code = mbedtls_ecp_point_write_binary(
      &mbedtls_pk_ec(this->private_key_context_)->private_grp,
      &mbedtls_pk_ec(this->private_key_context_)->private_Q,
      MBEDTLS_ECP_PF_UNCOMPRESSED, &this->public_key_size_, this->public_key_,
      sizeof(this->public_key_));

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return this->GenerateKeyId();
}

/*
 * This generates the key id from the public key
 *
 * @return int result code 0 for successful
 */
int Client::GenerateKeyId() {
  mbedtls_sha1_context sha1_context;
  mbedtls_sha1_init(&sha1_context);

  int return_code = mbedtls_sha1_starts(&sha1_context);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code = mbedtls_sha1_update(&sha1_context, this->public_key_,
                                    this->public_key_size_);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  unsigned char buffer[20];
  return_code = mbedtls_sha1_finish(&sha1_context, buffer);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  // we only need the first 4 bytes
  memcpy(this->key_id_, buffer, 4);
  mbedtls_sha1_free(&sha1_context);
  return 0;
}

/*
 * This will load the cars public key and
 * generates the shared secret
 *
 * @param public_key_buffer Pointer to where the public key buffer
 * @param public_key_size Size of the cars public key
 * @return int result code 0 for successful
 */
int Client::LoadTeslaKey(const uint8_t *public_key_buffer,
                         size_t public_key_size) {
  unsigned char temp_shared_secret[MBEDTLS_ECP_MAX_BYTES];
  size_t temp_shared_secret_length = 0;
  mbedtls_ecp_keypair_init(&this->tesla_key_);

  int return_code = mbedtls_ecp_group_load(&this->tesla_key_.private_grp,
                                           MBEDTLS_ECP_DP_SECP256R1);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code = mbedtls_ecp_point_read_binary(
      &this->tesla_key_.private_grp, &this->tesla_key_.private_Q,
      public_key_buffer, public_key_size);

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  mbedtls_ecdh_init(&this->ecdh_context_);

  return_code = mbedtls_ecdh_get_params(
      &this->ecdh_context_, mbedtls_pk_ec(this->private_key_context_),
      MBEDTLS_ECDH_OURS);

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code = mbedtls_ecdh_get_params(&this->ecdh_context_, &this->tesla_key_,
                                        MBEDTLS_ECDH_THEIRS);

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code =
      mbedtls_ecdh_calc_secret(&this->ecdh_context_, &temp_shared_secret_length,
                               temp_shared_secret, sizeof(temp_shared_secret),
                               mbedtls_ctr_drbg_random, &this->drbg_context_);

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  mbedtls_sha1_context sha1_context;
  mbedtls_sha1_init(&sha1_context);

  return_code = mbedtls_sha1_starts(&sha1_context);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code = mbedtls_sha1_update(&sha1_context, temp_shared_secret,
                                    temp_shared_secret_length);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code = mbedtls_sha1_finish(&sha1_context, this->shared_secret_);
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  mbedtls_sha1_free(&sha1_context);
  return 0;
}

/*
 * This will load the cars public key and
 * generates the shared secret
 *
 * @param input_buffer Pointer to the input buffer
 * @param input_buffer_length Size of the input buffer
 * @param output_buffer Pointer to the output buffer
 * @param output_buffer_length Size of the output buffer
 * @param output_length Pointer to size_t that will store the written length
 * @param signature_buffer Pointer to the signature buffer
 * @return int result code 0 for successful
 */
int Client::Encrypt(unsigned char *input_buffer, size_t input_buffer_length,
                    unsigned char *output_buffer, size_t output_buffer_length,
                    size_t *output_length, unsigned char *signature_buffer) {
  mbedtls_gcm_context aes_context;
  mbedtls_gcm_init(&aes_context);

  int return_code = mbedtls_gcm_setkey(&aes_context, MBEDTLS_CIPHER_ID_AES,
                                       this->shared_secret_, 128);

  uint8_t nonce[4];
  nonce[0] = (this->counter_ >> 24) & 255;
  nonce[1] = (this->counter_ >> 16) & 255;
  nonce[2] = (this->counter_ >> 8) & 255;
  nonce[3] = this->counter_ & 255;

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code = mbedtls_gcm_starts(&aes_context, MBEDTLS_GCM_ENCRYPT, nonce,
                                   sizeof(nonce));
  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  return_code =
      mbedtls_gcm_update(&aes_context, input_buffer, input_buffer_length,
                         output_buffer, output_buffer_length, output_length);

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  size_t finish_buffer_length = 0;
  unsigned char finish_buffer[15];

  return_code =
      mbedtls_gcm_finish(&aes_context, finish_buffer, sizeof(finish_buffer),
                         &finish_buffer_length, signature_buffer, 16);

  if (return_code != 0) {
    printf("Last error was: -0x%04x\n\n", (unsigned int)-return_code);
    return 1;
  }

  if (finish_buffer_length > 0) {
    memcpy(output_buffer + *output_length, finish_buffer, finish_buffer_length);
    *output_length = output_buffer_length + finish_buffer_length;
  }

  mbedtls_gcm_free(&aes_context);
  return 0;
}

/*
 * This will clean up the contexts used
 */
void Client::Cleanup() {
  mbedtls_pk_free(&this->private_key_context_);
  mbedtls_ecp_keypair_free(&this->tesla_key_);
  mbedtls_ecdh_free(&this->ecdh_context_);
  mbedtls_ctr_drbg_free(&this->drbg_context_);
}

/*
 * This prepends the size of the message to the
 * front of the message
 *
 * @param input_buffer Pointer to the input buffer
 * @param input_buffer_length Size of the input buffer
 * @param output_buffer Pointer to the output buffer
 * @param output_length Pointer to size_t that will store the written length
 */
void Client::PrependLength(const unsigned char *input_buffer,
                           size_t input_buffer_length,
                           unsigned char *output_buffer,
                           size_t *output_buffer_length) {
  uint8_t higher_byte = input_buffer_length >> 8;
  uint8_t lower_byte = input_buffer_length & 0xFF;

  uint8_t temp_buffer[2];
  temp_buffer[0] = higher_byte;
  temp_buffer[1] = lower_byte;

  memcpy(output_buffer, temp_buffer, sizeof(temp_buffer));
  memcpy(output_buffer + 2, input_buffer, input_buffer_length);
  *output_buffer_length = input_buffer_length + 2;
}

/*
 * This will build the message need to whitelist
 * the public key in the car.
 * Beware that the car does not show any signs of that
 * interaction before you tab your keyboard on the reader
 *
 * @param input_buffer Pointer to the input buffer
 * @param input_buffer_length Size of the input buffer
 * @param output_buffer Pointer to the output buffer
 * @param output_length Pointer to size_t that will store the written length
 * @return int result code 0 for successful
 */
int Client::BuildWhiteListMessage(unsigned char *output_buffer,
                                  size_t *output_length) {
  VCSEC_PermissionChange permissions_action =
      VCSEC_PermissionChange_init_default;
  permissions_action.has_key = true;
  memcpy(permissions_action.key.PublicKeyRaw.bytes, this->public_key_,
         this->public_key_size_);
  permissions_action.key.PublicKeyRaw.size = this->public_key_size_;

  permissions_action.permission[0] =
      VCSEC_WhitelistKeyPermission_E_WHITELISTKEYPERMISSION_LOCAL_UNLOCK;
  permissions_action.permission[1] =
      VCSEC_WhitelistKeyPermission_E_WHITELISTKEYPERMISSION_LOCAL_DRIVE;
  permissions_action.permission_count = 2;

  VCSEC_WhitelistOperation whitelist = VCSEC_WhitelistOperation_init_default;
  whitelist.which_sub_message =
      VCSEC_WhitelistOperation_addKeyToWhitelistAndAddPermissions_tag;
  whitelist.sub_message.addKeyToWhitelistAndAddPermissions = permissions_action;

  whitelist.has_metadataForKey = true;
  whitelist.metadataForKey.keyFormFactor =
      VCSEC_KeyFormFactor_KEY_FORM_FACTOR_ANDROID_DEVICE;

  VCSEC_UnsignedMessage unsigned_message = VCSEC_UnsignedMessage_init_default;
  unsigned_message.which_sub_message =
      VCSEC_UnsignedMessage_WhitelistOperation_tag;
  unsigned_message.sub_message.WhitelistOperation = whitelist;

  pb_ostream_t unsigned_message_size_stream = {nullptr};
  bool unsigned_message_size_stream_status =
      pb_encode(&unsigned_message_size_stream, VCSEC_UnsignedMessage_fields,
                &unsigned_message);
  if (!unsigned_message_size_stream_status) {
    printf("Failed to encode unsigned message: %s",
           PB_GET_ERROR(&unsigned_message_size_stream));
    return 1;
  }

  uint8_t unsigned_message_buffer[unsigned_message_size_stream.bytes_written];
  pb_ostream_t unsigned_message_stream = pb_ostream_from_buffer(
      unsigned_message_buffer, sizeof(unsigned_message_buffer));
  bool unsigned_message_status =
      pb_encode(&unsigned_message_stream, VCSEC_UnsignedMessage_fields,
                &unsigned_message);

  if (!unsigned_message_status) {
    printf("Failed to encode unsigned message: %s",
           PB_GET_ERROR(&unsigned_message_stream));
    return 1;
  }

  VCSEC_ToVCSECMessage vcsec_message = VCSEC_ToVCSECMessage_init_default;
  vcsec_message.which_sub_message = VCSEC_ToVCSECMessage_signedMessage_tag;
  vcsec_message.sub_message.signedMessage.signatureType =
      VCSEC_SignatureType_SIGNATURE_TYPE_PRESENT_KEY;
  memcpy(vcsec_message.sub_message.signedMessage.protobufMessageAsBytes.bytes,
         unsigned_message_buffer, unsigned_message_size_stream.bytes_written);
  vcsec_message.sub_message.signedMessage.protobufMessageAsBytes.size =
      unsigned_message_size_stream.bytes_written;

  pb_ostream_t vcsec_message_size_stream = {nullptr};
  bool vcsec_message_size_stream_status = pb_encode(
      &vcsec_message_size_stream, VCSEC_ToVCSECMessage_fields, &vcsec_message);
  if (!vcsec_message_size_stream_status) {
    printf("Failed to encode vcsec message: %s",
           PB_GET_ERROR(&vcsec_message_size_stream));
    return 1;
  }

  uint8_t vcsec_message_buffer[vcsec_message_size_stream.bytes_written];
  pb_ostream_t vcsec_message_stream = pb_ostream_from_buffer(
      vcsec_message_buffer, vcsec_message_size_stream.bytes_written);
  bool to_status = pb_encode(&vcsec_message_stream, VCSEC_ToVCSECMessage_fields,
                             &vcsec_message);
  if (!to_status) {
    printf("Failed to encode vcsec message: %s",
           PB_GET_ERROR(&vcsec_message_stream));
    return 1;
  }

  this->PrependLength(vcsec_message_buffer, vcsec_message_stream.bytes_written,
                      output_buffer, output_length);
  return 0;
}

/*
 * This will parse the incoming message
 *
 * @param input_buffer Pointer to the input buffer
 * @param input_buffer_length Size of the input buffer
 * @param output_message Pointer to the output message
 * @return int result code 0 for successful
 */
int Client::ParseFromVCSECMessage(unsigned char *input_buffer,
                                  size_t input_buffer_length,
                                  VCSEC_FromVCSECMessage *output_message) {
  unsigned char temp[input_buffer_length - 2];
  memcpy(&temp, input_buffer + 2, input_buffer_length - 2);

  pb_istream_t stream = pb_istream_from_buffer(temp, sizeof(temp));
  bool status =
      pb_decode(&stream, VCSEC_FromVCSECMessage_fields, output_message);
  if (!status) {
    printf("Decoding failed: %s\n", PB_GET_ERROR(&stream));
    return 1;
  }

  return 0;
}

/*
 * This build the message to ask the car for his
 * ephemeral public key
 *
 * @param output_buffer Pointer to the output buffer
 * @param output_length Size of the output buffer
 * @return int result code 0 for successful
 */
int Client::BuildEphemeralKeyMessage(unsigned char *output_buffer,
                                     size_t *output_length) {
  VCSEC_InformationRequest informationRequest =
      VCSEC_InformationRequest_init_default;
  informationRequest.informationRequestType =
      VCSEC_InformationRequestType_INFORMATION_REQUEST_TYPE_GET_EPHEMERAL_PUBLIC_KEY;

  VCSEC_KeyIdentifier keyIdentifier = VCSEC_KeyIdentifier_init_default;
  memcpy(keyIdentifier.publicKeySHA1, this->key_id_, 4);
  informationRequest.sub_message.keyId = keyIdentifier;
  informationRequest.which_sub_message = VCSEC_InformationRequest_keyId_tag;

  VCSEC_UnsignedMessage message = VCSEC_UnsignedMessage_init_default;
  message.which_sub_message = VCSEC_UnsignedMessage_InformationRequest_tag;
  message.sub_message.InformationRequest = informationRequest;

  this->BuildUnsignedToMessage(&message, output_buffer, output_length);
  return 0;
}

/*
 * This will build an unsigned message
 *
 * @param message Pointer to the message
 * @param output_buffer Pointer to the output buffer
 * @param output_length Size of the output buffer
 * @return int result code 0 for successful
 */
int Client::BuildUnsignedToMessage(VCSEC_UnsignedMessage *message,
                                   unsigned char *output_buffer,
                                   size_t *output_length) {
  pb_ostream_t unsigned_message_size_stream = {nullptr};
  bool unsigned_message_size_stream_status = pb_encode(
      &unsigned_message_size_stream, VCSEC_UnsignedMessage_fields, message);
  if (!unsigned_message_size_stream_status) {
    printf("Failed to encode message: %s",
           PB_GET_ERROR(&unsigned_message_size_stream));
    return 1;
  }

  uint8_t unsigned_message_buffer[unsigned_message_size_stream.bytes_written];
  pb_ostream_t unsigned_message_stream = pb_ostream_from_buffer(
      unsigned_message_buffer, sizeof(unsigned_message_buffer));
  bool status = pb_encode(&unsigned_message_stream,
                          VCSEC_UnsignedMessage_fields, message);

  if (!status) {
    printf("Failed to encode vcsec message: %s",
           PB_GET_ERROR(&unsigned_message_stream));
    return 1;
  }

  VCSEC_ToVCSECMessage vcsec_message = VCSEC_ToVCSECMessage_init_default;
  vcsec_message.which_sub_message = VCSEC_ToVCSECMessage_unsignedMessage_tag;
  vcsec_message.sub_message.unsignedMessage = *message;

  pb_ostream_t vcsec_message_size_stream = {nullptr};
  bool vcsec_message_size_stream_status = pb_encode(
      &vcsec_message_size_stream, VCSEC_ToVCSECMessage_fields, &vcsec_message);
  if (!vcsec_message_size_stream_status) {
    printf("Failed to encode vcsec message: %s",
           PB_GET_ERROR(&vcsec_message_size_stream));
    return 1;
  }

  uint8_t vcsec_message_buffer[vcsec_message_size_stream.bytes_written];
  pb_ostream_t vcsec_message_stream = pb_ostream_from_buffer(
      vcsec_message_buffer, vcsec_message_size_stream.bytes_written);
  bool vcsec_message_status = pb_encode(
      &vcsec_message_stream, VCSEC_ToVCSECMessage_fields, &vcsec_message);
  if (!vcsec_message_status) {
    printf("Failed to encode vcsec message: %s",
           PB_GET_ERROR(&vcsec_message_size_stream));
    return 1;
  }

  this->PrependLength(vcsec_message_buffer, vcsec_message_stream.bytes_written,
                      output_buffer, output_length);
  return 0;
}

/*
 * This will build a signed message
 *
 * @param message Pointer to the message
 * @param output_buffer Pointer to the output buffer
 * @param output_length Size of the output buffer
 * @return int result code 0 for successful
 */
int Client::BuildSignedToMessage(VCSEC_UnsignedMessage *message,
                                 unsigned char *output_buffer,
                                 size_t *output_length) {
  VCSEC_ToVCSECMessage vcsec_message = VCSEC_ToVCSECMessage_init_default;
  vcsec_message.which_sub_message = VCSEC_ToVCSECMessage_unsignedMessage_tag;
  vcsec_message.sub_message.unsignedMessage = *message;

  pb_ostream_t vcsec_message_size_stream = {nullptr};
  bool vcsec_message_stream_status = pb_encode(
      &vcsec_message_size_stream, VCSEC_ToVCSECMessage_fields, &vcsec_message);
  if (!vcsec_message_stream_status) {
    printf("Failed to encode vcsec message: %s",
           PB_GET_ERROR(&vcsec_message_size_stream));
    return 1;
  }

  unsigned char vcsec_message_message[vcsec_message_size_stream.bytes_written];
  pb_ostream_t vcsec_message_stream = pb_ostream_from_buffer(
      vcsec_message_message, sizeof(vcsec_message_message));

  bool vcsec_message_status = pb_encode(
      &vcsec_message_stream, VCSEC_ToVCSECMessage_fields, &vcsec_message);

  if (!vcsec_message_status) {
    printf("Failed to encode vcsec message: %s",
           PB_GET_ERROR(&vcsec_message_stream));
    return 1;
  }

  TeslaBLE::DumpHexBuffer("before", vcsec_message_message,
                          vcsec_message_stream.bytes_written);

  size_t encrypted_output_length = 0;
  unsigned char signature[16];
  unsigned char signed_message_buffer[vcsec_message_stream.bytes_written];

  this->Encrypt(vcsec_message_message, vcsec_message_stream.bytes_written,
                signed_message_buffer, sizeof(signed_message_buffer),
                &encrypted_output_length, signature);

  VCSEC_ToVCSECMessage encrypted_vcsec_message =
      VCSEC_ToVCSECMessage_init_default;
  encrypted_vcsec_message.which_sub_message =
      VCSEC_ToVCSECMessage_signedMessage_tag;
  encrypted_vcsec_message.sub_message.signedMessage.counter = this->counter_;
  memcpy(encrypted_vcsec_message.sub_message.signedMessage.keyId.bytes,
         this->key_id_, sizeof(this->key_id_));
  encrypted_vcsec_message.sub_message.signedMessage.keyId.size =
      sizeof(this->key_id_);
  memcpy(encrypted_vcsec_message.sub_message.signedMessage.signature.bytes,
         signature, sizeof(signature));
  encrypted_vcsec_message.sub_message.signedMessage.signature.size =
      sizeof(signature);
  memcpy(encrypted_vcsec_message.sub_message.signedMessage
             .protobufMessageAsBytes.bytes,
         signed_message_buffer, encrypted_output_length);
  encrypted_vcsec_message.sub_message.signedMessage.protobufMessageAsBytes
      .size = encrypted_output_length;
  encrypted_vcsec_message.sub_message.signedMessage.signatureType =
      VCSEC_SignatureType_SIGNATURE_TYPE_AES_GCM;

  pb_ostream_t encrypted_vcsec_message_size_stream = {nullptr};
  bool encrypted_vcsec_message_size_stream_status =
      pb_encode(&encrypted_vcsec_message_size_stream,
                VCSEC_ToVCSECMessage_fields, &encrypted_vcsec_message);
  if (!encrypted_vcsec_message_size_stream_status) {
    printf("Failed to encode vcsec message: %s",
           PB_GET_ERROR(&encrypted_vcsec_message_size_stream));
    return 1;
  }

  uint8_t encrypted_vcsec_message_buffer[encrypted_vcsec_message_size_stream
                                             .bytes_written];
  pb_ostream_t encrypted_vcsec_message_stream =
      pb_ostream_from_buffer(encrypted_vcsec_message_buffer,
                             encrypted_vcsec_message_size_stream.bytes_written);
  bool to_status =
      pb_encode(&encrypted_vcsec_message_stream, VCSEC_ToVCSECMessage_fields,
                &encrypted_vcsec_message);
  if (!to_status) {
    printf("Failed to encode message: %s",
           PB_GET_ERROR(&encrypted_vcsec_message_stream));
    return 1;
  }

  this->PrependLength(encrypted_vcsec_message_buffer,
                      encrypted_vcsec_message_stream.bytes_written,
                      output_buffer, output_length);
  return 0;
}

/*
 * This will build an action message to for
 * example open the trunk
 *
 * @param message Pointer to the message
 * @param output_buffer Pointer to the output buffer
 * @param output_length Size of the output buffer
 * @return int result code 0 for successful
 */
int Client::BuildActionMessage(const VCSEC_RKEAction_E *action,
                               unsigned char *output_buffer,
                               size_t *output_length) {
  VCSEC_UnsignedMessage unsigned_message = VCSEC_UnsignedMessage_init_default;
  unsigned_message.which_sub_message = VCSEC_UnsignedMessage_RKEAction_tag;
  unsigned_message.sub_message.RKEAction = *action;

  return this->BuildSignedToMessage(&unsigned_message, output_buffer,
                                    output_length);
}

/*
 * This will build an authentication level message to for
 * example open the trunk
 *
 * @param message Pointer to the message
 * @param output_buffer Pointer to the output buffer
 * @param output_length Size of the output buffer
 * @return int result code 0 for successful
 */
int Client::BuildAuthenticationResponse(
    const VCSEC_AuthenticationLevel_E *level, unsigned char *output_buffer,
    size_t *output_length) {
  VCSEC_UnsignedMessage unsigned_message = VCSEC_UnsignedMessage_init_default;
  unsigned_message.which_sub_message =
      VCSEC_UnsignedMessage_authenticationResponse_tag;
  unsigned_message.sub_message.authenticationResponse.authenticationLevel =
      *level;

  return this->BuildSignedToMessage(&unsigned_message, output_buffer,
                                    output_length);
}

int Client::BuildInformationRequestMessage(
    const VCSEC_InformationRequestType *information_request_type,
    unsigned char *output_buffer, size_t *output_length) {
  VCSEC_InformationRequest information_request =
      VCSEC_InformationRequest_init_default;
  information_request.informationRequestType = *information_request_type;

  VCSEC_KeyIdentifier key_identifier = VCSEC_KeyIdentifier_init_default;
  memcpy(key_identifier.publicKeySHA1, this->key_id_, 4);
  information_request.sub_message.keyId = key_identifier;
  information_request.which_sub_message = VCSEC_InformationRequest_keyId_tag;

  VCSEC_UnsignedMessage unsigned_message = VCSEC_UnsignedMessage_init_default;
  unsigned_message.which_sub_message =
      VCSEC_UnsignedMessage_InformationRequest_tag;
  unsigned_message.sub_message.InformationRequest = information_request;

  return this->BuildSignedToMessage(&unsigned_message, output_buffer,
                                    output_length);
}
}  // namespace TeslaBLE