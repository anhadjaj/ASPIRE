// ============================================================
// MEMORY DEBUG
// ============================================================

void printMemoryDebug(const char* tag) {

  LOG_PRINTF(
    "[MEM][%s] Free heap: %u bytes\n",
    tag,
    ESP.getFreeHeap()
  );

  LOG_PRINTF(
    "[MEM][%s] Min free heap: %u bytes\n",
    tag,
    ESP.getMinFreeHeap()
  );

  if (psramFound()) {

    LOG_PRINTF(
      "[MEM][%s] PSRAM total: %u bytes\n",
      tag,
      ESP.getPsramSize()
    );

    LOG_PRINTF(
      "[MEM][%s] PSRAM free: %u bytes\n",
      tag,
      ESP.getFreePsram()
    );

  } else {

    LOG_PRINTF(
      "[MEM][%s] PSRAM NOT FOUND\n",
      tag
    );
  }
}


// ============================================================
// WAV HEADER
// ============================================================

void writeWavHeader(
  uint8_t* header,
  uint32_t dataSize
) {

  uint32_t sampleRate = SAMPLE_RATE;

  uint16_t bitsPerSample = SAMPLE_BITS;

  uint16_t numChannels = 1;

  uint32_t byteRate =
    sampleRate *
    numChannels *
    bitsPerSample /
    8;

  uint16_t blockAlign =
    numChannels *
    bitsPerSample /
    8;

  uint32_t chunkSize =
    36 + dataSize;


  memcpy(header + 0, "RIFF", 4);

  memcpy(
    header + 4,
    &chunkSize,
    4
  );

  memcpy(
    header + 8,
    "WAVE",
    4
  );

  memcpy(
    header + 12,
    "fmt ",
    4
  );


  uint32_t subchunk1Size = 16;

  memcpy(
    header + 16,
    &subchunk1Size,
    4
  );


  uint16_t audioFormat = 1;

  memcpy(
    header + 20,
    &audioFormat,
    2
  );

  memcpy(
    header + 22,
    &numChannels,
    2
  );

  memcpy(
    header + 24,
    &sampleRate,
    4
  );

  memcpy(
    header + 28,
    &byteRate,
    4
  );

  memcpy(
    header + 32,
    &blockAlign,
    2
  );

  memcpy(
    header + 34,
    &bitsPerSample,
    2
  );

  memcpy(
    header + 36,
    "data",
    4
  );

  memcpy(
    header + 40,
    &dataSize,
    4
  );
}


// ============================================================
// MICROPHONE
// ============================================================

int16_t readSample() {

  int s = I2S.read();

  if (
    s == -1 ||
    s == 1
  ) {
    return 0;
  }

  return (int16_t)s;
}


void readBlock(
  int16_t* buf,
  uint32_t n
) {

  for (
    uint32_t i = 0;
    i < n;
    i++
  ) {

    buf[i] =
      readSample();
  }
}


float computeRMS(
  int16_t* buf,
  uint32_t n
) {

  double mean = 0.0;

  for (
    uint32_t i = 0;
    i < n;
    i++
  ) {

    mean +=
      buf[i];
  }

  mean /= n;


  double sumSq = 0.0;

  for (
    uint32_t i = 0;
    i < n;
    i++
  ) {

    double sample =
      (double)buf[i] -
      mean;

    sumSq +=
      sample *
      sample;
  }

  return sqrt(
    sumSq /
    n
  );
}


// ============================================================
// RECORD UNTIL SILENCE
// ============================================================

