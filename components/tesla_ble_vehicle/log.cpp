#include <inttypes.h>
#include <string.h>

#include <esphome/core/helpers.h>
#include <esphome/core/log.h>

#include <car_server.pb.h>
#include <signatures.pb.h>
#include <universal_message.pb.h>
#include <vcsec.pb.h>

using namespace esphome;

// Helper function to convert UniversalMessage_OperationStatus_E enum to string
const char *
operation_status_to_string(UniversalMessage_OperationStatus_E status) {
  switch (status) {
  case UniversalMessage_OperationStatus_E_OPERATIONSTATUS_OK:
    return "OK";
  case UniversalMessage_OperationStatus_E_OPERATIONSTATUS_WAIT:
    return "WAIT";
  case UniversalMessage_OperationStatus_E_OPERATIONSTATUS_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN_STATUS";
  }
}

const char *vcsec_operation_status_to_string(VCSEC_OperationStatus_E status) {
  switch (status) {
  case VCSEC_OperationStatus_E_OPERATIONSTATUS_OK:
    return "OK";
  case VCSEC_OperationStatus_E_OPERATIONSTATUS_WAIT:
    return "WAIT";
  case VCSEC_OperationStatus_E_OPERATIONSTATUS_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN_STATUS";
  }
}

const char *
information_request_type_to_string(VCSEC_InformationRequestType request_type) {
  switch (request_type) {
  case VCSEC_InformationRequestType_INFORMATION_REQUEST_TYPE_GET_STATUS:
    return "GET_STATUS";
  case VCSEC_InformationRequestType_INFORMATION_REQUEST_TYPE_GET_WHITELIST_INFO:
    return "GET_WHITELIST_INFO";
  case VCSEC_InformationRequestType_INFORMATION_REQUEST_TYPE_GET_WHITELIST_ENTRY_INFO:
    return "GET_WHITELIST_ENTRY_INFO";
  default:
    return "UNKNOWN_REQUEST_TYPE";
  }
}

// Helper function to convert UniversalMessage_MessageFault_E enum to string
const char *message_fault_to_string(UniversalMessage_MessageFault_E fault) {
  switch (fault) {
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_NONE:
    return "ERROR_NONE";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_BUSY:
    return "ERROR_BUSY";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_TIMEOUT:
    return "ERROR_TIMEOUT";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_UNKNOWN_KEY_ID:
    return "ERROR_UNKNOWN_KEY_ID";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INACTIVE_KEY:
    return "ERROR_INACTIVE_KEY";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INVALID_SIGNATURE:
    return "ERROR_INVALID_SIGNATURE";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INVALID_TOKEN_OR_COUNTER:
    return "ERROR_INVALID_TOKEN_OR_COUNTER";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INSUFFICIENT_PRIVILEGES:
    return "ERROR_INSUFFICIENT_PRIVILEGES";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INVALID_DOMAINS:
    return "ERROR_INVALID_DOMAINS";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INVALID_COMMAND:
    return "ERROR_INVALID_COMMAND";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_DECODING:
    return "ERROR_DECODING";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INTERNAL:
    return "ERROR_INTERNAL";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_WRONG_PERSONALIZATION:
    return "ERROR_WRONG_PERSONALIZATION";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_BAD_PARAMETER:
    return "ERROR_BAD_PARAMETER";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_KEYCHAIN_IS_FULL:
    return "ERROR_KEYCHAIN_IS_FULL";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_INCORRECT_EPOCH:
    return "ERROR_INCORRECT_EPOCH";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_IV_INCORRECT_LENGTH:
    return "ERROR_IV_INCORRECT_LENGTH";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_TIME_EXPIRED:
    return "ERROR_TIME_EXPIRED";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_NOT_PROVISIONED_WITH_IDENTITY:
    return "ERROR_NOT_PROVISIONED_WITH_IDENTITY";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_COULD_NOT_HASH_METADATA:
    return "ERROR_COULD_NOT_HASH_METADATA";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_TIME_TO_LIVE_TOO_LONG:
    return "ERROR_TIME_TO_LIVE_TOO_LONG";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_REMOTE_ACCESS_DISABLED:
    return "ERROR_REMOTE_ACCESS_DISABLED";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_REMOTE_SERVICE_ACCESS_DISABLED:
    return "ERROR_REMOTE_SERVICE_ACCESS_DISABLED";
  case UniversalMessage_MessageFault_E_MESSAGEFAULT_ERROR_COMMAND_REQUIRES_ACCOUNT_CREDENTIALS:
    return "ERROR_COMMAND_REQUIRES_ACCOUNT_CREDENTIALS";
  default:
    return "UNKNOWN_FAULT";
  }
}

