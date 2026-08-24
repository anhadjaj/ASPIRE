# ESP32-S3 C++ Handoff

## Scope

This directory contains the Arduino firmware for the ESP32-S3 version of ASPIRE. It is a voice-first assistant with wake-word detection, speech recognition, Groq chat and vision, tool calls, and spoken responses. The repository root also contains a Python implementation; do not assume changes there automatically apply to this firmware.

## Hardware and external services

- Target hardware: Seeed XIAO ESP32S3 Sense.
- Status LED: GPIO 21.
- I2S microphone: clock GPIO 42, data GPIO 41.
- MAX98357A speaker: BCLK GPIO 2, LRC GPIO 4, DIN GPIO 1.
- Camera pin mapping is defined in `embedded_code.ino`.
- Cloud services: Groq STT/chat/vision/TTS, Google OAuth/Gmail/Calendar/Drive, NewsAPI, and Telegram.
- Main Arduino dependencies: `WiFi`, `WiFiClientSecure`, `HTTPClient`, `ArduinoJson`, `ESP_I2S`, and `esp_camera`.

## Runtime flow

1. `setup()` initializes Serial logging, the LED, microphone, speaker pins, camera, Wi-Fi, time, and conversation state.
2. `loop()` waits for the configured wake word and switches between `STATE_WAITING_FOR_WAKE` and `STATE_AWAKE`.
3. `conversationTurn()` records until silence, transcribes the WAV, handles mode/sleep commands, calls text or vision chat, prints the final exchange, and plays TTS audio.
4. Text chat can invoke Gmail, Calendar, Drive, News, or Telegram tools before requesting the final model response.

## File map

| File | Responsibility |
| --- | --- |
| `embedded_code.ino` | Includes, configuration, shared state/types, `setup()`, and `loop()` |
| `module_01_audio_core.ino` | Memory diagnostics, WAV creation, microphone capture, silence detection, Base64/string helpers, and robust TLS writes |
| `module_02_speech_recognition.ino` | Groq Whisper STT and wake-word recognition |
| `module_03_camera.ino` | Camera initialization and frame capture |
| `module_04_conversation_state.ino` | Conversation history and state reset |
| `module_05_google_services.ino` | Google token refresh, Calendar, and Gmail |
| `module_06_external_services.ino` | News, Drive search/read, and Telegram photos |
| `module_07_groq_chat.ino` | Tool schema, text/tool-call flow, and vision requests |
| `module_08_network_time.ino` | Wi-Fi connection and clock synchronization |
| `module_09_tts.ino` | Groq TTS streaming and speaker playback |
| `module_10_conversation_turn.ino` | One complete awake-state interaction |

The numeric prefixes preserve Arduino sketch concatenation order. Keep new shared types and configuration in `embedded_code.ino`, and place implementation in the closest module.

## Configuration and secrets

User configuration is near the top of `embedded_code.ino`. The credential fields are intentionally empty in Git:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `GROQ_API_KEY`
- `GOOGLE_CLIENT_ID`
- `GOOGLE_CLIENT_SECRET`
- `GOOGLE_REFRESH_TOKEN`
- `NEWS_API_KEY`
- `TELEGRAM_BOT_TOKEN`
- `TELEGRAM_CHAT_ID`

Never commit populated credentials. Configure them locally before flashing. Other important settings include `WAKE_WORD`, Groq model names, `TZ_STRING`, audio thresholds, recording duration, and conversation-history size.

## Logging policy

Logging is controlled by `ACTIVE_LOG_LEVEL` in `embedded_code.ino`. The intended default is:

```cpp
const LogLevel ACTIVE_LOG_LEVEL = LOG_LEVEL_INFO;
```

| Level | Output |
| --- | --- |
| `LOG_LEVEL_NONE` | No Serial initialization or output |
| `LOG_LEVEL_ERROR` | Failures only |
| `LOG_LEVEL_INFO` | Errors plus important lifecycle messages, final STT text shown as `[You]`, final model output shown as `[Assistant]`, and TTS start/completion |
| `LOG_LEVEL_DEBUG` | Everything above plus memory stats, DNS/TLS details, HTTP metadata and bodies, raw API payloads/responses, upload progress, RSSI, and internal STT/wake diagnostics |

Use the typed `LOG_ERROR_*`, `LOG_INFO_*`, and `LOG_DEBUG_*` macros. Do not add direct `Serial.print`, `Serial.println`, or `Serial.printf` calls. Raw request/response bodies belong at `DEBUG`, not `INFO`.

## Change guidelines

- Keep `setup()` and `loop()` small; put new behavior in the appropriate module.
- Preserve the numeric module ordering when a definition must precede another definition.
- Check every PSRAM or heap allocation and release it on all return paths.
- Keep HTTP response bodies and tokens out of normal logs.
- Maintain the current `INFO` contract: useful user/model output without verbose transport diagnostics.
- Avoid committing credentials, captured audio, images, tokens, or device-specific build artifacts.

## Validation and handoff status

The source split and log classification pass `git diff --check`, and all Serial output is routed through level-aware macros. At the current handoff, 47 calls are `ERROR`, 41 are `INFO`, and 55 are `DEBUG`.

Arduino CLI and PlatformIO were not available in the development environment, so the modularized sketch has not received a local firmware compile. Before hardware release:

1. Compile for the Seeed XIAO ESP32S3 Sense with the required ESP32 Arduino libraries installed.
2. Confirm PSRAM and camera support are enabled in the board settings.
3. Test boot, Wi-Fi/time synchronization, wake-word detection, STT, text chat, vision, tool calls, and TTS playback.
4. Test `LOG_LEVEL_INFO` and confirm raw payloads are hidden, then test `LOG_LEVEL_DEBUG` for diagnostic output.
