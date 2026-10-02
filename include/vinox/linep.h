#ifndef VINOX_LINEP_H
#define VINOX_LINEP_H

#include <stddef.h>
#include <stdint.h>

#include "vinox/export.h"
#include "vinox/vinox.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vinox_linep_security_level {
    VINOX_LINEP_SL0_LOCAL  = 0,  /* Unencrypted local socket / IPC (LiNeP Core) */
    VINOX_LINEP_SL1_TOKEN  = 1,  /* PSK framing & token authentication (LiNeP Core) */
    VINOX_LINEP_SL2_MTLS   = 2,  /* Mutual TLS transport security (LiNeP-SL) */
    VINOX_LINEP_SL3_TPM    = 3,  /* TPM 2.0 enclave attestation (LiNeP-SL) */
    VINOX_LINEP_SL4_PROOFS = 4   /* Hardware evidence & zero-trust proofs (LiNeP-SL) */
} vinox_linep_security_level;

typedef enum vinox_linep_host_profile {
    VINOX_LINEP_PROFILE_BACKGROUND = 0,  /* Cooperative low-impact yield to interactive host */
    VINOX_LINEP_PROFILE_BALANCED   = 1,  /* Moderate resource allocation with host headroom */
    VINOX_LINEP_PROFILE_DEDICATED  = 2   /* Dedicated execution full-capacity profile */
} vinox_linep_host_profile;

typedef struct vinox_linep_worker_config {
    uint32_t struct_size;
    const char* server_address;
    uint16_t port;
    vinox_linep_security_level security_level;
    vinox_linep_host_profile host_profile;
    const char* target_device; /* "NPU", "GPU", "CPU" */
    uint32_t max_concurrent_jobs;
    uint32_t payload_limit_bytes;
    uint8_t allow_mock_models;
} vinox_linep_worker_config;

typedef struct vinox_linep_worker vinox_linep_worker;

VINOX_API vinox_status vinox_linep_worker_config_init(vinox_linep_worker_config* config);

VINOX_API vinox_status vinox_linep_worker_create(
    const vinox_linep_worker_config* config,
    vinox_linep_worker** out_worker);

VINOX_API void vinox_linep_worker_destroy(vinox_linep_worker* worker);

VINOX_API vinox_status vinox_linep_worker_start(vinox_linep_worker* worker);
VINOX_API vinox_status vinox_linep_worker_stop(vinox_linep_worker* worker);
VINOX_API int vinox_linep_worker_is_running(const vinox_linep_worker* worker);
VINOX_API uint16_t vinox_linep_worker_get_active_port(const vinox_linep_worker* worker);

typedef struct vinox_linep_session0_npu_status {
    uint32_t struct_size;
    uint32_t session_id;
    uint8_t is_session0;
    uint8_t npu_available;
    char device_name[64];
    char status_message[256];
} vinox_linep_session0_npu_status;

VINOX_API vinox_status vinox_linep_check_session0_npu_readiness(
    vinox_linep_session0_npu_status* out_status);

VINOX_API vinox_status vinox_linep_worker_dial_outbound_lease(
    vinox_linep_worker* worker,
    const char* orchestrator_host,
    uint16_t orchestrator_port,
    const char* sl1_auth_token);

VINOX_API vinox_status vinox_linep_worker_dispatch_request(
    vinox_linep_worker* worker,
    const char* request_id,
    const char* model_id,
    const char* prompt,
    const char* system_prompt,
    const char* target_device,
    uint32_t max_tokens,
    float temperature,
    char** out_response_json);

#ifdef __cplusplus
}
#endif

#endif /* VINOX_LINEP_H */
