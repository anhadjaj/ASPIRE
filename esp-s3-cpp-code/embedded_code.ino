#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include "esp_camera.h"
#include <time.h>
#define LED_PIN 21

// ================= USER CONFIG =================
const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";
const char* GROQ_API_KEY  = "";   // <-- fill in from console.groq.com

const char* GOOGLE_CLIENT_ID     = "";
const char* GOOGLE_CLIENT_SECRET = "";
const char* GOOGLE_REFRESH_TOKEN = "";

String googleAccessToken = "";
unsigned long tokenExpiryMillis = 0;

const char* WAKE_WORD        = "aspire";                 // lowercase
const char* GROQ_STT_MODEL   = "whisper-large-v3-turbo";
const char* GROQ_CHAT_MODEL  = "qwen/qwen3.6-27b";         // same model your Python used for both
                                                            // text AND image turns - swap this if it
                                                            // isn't vision-capable on your Groq account

// India Standard Time. See configTzTime()/POSIX TZ docs if you're elsewhere.
const char* TZ_STRING = "IST-5:30";

const uint32_t WAKE_WINDOW_MS = 10000;  // one listening window before we retry
const uint32_t WAKE_CHUNK_MS  = 3500;   // clip length sent to Whisper while waiting for the wake word
// =================================================

// ---------------- Mic / audio config (mirrors the Python VAD) ----------------
const int8_t   I2S_CLK      = 42;
const int8_t   I2S_DIN      = 41;
const uint32_t SAMPLE_RATE  = 16000;
const uint8_t  SAMPLE_BITS  = 16;
const uint32_t WAV_HEADER_SIZE = 44;

const uint32_t BLOCK_SAMPLES           = 4000;  // ~250ms/block, same as Python's chunk_size
const float    RMS_THRESHOLD           = 200.0; // same loudness gate as the Python VAD
const int      SPEECH_DEBOUNCE_BLOCKS  = 1;      // ~0.5s loud before it counts as real speech
const int      TRAILING_SILENCE_BLOCKS = 3;      // ~0.75s silence ends the turn
const int      IDLE_TIMEOUT_BLOCKS     = 24;     // ~6s silence with no speech = timeout
const uint32_t MAX_RECORD_MS           = 12000;  // hard cap so PSRAM can't fill forever

I2SClass I2S;

// ---------------- Camera pins (Seeed XIAO ESP32S3 Sense onboard OV2640) ----------------
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// ---------------- System prompt ----------------
const char* SYSTEM_PROMPT_BASE =
  "You are an articulate, engaging, and conversational AI assistant running on a pair of smart glasses. Your name is ASPIRE. "
  "Your responses are spoken aloud directly into the ear of your user, Anhad. "
  "Speak naturally and expressively, like a knowledgeable friend. "
  "Stay focused on the user's intent and provide only information relevant to fulfilling it. "
  "Keep responses concise by default, and provide additional detail only when it is useful or requested. "
  "CAMERA ORIENTATION RULE: The images provided to you are rotated 90 degrees counter-clockwise because of how the camera is mounted. You must mentally rotate the image 90 degrees clockwise before analyzing it. What appears on the left side of the image is actually the bottom/floor, and what appears on the right side is the top/ceiling. Read any text as if it were rotated correctly. "
  "LIVE TRANSLATION RULE: Act as a real-time visual translator. When asked to read or translate text, output ONLY the direct English translation of the visible text. Provide absolutely no introductions, context, or conversational filler. Just the translated words. "
  "VISION RULE: When asked about what you see, hyper-focus ONLY on the primary subject relevant to the user's request. "
  "Do not describe the background, room, user's body, lighting, or unrelated objects unless explicitly asked. "
  "UNCERTAINTY RULE: Never invent, assume, or hallucinate visual details. "
  "If the subject is unclear, obscured, distant, or unreadable, say so rather than guessing. "
  "Do not use filler phrases like 'I see'. Do not use markdown, emojis, asterisks, or special formatting. "
  "For time based outputs don't say 7 o clock pm, instead say 7 p.m. or 7 in the evening. "
  "For dates, say 'August 16th' instead of 'August 16'.";

// ================= Runtime state =================
enum AppState { STATE_WAITING_FOR_WAKE, STATE_AWAKE };
AppState appState = STATE_WAITING_FOR_WAKE;
bool visionMode = false;

const int MAX_HISTORY = 6; // mirrors the Python "prune to last 6" behaviour
String historyRole[MAX_HISTORY];
String historyContent[MAX_HISTORY];
int historyCount = 0;
String systemPrompt; // rebuilt on every wake-up with the current date/time baked in

struct RecordResult {
  uint8_t* wav;
  size_t   wavLen;
  bool     timedOut;
};

