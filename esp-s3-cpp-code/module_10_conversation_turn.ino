// ============================================================
// ONE CONVERSATION TURN
// ============================================================

void conversationTurn() {

  RecordResult rec =
    recordUntilSilence();


  digitalWrite(
    LED_PIN,
    LOW
  );


  if (
    rec.timedOut
  ) {

    LOG_PRINTLN(
      "[System]: Idle timeout - going back to sleep."
    );


    appState =
      STATE_WAITING_FOR_WAKE;


    resetConversationState();


    return;
  }


  if (
    !rec.wav
  ) {

    LOG_PRINTLN(
      "[Error]: Recording failed."
    );


    return;
  }


  camera_fb_t* fb =
    nullptr;


  if (
    visionMode
  ) {

    fb =
      captureHighResFrame();
  }


  LOG_PRINTLN(
    "[System]: Transcribing..."
  );


  String question =
    transcribeWithGroq(
      rec.wav,
      rec.wavLen
    );


  free(
    rec.wav
  );


  if (
    question.length() ==
    0
  ) {

    LOG_PRINTLN(
      "[System]: Didn't catch that."
    );


    if (fb) {

      esp_camera_fb_return(
        fb
      );
    }


    return;
  }


  LOG_PRINT(
    "[You]: "
  );


  LOG_PRINTLN(
    question
  );


  String lowerQ =
    question;


  lowerQ.toLowerCase();


  // ----------------------------------------------------------
  // VISION ON
  // ----------------------------------------------------------

  if (
    lowerQ.indexOf(
      "start vision"
    ) >=
    0
  ) {

    visionMode =
      true;


    LOG_PRINTLN(
      "[System]: Vision mode ON."
    );


    if (fb) {

      esp_camera_fb_return(
        fb
      );
    }


    return;
  }


  // ----------------------------------------------------------
  // VISION OFF
  // ----------------------------------------------------------

  if (
    lowerQ.indexOf(
      "start voice"
    ) >=
      0
    ||
    lowerQ.indexOf(
      "stop vision"
    ) >=
      0
  ) {

    visionMode =
      false;


    LOG_PRINTLN(
      "[System]: Vision mode OFF."
    );


    if (fb) {

      esp_camera_fb_return(
        fb
      );
    }


    return;
  }


  // ----------------------------------------------------------
  // SLEEP
  // ----------------------------------------------------------

  if (
    lowerQ.indexOf(
      "sleep"
    ) >=
      0
    ||
    lowerQ.indexOf(
      "quit"
    ) >=
      0
    ||
    lowerQ.indexOf(
      "bye"
    ) >=
      0
    ||
    lowerQ.indexOf(
      "shut down"
    ) >=
      0
  ) {

    LOG_PRINTLN(
      "[System]: Going offline."
    );


    if (fb) {

      esp_camera_fb_return(
        fb
      );
    }


    appState =
      STATE_WAITING_FOR_WAKE;


    resetConversationState();


    return;
  }


  // ----------------------------------------------------------
  // ASK MODEL
  // ----------------------------------------------------------

  String answer;


  if (
    visionMode &&
    fb
  ) {

    LOG_PRINTLN(
      "[System]: Asking Groq (vision)..."
    );


    answer =
      sendVisionChat(
        question,
        fb->buf,
        fb->len
      );


    esp_camera_fb_return(
      fb
    );

  } else {

    if (fb) {

      esp_camera_fb_return(
        fb
      );
    }


    LOG_PRINTLN(
      "[System]: Asking Groq (text)..."
    );


    answer =
      sendTextChat(
        question
      );
  }


  LOG_PRINTLN(
    "[Assistant]:"
  );


  if (
    answer.length() >
    0
  ) {

    LOG_PRINTLN(
      answer
    );


    playAudioTTS(
      answer
    );

  } else {

    LOG_PRINTLN(
      "(no response)"
    );
  }
}
