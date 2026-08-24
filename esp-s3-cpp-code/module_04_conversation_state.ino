// ============================================================
// CONVERSATION HISTORY
// ============================================================

void addToHistory(
  const String& role,
  const String& content
) {

  if (
    historyCount <
    MAX_HISTORY
  ) {

    historyRole[
      historyCount
    ] =
      role;


    historyContent[
      historyCount
    ] =
      content;


    historyCount++;

  } else {

    for (
      int i = 1;
      i < MAX_HISTORY;
      i++
    ) {

      historyRole[
        i - 1
      ] =
        historyRole[i];


      historyContent[
        i - 1
      ] =
        historyContent[i];
    }


    historyRole[
      MAX_HISTORY - 1
    ] =
      role;


    historyContent[
      MAX_HISTORY - 1
    ] =
      content;
  }
}


// ============================================================
// RESET CONVERSATION
// ============================================================

void resetConversationState() {

  struct tm timeinfo;


  String stamp =
    "unavailable (no time sync yet)";


  if (
    getLocalTime(
      &timeinfo
    )
  ) {

    char buf[64];


    strftime(
      buf,
      sizeof(buf),
      "%A, %B %d, %Y %I:%M %p",
      &timeinfo
    );


    stamp =
      String(buf) +
      " (" +
      TZ_STRING +
      ")";
  }


  systemPrompt =
    String(
      SYSTEM_PROMPT_BASE
    ) +
    "\nCRITICAL DATA: Today is " +
    stamp +
    ".";


  historyCount =
    0;


  visionMode =
    false;
}
