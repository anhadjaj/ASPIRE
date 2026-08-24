// ============================================================
// GROQ TTS
// ============================================================

void playAudioTTS(
  String text
) {

  if (
    text.length() ==
    0
  ) {

    return;
  }


  LOG_INFO_PRINTLN(
    "[System]: Streaming TTS audio from Groq..."
  );


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  if (
    !https.begin(
      client,
      "https://api.groq.com/openai/v1/audio/speech"
    )
  ) {

    LOG_ERROR_PRINTLN(
      "[Error]: Failed to initialize TTS connection"
    );


    return;
  }


  https.addHeader(
    "Authorization",
    String("Bearer ") +
    GROQ_API_KEY
  );


  https.addHeader(
    "Content-Type",
    "application/json"
  );


  char* escapedText =
    jsonEscape(
      text
    );


  if (!escapedText) {

    LOG_ERROR_PRINTLN(
      "[Error]: TTS memory allocation failed"
    );


    https.end();


    return;
  }


  String payload =
    "{\"model\":\"canopylabs/orpheus-v1-english\","
    "\"voice\":\"troy\","
    "\"response_format\":\"wav\","
    "\"input\":\"" +
    String(escapedText) +
    "\"}";


  free(
    escapedText
  );


  int httpCode =
    https.POST(
      payload
    );


  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    WiFiClient* stream =
      https.getStreamPtr();


    uint8_t header[44];


    size_t headerRead =
      stream->readBytes(
        header,
        44
      );


    if (
      headerRead !=
      44
    ) {

      LOG_ERROR_PRINTLN(
        "[Error]: Invalid TTS WAV header"
      );


      https.end();


      return;
    }


    I2S.end();


    I2S.setPins(
      I2S_SPK_BCLK,
      I2S_SPK_LRC,
      I2S_SPK_DIN,
      -1,
      -1
    );


    if (
      !I2S.begin(
        I2S_MODE_STD,
        24000,
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO
      )
    ) {

      LOG_ERROR_PRINTLN(
        "[Error]: Failed to start I2S speaker mode"
      );


      https.end();


      return;
    }


    uint8_t inBuf[1024];

    uint8_t outBuf[2048];


    while (
      https.connected() ||
      stream->available()
    ) {

      if (
        !stream->available()
      ) {

        delay(1);

        yield();

        continue;
      }


      int len =
        stream->readBytes(
          inBuf,
          sizeof(inBuf)
        );


      if (
        len <=
        0
      ) {

        continue;
      }


      int numSamples =
        len /
        2;


      for (
        int i = 0;
        i < numSamples;
        i++
      ) {

        outBuf[
          i * 4 + 0
        ] =
          inBuf[
            i * 2 + 0
          ];


        outBuf[
          i * 4 + 1
        ] =
          inBuf[
            i * 2 + 1
          ];


        outBuf[
          i * 4 + 2
        ] =
          inBuf[
            i * 2 + 0
          ];


        outBuf[
          i * 4 + 3
        ] =
          inBuf[
            i * 2 + 1
          ];
      }


      I2S.write(
        outBuf,
        numSamples *
        4
      );
    }


    LOG_INFO_PRINTLN(
      "[System]: Audio playback complete."
    );


    I2S.end();


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

      LOG_ERROR_PRINTLN(
        "[Error]: Failed to restore microphone mode"
      );
    }

  } else {

    LOG_ERROR_PRINTF(
      "[Error]: TTS HTTP failed with code %d\n",
      httpCode
    );


    LOG_DEBUG_PRINTLN(
      https.getString()
    );
  }


  https.end();
}