// Function to log UniversalMessage_MessageStatus
void log_message_status(const char *tag,
                        const UniversalMessage_MessageStatus *status) {
  ESP_LOGE(tag, "  MessageStatus:");
  ESP_LOGE(tag, "    operation_status: %s",
           operation_status_to_string(status->operation_status));
  ESP_LOGE(tag, "    signed_message_fault: %s",
           message_fault_to_string(status->signed_message_fault));
}

const char *vssec_signed_message_information_to_string(
    VCSEC_SignedMessage_information_E information) {
  switch (information) {
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_NONE:
    return "NONE";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_UNKNOWN:
    return "UNKNOWN";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_NOT_ON_WHITELIST:
    return "NOT_ON_WHITELIST";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_IV_SMALLER_THAN_EXPECTED:
    return "IV_SMALLER_THAN_EXPECTED";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_INVALID_TOKEN:
    return "INVALID_TOKEN";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_TOKEN_AND_COUNTER_INVALID:
    return "TOKEN_AND_COUNTER_INVALID";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_AES_DECRYPT_AUTH:
    return "AES_DECRYPT_AUTH";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_ECDSA_INPUT:
    return "ECDSA_INPUT";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_ECDSA_SIGNATURE:
    return "ECDSA_SIGNATURE";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_LOCAL_ENTITY_START:
    return "LOCAL_ENTITY_START";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_LOCAL_ENTITY_RESULT:
    return "LOCAL_ENTITY_RESULT";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_COULD_NOT_RETRIEVE_KEY:
    return "COULD_NOT_RETRIEVE_KEY";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_COULD_NOT_RETRIEVE_TOKEN:
    return "COULD_NOT_RETRIEVE_TOKEN";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_SIGNATURE_TOO_SHORT:
    return "SIGNATURE_TOO_SHORT";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_TOKEN_IS_INCORRECT_LENGTH:
    return "TOKEN_IS_INCORRECT_LENGTH";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_INCORRECT_EPOCH:
    return "INCORRECT_EPOCH";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_IV_INCORRECT_LENGTH:
    return "IV_INCORRECT_LENGTH";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_TIME_EXPIRED:
    return "TIME_EXPIRED";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_NOT_PROVISIONED_WITH_IDENTITY:
    return "NOT_PROVISIONED_WITH_IDENTITY";
  case VCSEC_SignedMessage_information_E_SIGNEDMESSAGE_INFORMATION_FAULT_COULD_NOT_HASH_METADATA:
    return "COULD_NOT_HASH_METADATA";
  default:
    return "UNKNOWN_INFORMATION";
  }
}

