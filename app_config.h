#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// --- Wi-Fi Configuration ---
#define WIFI_SSID              "your_wifi_ssid"
#define WIFI_PASSWORD          "your_wifi_password"
#define WIFI_MAX_RETRY         5

// --- SIP Configuration ---
#define SIP_SERVER_IP          "192.168.1.100" // Replace with your SIP server IP or domain
#define SIP_SERVER_PORT        5060
#define SIP_USER               "1000"          // Your SIP extension/username
#define SIP_PASSWORD           "your_sip_password"
#define SIP_DISPLAY_NAME       "ESP32 Phone"
#define SIP_DOMAIN             SIP_SERVER_IP   // Often same as server IP/domain
#define SIP_LOCAL_PORT         5060            // Local port for SIP UDP socket
#define SIP_REGISTRATION_EXPIRY 3600           // Registration duration in seconds
#define SIP_RETRY_INTERVAL_MS  5000            // Interval to retry registration on failure

// --- RTP Configuration ---
#define RTP_LOCAL_PORT_BASE    16384           // Starting local port for RTP (even number)
#define AUDIO_SAMPLE_RATE      8000
#define AUDIO_CODEC_PAYLOAD_TYPE 0             // 0 for PCMU (G.711 u-law), 8 for PCMA (G.711 a-law)
#define AUDIO_FRAME_MS         20              // Packetization interval
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000) // 160 samples for 8kHz/20ms
#define RTP_TX_BUFFER_SIZE     (AUDIO_SAMPLES_PER_FRAME + 12) // G711 byte + RTP Header
#define RTP_RX_BUFFER_SIZE     (RTP_TX_BUFFER_SIZE * 5)       // Room for multiple packets / jitter

// --- Audio / I2S Configuration ---
#define I2S_NUM                 I2S_NUM_0
#define I2S_BCK_PIN             GPIO_NUM_26      // Example Pin - Configure for your board
#define I2S_WS_PIN              GPIO_NUM_25      // Example Pin - Configure for your board
#define I2S_DATA_OUT_PIN        GPIO_NUM_22      // Example Pin - Configure for your board
#define I2S_DATA_IN_PIN         GPIO_NUM_21      // Example Pin - Configure for your board
// Add Codec specific pins if needed (e.g., I2C for ES8388)
#define CODEC_I2C_SCL_PIN       GPIO_NUM_18      // Example Pin
#define CODEC_I2C_SDA_PIN       GPIO_NUM_19      // Example Pin

// --- Task Configuration ---
#define WIFI_TASK_PRIORITY      5
#define SIP_TASK_PRIORITY       8               // Higher than WiFi, lower than audio
#define AUDIO_IO_TASK_PRIORITY  10              // High priority for real-time audio

#define SIP_TASK_STACK_SIZE     8192            // SIP processing can require significant stack
#define AUDIO_TASK_STACK_SIZE   4096

#endif // APP_CONFIG_H