// ================= WAV header =================
void writeWavHeader(uint8_t* header, uint32_t dataSize) {
  uint32_t sampleRate    = SAMPLE_RATE;
  uint16_t bitsPerSample = SAMPLE_BITS;
  uint16_t numChannels   = 1;
  uint32_t byteRate      = sampleRate * numChannels * bitsPerSample / 8;
  uint16_t blockAlign    = numChannels * bitsPerSample / 8;
  uint32_t chunkSize     = 36 + dataSize;

  memcpy(header + 0,  "RIFF", 4);
  memcpy(header + 4,  &chunkSize, 4);
  memcpy(header + 8,  "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  uint32_t subchunk1Size = 16;
  memcpy(header + 16, &subchunk1Size, 4);
  uint16_t audioFormat = 1; // PCM
  memcpy(header + 20, &audioFormat, 2);
  memcpy(header + 22, &numChannels, 2);
  memcpy(header + 24, &sampleRate, 4);
  memcpy(header + 28, &byteRate, 4);
  memcpy(header + 32, &blockAlign, 2);
  memcpy(header + 34, &bitsPerSample, 2);
  memcpy(header + 36, "data", 4);
  memcpy(header + 40, &dataSize, 4);
}

// ================= Mic reading =================
int16_t readSample() {
  int s = I2S.read();
  // I2S.read() can return -1/1 as "no data yet" sentinels on this driver;
  // treat those as silence instead of letting them corrupt the recording.
  if (s == -1 || s == 1) return 0;
  return (int16_t)s;
}

void readBlock(int16_t* buf, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) buf[i] = readSample();
}

float computeRMS(int16_t* buf, uint32_t n) {
    double mean = 0;

    // Calculate DC offset
    for (uint32_t i = 0; i < n; i++) {
        mean += buf[i];
    }

    mean /= n;

    // RMS after removing DC offset
    double sumSq = 0;

    for (uint32_t i = 0; i < n; i++) {
        double sample = (double)buf[i] - mean;
        sumSq += sample * sample;
    }

    return sqrt(sumSq / n);
}

// ================= VAD-based recording (mirrors record_and_transcribe_in_ram) =================
RecordResult recordUntilSilence() {
  RecordResult result = { nullptr, 0, false };

  size_t maxSamples = (SAMPLE_RATE * MAX_RECORD_MS) / 1000;
  size_t maxBytes = maxSamples * (SAMPLE_BITS / 8) + WAV_HEADER_SIZE;
  uint8_t* buf = (uint8_t*) ps_malloc(maxBytes);
  if (!buf) {
    Serial.println("[Error]: PSRAM alloc failed for recording buffer");
    return result;
  }

  // FIX: Allocate the audio block on the heap instead of the stack
  // 1. Allocate current block, plus TWO past blocks for a 0.5s pre-roll
  int16_t* block = (int16_t*) malloc(BLOCK_SAMPLES * sizeof(int16_t));
  
  // Using calloc automatically fills them with digital silence (zeros)
  int16_t* prevBlock1 = (int16_t*) calloc(BLOCK_SAMPLES, sizeof(int16_t)); // 0.5s ago
  int16_t* prevBlock2 = (int16_t*) calloc(BLOCK_SAMPLES, sizeof(int16_t)); // 0.25s ago
  
  if (!block || !prevBlock1 || !prevBlock2) {
    Serial.println("[Error]: Malloc failed for audio blocks");
    if (block) free(block);
    if (prevBlock1) free(prevBlock1);
    if (prevBlock2) free(prevBlock2);
    free(buf);
    return result;
  }

  bool hasSpoken = false;
  int consecutiveLoud = 0;
  int idleBlocks = 0;
  int trailingSilence = 0;
  size_t samplesWritten = 0;
  int16_t* pcmStart = (int16_t*)(buf + WAV_HEADER_SIZE);

  Serial.println(visionMode ? "[System]: Listening... (VISION ACTIVE)" : "[System]: Listening...");
  digitalWrite(LED_PIN, HIGH); // Turn LED OFF while actively listening

  while (true) {
    readBlock(block, BLOCK_SAMPLES);
    float rms = computeRMS(block, BLOCK_SAMPLES);

    if (rms > RMS_THRESHOLD) {
      consecutiveLoud++;
      idleBlocks = 0;
      
      // TRIGGER POINT: We just detected speech!
      if (consecutiveLoud >= SPEECH_DEBOUNCE_BLOCKS && !hasSpoken) {
        hasSpoken = true;
        trailingSilence = 0;
        
        // PRE-ROLL INJECTION: Inject the last 0.5 seconds (both past blocks)
        if (samplesWritten + (2 * BLOCK_SAMPLES) <= maxSamples) {
          // Inject oldest audio first
          memcpy(pcmStart + samplesWritten, prevBlock1, BLOCK_SAMPLES * sizeof(int16_t));
          samplesWritten += BLOCK_SAMPLES;
          
          // Inject slightly newer audio next
          memcpy(pcmStart + samplesWritten, prevBlock2, BLOCK_SAMPLES * sizeof(int16_t));
          samplesWritten += BLOCK_SAMPLES;
        }
      } else if (hasSpoken) {
        trailingSilence = 0;
      }
    } else {
      consecutiveLoud = 0;
      if (hasSpoken) trailingSilence++;
      else idleBlocks++;
    }

    if (hasSpoken) {
      // Save the current loud audio
      if (samplesWritten + BLOCK_SAMPLES <= maxSamples) {
        memcpy(pcmStart + samplesWritten, block, BLOCK_SAMPLES * sizeof(int16_t));
        samplesWritten += BLOCK_SAMPLES;
      } else {
        break; // Buffer full, force end of turn
      }
    } else {
      // Not spoken yet? Shift the rolling buffer!
      // Oldest block gets overwritten by the newer block
      memcpy(prevBlock1, prevBlock2, BLOCK_SAMPLES * sizeof(int16_t));
      // Newer block gets overwritten by the current block
      memcpy(prevBlock2, block, BLOCK_SAMPLES * sizeof(int16_t));
    }

    if (hasSpoken && trailingSilence >= TRAILING_SILENCE_BLOCKS) break;
    
    if (!hasSpoken && idleBlocks >= IDLE_TIMEOUT_BLOCKS) {
      free(buf);
      free(block);
      free(prevBlock1);
      free(prevBlock2);
      result.timedOut = true;
      return result;
    }
  }

  uint32_t dataSize = samplesWritten * (SAMPLE_BITS / 8);
  writeWavHeader(buf, dataSize);
  result.wav = buf;
  result.wavLen = dataSize + WAV_HEADER_SIZE;
  
  // Clean up all memory
  free(block); 
  free(prevBlock1);
  free(prevBlock2); 
  return result;
}