const char *vssec_whitelist_operation_information_to_string(
    VCSEC_WhitelistOperation_information_E request_type) {
  switch (request_type) {
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_NONE:
    return "NONE";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_UNDOCUMENTED_ERROR:
    return "UNDOCUMENTED_ERROR";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_NO_PERMISSION_TO_REMOVE_ONESELF:
    return "NO_PERMISSION_TO_REMOVE_ONESELF";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_KEYFOB_SLOTS_FULL:
    return "KEYFOB_SLOTS_FULL";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_WHITELIST_FULL:
    return "WHITELIST_FULL";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_NO_PERMISSION_TO_ADD:
    return "NO_PERMISSION_TO_ADD";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_INVALID_PUBLIC_KEY:
    return "INVALID_PUBLIC_KEY";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_NO_PERMISSION_TO_REMOVE:
    return "NO_PERMISSION_TO_REMOVE";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_NO_PERMISSION_TO_CHANGE_PERMISSIONS:
    return "NO_PERMISSION_TO_CHANGE_PERMISSIONS";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_ATTEMPTING_TO_ELEVATE_OTHER_ABOVE_ONESELF:
    return "ATTEMPTING_TO_ELEVATE_OTHER_ABOVE_ONESELF";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_ATTEMPTING_TO_DEMOTE_SUPERIOR_TO_ONESELF:
    return "ATTEMPTING_TO_DEMOTE_SUPERIOR_TO_ONESELF";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_ATTEMPTING_TO_REMOVE_OWN_PERMISSIONS:
    return "ATTEMPTING_TO_REMOVE_OWN_PERMISSIONS";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_PUBLIC_KEY_NOT_ON_WHITELIST:
    return "PUBLIC_KEY_NOT_ON_WHITELIST";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_ATTEMPTING_TO_ADD_KEY_THAT_IS_ALREADY_ON_THE_WHITELIST:
    return "ATTEMPTING_TO_ADD_KEY_THAT_IS_ALREADY_ON_THE_WHITELIST";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_NOT_ALLOWED_TO_ADD_UNLESS_ON_READER:
    return "NOT_ALLOWED_TO_ADD_UNLESS_ON_READER";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_FM_MODIFYING_OUTSIDE_OF_F_MODE:
    return "FM_MODIFYING_OUTSIDE_OF_F_MODE";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_FM_ATTEMPTING_TO_ADD_PERMANENT_KEY:
    return "FM_ATTEMPTING_TO_ADD_PERMANENT_KEY";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_FM_ATTEMPTING_TO_REMOVE_PERMANENT_KEY:
    return "FM_ATTEMPTING_TO_REMOVE_PERMANENT_KEY";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_KEYCHAIN_WHILE_FS_FULL:
    return "KEYCHAIN_WHILE_FS_FULL";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_ATTEMPTING_TO_ADD_KEY_WITHOUT_ROLE:
    return "ATTEMPTING_TO_ADD_KEY_WITHOUT_ROLE";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_ATTEMPTING_TO_ADD_KEY_WITH_SERVICE_ROLE:
    return "ATTEMPTING_TO_ADD_KEY_WITH_SERVICE_ROLE";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_NON_SERVICE_KEY_ATTEMPTING_TO_ADD_SERVICE_TECH:
    return "NON_SERVICE_KEY_ATTEMPTING_TO_ADD_SERVICE_TECH";
  case VCSEC_WhitelistOperation_information_E_WHITELISTOPERATION_INFORMATION_SERVICE_KEY_ATTEMPTING_TO_ADD_SERVICE_TECH_OUTSIDE_SERVICE_MODE:
    return "SERVICE_KEY_ATTEMPTING_TO_ADD_SERVICE_TECH_OUTSIDE_SERVICE_MODE";
  default:
    return "UNKNOWN_REQUEST_TYPE";
  }
}

// Function to convert UniversalMessage_Domain enum to string
const char *domain_to_string(UniversalMessage_Domain domain) {
  switch (domain) {
  case UniversalMessage_Domain_DOMAIN_BROADCAST:
    return "DOMAIN_BROADCAST";
  case UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY:
    return "DOMAIN_VEHICLE_SECURITY";
  case UniversalMessage_Domain_DOMAIN_INFOTAINMENT:
    return "DOMAIN_INFOTAINMENT";
  default:
    return "UNKNOWN_DOMAIN";
  }
}

const char *generic_error_to_string(Errors_GenericError_E error) {
  switch (error) {
  case Errors_GenericError_E_GENERICERROR_NONE:
    return "NONE";
  case Errors_GenericError_E_GENERICERROR_UNKNOWN:
    return "UNKNOWN";
  case Errors_GenericError_E_GENERICERROR_CLOSURES_OPEN:
    return "CLOSURES_OPEN";
  case Errors_GenericError_E_GENERICERROR_ALREADY_ON:
    return "ALREADY_ON";
  case Errors_GenericError_E_GENERICERROR_DISABLED_FOR_USER_COMMAND:
    return "DISABLED_FOR_USER_COMMAND";
  case Errors_GenericError_E_GENERICERROR_VEHICLE_NOT_IN_PARK:
    return "VEHICLE_NOT_IN_PARK";
  case Errors_GenericError_E_GENERICERROR_UNAUTHORIZED:
    return "UNAUTHORIZED";
  case Errors_GenericError_E_GENERICERROR_NOT_ALLOWED_OVER_TRANSPORT:
    return "NOT_ALLOWED_OVER_TRANSPORT";
  default:
    return "UNKNOWN_ERROR";
  }
}