RecordResult recordUntilSilence() {

  RecordResult result = {
    nullptr,
    0,
    false
  };


  size_t maxSamples =
    (
      SAMPLE_RATE *
      MAX_RECORD_MS
    ) /
    1000;


  size_t maxBytes =
    maxSamples *
    (
      SAMPLE_BITS /
      8
    ) +
    WAV_HEADER_SIZE;


  uint8_t* buf =
    (uint8_t*)ps_malloc(
      maxBytes
    );


  if (!buf) {

    LOG_PRINTLN(
      "[Error]: PSRAM alloc failed for recording buffer"
    );

    return result;
  }


  int16_t* block =
    (int16_t*)malloc(
      BLOCK_SAMPLES *
      sizeof(int16_t)
    );


  int16_t* prevBlock1 =
    (int16_t*)calloc(
      BLOCK_SAMPLES,
      sizeof(int16_t)
    );


  int16_t* prevBlock2 =
    (int16_t*)calloc(
      BLOCK_SAMPLES,
      sizeof(int16_t)
    );


  if (
    !block ||
    !prevBlock1 ||
    !prevBlock2
  ) {

    LOG_PRINTLN(
      "[Error]: Malloc failed for audio blocks"
    );

    if (block) {
      free(block);
    }

    if (prevBlock1) {
      free(prevBlock1);
    }

    if (prevBlock2) {
      free(prevBlock2);
    }

    free(buf);

    return result;
  }


  bool hasSpoken = false;

  int consecutiveLoud = 0;

  int idleBlocks = 0;

  int trailingSilence = 0;

  size_t samplesWritten = 0;


  int16_t* pcmStart =
    (int16_t*)(
      buf +
      WAV_HEADER_SIZE
    );


  if (visionMode) {

    LOG_PRINTLN(
      "[System]: Listening... (VISION ACTIVE)"
    );

  } else {

    LOG_PRINTLN(
      "[System]: Listening..."
    );
  }


  digitalWrite(
    LED_PIN,
    HIGH
  );


  while (true) {

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
      rms >
      RMS_THRESHOLD
    ) {

      consecutiveLoud++;

      idleBlocks = 0;


      if (
        consecutiveLoud >=
          SPEECH_DEBOUNCE_BLOCKS
        &&
        !hasSpoken
      ) {

        hasSpoken = true;

        trailingSilence = 0;


        if (
          samplesWritten +
            2 * BLOCK_SAMPLES
          <=
          maxSamples
        ) {

          memcpy(
            pcmStart +
              samplesWritten,
            prevBlock1,
            BLOCK_SAMPLES *
              sizeof(int16_t)
          );

          samplesWritten +=
            BLOCK_SAMPLES;


          memcpy(
            pcmStart +
              samplesWritten,
            prevBlock2,
            BLOCK_SAMPLES *
              sizeof(int16_t)
          );

          samplesWritten +=
            BLOCK_SAMPLES;
        }

      } else if (
        hasSpoken
      ) {

        trailingSilence = 0;
      }

    } else {

      consecutiveLoud = 0;


      if (hasSpoken) {

        trailingSilence++;

      } else {

        idleBlocks++;
      }
    }


    if (hasSpoken) {

      if (
        samplesWritten +
          BLOCK_SAMPLES
        <=
        maxSamples
      ) {

        memcpy(
          pcmStart +
            samplesWritten,
          block,
          BLOCK_SAMPLES *
            sizeof(int16_t)
        );

        samplesWritten +=
          BLOCK_SAMPLES;

      } else {

        break;
      }

    } else {

      memcpy(
        prevBlock1,
        prevBlock2,
        BLOCK_SAMPLES *
          sizeof(int16_t)
      );


      memcpy(
        prevBlock2,
        block,
        BLOCK_SAMPLES *
          sizeof(int16_t)
      );
    }


    if (
      hasSpoken &&
      trailingSilence >=
        TRAILING_SILENCE_BLOCKS
    ) {

      break;
    }


    if (
      !hasSpoken &&
      idleBlocks >=
        IDLE_TIMEOUT_BLOCKS
    ) {

      free(buf);

      free(block);

      free(prevBlock1);

      free(prevBlock2);

      result.timedOut =
        true;

      return result;
    }
  }


  uint32_t dataSize =
    samplesWritten *
    (
      SAMPLE_BITS /
      8
    );


  writeWavHeader(
    buf,
    dataSize
  );


  result.wav =
    buf;


  result.wavLen =
    dataSize +
    WAV_HEADER_SIZE;


  free(block);

  free(prevBlock1);

  free(prevBlock2);


  return result;
}


