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
#define USE_SIPS 0 // Set to 1 to enable SIP over TLS (SIPS)

#define SIP_TARGET_URI         "sip:1001@192.168.1.100" // Target URI to call

// --- RTP Configuration ---
#define RTP_LOCAL_PORT_BASE    16384           // Starting local port for RTP (even number)
#define AUDIO_SAMPLE_RATE      8000
#define AUDIO_CODEC_PAYLOAD_TYPE 0             // 0 for PCMU (G.711 u-law), 8 for PCMA (G.711 a-law)
#define AUDIO_FRAME_MS         20              // Packetization interval
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000) // 160 samples for 8kHz/20ms
#define RTP_TX_BUFFER_SIZE     (AUDIO_SAMPLES_PER_FRAME + 12) // G711 byte + RTP Header
#define RTP_RX_BUFFER_SIZE     (RTP_TX_BUFFER_SIZE * 5)       // Room for multiple packets / jitter
#define JITTER_BUFFER_SIZE     8                              // Number of packets in jitter buffer

// --- Audio / I2S Configuration ---
#define I2S_NUM                 I2S_NUM_0
#define I2S_BCK_PIN             GPIO_NUM_26      // Example Pin - Configure for your board
#define I2S_WS_PIN              GPIO_NUM_25      // Example Pin - Configure for your board
#define I2S_DATA_OUT_PIN        GPIO_NUM_22      // Example Pin - Configure for your board
#define I2S_DATA_IN_PIN         GPIO_NUM_21      // Example Pin - Configure for your board
// Add Codec specific pins if needed (e.g., I2C for ES8388)
#define CODEC_I2C_SCL_PIN       GPIO_NUM_18      // Example Pin
#define CODEC_I2C_SDA_PIN       GPIO_NUM_19      // Example Pin

// --- Phone Hardware Configuration (v1.3.0) ---
// 1. Keypad Selection (Uncomment ONE)
#define USE_KEYPAD_4X4_GPIO
//#define USE_KEYPAD_I2C_PCF8574
#define KEYPAD_R1 GPIO_NUM_32
#define KEYPAD_R2 GPIO_NUM_33
#define KEYPAD_R3 GPIO_NUM_27
#define KEYPAD_R4 GPIO_NUM_14
#define KEYPAD_C1 GPIO_NUM_12
#define KEYPAD_C2 GPIO_NUM_13
#define KEYPAD_C3 GPIO_NUM_4
#define KEYPAD_C4 GPIO_NUM_5

// I2C Keypad Pins (if USE_KEYPAD_I2C_PCF8574 is used)
#define KEYPAD_I2C_ADDR 0x20
#define KEYPAD_I2C_SDA  GPIO_NUM_21
#define KEYPAD_I2C_SCL  GPIO_NUM_22

// 2. Display Selection (Uncomment ONE)
#define USE_DISPLAY_ST7789 // 1.3" / 1.54" IPS
//#define USE_DISPLAY_ILI9341 // 2.4" / 2.8" TFT
// TFT Pins (SPI)
#define TFT_MOSI GPIO_NUM_23
#define TFT_SCLK GPIO_NUM_18
#define TFT_CS   GPIO_NUM_15
#define TFT_DC   GPIO_NUM_2
#define TFT_RST  GPIO_NUM_4

// 3. Touch Controller (Uncomment to enable Touch UI)
#define USE_TOUCH_XPT2046
#define TOUCH_CS   GPIO_NUM_14 // Usually shares SPI with TFT, just needs its own CS
#define TOUCH_IRQ  GPIO_NUM_27 // Interrupt pin

// --- Audio & Codec Configuration (Auto-configured by Kconfig) ---
#if defined(CONFIG_SIP_PROFILE_PRO)
    #define USE_CODEC_OPUS
    #define USE_FULL_DUPLEX_AEC
#elif defined(CONFIG_SIP_PROFILE_STANDARD)
    #define USE_CODEC_G722
#else
    // LITE Profile defaults to G.711 (8kHz)
#endif

#if defined(USE_CODEC_OPUS)
#define AUDIO_SAMPLE_RATE       48000
#elif defined(USE_CODEC_G722)
#define AUDIO_SAMPLE_RATE       16000
#else
#define AUDIO_SAMPLE_RATE       8000
#endif

// --- Task Configuration ---
#define WIFI_TASK_PRIORITY      5
#define SIP_TASK_PRIORITY       8               // Higher than WiFi, lower than audio
#define AUDIO_IO_TASK_PRIORITY  10              // High priority for real-time audio

#define SIP_TASK_STACK_SIZE     8192            // SIP processing can require significant stack
#define AUDIO_TASK_STACK_SIZE   4096

// --- User Choices ---

// 1. Audio Codec Selection
// Uncomment ONE of the following:
#define USE_CODEC_ES8388
//#define USE_CODEC_INMP441_MAX98357A

// 2. NAT Traversal (STUN)
// Set to 1 to enable STUN for external network access
#define USE_STUN 1
#define STUN_SERVER_IP         "stun.l.google.com"
#define STUN_SERVER_PORT       19302

// 3. Call Management Interface
// Uncomment ONE of the following:
#define CTRL_METHOD_BUTTONS    // Use physical buttons on the board
//#define CTRL_METHOD_WEB        // Use web interface
//#define CTRL_METHOD_AUTO       // Auto-answer incoming calls

#define BUTTON_GPIO            GPIO_NUM_0      // GPIO for Call/Answer/Hangup button

#endif // APP_CONFIG_H