void log_destination(const char *tag, const char *direction,
                     const UniversalMessage_Destination *dest) {
  ESP_LOGD(tag, "Destination: %s", direction);
  ESP_LOGD(tag, "  which_sub_destination: %d", dest->which_sub_destination);
  switch (dest->which_sub_destination) {
  case UniversalMessage_Destination_domain_tag:
    ESP_LOGD(tag, "  domain: %s",
             domain_to_string(dest->sub_destination.domain));
    break;
  case UniversalMessage_Destination_routing_address_tag:
    ESP_LOGD(tag, "  routing_address: %s",
             format_hex(dest->sub_destination.routing_address.bytes,
                        dest->sub_destination.routing_address.size)
                 .c_str());
    break;
  default:
    ESP_LOGD(tag, "  unknown sub_destination");
  }
}

void log_session_info_request(const char *tag,
                              const UniversalMessage_SessionInfoRequest *req) {
  ESP_LOGD(tag, "  SessionInfoRequest:");
  ESP_LOGD(tag, "    public_key: %s", req->public_key.bytes);
  ESP_LOGD(tag, "    challenge: %s", req->challenge.bytes);
}

void log_session_info(const char *tag, const Signatures_SessionInfo *req) {
  ESP_LOGD(tag, "SessionInfo:");
  ESP_LOGD(tag, "  counter: %" PRIu32, req->counter);
  ESP_LOGD(tag, "  publicKey: %s",
           format_hex(req->publicKey.bytes, req->publicKey.size).c_str());
  ESP_LOGD(tag, "  epoch: %s", format_hex(req->epoch, 16).c_str());
  ESP_LOGD(tag, "  clock_time: %" PRIu32, req->clock_time);
  ESP_LOGD(tag, "  status: %s",
           req->status == Signatures_Session_Info_Status_SESSION_INFO_STATUS_OK
               ? "OK"
               : "KEY_NOT_ON_WHITELIST");
}

void log_aes_gcm_personalized_signature_data(
    const char *tag,
    const Signatures_AES_GCM_Personalized_Signature_Data *data) {
  ESP_LOGD(tag, "    AES_GCM_Personalized_Signature_Data:");
  ESP_LOGD(tag, "      epoch: %s", format_hex(data->epoch, 16).c_str());
  ESP_LOGD(tag, "      nonce: %s", format_hex(data->nonce, 12).c_str());
  ESP_LOGD(tag, "      counter: %" PRIu32, data->counter);
  ESP_LOGD(tag, "      expires_at: %" PRIu32, data->expires_at);
  ESP_LOGD(tag, "      tag: %s", format_hex(data->tag, 16).c_str());
}

void log_signature_data(const char *tag, const Signatures_SignatureData *sig) {
  ESP_LOGD(tag, "  SignatureData:");
  ESP_LOGD(tag, "    has_signer_identity: %s",
           sig->has_signer_identity ? "true" : "false");
  if (sig->has_signer_identity) {
    ESP_LOGD(tag, "    signer_identity: ");
    ESP_LOGD(tag, "      public_key: %s",
             format_hex(sig->signer_identity.identity_type.public_key.bytes,
                        sig->signer_identity.identity_type.public_key.size)
                 .c_str());
  }
  ESP_LOGD(tag, "    which_sig_type: %d", sig->which_sig_type);
  switch (sig->which_sig_type) {
  case Signatures_SignatureData_AES_GCM_Personalized_data_tag:
    log_aes_gcm_personalized_signature_data(
        tag, &sig->sig_type.AES_GCM_Personalized_data);
    break;
  case Signatures_SignatureData_session_info_tag_tag:
    ESP_LOGD(tag, "    session_info_tag: %s",
             format_hex(sig->sig_type.session_info_tag.tag.bytes,
                        sig->sig_type.session_info_tag.tag.size)
                 .c_str());
    break;
  case Signatures_SignatureData_HMAC_Personalized_data_tag:
    ESP_LOGD(tag, "    HMAC_Personalized_data: ");
    ESP_LOGD(
        tag, "      epoch: %s",
        format_hex(sig->sig_type.HMAC_Personalized_data.epoch, 16).c_str());
    ESP_LOGD(tag, "      counter: %" PRIu32,
             sig->sig_type.HMAC_Personalized_data.counter);
    ESP_LOGD(tag, "      expires_at: %" PRIu32,
             sig->sig_type.HMAC_Personalized_data.expires_at);
    ESP_LOGD(tag, "      tag: %s",
             format_hex(sig->sig_type.HMAC_Personalized_data.tag, 16).c_str());
    break;
  default:
    ESP_LOGD(tag, "    unknown sig_type");
  }
}