// ================= base64 (for image data-URIs) =================
static const char B64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* base64Encode(const uint8_t* data, size_t len, size_t* outLen) {
  size_t encodedLen = 4 * ((len + 2) / 3);
  char* out = (char*) ps_malloc(encodedLen + 1);
  if (!out) return nullptr;
  size_t i = 0, j = 0;
  while (i + 3 <= len) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
    out[j++] = B64_TABLE[(n >> 18) & 0x3F];
    out[j++] = B64_TABLE[(n >> 12) & 0x3F];
    out[j++] = B64_TABLE[(n >> 6) & 0x3F];
    out[j++] = B64_TABLE[n & 0x3F];
    i += 3;
  }
  size_t rem = len - i;
  if (rem == 1) {
    uint32_t n = (uint32_t)data[i] << 16;
    out[j++] = B64_TABLE[(n >> 18) & 0x3F];
    out[j++] = B64_TABLE[(n >> 12) & 0x3F];
    out[j++] = '=';
    out[j++] = '=';
  } else if (rem == 2) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    out[j++] = B64_TABLE[(n >> 18) & 0x3F];
    out[j++] = B64_TABLE[(n >> 12) & 0x3F];
    out[j++] = B64_TABLE[(n >> 6) & 0x3F];
    out[j++] = '=';
  }
  out[j] = '\0';
  *outLen = j;
  return out;
}

size_t appendStr(char* dest, size_t pos, const char* src) {
  size_t l = strlen(src);
  memcpy(dest + pos, src, l);
  return pos + l;
}

char* jsonEscape(const String& input) {
  size_t n = input.length();
  char* out = (char*) ps_malloc(n * 2 + 1); // worst case: every char escaped
  size_t j = 0;
  for (size_t i = 0; i < n; i++) {
    char c = input[i];
    if (c == '"' || c == '\\') { out[j++] = '\\'; out[j++] = c; }
    else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
    else if (c == '\r') { /* drop */ }
    else { out[j++] = c; }
  }
  out[j] = '\0';
  return out;
}

// ================= Groq Whisper STT =================
String transcribeWithGroq(uint8_t* wavData, size_t wavLen) {
  WiFiClientSecure client;
  client.setInsecure(); // simplest path; pin/verify the cert for production

  HTTPClient https;
  String boundary = "----XiaoGroqBoundary7331";

  if (!https.begin(client, "https://api.groq.com/openai/v1/audio/transcriptions")) {
    Serial.println("[Error]: HTTPClient begin failed (STT)");
    return "";
  }
  https.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  https.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  String preFile =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
    String(GROQ_STT_MODEL) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"language\"\r\n\r\n" +
    "en\r\n" +
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
    "json\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"chunk.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n";

  String postFile = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = preFile.length() + wavLen + postFile.length();
  uint8_t* body = (uint8_t*) ps_malloc(totalLen);
  if (!body) {
    Serial.println("[Error]: PSRAM alloc failed for STT request body");
    https.end();
    return "";
  }
  size_t offset = 0;
  memcpy(body + offset, preFile.c_str(), preFile.length()); offset += preFile.length();
  memcpy(body + offset, wavData, wavLen);                   offset += wavLen;
  memcpy(body + offset, postFile.c_str(), postFile.length()); offset += postFile.length();

  int httpCode = https.POST(body, totalLen);
  String result = "";

  if (httpCode == HTTP_CODE_OK) {
    String payload = https.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      const char* text = doc["text"] | "";
      result = String(text);
      result.trim();
    } else {
      Serial.print("[Error]: STT JSON parse: ");
      Serial.println(err.c_str());
    }
  } else {
    Serial.print("[Error]: Groq STT HTTP ");
    Serial.println(httpCode);
    Serial.println(https.getString());
  }

  free(body);
  https.end();
  return result;
}

// ================= Wake word listening =================
bool listenForWakeWord() {
  // 1. Allocate block on the heap to protect the stack
  int16_t* block = (int16_t*) malloc(BLOCK_SAMPLES * sizeof(int16_t));
  if (!block) {
    Serial.println("[Error]: Malloc failed in wake listener");
    return false;
  }

  // 2. Quick check: Is anyone actually talking?
  readBlock(block, BLOCK_SAMPLES);
  float rms = computeRMS(block, BLOCK_SAMPLES);

  // If room is quiet, free memory and exit immediately
  if (rms < RMS_THRESHOLD) {
    free(block);
    return false;
  }

  // 3. Sound detected: record a snappy 1.8-second clip
  const uint32_t SNAPPY_WAKE_MS = 1800;
  uint32_t numSamples = (SAMPLE_RATE * SNAPPY_WAKE_MS) / 1000;
  uint32_t dataSize = numSamples * (SAMPLE_BITS / 8);
  uint32_t totalSize = dataSize + WAV_HEADER_SIZE;

  uint8_t* buf = (uint8_t*) ps_malloc(totalSize);
  if (!buf) {
    free(block);
    return false;
  }

  writeWavHeader(buf, dataSize);
  int16_t* pcm = (int16_t*)(buf + WAV_HEADER_SIZE);

  // Pre-fill the first block we already read
  memcpy(pcm, block, BLOCK_SAMPLES * sizeof(int16_t));
  free(block); // Done with the temporary block

  // Read the rest of the clip
  for (uint32_t i = BLOCK_SAMPLES; i < numSamples; i++) {
    pcm[i] = readSample();
  }

  String transcript = transcribeWithGroq(buf, totalSize);
  free(buf);

  transcript.toLowerCase();
  if (transcript.indexOf(WAKE_WORD) >= 0) {
    return true;
  }

  return false;
}

