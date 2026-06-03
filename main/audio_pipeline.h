// audio_pipeline.h
#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include "esp_err.h"
#include <stdint.h>
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle. NOTE: we intentionally do NOT include sip_client.h here to
// avoid a circular include (sip_client.h includes audio_pipeline.h). The SIP
// handle is passed as a void* and cast internally.
typedef struct audio_pipeline_s* audio_pipeline_handle_t;

audio_pipeline_handle_t audio_pipeline_init(void);
esp_err_t audio_pipeline_start(audio_pipeline_handle_t handle, uint16_t local_rtp_port);
esp_err_t audio_pipeline_stop(audio_pipeline_handle_t handle);
void      audio_pipeline_delete(audio_pipeline_handle_t handle);
void      audio_pipeline_set_sip_handle(audio_pipeline_handle_t handle, void* sip_handle);
void      audio_pipeline_set_wake_word_cb(audio_pipeline_handle_t handle, void (*cb)(void));

#ifdef __cplusplus
}
#endif

#endif // AUDIO_PIPELINE_H