void log_information_request(const char *tag,
                             const VCSEC_InformationRequest *msg) {
  ESP_LOGD(tag, "VCSEC_InformationRequest:");
  ESP_LOGD(tag, "  which_request: %d", msg->which_key);

  ESP_LOGD(tag, "  informationRequestType: %s",
           information_request_type_to_string(msg->informationRequestType));
  ESP_LOGD(tag, "  publicKeySHA1: %s",
           format_hex(msg->key.keyId.publicKeySHA1.bytes,
                      msg->key.keyId.publicKeySHA1.size)
               .c_str());
  ESP_LOGD(
      tag, "  publicKey: %s",
      format_hex(msg->key.publicKey.bytes, msg->key.publicKey.size).c_str());
  ESP_LOGD(tag, "  publicKeySHA1: %" PRIu32, msg->key.slot);
}

void log_routable_message(const char *tag,
                          const UniversalMessage_RoutableMessage *msg) {
  ESP_LOGD(tag, "UniversalMessage_RoutableMessage:");
  ESP_LOGD(tag, "  has_to_destination: %s",
           msg->has_to_destination ? "true" : "false");
  if (msg->has_to_destination) {
    log_destination(tag, "to_destination", &msg->to_destination);
  }

  ESP_LOGD(tag, "  has_from_destination: %s",
           msg->has_from_destination ? "true" : "false");
  if (msg->has_from_destination) {
    log_destination(tag, "from_destination", &msg->from_destination);
  }

  ESP_LOGD(tag, "  which_payload: %d", msg->which_payload);
  switch (msg->which_payload) {
  case UniversalMessage_RoutableMessage_protobuf_message_as_bytes_tag:
    ESP_LOGD(tag, "  payload: protobuf_message_as_bytes (callback)");
    // log byte array as string
    ESP_LOGD(tag, "    payload: %s",
             format_hex(msg->payload.protobuf_message_as_bytes.bytes,
                        msg->payload.protobuf_message_as_bytes.size)
                 .c_str());
    break;
  case UniversalMessage_RoutableMessage_session_info_request_tag:
    ESP_LOGD(tag, "  payload: session_info_request");
    log_session_info_request(tag, &msg->payload.session_info_request);
    break;
  case UniversalMessage_RoutableMessage_session_info_tag:
    ESP_LOGD(tag, "  payload: session_info (callback)");
    // log byte array as string
    ESP_LOGD(tag, "    payload: %s",
             format_hex(msg->payload.session_info.bytes,
                        msg->payload.session_info.size)
                 .c_str());
    break;
  default:
    ESP_LOGD(tag, "  payload: unknown");
  }

  ESP_LOGD(tag, "  has_signedMessageStatus: %s",
           msg->has_signedMessageStatus ? "true" : "false");
  if (msg->has_signedMessageStatus) {
    log_message_status(tag, &msg->signedMessageStatus);
  }

  ESP_LOGD(tag, "  which_sub_sigData: %d", msg->which_sub_sigData);
  if (msg->which_sub_sigData ==
      UniversalMessage_RoutableMessage_signature_data_tag) {
    log_signature_data(tag, &msg->sub_sigData.signature_data);
  }

  if (msg->request_uuid.size > 0) {
    ESP_LOGD(
        tag, "  request_uuid: %s",
        format_hex(msg->request_uuid.bytes, msg->request_uuid.size).c_str());
  }
  if (msg->uuid.size > 0) {
    ESP_LOGD(tag, "  uuid: %s",
             format_hex(msg->uuid.bytes, msg->uuid.size).c_str());
  }
  ESP_LOGD(tag, "  flags: %" PRIu32, msg->flags);
}