// ================= Camera =================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;   // 1600x1200, the max the onboard OV2640 supports
    config.jpeg_quality = 10;             // lower number = higher quality
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[Error]: Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

camera_fb_t* captureHighResFrame() {
  Serial.println("[System]: Capturing image...");
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[Error]: Camera capture failed");
    return nullptr;
  }
  Serial.printf("[System]: Captured %dx%d, %u bytes\n", fb->width, fb->height, (unsigned)fb->len);
  return fb;
}

// ================= Conversation history =================
void addToHistory(const String& role, const String& content) {
  if (historyCount < MAX_HISTORY) {
    historyRole[historyCount] = role;
    historyContent[historyCount] = content;
    historyCount++;
  } else {
    for (int i = 1; i < MAX_HISTORY; i++) {
      historyRole[i - 1] = historyRole[i];
      historyContent[i - 1] = historyContent[i];
    }
    historyRole[MAX_HISTORY - 1] = role;
    historyContent[MAX_HISTORY - 1] = content;
  }
}

void resetConversationState() {
  struct tm timeinfo;
  String stamp = "unavailable (no time sync yet)";
  if (getLocalTime(&timeinfo)) {
    char buf[64];
    strftime(buf, sizeof(buf), "%A, %B %d, %Y %I:%M %p", &timeinfo);
    stamp = String(buf) + " (" + TZ_STRING + ")";
  }
  systemPrompt = String(SYSTEM_PROMPT_BASE) + "\nCRITICAL DATA: Today is " + stamp + ".";
  historyCount = 0;
  visionMode = false;
}

// ================= GOOGLE API CONFIG =================

String getValidGoogleAccessToken() {
  // Return cached token if still valid (with a 5-minute safety buffer)
  if (googleAccessToken.length() > 0 && millis() < (tokenExpiryMillis - 300000)) {
    return googleAccessToken;
  }

  Serial.println("[System]: Google Access Token expired or missing. Fetching new token...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  if (!https.begin(client, "https://oauth2.googleapis.com/token")) {
    Serial.println("[Error]: Failed to connect to Google OAuth server.");
    return "";
  }

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String payload = "client_id=" + String(GOOGLE_CLIENT_ID) +
                   "&client_secret=" + String(GOOGLE_CLIENT_SECRET) +
                   "&refresh_token=" + String(GOOGLE_REFRESH_TOKEN) +
                   "&grant_type=refresh_token";

  int httpCode = https.POST(payload);
  String newToken = "";

  if (httpCode == HTTP_CODE_OK) {
    String response = https.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);

    if (!err) {
      newToken = String(doc["access_token"] | "");
      int expiresIn = doc["expires_in"] | 3600;
      googleAccessToken = newToken;
      tokenExpiryMillis = millis() + ((unsigned long)expiresIn * 1000);
      Serial.println("[System]: Google Access Token refreshed successfully.");
    } else {
      Serial.printf("[Error]: Token JSON parse failed: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[Error]: Token fetch failed (HTTP %d): %s\n", httpCode, https.getString().c_str());
  }

  https.end();
  return newToken;
}

String getUpcomingCalendarEvents(int maxResults = 5) {
  String token = getValidGoogleAccessToken();
  if (token.length() == 0) return "Authentication error: Unable to get access token.";

  // Get current ISO 8601 UTC timestamp
  time_t now;
  time(&now);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char timeBuf[30];
  strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

  String url = "https://www.googleapis.com/calendar/v3/calendars/primary/events?timeMin=";
  url += String(timeBuf);
  url += "&maxResults=" + String(maxResults) + "&singleEvents=true&orderBy=startTime";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + token);

  int httpCode = https.GET();
  String result = "You have no upcoming events.";

  if (httpCode == HTTP_CODE_OK) {
    String response = https.getString();
    JsonDocument doc;
    deserializeJson(doc, response);

    JsonArray items = doc["items"].as<JsonArray>();
    if (items.size() > 0) {
      result = "Upcoming events:\n";
      for (JsonObject item : items) {
        const char* summary = item["summary"] | "Untitled Event";
        const char* start = item["start"]["dateTime"] | item["start"]["date"] | "";
        result += "- " + String(summary) + " (Starts: " + String(start) + ")\n";
      }
    }
  } else {
    result = "Failed to fetch calendar events (HTTP " + String(httpCode) + ").";
  }

  https.end();
  return result;
}

