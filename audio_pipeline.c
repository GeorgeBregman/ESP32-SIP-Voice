// audio_pipeline.h
#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include "esp_err.h"
#include "lwip/ip_addr.h"
#include "sip_client.h" // Include SIP handle

typedef struct audio_pipeline_s* audio_pipeline_handle_t;

audio_pipeline_handle_t audio_pipeline_init(void);
esp_err_t audio_pipeline_start(audio_pipeline_handle_t handle, uint16_t local_rtp_port);
esp_err_t audio_pipeline_stop(audio_pipeline_handle_t handle);
void audio_pipeline_delete(audio_pipeline_handle_t handle);
void audio_pipeline_set_sip_handle(audio_pipeline_handle_t handle, sip_client_handle_t sip_handle); // Link SIP client

#endif // AUDIO_PIPELINE_H


// audio_pipeline.c
#include "audio_pipeline.h"
#include "app_config.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h" // Optional: For buffering between tasks if needed
#include "g711_codec.h"
#include "rtp_handler.h"
#include "codec_driver.h" // Include your specific codec driver header

static const char* TAG = "AUDIO";

#define I2S_DMA_BUF_COUNT   4 // Number of DMA buffers
#define I2S_DMA_BUF_LEN     (AUDIO_SAMPLES_PER_FRAME * 2) // Size of each DMA buffer (bytes = samples for G711)

typedef struct audio_pipeline_s {
    TaskHandle_t audio_io_task_handle;
    volatile bool is_running;
    rtp_session_handle_t rtp_session;
    sip_client_handle_t sip_client; // Handle to get remote RTP info

    // Buffers for audio processing
    uint8_t i2s_read_buffer[AUDIO_SAMPLES_PER_FRAME];
    uint8_t encoded_buffer[AUDIO_SAMPLES_PER_FRAME]; // G711 encoded
    uint8_t rtp_receive_buffer[RTP_RX_BUFFER_SIZE];
    uint8_t decoded_buffer[AUDIO_SAMPLES_PER_FRAME]; // G711 decoded

} audio_pipeline_t;

static void audio_io_task(void* pvParameters);
static esp_err_t i2s_init(void);
static esp_err_t i2s_deinit(void);