const char *closure_state_to_string(VCSEC_ClosureState_E state) {
  switch (state) {
  case VCSEC_ClosureState_E_CLOSURESTATE_CLOSED:
    return "CLOSED";
  case VCSEC_ClosureState_E_CLOSURESTATE_OPEN:
    return "OPEN";
  case VCSEC_ClosureState_E_CLOSURESTATE_AJAR:
    return "AJAR";
  case VCSEC_ClosureState_E_CLOSURESTATE_UNKNOWN:
    return "UNKNOWN";
  case VCSEC_ClosureState_E_CLOSURESTATE_FAILED_UNLATCH:
    return "FAILED_UNLATCH";
  case VCSEC_ClosureState_E_CLOSURESTATE_OPENING:
    return "OPENING";
  case VCSEC_ClosureState_E_CLOSURESTATE_CLOSING:
    return "CLOSING";
  default:
    return "UNKNOWN_STATE";
  }
}

const char *vehicle_lock_state_to_string(VCSEC_VehicleLockState_E state) {
  switch (state) {
  case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_UNLOCKED:
    return "UNLOCKED";
  case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_LOCKED:
    return "LOCKED";
  case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_INTERNAL_LOCKED:
    return "INTERNAL_LOCKED";
  case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_SELECTIVE_UNLOCKED:
    return "SELECTIVE_UNLOCKED";
  default:
    return "UNKNOWN_STATE";
  }
}

const char *vehicle_sleep_status_to_string(VCSEC_VehicleSleepStatus_E state) {
  switch (state) {
  case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_UNKNOWN:
    return "UNKNOWN";
  case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE:
    return "AWAKE";
  case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_ASLEEP:
    return "ASLEEP";
  default:
    return "UNKNOWN_STATE";
  }
}

const char *user_presence_to_string(VCSEC_UserPresence_E state) {
  switch (state) {
  case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_UNKNOWN:
    return "UNKNOWN";
  case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_NOT_PRESENT:
    return "NOT_PRESENT";
  case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_PRESENT:
    return "PRESENT";
  default:
    return "UNKNOWN_STATE";
  }
}

void log_vehicle_status(const char *tag, const VCSEC_VehicleStatus *msg) {
  ESP_LOGD(tag, "VCSEC_VehicleStatus:");
  ESP_LOGD(tag, "  has_closureStatuses: %s",
           msg->has_closureStatuses ? "true" : "false");
  if (msg->has_closureStatuses) {
    ESP_LOGD(tag, "  closureStatuses:");
    ESP_LOGD(tag, "    frontDriverDoor: %s",
             closure_state_to_string(msg->closureStatuses.frontDriverDoor));
    ESP_LOGD(tag, "    frontPassengerDoor: %s",
             closure_state_to_string(msg->closureStatuses.frontPassengerDoor));
    ESP_LOGD(tag, "    rearDriverDoor: %s",
             closure_state_to_string(msg->closureStatuses.rearDriverDoor));
    ESP_LOGD(tag, "    rearPassengerDoor: %s",
             closure_state_to_string(msg->closureStatuses.rearPassengerDoor));
    ESP_LOGD(tag, "    rearTrunk: %s",
             closure_state_to_string(msg->closureStatuses.rearTrunk));
    ESP_LOGD(tag, "    frontTrunk: %s",
             closure_state_to_string(msg->closureStatuses.frontTrunk));
    ESP_LOGD(tag, "    chargePort: %s",
             closure_state_to_string(msg->closureStatuses.chargePort));
  }
  ESP_LOGD(tag, "  vehicleLockState: %s",
           vehicle_lock_state_to_string(msg->vehicleLockState));
  ESP_LOGD(tag, "  vehicleSleepStatus: %s",
           vehicle_sleep_status_to_string(msg->vehicleSleepStatus));
  ESP_LOGD(tag, "  userPresence: %s",
           user_presence_to_string(msg->userPresence));
}

