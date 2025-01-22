#pragma once

#include <car_server.pb.h>
#include <signatures.pb.h>
#include <universal_message.pb.h>
#include <vcsec.pb.h>

// Main logging function for UniversalMessage_RoutableMessage
// Helper functions for nested structures
const char *domain_to_string(UniversalMessage_Domain domain);
const char *
information_request_type_to_string(VCSEC_InformationRequestType request_type);
const char *message_fault_to_string(UniversalMessage_MessageFault_E fault);
const char *
operation_status_to_string(UniversalMessage_OperationStatus_E status);
const char *vcsec_operation_status_to_string(VCSEC_OperationStatus_E status);
const char *vssec_signed_message_information_to_string(
    VCSEC_SignedMessage_information_E information);
const char *closure_state_to_string(VCSEC_ClosureState_E state);
const char *vehicle_lock_state_to_string(VCSEC_VehicleLockState_E state);
const char *vehicle_sleep_status_to_string(VCSEC_VehicleSleepStatus_E state);
const char *user_presence_to_string(VCSEC_UserPresence_E state);
const char *generic_error_to_string(Errors_GenericError_E error);
const char *
carserver_operation_status_to_string(CarServer_OperationStatus_E status);
void log_aes_gcm_personalized_signature_data(
    const char *tag,
    const Signatures_AES_GCM_Personalized_Signature_Data *data);
void log_destination(const char *tag, const char *prefix,
                     const UniversalMessage_Destination *dest);
void log_information_request(const char *tag,
                             const VCSEC_InformationRequest *msg);
void log_message_status(const char *tag,
                        const UniversalMessage_MessageStatus *status);
void log_routable_message(const char *tag,
                          const UniversalMessage_RoutableMessage *msg);
void log_session_info_request(const char *tag,
                              const UniversalMessage_SessionInfoRequest *req);
void log_session_info(const char *tag, const Signatures_SessionInfo *req);
void log_signature_data(const char *tag, const Signatures_SignatureData *sig);
void log_vehicle_status(const char *tag, const VCSEC_VehicleStatus *msg);
void log_vssec_signed_message_status(const char *tag,
                                     const VCSEC_SignedMessage_status *status);
void log_vssec_whitelist_operation_status(
    const char *tag, const VCSEC_WhitelistOperation_status *status);
void log_vcsec_command_status(const char *tag, const VCSEC_CommandStatus *msg);
void log_carserver_response(const char *tag, const CarServer_Response *msg);
