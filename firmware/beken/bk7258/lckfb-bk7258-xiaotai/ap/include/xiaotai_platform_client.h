#ifndef XIAOTAI_PLATFORM_CLIENT_H
#define XIAOTAI_PLATFORM_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#include "xiaotai_platform.h"

#define XIAOTAI_PLATFORM_DISCOVERY_URL "http://ep-open.tangeopen.com/services"
#define XIAOTAI_BIND_TIMEOUT_SECONDS 180U

/**
 * Run the first-binding exchange synchronously on the caller's worker task.
 * The verification code remains available while this call waits for auth_grant.
 */
int xiaotai_platform_bind(xiaotai_device_credentials_t *out);
/** Authenticate with stored credentials and publish the complete product profile. */
int xiaotai_platform_report_capabilities(
    const xiaotai_device_credentials_t *credentials);
/** Copy the TiRTC signaling endpoint returned by discovery. */
int xiaotai_platform_tirtc_endpoint(char *output, size_t capacity);
/** Copy the stable hardware identity used as TiRTC CLIENT_ID. */
int xiaotai_platform_client_id(char *output, size_t capacity);
typedef void (*xiaotai_platform_signal_fn)(const char *json, size_t size,
                                           void *context);
typedef void (*xiaotai_platform_response_fn)(const char *body, void *context);
/**
 * Run the authenticated MQTT control channel on the caller's task.
 * This function reconnects until a non-network configuration error occurs.
 */
int xiaotai_platform_run(const xiaotai_device_credentials_t *credentials,
                         xiaotai_platform_signal_fn signal,
                         void *context);
/** Send a device-authenticated GET (json == NULL) or POST to the service
 * selected from discovery by the path prefix. The callback completes before
 * this function returns. */
int xiaotai_platform_service_request(const char *path, const char *json,
                                     xiaotai_platform_response_fn response,
                                     void *context);
bool xiaotai_platform_binding_active(void);
const char *xiaotai_platform_verification_code(void);

#endif