void log_vssec_signed_message_status(const char *tag,
                                     const VCSEC_SignedMessage_status *status) {
  ESP_LOGI(tag, "  SignedMessage status:");
  ESP_LOGI(tag, "    counter: %" PRIu32, status->counter);
  ESP_LOGI(tag, "    signed_message_information: %s",
           vssec_signed_message_information_to_string(
               status->signedMessageInformation));
}

void log_vssec_whitelist_operation_status(
    const char *tag, const VCSEC_WhitelistOperation_status *status) {
  ESP_LOGI(tag, "  WhitelistOperation status:");
  // has_signerOfOperation;
  if (status->has_signerOfOperation) {
    ESP_LOGD(tag, "    signerOfOperation:");
    ESP_LOGD(tag, "      public_key: %s",
             format_hex(status->signerOfOperation.publicKeySHA1.bytes,
                        status->signerOfOperation.publicKeySHA1.size)
                 .c_str());
  }
  ESP_LOGI(tag, "    operation_status: %s",
           vcsec_operation_status_to_string(status->operationStatus));
  ESP_LOGI(tag, "    information: %s",
           vssec_whitelist_operation_information_to_string(
               status->whitelistOperationInformation));
}

void log_vcsec_command_status(const char *tag, const VCSEC_CommandStatus *msg) {
  ESP_LOGI(tag, "VCSEC_CommandStatus:");
  ESP_LOGI(tag, "  commandStatus: %s",
           vcsec_operation_status_to_string(msg->operationStatus));

  ESP_LOGI(tag, "  which_sub_message: %d", msg->which_sub_message);
  switch (msg->which_sub_message) {
  case VCSEC_CommandStatus_signedMessageStatus_tag:
    log_vssec_signed_message_status(tag, &msg->sub_message.signedMessageStatus);
    break;
  case VCSEC_CommandStatus_whitelistOperationStatus_tag:
    log_vssec_whitelist_operation_status(
        tag, &msg->sub_message.whitelistOperationStatus);
    break;
  default:
    ESP_LOGD(tag, "  unknown sub_message");
  }
}

void carserver_result_reason_to_string(const char *tag,
                                       const CarServer_ResultReason *reason) {
  ESP_LOGI(tag, "  ResultReason:");
  ESP_LOGI(tag, "    which_reason: %d", reason->which_reason);
  switch (reason->which_reason) {
  case CarServer_ResultReason_plain_text_tag:
    ESP_LOGI(tag, "    plain_text: %s", reason->reason.plain_text);
    break;
  default:
    ESP_LOGD(tag, "    unknown reason");
  }
}

const char *
carserver_operation_status_to_string(CarServer_OperationStatus_E status) {
  switch (status) {
  case CarServer_OperationStatus_E_OPERATIONSTATUS_OK:
    return "OK";
  case CarServer_OperationStatus_E_OPERATIONSTATUS_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN_STATUS";
  }
}

void log_carserver_response(const char *tag, const CarServer_Response *msg) {
  ESP_LOGD(tag, "CarServerResponse:");
  if (msg->has_actionStatus) {
    ESP_LOGD(tag, "  ActionStatus:");
    ESP_LOGD(tag, "    result: %s",
             carserver_operation_status_to_string(msg->actionStatus.result));
    if (msg->actionStatus.has_result_reason) {
      switch (msg->actionStatus.result_reason.which_reason) {
      case CarServer_ResultReason_plain_text_tag:
        ESP_LOGD(tag, "    reason: %s",
                 msg->actionStatus.result_reason.reason.plain_text);
        break;
      default:
        ESP_LOGD(tag, "    unknown reason");
      }
    }
  }

  switch (msg->which_response_msg) {
  case CarServer_Response_getSessionInfoResponse_tag:
    ESP_LOGI(tag, "  getSessionInfoResponse:");
    log_session_info(tag, &msg->response_msg.getSessionInfoResponse);
    break;
  case CarServer_Response_getNearbyChargingSites_tag:
    ESP_LOGI(tag, "  getNearbyChargingSites:");
    break;
  case CarServer_Response_ping_tag:
    ESP_LOGD(tag, "  ping:");
    ESP_LOGD(tag, "    ping: %ld", msg->response_msg.ping.ping_id);
    break;
  default:
    // do nothing
    break;
  }
}
