// ============================================================
// GROQ WHISPER STT
// ============================================================

String transcribeWithGroq(
  uint8_t* wavData,
  size_t wavLen
) {

  if (
    !wavData ||
    wavLen <=
      WAV_HEADER_SIZE
  ) {

    LOG_PRINTLN(
      "[Error]: Invalid WAV buffer"
    );

    return "";
  }


  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    LOG_PRINTLN(
      "[Error]: WiFi disconnected before STT"
    );

    return "";
  }


  LOG_PRINTLN();

  LOG_PRINTLN(
    "========== STT DEBUG =========="
  );


  LOG_PRINTF(
    "[STT] WiFi status: %d\n",
    WiFi.status()
  );


  LOG_PRINTF(
    "[STT] Local IP: %s\n",
    WiFi.localIP()
      .toString()
      .c_str()
  );


  LOG_PRINTF(
    "[STT] RSSI: %d dBm\n",
    WiFi.RSSI()
  );


  printMemoryDebug(
    "BEFORE_STT_TLS"
  );


  LOG_PRINTF(
    "[STT] WAV size: %u bytes\n",
    (unsigned)wavLen
  );


  // ----------------------------------------------------------
  // DNS
  // ----------------------------------------------------------

  IPAddress groqIP;


  LOG_PRINTLN(
    "[STT] Resolving api.groq.com..."
  );


  if (
    !WiFi.hostByName(
      "api.groq.com",
      groqIP
    )
  ) {

    LOG_PRINTLN(
      "[Error]: DNS lookup failed for api.groq.com"
    );

    return "";
  }


  LOG_PRINT(
    "[STT] api.groq.com resolved to: "
  );

  LOG_PRINTLN(
    groqIP
  );


  // ----------------------------------------------------------
  // TLS
  // ----------------------------------------------------------

  WiFiClientSecure client;


  client.setInsecure();

  client.setTimeout(
    20000
  );


  LOG_PRINTLN(
    "[STT] Starting TLS connection to api.groq.com:443..."
  );


  unsigned long tlsStart =
    millis();


  if (
    !client.connect(
      "api.groq.com",
      443
    )
  ) {

    LOG_PRINTF(
      "[Error]: Secure client connection failed after %lu ms\n",
      millis() -
      tlsStart
    );


    printMemoryDebug(
      "STT_TLS_FAIL"
    );


    return "";
  }


  LOG_PRINTF(
    "[STT] TLS connection SUCCESS in %lu ms\n",
    millis() -
    tlsStart
  );


  printMemoryDebug(
    "AFTER_STT_TLS"
  );


  // ----------------------------------------------------------
  // MULTIPART BODY
  // ----------------------------------------------------------

  const String boundary =
    "----ViperGroqBoundary7331";


  String preFile =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
    String(GROQ_STT_MODEL) +
    "\r\n" +

    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"language\"\r\n\r\n"
    "en\r\n" +

    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
    "json\r\n" +

    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n";


  String postFile =
    "\r\n--" +
    boundary +
    "--\r\n";


  size_t totalLen =
    preFile.length() +
    wavLen +
    postFile.length();


  LOG_PRINTF(
    "[STT] HTTP multipart body: %u bytes\n",
    (unsigned)totalLen
  );


  // ----------------------------------------------------------
  // HTTP REQUEST
  // ----------------------------------------------------------

  client.print(
    "POST /openai/v1/audio/transcriptions HTTP/1.1\r\n"
  );


  client.print(
    "Host: api.groq.com\r\n"
  );


  client.print(
    "Authorization: Bearer "
  );


  client.print(
    GROQ_API_KEY
  );


  client.print(
    "\r\n"
  );


  client.print(
    "Content-Type: multipart/form-data; boundary="
  );


  client.print(
    boundary
  );


  client.print(
    "\r\n"
  );


  client.print(
    "Content-Length: "
  );


  client.print(
    totalLen
  );


  client.print(
    "\r\n"
  );


  client.print(
    "Connection: close\r\n\r\n"
  );


  // ----------------------------------------------------------
  // SEND PREFIX
  // ----------------------------------------------------------

  if (
    !secureWriteAll(
      client,
      (const uint8_t*)preFile.c_str(),
      preFile.length()
    )
  ) {

    LOG_PRINTLN(
      "[Error]: Failed sending multipart prefix"
    );


    client.stop();

    return "";
  }


  // ----------------------------------------------------------
  // SEND WAV FROM PSRAM
  // ----------------------------------------------------------

  LOG_PRINTLN(
    "[STT] Uploading WAV from PSRAM..."
  );


  const size_t CHUNK_SIZE =
    512;


  const unsigned long WRITE_STALL_TIMEOUT_MS =
    8000;


  size_t sent = 0;


  unsigned long uploadStart =
    millis();


  while (
    sent <
    wavLen
  ) {

    size_t remaining =
      wavLen -
      sent;


    size_t chunkSize =
      remaining <
        CHUNK_SIZE
      ?
        remaining
      :
        CHUNK_SIZE;


    size_t chunkSent = 0;


    unsigned long lastProgress =
      millis();


    int zeroWriteCount =
      0;


    while (
      chunkSent <
      chunkSize
    ) {

      if (
        !client.connected()
      ) {

        LOG_PRINTF(
          "[Error]: TLS connection closed at %u/%u bytes\n",
          (unsigned)(
            sent +
            chunkSent
          ),
          (unsigned)wavLen
        );


        client.stop();

        return "";
      }


      size_t written =
        client.write(
          wavData +
            sent +
            chunkSent,
          chunkSize -
            chunkSent
        );


      if (
        written >
        0
      ) {

        chunkSent +=
          written;


        lastProgress =
          millis();


        zeroWriteCount =
          0;

      } else {

        zeroWriteCount++;


        if (
          zeroWriteCount ==
            1
          ||
          zeroWriteCount %
            20 ==
            0
        ) {

          LOG_PRINTF(
            "[STT] Write stalled at %u bytes, retry %d\n",
            (unsigned)(
              sent +
              chunkSent
            ),
            zeroWriteCount
          );
        }


        if (
          millis() -
            lastProgress >
          WRITE_STALL_TIMEOUT_MS
        ) {

          LOG_PRINTF(
            "[Error]: STT upload stalled at %u/%u bytes\n",
            (unsigned)(
              sent +
              chunkSent
            ),
            (unsigned)wavLen
          );


          printMemoryDebug(
            "STT_UPLOAD_STALL"
          );


          client.stop();

          return "";
        }


        delay(10);

        yield();
      }
    }


    sent +=
      chunkSent;


    yield();


    if (
      sent %
        8192 <
      CHUNK_SIZE
    ) {

      LOG_PRINTF(
        "[STT] Uploaded %u/%u bytes\n",
        (unsigned)sent,
        (unsigned)wavLen
      );
    }
  }


  LOG_PRINTF(
    "[STT] WAV upload complete: %u bytes in %lu ms\n",
    (unsigned)sent,
    millis() -
    uploadStart
  );


  // ----------------------------------------------------------
  // MULTIPART END
  // ----------------------------------------------------------

  if (
    !secureWriteAll(
      client,
      (const uint8_t*)postFile.c_str(),
      postFile.length()
    )
  ) {

    LOG_PRINTLN(
      "[Error]: Failed sending multipart ending"
    );


    client.stop();

    return "";
  }


  // ----------------------------------------------------------
  // WAIT FOR RESPONSE
  // ----------------------------------------------------------

  LOG_PRINTLN(
    "[STT] Waiting for Groq response..."
  );


  unsigned long responseStart =
    millis();


  while (
    !client.available() &&
    millis() -
      responseStart <
      20000
  ) {

    if (
      !client.connected()
    ) {

      break;
    }


    delay(10);

    yield();
  }


  if (
    !client.available()
  ) {

    LOG_PRINTLN(
      "[Error]: STT response timeout"
    );


    client.stop();

    return "";
  }


  // ----------------------------------------------------------
  // STATUS LINE
  // ----------------------------------------------------------

  String statusLine =
    client.readStringUntil(
      '\n'
    );


  statusLine.trim();


  LOG_PRINT(
    "[STT] HTTP status: "
  );

  LOG_PRINTLN(
    statusLine
  );


  // ----------------------------------------------------------
  // RESPONSE HEADERS
  // ----------------------------------------------------------

  int contentLength =
    -1;


  while (
    client.connected() ||
    client.available()
  ) {

    String line =
      client.readStringUntil(
        '\n'
      );


    if (
      line ==
        "\r"
      ||
      line.length() ==
        0
    ) {

      break;
    }


    line.trim();


    if (
      line.startsWith(
        "Content-Length:"
      )
    ) {

      String lengthText =
        line.substring(
          strlen(
            "Content-Length:"
          )
        );


      lengthText.trim();


      contentLength =
        lengthText.toInt();
    }
  }


  LOG_PRINTF(
    "[STT] Response content length: %d\n",
    contentLength
  );


  // ----------------------------------------------------------
  // BODY
  // ----------------------------------------------------------

  String payload =
    "";


  if (
    contentLength >
    0
  ) {

    payload.reserve(
      contentLength +
      1
    );


    unsigned long bodyStart =
      millis();


    while (
      payload.length() <
        (size_t)contentLength
      &&
      millis() -
        bodyStart <
        10000
    ) {

      while (
        client.available() &&
        payload.length() <
          (size_t)contentLength
      ) {

        payload +=
          (char)client.read();
      }


      delay(1);

      yield();
    }

  } else {

    unsigned long lastData =
      millis();


    while (
      millis() -
        lastData <
        3000
    ) {

      while (
        client.available()
      ) {

        payload +=
          (char)client.read();


        lastData =
          millis();
      }


      if (
        !client.connected() &&
        !client.available()
      ) {

        break;
      }


      delay(1);

      yield();
    }
  }


  client.stop();


  LOG_PRINTF(
    "[STT] Response body: %u bytes\n",
    payload.length()
  );


  // ----------------------------------------------------------
  // HTTP ERROR
  // ----------------------------------------------------------

  if (
    statusLine.indexOf(
      "200"
    ) <
    0
  ) {

    LOG_PRINTLN(
      "[Error]: Groq STT request failed"
    );


    LOG_PRINTLN(
      payload
    );


    return "";
  }


  // ----------------------------------------------------------
  // JSON
  // ----------------------------------------------------------

  JsonDocument doc;


  DeserializationError err =
    deserializeJson(
      doc,
      payload
    );


  if (err) {

    LOG_PRINT(
      "[Error]: STT JSON parse failed: "
    );


    LOG_PRINTLN(
      err.c_str()
    );


    LOG_PRINTLN(
      "[STT] Raw response:"
    );


    LOG_PRINTLN(
      payload
    );


    return "";
  }


  String result =
    String(
      doc["text"] |
      ""
    );


  result.trim();


  LOG_PRINT(
    "[STT] Transcript: "
  );


  LOG_PRINTLN(
    result
  );


  printMemoryDebug(
    "AFTER_STT"
  );


  LOG_PRINTLN(
    "========== STT END =========="
  );


  LOG_PRINTLN();


  return result;
}