String createCalendarEvent(String summary, String startTimeIso, String endTimeIso = "") {
  String token = getValidGoogleAccessToken();
  if (token.length() == 0) return "Authentication error.";

  // If end time is not provided, default to start time + 1 hour
  if (endTimeIso.length() == 0) {
    endTimeIso = startTimeIso; // Or compute +1 hr if using epoch parsing
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  https.begin(client, "https://www.googleapis.com/calendar/v3/calendars/primary/events");
  https.addHeader("Authorization", "Bearer " + token);
  https.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["summary"] = summary;
  doc["start"]["dateTime"] = startTimeIso;
  doc["end"]["dateTime"] = endTimeIso;

  String body;
  serializeJson(doc, body);

  int httpCode = https.POST(body);
  String result = "";

  if (httpCode == HTTP_CODE_OK || httpCode == 201) {
    result = "Event '" + summary + "' successfully created.";
  } else {
    result = "Failed to create event (HTTP " + String(httpCode) + ").";
  }

  https.end();
  return result;
}

String getUnreadEmails(int maxResults = 3) {
  String token = getValidGoogleAccessToken();
  if (token.length() == 0) return "Authentication error.";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://gmail.googleapis.com/gmail/v1/users/me/messages?q=is:unread&maxResults=" + String(maxResults);
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + token);

  int httpCode = https.GET();
  String result = "You have no unread emails.";

  if (httpCode == HTTP_CODE_OK) {
    String response = https.getString();
    JsonDocument doc;
    deserializeJson(doc, response);

    JsonArray messages = doc["messages"].as<JsonArray>();
    if (messages.size() > 0) {
      result = "Unread emails:\n";
      for (JsonObject msg : messages) {
        String msgId = String(msg["id"] | "");
        https.end(); // close listing connection

        // Fetch headers for each message
        String detailUrl = "https://gmail.googleapis.com/gmail/v1/users/me/messages/" + msgId + 
                           "?format=metadata&metadataHeaders=From&metadataHeaders=Subject";
        https.begin(client, detailUrl);
        https.addHeader("Authorization", "Bearer " + token);
        
        if (https.GET() == HTTP_CODE_OK) {
          JsonDocument msgDoc;
          deserializeJson(msgDoc, https.getString());
          String sender = "Unknown", subject = "No Subject";

          for (JsonObject header : msgDoc["payload"]["headers"].as<JsonArray>()) {
            String name = String(header["name"] | "");
            if (name.equalsIgnoreCase("From")) sender = String(header["value"] | "Unknown");
            if (name.equalsIgnoreCase("Subject")) subject = String(header["value"] | "No Subject");
          }
          result += "- From: " + sender + " | Subject: " + subject + "\n";
        }
      }
    }
  } else {
    result = "Failed to fetch emails (HTTP " + String(httpCode) + ").";
  }

  https.end();
  return result;
}

String sendEmail(String toAddress, String subject, String bodyContent) {
  String token = getValidGoogleAccessToken();
  if (token.length() == 0) return "Authentication error.";

  // 1. Construct the raw MIME message
  String mimeMsg = "To: " + toAddress + "\r\n" +
                   "Subject: " + subject + "\r\n" +
                   "Content-Type: text/plain; charset=\"UTF-8\"\r\n" +
                   "\r\n" +
                   bodyContent;

  // 2. Base64 encode the message
  size_t outLen = 0;
  char* b64 = base64Encode((const uint8_t*)mimeMsg.c_str(), mimeMsg.length(), &outLen);
  if (!b64) return "Error encoding email.";
  
  String encodedMsg = String(b64);
  free(b64);

  // 3. Make it URL-safe Base64 (replace + with -, / with _, remove =)
  encodedMsg.replace("+", "-");
  encodedMsg.replace("/", "_");
  encodedMsg.replace("=", "");

  // 4. Send via Gmail REST API
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  https.begin(client, "https://gmail.googleapis.com/gmail/v1/users/me/messages/send");
  https.addHeader("Authorization", "Bearer " + token);
  https.addHeader("Content-Type", "application/json");

  // Create the JSON payload
  JsonDocument doc;
  doc["raw"] = encodedMsg;
  String payload;
  serializeJson(doc, payload);

  int httpCode = https.POST(payload);
  String result = "";

  if (httpCode == HTTP_CODE_OK || httpCode == 201) {
    result = "Email successfully sent to " + toAddress;
  } else {
    result = "Failed to send email (HTTP " + String(httpCode) + ").";
    Serial.print("[Error]: Gmail send failed: ");
    Serial.println(https.getString());
  }

  https.end();
  return result;
}

String searchAndReadDriveFile(String query) {
  String token = getValidGoogleAccessToken();
  if (token.length() == 0) return "Authentication error.";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  // Search file metadata
  String url = "https://www.googleapis.com/drive/v3/files?q=name+contains+'" + query + 
               "'&pageSize=1&fields=files(id,name,mimeType)";
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + token);

  int httpCode = https.GET();
  String fileContent = "No matching file found in Google Drive.";

  if (httpCode == HTTP_CODE_OK) {
    JsonDocument doc;
    deserializeJson(doc, https.getString());
    JsonArray files = doc["files"].as<JsonArray>();

    if (files.size() > 0) {
      String fileId = String(files[0]["id"] | "");
      String fileName = String(files[0]["name"] | "");
      String mimeType = String(files[0]["mimeType"] | "");
      https.end();

      String fetchUrl = "";
      if (mimeType == "application/vnd.google-apps.document") {
        fetchUrl = "https://www.googleapis.com/drive/v3/files/" + fileId + "/export?mimeType=text/plain";
      } else if (mimeType == "text/plain") {
        fetchUrl = "https://www.googleapis.com/drive/v3/files/" + fileId + "?alt=media";
      }

      if (fetchUrl.length() > 0) {
        https.begin(client, fetchUrl);
        https.addHeader("Authorization", "Bearer " + token);
        if (https.GET() == HTTP_CODE_OK) {
          String text = https.getString();
          if (text.length() > 1500) text = text.substring(0, 1500) + "... [Truncated]";
          fileContent = "Content of '" + fileName + "':\n" + text;
        }
      } else {
        fileContent = "Found '" + fileName + "', but its file type cannot be read directly.";
      }
    }
  }

  https.end();
  return fileContent;
}

