#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include "esp_camera.h"
#include <time.h>
#include <math.h>

// ============================================================
// LOGGING CONFIG
// ============================================================

// Set to false to disable all Serial logging.
const bool ENABLE_LOGS = true;

#define LOG_BEGIN(...)   do { if (ENABLE_LOGS) Serial.begin(__VA_ARGS__); } while (false)
#define LOG_PRINT(...)   do { if (ENABLE_LOGS) Serial.print(__VA_ARGS__); } while (false)
#define LOG_PRINTLN(...) do { if (ENABLE_LOGS) Serial.println(__VA_ARGS__); } while (false)
#define LOG_PRINTF(...)  do { if (ENABLE_LOGS) Serial.printf(__VA_ARGS__); } while (false)

// ============================================================
// LED
// ============================================================

#define LED_PIN 21


// ============================================================
// SPEAKER - MAX98357A
// ============================================================

#define I2S_SPK_BCLK 2
#define I2S_SPK_LRC  4
#define I2S_SPK_DIN  1


// ============================================================
// USER CONFIG
// ============================================================

const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";

const char* GROQ_API_KEY = "";

const char* GOOGLE_CLIENT_ID     = "";
const char* GOOGLE_CLIENT_SECRET = "";
const char* GOOGLE_REFRESH_TOKEN = "";

const char* NEWS_API_KEY = "";

const char* TELEGRAM_BOT_TOKEN = "";
const char* TELEGRAM_CHAT_ID   = "";


// ============================================================
// GROQ CONFIG
// ============================================================

const char* WAKE_WORD = "viper";

const char* GROQ_STT_MODEL =
  "whisper-large-v3-turbo";

const char* GROQ_CHAT_MODEL =
  "qwen/qwen3.6-27b";


// ============================================================
// TIME CONFIG
// ============================================================

const char* TZ_STRING = "IST-5:30";


// ============================================================
// AUDIO CONFIG
// ============================================================

const int8_t I2S_CLK = 42;
const int8_t I2S_DIN = 41;

const uint32_t SAMPLE_RATE = 16000;
const uint8_t SAMPLE_BITS = 16;

const uint32_t WAV_HEADER_SIZE = 44;

const uint32_t BLOCK_SAMPLES = 4000;

const float RMS_THRESHOLD = 275.0;

const int SPEECH_DEBOUNCE_BLOCKS = 1;
const int TRAILING_SILENCE_BLOCKS = 3;
const int IDLE_TIMEOUT_BLOCKS = 24;

const uint32_t MAX_RECORD_MS = 12000;


// ============================================================
// GLOBAL I2S
// ============================================================

I2SClass I2S;


// ============================================================
// CAMERA PINS
// Seeed XIAO ESP32S3 Sense
// ============================================================

#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1

#define XCLK_GPIO_NUM 10

#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39

#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15

#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13


// ============================================================
// SYSTEM PROMPT
// ============================================================

const char* SYSTEM_PROMPT_BASE =
  "You are an articulate, engaging, and conversational AI assistant "
  "running on a pair of smart glasses. Your name is VIPER. "

  "Your responses are spoken aloud directly into the ear of your user, Anhad. "

  "Speak naturally and expressively, like a knowledgeable friend. "

  "Stay focused on the user's intent and provide only information relevant "
  "to fulfilling it. "

  "Keep responses concise by default, and provide additional detail only "
  "when it is useful or requested. "

  "CAMERA ORIENTATION RULE: The images provided to you are rotated 90 degrees "
  "counter-clockwise because of how the camera is mounted. You must mentally "
  "rotate the image 90 degrees clockwise before analyzing it. "

  "What appears on the left side of the image is actually the bottom or floor, "
  "and what appears on the right side is the top or ceiling. "

  "Read any text as if it were rotated correctly. "

  "LIVE TRANSLATION RULE: Act as a real-time visual translator. "
  "When asked to read or translate text, output ONLY the direct English "
  "translation of the visible text. Provide absolutely no introductions, "
  "context, or conversational filler. Just the translated words. "

  "VISION RULE: When asked about what you see, hyper-focus ONLY on the primary "
  "subject relevant to the user's request. "

  "Do not describe the background, room, user's body, lighting, or unrelated "
  "objects unless explicitly asked. "

  "UNCERTAINTY RULE: Never invent, assume, or hallucinate visual details. "

  "If the subject is unclear, obscured, distant, or unreadable, say so rather "
  "than guessing. "

  "Do not use filler phrases like 'I see'. "
  "Do not use markdown, emojis, asterisks, or special formatting. "

  "For time based outputs don't say 7 o clock pm, instead say 7 p.m. "
  "or 7 in the evening. "

  "For dates, say August 16th instead of August 16.";