audio_pipeline_handle_t audio_pipeline_init(void) {
    audio_pipeline_t* pipeline = (audio_pipeline_t*)calloc(1, sizeof(audio_pipeline_t));
    if (!pipeline) {
        ESP_LOGE(TAG, "Failed to allocate audio pipeline memory");
        return NULL;
    }

    pipeline->is_running = false;

    // Initialize I2S
    if (i2s_init() != ESP_OK) {
        free(pipeline);
        return NULL;
    }

    // Initialize Codec
    if (codec_init() != ESP_OK) { // Assuming codec_init() in codec_driver.c
        ESP_LOGE(TAG, "Failed to initialize audio codec");
        i2s_deinit();
        free(pipeline);
        return NULL;
    }
     codec_set_mic_gain(30); // Example gain setting
     codec_set_volume(80);  // Example volume setting


    // Create the combined Audio I/O task
    if (xTaskCreate(audio_io_task, "audio_io_task", AUDIO_TASK_STACK_SIZE, pipeline,
                    AUDIO_IO_TASK_PRIORITY, &pipeline->audio_io_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio I/O task");
        codec_deinit(); // Assuming codec_deinit()
        i2s_deinit();
        free(pipeline);
        return NULL;
    }

    ESP_LOGI(TAG, "Audio pipeline initialized.");
    return pipeline;
}

void audio_pipeline_set_sip_handle(audio_pipeline_handle_t handle, sip_client_handle_t sip_handle) {
     audio_pipeline_t* pipeline = (audio_pipeline_t*)handle;
     if (pipeline) {
         pipeline->sip_client = sip_handle;
     }
}


esp_err_t audio_pipeline_start(audio_pipeline_handle_t handle, uint16_t local_rtp_port) {
    audio_pipeline_t* pipeline = (audio_pipeline_t*)handle;
    if (!pipeline || pipeline->is_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pipeline->sip_client) {
        ESP_LOGE(TAG,"SIP client handle not set in audio pipeline!");
        return ESP_FAIL;
    }


    // Create RTP session
    pipeline->rtp_session = rtp_session_create(local_rtp_port);
    if (!pipeline->rtp_session) {
        ESP_LOGE(TAG, "Failed to create RTP session for port %d", local_rtp_port);
        return ESP_FAIL;
    }

    pipeline->is_running = true;
    // Signal the task if it's waiting, or it will check the flag
    ESP_LOGI(TAG, "Audio pipeline started. RTP on port %d", local_rtp_port);
    return ESP_OK;
}

esp_err_t audio_pipeline_stop(audio_pipeline_handle_t handle) {
    audio_pipeline_t* pipeline = (audio_pipeline_t*)handle;
    if (!pipeline || !pipeline->is_running) {
        return ESP_ERR_INVALID_STATE;
    }

    pipeline->is_running = false;
    // Signal the task if it's blocked waiting for data?
    // Or just let it finish its current loop and exit based on the flag.

    // Delete RTP session
    rtp_session_delete(pipeline->rtp_session);
    pipeline->rtp_session = NULL;

    // Clear I2S buffers (optional)
    i2s_zero_dma_buffer(I2S_NUM);

    ESP_LOGI(TAG, "Audio pipeline stopped.");
    return ESP_OK;
}

void audio_pipeline_delete(audio_pipeline_handle_t handle) {
     audio_pipeline_t* pipeline = (audio_pipeline_t*)handle;
     if (!pipeline) return;

     if (pipeline->is_running) {
         audio_pipeline_stop(handle);
     }

     // Delete the task (ensure it has exited cleanly first)
     if (pipeline->audio_io_task_handle) {
         // Maybe use a semaphore to wait for task exit confirmation
         vTaskDelete(pipeline->audio_io_task_handle);
     }

     codec_deinit(); // Assuming codec_deinit()
     i2s_deinit();
     free(pipeline);
     ESP_LOGI(TAG, "Audio pipeline deleted.");
}


// Combined task for reading I2S, encoding, sending RTP
// AND receiving RTP, decoding, writing I2S
static void audio_io_task(void* pvParameters) {
    audio_pipeline_t* pipeline = (audio_pipeline_t*)pvParameters;
    size_t bytes_read = 0;
    size_t bytes_written = 0;
    esp_err_t i2s_status;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t ticks_per_frame = pdMS_TO_TICKS(AUDIO_FRAME_MS);

    ip_addr_t remote_rtp_ip;
    uint16_t remote_rtp_port = 0;
    bool remote_info_valid = false;
    uint32_t current_timestamp = 0;


    ESP_LOGI(TAG, "Audio I/O Task Started.");

    while (1) {
        if (pipeline->is_running) {

            // --- Get Remote RTP destination info ---
            // Do this check periodically in case call details change (though unlikely in basic client)
             if (!remote_info_valid && pipeline->sip_client) {
                  if (sip_client_get_remote_rtp_info(pipeline->sip_client, &remote_rtp_ip, &remote_rtp_port) == ESP_OK) {
                      ESP_LOGI(TAG, "Got remote RTP destination: %s:%d", ipaddr_ntoa(&remote_rtp_ip), remote_rtp_port);
                      remote_info_valid = true;
                  } else {
                       ESP_LOGW(TAG,"Waiting for remote RTP info from SIP client...");
                       // Continue trying, maybe delay slightly?
                  }
             }


            // --- Audio Input Path (I2S Read -> Encode -> RTP Send) ---
            i2s_status = i2s_read(I2S_NUM, pipeline->i2s_read_buffer, AUDIO_SAMPLES_PER_FRAME, &bytes_read, pdMS_TO_TICKS(AUDIO_FRAME_MS*2)); // Block max 2 frame times

            if (i2s_status == ESP_OK && bytes_read == AUDIO_SAMPLES_PER_FRAME) {
                // Encode (G.711 u-law)
                for(int i=0; i<AUDIO_SAMPLES_PER_FRAME; i++) {
                    // Assuming I2S gives 16-bit samples, need linear PCM first
                    // This depends heavily on I2S/Codec config. Assume we get 8-bit linear for simplicity?
                    // Or if 16-bit: int16_t sample = ((int16_t*)pipeline->i2s_read_buffer)[i];
                    // pipeline->encoded_buffer[i] = linear16_to_ulaw(sample); // Need conversion func
                    pipeline->encoded_buffer[i] = pipeline->i2s_read_buffer[i]; // Placeholder: direct copy if I2S gives 8-bit ulaw? Unlikely.
                }
                g711_encode(pipeline->encoded_buffer, pipeline->i2s_read_buffer, AUDIO_SAMPLES_PER_FRAME, G711_ULAW); // Correct way


                // Send RTP Packet (if destination is known)
                if (pipeline->rtp_session && remote_info_valid) {
                     // current_timestamp relates to samples processed
                     rtp_send_packet(pipeline->rtp_session, &remote_rtp_ip, remote_rtp_port,
                                     AUDIO_CODEC_PAYLOAD_TYPE, current_timestamp,
                                     pipeline->encoded_buffer, AUDIO_SAMPLES_PER_FRAME);
                     current_timestamp += AUDIO_SAMPLES_PER_FRAME; // Increment timestamp by sample count
                }
            } else {
                 ESP_LOGW(TAG, "I2S Read failed or wrong size: ret=%d, bytes=%d", i2s_status, (int)bytes_read);
                 // Handle read error - maybe send silence?
            }


             // --- Audio Output Path (RTP Receive -> Jitter Buffer -> Decode -> I2S Write) ---
             if (pipeline->rtp_session) {
                 rtp_header_t rx_header;
                 ip_addr_t src_ip; // We might not use these if only talking to one peer
                 uint16_t src_port;

                 // Attempt to receive (might block briefly or timeout depending on socket opts)
                 // Better: Use select/non-blocking in a real app, integrate with jitter buffer properly
                 int payload_len = rtp_receive_packet(pipeline->rtp_session,
                                                      pipeline->rtp_receive_buffer, sizeof(pipeline->rtp_receive_buffer),
                                                      &src_ip, &src_port, &rx_header);

                 uint8_t* payload_ptr = pipeline->rtp_receive_buffer + sizeof(rtp_header_t); // Point to payload start

                 if (payload_len > 0) {
                     // Basic check: Ensure payload matches expected size?
                     if (payload_len != AUDIO_SAMPLES_PER_FRAME) {
                          ESP_LOGW(TAG,"Received RTP packet with unexpected payload size: %d", payload_len);
                          // Discard? Or try to process? For G711 fixed size is expected.
                     } else {
                           // Add to jitter buffer
                           rtp_jitter_buffer_put(pipeline->rtp_session, &rx_header, payload_ptr, payload_len);
                     }

                 } else if (payload_len < 0) {
                      ESP_LOGE(TAG, "RTP Receive error");
                      // Handle receive error
                 }
                 // else: No packet received this cycle

                 // --- Get packet from Jitter Buffer ---
                 rtp_header_t play_header; // Header info for the packet being played
                 int play_len = rtp_jitter_buffer_get(pipeline->rtp_session, &play_header,
                                                       pipeline->decoded_buffer, sizeof(pipeline->decoded_buffer)); // Get data TO decoded buffer

                 if (play_len == AUDIO_SAMPLES_PER_FRAME) {
                     // Packet available from jitter buffer, data is in decoded_buffer
                     // Decode (G.711 u-law) -> Needs linear buffer
                      uint8_t linear_buffer[AUDIO_SAMPLES_PER_FRAME * 2]; // Assuming 16-bit output
                      g711_decode(linear_buffer, pipeline->decoded_buffer, play_len, G711_ULAW);

                      // Write decoded audio to I2S
                      i2s_status = i2s_write(I2S_NUM, linear_buffer, play_len * 2, &bytes_written, portMAX_DELAY); // Block until buffer space

                 } else { // Jitter buffer decided to generate silence/PLC
                       uint8_t silence[AUDIO_SAMPLES_PER_FRAME * 2]; // 16-bit silence
                       memset(silence, 0, sizeof(silence));
                       ESP_LOGD(TAG,"Playing silence/PLC data (len=%d)", play_len);
                       i2s_status = i2s_write(I2S_NUM, silence, sizeof(silence), &bytes_written, portMAX_DELAY);
                 }


                 if (i2s_status != ESP_OK) {
                     ESP_LOGE(TAG, "I2S Write failed: %d", i2s_status);
                     // Handle write error
                 } else if (bytes_written != play_len * 2 && play_len > 0) { // Check against expected write size (16-bit)
                     ESP_LOGW(TAG, "I2S Write partial data (%d / %d)", (int)bytes_written, play_len*2);
                 }
             }


            // Precise delay to maintain frame rate
            vTaskDelayUntil(&last_wake_time, ticks_per_frame);

        } else {
            // Pipeline is stopped, wait to be started
            remote_info_valid = false; // Reset remote info when stopped
            current_timestamp = 0;     // Reset timestamp
            vTaskDelay(pdMS_TO_TICKS(100)); // Sleep longer when inactive
            last_wake_time = xTaskGetTickCount(); // Reset wake time for next start
        }
    } // End while(1)
}


static esp_err_t i2s_init(void) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX), // Master, Tx & Rx
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // Use 16-bit for codec compatibility usually
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Or I2S_CHANNEL_FMT_ALL_LEFT / I2S_CHANNEL_FMT_ALL_RIGHT
        .communication_format = I2S_COMM_FORMAT_STAND_I2S, // Standard I2S
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // Interrupt level 1
        .dma_buf_count = I2S_DMA_BUF_COUNT,
        .dma_buf_len = I2S_DMA_BUF_LEN,
        .use_apll = false, // Use internal APLL clock? Set false for stability unless needed.
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0 // Set MCLK frequency if needed by codec, 0 for auto
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_DATA_OUT_PIN,
        .data_in_num = I2S_DATA_IN_PIN
    };

    esp_err_t ret = ESP_OK;

    ret = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S driver install failed: %d", ret);
        return ret;
    }

    ret = i2s_set_pin(I2S_NUM, &pin_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S set pins failed: %d", ret);
        i2s_driver_uninstall(I2S_NUM);
        return ret;
    }

     // Optional: Set clock - needed for some codecs
     // ret = i2s_set_clk(I2S_NUM, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
     // if (ret != ESP_OK) { ... }


    // Zero DMA buffers initially
    i2s_zero_dma_buffer(I2S_NUM);

    ESP_LOGI(TAG, "I2S Initialized (Mode: TX & RX, Rate: %d, Bits: 16)", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

static esp_err_t i2s_deinit(void) {
    esp_err_t ret = i2s_driver_uninstall(I2S_NUM);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2S driver uninstall failed: %d", ret);
     } else {
          ESP_LOGI(TAG, "I2S Deinitialized.");
     }
     return ret;
}
