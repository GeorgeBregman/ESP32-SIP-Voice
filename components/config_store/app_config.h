#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// =====================================================================
//  ESP32-SIP-Voice — central configuration
//  (Defaults below are compiled-in fallbacks; the Captive Portal / Web UI
//   override Wi-Fi + SIP credentials and GPIO pins at runtime via NVS.)
// =====================================================================

// --- Wi-Fi Configuration ---
#define WIFI_SSID              "your_wifi_ssid"
#define WIFI_PASSWORD          "your_wifi_password"
#define WIFI_MAX_RETRY         5

// --- SIP Configuration ---
#define SIP_SERVER_IP          "192.168.1.100"
#define SIP_SERVER_PORT        5060
#define SIP_USER               "1000"
#define SIP_PASSWORD           "your_sip_password"
#define SIP_DISPLAY_NAME       "ESP32 Phone"
#define SIP_DOMAIN             SIP_SERVER_IP
#define SIP_LOCAL_PORT         5060
#define SIP_REGISTRATION_EXPIRY 3600
#define SIP_RETRY_INTERVAL_MS  5000
#define USE_SIPS               0          // 1 = SIP over TLS (experimental)
#define SIP_TARGET_URI         "sip:1001@192.168.1.100" // Default call target

// --- AI & Voice Activation ---
#define USE_WAKE_WORD          1          // esp-sr WakeNet (needs PSRAM / S3)
#define WAKE_WORD_MODEL        "alexa"

// =====================================================================
//  Audio codec selection (driven by the Kconfig hardware profile).
//  AUDIO_SAMPLE_RATE must be resolved BEFORE the frame-size math below,
//  so this block comes first.
// =====================================================================
#if defined(CONFIG_SIP_PROFILE_PRO)
    #define USE_CODEC_OPUS
    #define USE_FULL_DUPLEX_AEC
#elif defined(CONFIG_SIP_PROFILE_STANDARD)
    #define USE_CODEC_G722
#else
    // LITE profile -> G.711 only (8 kHz)
#endif

#if defined(USE_CODEC_OPUS)
    #define AUDIO_SAMPLE_RATE   48000      // OPUS full-band
    #define RTP_CLOCK_RATE      48000
    #define RTP_DYN_PAYLOAD_TYPE 96        // dynamic PT advertised for OPUS
#elif defined(USE_CODEC_G722)
    #define AUDIO_SAMPLE_RATE   16000      // G.722 wideband audio
    #define RTP_CLOCK_RATE      8000       // RFC 3551 quirk: G.722 RTP clock = 8 kHz
#else
    #define AUDIO_SAMPLE_RATE   8000       // G.711 narrowband
    #define RTP_CLOCK_RATE      8000
#endif

// G.711 variant used by the LITE path / fallback: 0 = PCMU (u-law), 8 = PCMA (a-law)
#define AUDIO_CODEC_PAYLOAD_TYPE 0

// --- Frame / RTP timing (derived) ---
#define AUDIO_FRAME_MS          20
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS / 1000) // PCM samples / frame
#define RTP_TS_INCREMENT        (RTP_CLOCK_RATE * AUDIO_FRAME_MS / 1000)    // RTP timestamp step / frame

#define RTP_LOCAL_PORT_BASE     16384      // even base port for RTP
#define RTP_MAX_PAYLOAD         512        // worst-case encoded frame size
#define JITTER_BUFFER_SIZE      8          // SRAM fallback jitter depth (packets)

// --- I2S pins (compiled-in defaults; overridable via Web HW config) ---
#define I2S_NUM                 I2S_NUM_0
#define I2S_BCK_PIN             GPIO_NUM_26
#define I2S_WS_PIN              GPIO_NUM_25
#define I2S_DATA_OUT_PIN        GPIO_NUM_22
#define I2S_DATA_IN_PIN         GPIO_NUM_21
#define CODEC_I2C_SCL_PIN       GPIO_NUM_18
#define CODEC_I2C_SDA_PIN       GPIO_NUM_19

// --- Keypad ---
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
#define KEYPAD_I2C_ADDR 0x20
#define KEYPAD_I2C_SDA  GPIO_NUM_21
#define KEYPAD_I2C_SCL  GPIO_NUM_22

// --- Display ---
#define USE_DISPLAY_ST7789
//#define USE_DISPLAY_ILI9341
#define TFT_MOSI GPIO_NUM_23
#define TFT_SCLK GPIO_NUM_18
#define TFT_CS   GPIO_NUM_15
#define TFT_DC   GPIO_NUM_2
#define TFT_RST  GPIO_NUM_4

// --- Touch ---
#define USE_TOUCH_XPT2046
#define TOUCH_CS   GPIO_NUM_14
#define TOUCH_IRQ  GPIO_NUM_27

// --- Task configuration ---
#define WIFI_TASK_PRIORITY      5
#define SIP_TASK_PRIORITY       8
#define AUDIO_IO_TASK_PRIORITY  10
#define SIP_TASK_STACK_SIZE     8192
#define AUDIO_TASK_STACK_SIZE   6144

// --- Audio codec hardware driver ---
#define USE_CODEC_ES8388
//#define USE_CODEC_INMP441_MAX98357A

// --- NAT Traversal (STUN) ---
#define USE_STUN 1
#define STUN_SERVER_IP         "stun.l.google.com"
#define STUN_SERVER_PORT       19302

// --- Call Management Interface ---
#define CTRL_METHOD_BUTTONS
//#define CTRL_METHOD_WEB
//#define CTRL_METHOD_AUTO
#define BUTTON_GPIO            GPIO_NUM_0

// --- Time / NTP (for the on-screen clock themes) ---
#define NTP_SERVER             "pool.ntp.org"
#define TIMEZONE               "GMT0"   // POSIX TZ, e.g. "MSK-3", "GMT0", "EST5EDT,M3.2.0,M11.1.0"

// --- Web interface security ---
// Empty string disables the gate (LAN-only use). Set a PIN to require it on
// every state-changing endpoint (/call, /answer, /hangup, /setup, /hardware...).
#define WEB_UI_PIN             ""

// --- Shared application event-group bits ---
// (Defined centrally so wifi_manager, sip_client and main agree on them.
//  Plain literals so this header has no FreeRTOS dependency and can be included
//  by low-level driver components.)
#ifndef WIFI_CONNECTED_BIT
#define WIFI_CONNECTED_BIT  (1 << 0)
#endif
#ifndef SIP_REGISTERED_BIT
#define SIP_REGISTERED_BIT  (1 << 1)
#endif
#ifndef IP_ACQUIRED_BIT
#define IP_ACQUIRED_BIT     (1 << 2)
#endif

#endif // APP_CONFIG_H