// ================= Groq chat: text-only turn =================
// ================= Groq chat: text-only turn (WITH TOOLS) =================

// The JSON schema telling Groq what functions it can call
// The JSON schema telling Groq what functions it can call
const char* TOOLS_JSON = R"([
  {"type":"function","function":{"name":"get_unread_emails","description":"Fetch user's latest unread emails.","parameters":{"type":"object","properties":{"max_results":{"type":"integer"}}}}},
  {"type":"function","function":{"name":"send_email","description":"Send an email to a specific address.","parameters":{"type":"object","properties":{"to_address":{"type":"string"},"subject":{"type":"string"},"body":{"type":"string"}},"required":["to_address","subject","body"]}}},
  {"type":"function","function":{"name":"get_upcoming_events","description":"Fetch upcoming schedule.","parameters":{"type":"object","properties":{"max_results":{"type":"integer"}}}}},
  {"type":"function","function":{"name":"create_event","description":"Create a new event. start_time MUST be ISO 8601.","parameters":{"type":"object","properties":{"summary":{"type":"string"},"start_time":{"type":"string"},"end_time":{"type":"string"}},"required":["summary","start_time"]}}},
  {"type":"function","function":{"name":"search_drive","description":"Search Drive for a document.","parameters":{"type":"object","properties":{"query":{"type":"string"}},"required":["query"]}}}
])";

String sendTextChat(const String& question) {
  // ---------------- PHASE 1: Ask Groq with Tools ----------------
  JsonDocument doc;
  doc["model"] = GROQ_CHAT_MODEL;
  doc["temperature"] = 0.1;
  doc["max_tokens"] = 500;
  doc["reasoning_effort"] = "none";
  
  // Attach the tools schema
  deserializeJson(doc["tools"], TOOLS_JSON);
  doc["tool_choice"] = "auto";

  JsonArray messages = doc["messages"].to<JsonArray>();
  
  JsonObject sysMsg = messages.add<JsonObject>();
  sysMsg["role"] = "system";
  sysMsg["content"] = systemPrompt;

  for (int i = 0; i < historyCount; i++) {
    JsonObject m = messages.add<JsonObject>();
    m["role"] = historyRole[i];
    m["content"] = historyContent[i];
  }

  JsonObject userMsg = messages.add<JsonObject>();
  userMsg["role"] = "user";
  userMsg["content"] = question;

  String body;
  serializeJson(doc, body);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.begin(client, "https://api.groq.com/openai/v1/chat/completions");
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);

  int code = https.POST(body);
  String payload = https.getString();
  
  if (code != HTTP_CODE_OK) {
    Serial.printf("[Error]: Groq Phase 1 HTTP %d\n%s\n", code, payload.c_str());
    https.end();
    return "I'm having trouble connecting to my brain right now.";
  }

  JsonDocument respDoc;
  deserializeJson(respDoc, payload);
  
  JsonObject responseMsg = respDoc["choices"][0]["message"];
  String assistantText = responseMsg["content"] | "";
  
  // ---------------- PHASE 2: Execute Tools if Requested ----------------
  if (responseMsg.containsKey("tool_calls")) {
    JsonArray toolCalls = responseMsg["tool_calls"];
    
    // Create a new payload for Phase 2
    JsonDocument phase2Doc;
    phase2Doc["model"] = GROQ_CHAT_MODEL;
    phase2Doc["temperature"] = 0.1;
    phase2Doc["max_tokens"] = 500;
    phase2Doc["reasoning_effort"] = "none";
    JsonArray p2Messages = phase2Doc["messages"].to<JsonArray>();
    
    // Copy original messages over
    p2Messages.add<JsonObject>()["role"] = "system";
    p2Messages[0]["content"] = systemPrompt;
    for (int i = 0; i < historyCount; i++) {
      JsonObject m = p2Messages.add<JsonObject>();
      m["role"] = historyRole[i];
      m["content"] = historyContent[i];
    }
    p2Messages.add<JsonObject>()["role"] = "user";
    p2Messages[historyCount + 1]["content"] = question;

    // Append the AI's tool call intent
    JsonObject aiIntent = p2Messages.add<JsonObject>();
    aiIntent["role"] = "assistant";
    if (assistantText.length() > 0) aiIntent["content"] = assistantText;
    aiIntent["tool_calls"] = toolCalls;

    // Execute each tool and append the results
    for (JsonObject toolCall : toolCalls) {
      String toolId = toolCall["id"] | "";
      String funcName = toolCall["function"]["name"] | "";
      String argsStr = toolCall["function"]["arguments"] | "{}";
      
      JsonDocument argsDoc;
      deserializeJson(argsDoc, argsStr);
      
      String toolResult = "";
      
      if (funcName == "get_unread_emails") {
        Serial.println("[System]: Executing tool -> get_unread_emails");
        toolResult = getUnreadEmails(argsDoc["max_results"] | 3);
      } 
      else if (funcName == "send_email") {
        Serial.println("[System]: Executing tool -> send_email");
        toolResult = sendEmail(
          argsDoc["to_address"] | "",
          argsDoc["subject"] | "Sent from ASPIRE",
          argsDoc["body"] | ""
        );
      }
      else if (funcName == "get_upcoming_events") {
        Serial.println("[System]: Executing tool -> get_upcoming_events");
        toolResult = getUpcomingCalendarEvents(argsDoc["max_results"] | 5);
      } 
      else if (funcName == "create_event") {
        Serial.println("[System]: Executing tool -> create_event");
        toolResult = createCalendarEvent(
          argsDoc["summary"] | "Untitled",
          argsDoc["start_time"] | "",
          argsDoc["end_time"] | ""
        );
      }
      else if (funcName == "search_drive") {
        Serial.println("[System]: Executing tool -> search_drive");
        toolResult = searchAndReadDriveFile(argsDoc["query"] | "");
      }
      else {
        toolResult = "Error: Tool not implemented.";
      }

      // Add the raw data back to the conversation
      JsonObject toolData = p2Messages.add<JsonObject>();
      toolData["role"] = "tool";
      toolData["tool_call_id"] = toolId;
      toolData["name"] = funcName;
      toolData["content"] = toolResult;
    }

    // Send the Phase 2 request with the injected data
    String p2Body;
    serializeJson(phase2Doc, p2Body);
    int p2Code = https.POST(p2Body);
    
    if (p2Code == HTTP_CODE_OK) {
      JsonDocument p2Resp;
      deserializeJson(p2Resp, https.getString());
      assistantText = String(p2Resp["choices"][0]["message"]["content"] | "");
    } else {
      Serial.printf("[Error]: Groq Phase 2 HTTP %d\n", p2Code);
    }
  }

  https.end();

  if (assistantText.length() > 0) {
    addToHistory("user", question);
    addToHistory("assistant", assistantText);
  }
  return assistantText;
}