// ============================================================
// RUNTIME STATE
// ============================================================

enum AppState {
  STATE_WAITING_FOR_WAKE,
  STATE_AWAKE
};

AppState appState = STATE_WAITING_FOR_WAKE;

bool visionMode = false;

const int MAX_HISTORY = 6;

String historyRole[MAX_HISTORY];
String historyContent[MAX_HISTORY];

int historyCount = 0;

String systemPrompt;


// ============================================================
// GOOGLE TOKEN STATE
// ============================================================

String googleAccessToken = "";

unsigned long tokenExpiryMillis = 0;


// ============================================================
// RECORD RESULT
// ============================================================

struct RecordResult {
  uint8_t* wav;
  size_t wavLen;
  bool timedOut;
};


// ============================================================
// SETUP
// ============================================================

void setup() {

  LOG_BEGIN(
    115200
  );


  pinMode(
    LED_PIN,
    OUTPUT
  );


  digitalWrite(
    LED_PIN,
    HIGH
  );


  delay(300);


  LOG_PRINTLN();

  LOG_PRINTLN(
    "======================================"
  );

  LOG_PRINTLN(
    "         VIPER INITIALIZING"
  );

  LOG_PRINTLN(
    "======================================"
  );


  printMemoryDebug(
    "BOOT"
  );


  // ----------------------------------------------------------
  // MICROPHONE
  // ----------------------------------------------------------

  LOG_PRINTLN(
    "[System]: Initializing microphone..."
  );


  I2S.setPinsPdmRx(
    I2S_CLK,
    I2S_DIN
  );


  if (
    !I2S.begin(
      I2S_MODE_PDM_RX,
      SAMPLE_RATE,
      I2S_DATA_BIT_WIDTH_16BIT,
      I2S_SLOT_MODE_MONO
    )
  ) {

    LOG_PRINTLN(
      "[Error]: Failed to initialize I2S mic!"
    );


    while (true) {

      delay(1000);
    }
  }


  LOG_PRINTLN(
    "[System]: Microphone initialized."
  );


  printMemoryDebug(
    "AFTER_MIC"
  );


  // ----------------------------------------------------------
  // CAMERA
  // ----------------------------------------------------------

  if (
    !initCamera()
  ) {

    LOG_PRINTLN(
      "[Error]: Camera unavailable - vision mode will fail."
    );
  }


  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  connectWiFi();


  // ----------------------------------------------------------
  // TIME
  // ----------------------------------------------------------

  syncTime();


  // ----------------------------------------------------------
  // STATE
  // ----------------------------------------------------------

  resetConversationState();


  printMemoryDebug(
    "SYSTEM_READY"
  );


  LOG_PRINTLN(
    "[System]: Ready. Say \"viper\" to start a conversation."
  );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  static bool justEnteredWaiting =
    true;


  switch (
    appState
  ) {

    // --------------------------------------------------------
    // WAITING FOR WAKE WORD
    // --------------------------------------------------------

    case STATE_WAITING_FOR_WAKE: {

      digitalWrite(
        LED_PIN,
        LOW
      );


      if (
        justEnteredWaiting
      ) {

        LOG_PRINTLN(
          "[System]: Listening for wake word..."
        );


        justEnteredWaiting =
          false;
      }


      if (
        listenForWakeWord()
      ) {

        LOG_PRINTLN(
          "[System]: >>> Wake word detected <<<"
        );


        resetConversationState();


        appState =
          STATE_AWAKE;


        justEnteredWaiting =
          true;
      }


      break;
    }


    // --------------------------------------------------------
    // ACTIVE CONVERSATION
    // --------------------------------------------------------

    case STATE_AWAKE: {

      conversationTurn();


      if (
        appState ==
        STATE_WAITING_FOR_WAKE
      ) {

        justEnteredWaiting =
          true;
      }


      break;
    }
  }
}