// ============================================================
// WAKE WORD
// ============================================================

bool listenForWakeWord() {

  int16_t* block =
    (int16_t*)malloc(
      BLOCK_SAMPLES *
      sizeof(int16_t)
    );


  if (!block) {

    LOG_PRINTLN(
      "[Error]: Malloc failed in wake listener"
    );

    return false;
  }


  readBlock(
    block,
    BLOCK_SAMPLES
  );


  float rms =
    computeRMS(
      block,
      BLOCK_SAMPLES
    );


  if (
    rms <
    RMS_THRESHOLD
  ) {

    free(block);

    return false;
  }


  LOG_PRINTF(
    "[WAKE] Sound detected. RMS: %.2f\n",
    rms
  );


  const uint32_t WAKE_RECORD_MS =
    1800;


  uint32_t numSamples =
    (
      SAMPLE_RATE *
      WAKE_RECORD_MS
    ) /
    1000;


  uint32_t dataSize =
    numSamples *
    (
      SAMPLE_BITS /
      8
    );


  uint32_t totalSize =
    dataSize +
    WAV_HEADER_SIZE;


  uint8_t* buf =
    (uint8_t*)ps_malloc(
      totalSize
    );


  if (!buf) {

    LOG_PRINTLN(
      "[Error]: Wake-word PSRAM allocation failed"
    );


    free(block);

    return false;
  }


  writeWavHeader(
    buf,
    dataSize
  );


  int16_t* pcm =
    (int16_t*)(
      buf +
      WAV_HEADER_SIZE
    );


  memcpy(
    pcm,
    block,
    BLOCK_SAMPLES *
      sizeof(int16_t)
  );


  free(block);


  for (
    uint32_t i =
      BLOCK_SAMPLES;
    i <
      numSamples;
    i++
  ) {

    pcm[i] =
      readSample();
  }


  String transcript =
    transcribeWithGroq(
      buf,
      totalSize
    );


  free(buf);


  transcript.toLowerCase();


  LOG_PRINT(
    "[WAKE] Transcript: "
  );

  LOG_PRINTLN(
    transcript
  );


  if (
    transcript.indexOf(
      WAKE_WORD
    ) >=
    0
    ||
    transcript.indexOf(
      "vyper"
    ) >=
    0
  ) {

    return true;
  }


  return false;
}