// ================= Groq chat: vision turn =================
String sendVisionChat(const String& question, uint8_t* jpgBuf, size_t jpgLen) {
  size_t b64Len = 0;
  char* b64 = base64Encode(jpgBuf, jpgLen, &b64Len);
  if (!b64) {
    Serial.println("[Error]: base64 encode failed");
    return "";
  }

  char* sysEsc = jsonEscape(systemPrompt);
  char* qEsc = jsonEscape(question);

  size_t bodyCap = strlen(sysEsc) + strlen(qEsc) + b64Len + 512;
  char* bodyBuf = (char*) ps_malloc(bodyCap);
  if (!bodyBuf) {
    Serial.println("[Error]: PSRAM alloc failed for vision request body");
    free(b64); free(sysEsc); free(qEsc);
    return "";
  }

  size_t pos = 0;
  pos = appendStr(bodyBuf, pos, "{\"model\":\"");
  pos = appendStr(bodyBuf, pos, GROQ_CHAT_MODEL);
  pos = appendStr(bodyBuf, pos, "\",\"temperature\":0.1,\"max_tokens\":400,\"reasoning_effort\":\"none\",\"messages\":[{\"role\":\"system\",\"content\":\"");
  pos = appendStr(bodyBuf, pos, sysEsc);
  pos = appendStr(bodyBuf, pos, "\"},{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"");
  pos = appendStr(bodyBuf, pos, qEsc);
  pos = appendStr(bodyBuf, pos, "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,");
  pos = appendStr(bodyBuf, pos, b64);
  pos = appendStr(bodyBuf, pos, "\"}}]}]}");
  bodyBuf[pos] = '\0';

  free(b64); free(sysEsc); free(qEsc);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.begin(client, "https://api.groq.com/openai/v1/chat/completions");
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);

  int code = https.POST((uint8_t*)bodyBuf, pos);
  String responseText = "";
  if (code == HTTP_CODE_OK) {
    String payload = https.getString();
    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, payload);
    if (!err) {
      const char* c = respDoc["choices"][0]["message"]["content"] | "";
      responseText = String(c);
    } else {
      Serial.print("[Error]: vision JSON parse: ");
      Serial.println(err.c_str());
    }
  } else {
    Serial.print("[Error]: Groq vision HTTP ");
    Serial.println(code);
    Serial.println(https.getString());
  }
  https.end();
  free(bodyBuf);

  // Mirrors the Python behaviour: an image turn resets the running text
  // history, then this exchange becomes the new (short) history.
  historyCount = 0;
  if (responseText.length() > 0) {
    addToHistory("user", question);
    addToHistory("assistant", responseText);
  }
  return responseText;
}

// ================= Networking / time setup =================
void connectWiFi() {
  Serial.print("[System]: Initializing WiFi...");
  
  // --- THE GLITCH FIX ---
  // Stop saving WiFi state to flash memory to prevent corruption
  WiFi.persistent(false);       
  
  // Hard disconnect, turn off radio, and clear old credentials
  WiFi.disconnect(true, true);  
  delay(200); // Give the radio hardware a moment to reset
  // ----------------------
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  bool ledState = false;
  unsigned long startAttemptTime = millis();
  
  // Try connecting for 15 seconds max
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    digitalWrite(LED_PIN, ledState ? LOW : HIGH);
    ledState = !ledState;
    delay(300);
    Serial.print(".");
  }
  
  // If it still fails, force the entire chip to reboot
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[Error]: WiFi connection timed out. Restarting board...");
    digitalWrite(LED_PIN, HIGH);
    delay(1000);
    ESP.restart(); 
  }
  
  digitalWrite(LED_PIN, HIGH);
  Serial.println(" connected!");
}