// ============================================================
// BASE64
// ============================================================

static const char B64_TABLE[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789+/";


char* base64Encode(
  const uint8_t* data,
  size_t len,
  size_t* outLen
) {

  size_t encodedLen =
    4 *
    (
      (len + 2) /
      3
    );


  char* out =
    (char*)ps_malloc(
      encodedLen +
      1
    );


  if (!out) {

    return nullptr;
  }


  size_t i = 0;

  size_t j = 0;


  while (
    i + 3 <=
    len
  ) {

    uint32_t n =
      (
        (uint32_t)data[i]
        << 16
      )
      |
      (
        (uint32_t)data[i + 1]
        << 8
      )
      |
      data[i + 2];


    out[j++] =
      B64_TABLE[
        (n >> 18) &
        0x3F
      ];


    out[j++] =
      B64_TABLE[
        (n >> 12) &
        0x3F
      ];


    out[j++] =
      B64_TABLE[
        (n >> 6) &
        0x3F
      ];


    out[j++] =
      B64_TABLE[
        n &
        0x3F
      ];


    i += 3;
  }


  size_t rem =
    len - i;


  if (
    rem == 1
  ) {

    uint32_t n =
      (uint32_t)data[i]
      << 16;


    out[j++] =
      B64_TABLE[
        (n >> 18) &
        0x3F
      ];


    out[j++] =
      B64_TABLE[
        (n >> 12) &
        0x3F
      ];


    out[j++] = '=';

    out[j++] = '=';

  } else if (
    rem == 2
  ) {

    uint32_t n =
      (
        (uint32_t)data[i]
        << 16
      )
      |
      (
        (uint32_t)data[i + 1]
        << 8
      );


    out[j++] =
      B64_TABLE[
        (n >> 18) &
        0x3F
      ];


    out[j++] =
      B64_TABLE[
        (n >> 12) &
        0x3F
      ];


    out[j++] =
      B64_TABLE[
        (n >> 6) &
        0x3F
      ];


    out[j++] = '=';
  }


  out[j] =
    '\0';


  *outLen =
    j;


  return out;
}


// ============================================================
// STRING HELPERS
// ============================================================

size_t appendStr(
  char* dest,
  size_t pos,
  const char* src
) {

  size_t len =
    strlen(src);


  memcpy(
    dest + pos,
    src,
    len
  );


  return pos + len;
}


char* jsonEscape(
  const String& input
) {

  size_t n =
    input.length();


  char* out =
    (char*)ps_malloc(
      n * 2 +
      1
    );


  if (!out) {

    return nullptr;
  }


  size_t j = 0;


  for (
    size_t i = 0;
    i < n;
    i++
  ) {

    char c =
      input[i];


    if (
      c == '"' ||
      c == '\\'
    ) {

      out[j++] = '\\';

      out[j++] = c;

    } else if (
      c == '\n'
    ) {

      out[j++] = '\\';

      out[j++] = 'n';

    } else if (
      c == '\r'
    ) {

      // Drop carriage return.

    } else {

      out[j++] = c;
    }
  }


  out[j] =
    '\0';


  return out;
}


// ============================================================
// ROBUST TLS WRITE
// ============================================================

bool secureWriteAll(
  WiFiClientSecure& client,
  const uint8_t* data,
  size_t len,
  unsigned long stallTimeoutMs = 8000
) {

  size_t sent = 0;

  unsigned long lastProgress =
    millis();


  while (
    sent <
    len
  ) {

    if (
      !client.connected()
    ) {

      return false;
    }


    size_t written =
      client.write(
        data + sent,
        len - sent
      );


    if (
      written > 0
    ) {

      sent +=
        written;

      lastProgress =
        millis();

    } else {

      if (
        millis() -
          lastProgress >
        stallTimeoutMs
      ) {

        return false;
      }


      delay(10);

      yield();
    }
  }


  return true;
}