void syncTime() {
  // 1. Give the router a brief moment to stabilize DNS after WiFi connects
  delay(1000); 
  
  // 2. Configure time using three highly reliable NTP servers
  configTzTime(TZ_STRING, "pool.ntp.org", "time.google.com", "time.windows.com");
  
  Serial.print("[System]: Syncing time");
  struct tm timeinfo;
  
  bool ledState = false;
  unsigned long startAttemptTime = millis();
  
  // 3. Try for exactly 10 seconds max. 
  // We pass '100' so getLocalTime only blocks for 100ms instead of 5,000ms!
  while (!getLocalTime(&timeinfo, 100) && millis() - startAttemptTime < 10000) {
    digitalWrite(LED_PIN, ledState ? LOW : HIGH); // Toggle LED
    ledState = !ledState;
    Serial.print(".");
    delay(400); // Pad the loop so the LED blinks nicely
  }
  
  digitalWrite(LED_PIN, HIGH); // Ensure LED is OFF when done
  
  // 4. Handle success vs timeout gracefully
  if (millis() - startAttemptTime >= 10000) {
    Serial.println("\n[Warning]: Time sync timed out. Falling back to offline time.");
  } else {
    Serial.println(" synced!");
  }
}

// ================= One turn of conversation =================
void conversationTurn() {
  RecordResult rec = recordUntilSilence();
  digitalWrite(LED_PIN, LOW);

  if (rec.timedOut) {
    Serial.println("[System]: Idle timeout - going back to sleep.");
    appState = STATE_WAITING_FOR_WAKE;
    resetConversationState();
    return;
  }
  if (!rec.wav) {
    Serial.println("[Error]: Recording failed.");
    return;
  }

  // Snap the photo the instant recording ends, same as the Python
  // "SNAP PHOTO INSTANTLY ON SILENCE" behaviour, before transcription eats time.
  camera_fb_t* fb = nullptr;
  if (visionMode) fb = captureHighResFrame();

  Serial.println("[System]: Transcribing...");
  String question = transcribeWithGroq(rec.wav, rec.wavLen);
  free(rec.wav);

  if (question.length() == 0) {
    Serial.println("[System]: Didn't catch that.");
    if (fb) esp_camera_fb_return(fb);
    return;
  }

  Serial.print("[You]: ");
  Serial.println(question);

  String lowerQ = question;
  lowerQ.toLowerCase();

  if (lowerQ.indexOf("start vision") >= 0) {
    visionMode = true;
    Serial.println("[System]: Vision mode ON.");
    if (fb) esp_camera_fb_return(fb);
    return;
  }
  if (lowerQ.indexOf("start voice") >= 0 || lowerQ.indexOf("stop vision") >= 0) {
    visionMode = false;
    Serial.println("[System]: Vision mode OFF.");
    if (fb) esp_camera_fb_return(fb);
    return;
  }
  if (lowerQ.indexOf("sleep") >= 0 || lowerQ.indexOf("quit") >= 0 ||
      lowerQ.indexOf("bye") >= 0 || lowerQ.indexOf("shut down") >= 0) {
    Serial.println("[System]: Going offline.");
    if (fb) esp_camera_fb_return(fb);
    appState = STATE_WAITING_FOR_WAKE;
    resetConversationState();
    return;
  }

  String answer;
  if (visionMode && fb) {
    Serial.println("[System]: Asking Groq (vision)...");
    answer = sendVisionChat(question, fb->buf, fb->len);
    esp_camera_fb_return(fb);
  } else {
    if (fb) esp_camera_fb_return(fb);
    Serial.println("[System]: Asking Groq (text)...");
    answer = sendTextChat(question);
  }

  Serial.println("[Assistant]:");
  Serial.println(answer.length() ? answer : "(no response)");
}

// ================= Setup / loop =================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  delay(300);

  I2S.setPinsPdmRx(I2S_CLK, I2S_DIN);
  if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[Error]: Failed to initialize I2S mic!");
    while (1) delay(1000);
  }

  if (!initCamera()) {
    Serial.println("[Error]: Camera unavailable - vision mode will fail until this is fixed.");
  }

  connectWiFi();
  syncTime();
  resetConversationState();

  Serial.println("[System]: Ready. Say \"wake up\" to start a conversation.");
}

void loop() {
  switch (appState) {
    case STATE_WAITING_FOR_WAKE: {
      digitalWrite(LED_PIN, LOW); // Turn LED ON (Static)
      
      // Only print if we just entered this state (using historyCount as a lazy state-tracker)
      static bool justEntered = true;
      if (justEntered) {
        Serial.println("[System]: Listening for wake word...");
        justEntered = false;
      }

      if (listenForWakeWord()) {
        Serial.println("[System]: >>> Wake word detected <<<");
        resetConversationState();
        appState = STATE_AWAKE;
        justEntered = true; // Reset for the next time we go to sleep
      }
      break; 
    }
    case STATE_AWAKE: {
      conversationTurn();
      break;
    }
  }
}