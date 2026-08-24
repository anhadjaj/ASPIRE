// ============================================================
// GROQ TOOLS JSON
// ============================================================

const char* TOOLS_JSON = R"json(
[
  {
    "type": "function",
    "function": {
      "name": "get_unread_emails",
      "description": "Fetch user's latest unread emails.",
      "parameters": {
        "type": "object",
        "properties": {
          "max_results": {
            "type": "integer"
          }
        }
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "send_email",
      "description": "Send an email to a specific address.",
      "parameters": {
        "type": "object",
        "properties": {
          "to_address": {
            "type": "string"
          },
          "subject": {
            "type": "string"
          },
          "body": {
            "type": "string"
          }
        },
        "required": [
          "to_address",
          "subject",
          "body"
        ]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "get_upcoming_events",
      "description": "Fetch upcoming schedule.",
      "parameters": {
        "type": "object",
        "properties": {
          "max_results": {
            "type": "integer"
          }
        }
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "create_event",
      "description": "Create a new event. start_time MUST be ISO 8601.",
      "parameters": {
        "type": "object",
        "properties": {
          "summary": {
            "type": "string"
          },
          "start_time": {
            "type": "string"
          },
          "end_time": {
            "type": "string"
          }
        },
        "required": [
          "summary",
          "start_time"
        ]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "search_drive",
      "description": "Search Drive for a document.",
      "parameters": {
        "type": "object",
        "properties": {
          "query": {
            "type": "string"
          }
        },
        "required": [
          "query"
        ]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "get_news",
      "description": "Fetch top news headlines for a category such as technology, business, sports, or general.",
      "parameters": {
        "type": "object",
        "properties": {
          "category": {
            "type": "string"
          }
        }
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "take_photo",
      "description": "Takes a photo using the smart glasses camera and sends it to the user's phone via Telegram.",
      "parameters": {
        "type": "object",
        "properties": {}
      }
    }
  }
]
)json";


// ============================================================
// GROQ TEXT CHAT
// ============================================================

String sendTextChat(
  const String& question
) {

  JsonDocument doc;


  doc["model"] =
    GROQ_CHAT_MODEL;


  doc["temperature"] =
    0.1;


  doc["max_tokens"] =
    500;


  doc["reasoning_effort"] =
    "none";


  JsonDocument toolsDoc;


  DeserializationError toolsErr =
    deserializeJson(
      toolsDoc,
      TOOLS_JSON
    );


  if (!toolsErr) {

    doc["tools"] =
      toolsDoc.as<JsonArray>();
  }


  doc["tool_choice"] =
    "auto";


  JsonArray messages =
    doc["messages"]
    .to<JsonArray>();


  JsonObject sysMsg =
    messages.add<JsonObject>();


  sysMsg["role"] =
    "system";


  sysMsg["content"] =
    systemPrompt;


  for (
    int i = 0;
    i < historyCount;
    i++
  ) {

    JsonObject m =
      messages.add<JsonObject>();


    m["role"] =
      historyRole[i];


    m["content"] =
      historyContent[i];
  }


  JsonObject userMsg =
    messages.add<JsonObject>();


  userMsg["role"] =
    "user";


  userMsg["content"] =
    question;


  String body;


  serializeJson(
    doc,
    body
  );


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  if (
    !https.begin(
      client,
      "https://api.groq.com/openai/v1/chat/completions"
    )
  ) {

    return
      "I'm having trouble connecting right now.";
  }


  https.addHeader(
    "Content-Type",
    "application/json"
  );


  https.addHeader(
    "Authorization",
    String("Bearer ") +
    GROQ_API_KEY
  );


  int code =
    https.POST(
      body
    );


  String payload =
    https.getString();


  if (
    code !=
    HTTP_CODE_OK
  ) {

    LOG_PRINTF(
      "[Error]: Groq Phase 1 HTTP %d\n",
      code
    );


    LOG_PRINTLN(
      payload
    );


    https.end();


    return
      "I'm having trouble connecting to my brain right now.";
  }


  JsonDocument respDoc;


  DeserializationError respErr =
    deserializeJson(
      respDoc,
      payload
    );


  if (respErr) {

    LOG_PRINT(
      "[Error]: Groq response JSON parse: "
    );


    LOG_PRINTLN(
      respErr.c_str()
    );


    https.end();


    return "";
  }


  JsonObject responseMsg =
    respDoc["choices"][0]["message"];


  String assistantText =
    String(
      responseMsg["content"] |
      ""
    );


  // ==========================================================
  // TOOL CALLS
  // ==========================================================

  if (
    responseMsg["tool_calls"]
    .is<JsonArray>()
  ) {

    JsonArray toolCalls =
      responseMsg["tool_calls"]
      .as<JsonArray>();


    JsonDocument phase2Doc;


    phase2Doc["model"] =
      GROQ_CHAT_MODEL;


    phase2Doc["temperature"] =
      0.1;


    phase2Doc["max_tokens"] =
      500;


    phase2Doc["reasoning_effort"] =
      "none";


    JsonArray p2Messages =
      phase2Doc["messages"]
      .to<JsonArray>();


    JsonObject p2System =
      p2Messages.add<JsonObject>();


    p2System["role"] =
      "system";


    p2System["content"] =
      systemPrompt;


    for (
      int i = 0;
      i < historyCount;
      i++
    ) {

      JsonObject m =
        p2Messages.add<JsonObject>();


      m["role"] =
        historyRole[i];


      m["content"] =
        historyContent[i];
    }


    JsonObject p2User =
      p2Messages.add<JsonObject>();


    p2User["role"] =
      "user";


    p2User["content"] =
      question;


    JsonObject aiIntent =
      p2Messages.add<JsonObject>();


    aiIntent["role"] =
      "assistant";


    if (
      assistantText.length() >
      0
    ) {

      aiIntent["content"] =
        assistantText;
    }


    aiIntent["tool_calls"] =
      toolCalls;


    for (
      JsonObject toolCall :
      toolCalls
    ) {

      String toolId =
        String(
          toolCall["id"] |
          ""
        );


      String funcName =
        String(
          toolCall["function"]["name"] |
          ""
        );


      String argsStr =
        String(
          toolCall["function"]["arguments"] |
          "{}"
        );


      JsonDocument argsDoc;


      DeserializationError argsErr =
        deserializeJson(
          argsDoc,
          argsStr
        );


      if (argsErr) {

        argsDoc.clear();
      }


      String toolResult =
        "";


      if (
        funcName ==
        "get_unread_emails"
      ) {

        LOG_PRINTLN(
          "[System]: Executing tool -> get_unread_emails"
        );


        toolResult =
          getUnreadEmails(
            argsDoc["max_results"] |
            3
          );

      } else if (
        funcName ==
        "send_email"
      ) {

        LOG_PRINTLN(
          "[System]: Executing tool -> send_email"
        );


        toolResult =
          sendEmail(
            String(
              argsDoc["to_address"] |
              ""
            ),

            String(
              argsDoc["subject"] |
              "Sent from VIPER"
            ),

            String(
              argsDoc["body"] |
              ""
            )
          );

      } else if (
        funcName ==
        "get_upcoming_events"
      ) {

        LOG_PRINTLN(
          "[System]: Executing tool -> get_upcoming_events"
        );


        toolResult =
          getUpcomingCalendarEvents(
            argsDoc["max_results"] |
            5
          );

      } else if (
        funcName ==
        "create_event"
      ) {

        LOG_PRINTLN(
          "[System]: Executing tool -> create_event"
        );


        toolResult =
          createCalendarEvent(
            String(
              argsDoc["summary"] |
              "Untitled"
            ),

            String(
              argsDoc["start_time"] |
              ""
            ),

            String(
              argsDoc["end_time"] |
              ""
            )
          );

      } else if (
        funcName ==
        "search_drive"
      ) {

        LOG_PRINTLN(
          "[System]: Executing tool -> search_drive"
        );


        toolResult =
          searchAndReadDriveFile(
            String(
              argsDoc["query"] |
              ""
            )
          );

      } else if (
        funcName ==
        "get_news"
      ) {

        LOG_PRINTLN(
          "[System]: Executing tool -> get_news"
        );


        toolResult =
          getTopNews(
            String(
              argsDoc["category"] |
              "general"
            )
          );

      } else if (
        funcName ==
        "take_photo"
      ) {

        LOG_PRINTLN(
          "[System]: Executing tool -> take_photo"
        );


        toolResult =
          takePhotoAndSendToPhone();

      } else {

        toolResult =
          "Error: Tool not implemented.";
      }


      JsonObject toolData =
        p2Messages.add<JsonObject>();


      toolData["role"] =
        "tool";


      toolData["tool_call_id"] =
        toolId;


      toolData["name"] =
        funcName;


      toolData["content"] =
        toolResult;
    }


    String p2Body;


    serializeJson(
      phase2Doc,
      p2Body
    );


    // IMPORTANT:
    // Close first HTTP request before Phase 2.
    https.end();


    WiFiClientSecure client2;

    client2.setInsecure();


    HTTPClient https2;


    if (
      !https2.begin(
        client2,
        "https://api.groq.com/openai/v1/chat/completions"
      )
    ) {

      return
        "The requested action completed, but I couldn't generate the final response.";
    }


    https2.addHeader(
      "Content-Type",
      "application/json"
    );


    https2.addHeader(
      "Authorization",
      String("Bearer ") +
      GROQ_API_KEY
    );


    int p2Code =
      https2.POST(
        p2Body
      );


    if (
      p2Code ==
      HTTP_CODE_OK
    ) {

      String p2Payload =
        https2.getString();


      JsonDocument p2Resp;


      DeserializationError p2Err =
        deserializeJson(
          p2Resp,
          p2Payload
        );


      if (!p2Err) {

        assistantText =
          String(
            p2Resp["choices"][0]["message"]["content"] |
            ""
          );
      }

    } else {

      LOG_PRINTF(
        "[Error]: Groq Phase 2 HTTP %d\n",
        p2Code
      );


      LOG_PRINTLN(
        https2.getString()
      );
    }


    https2.end();

  } else {

    https.end();
  }


  if (
    assistantText.length() >
    0
  ) {

    addToHistory(
      "user",
      question
    );


    addToHistory(
      "assistant",
      assistantText
    );
  }


  return assistantText;
}


// ============================================================
// GROQ VISION
// ============================================================

String sendVisionChat(
  const String& question,
  uint8_t* jpgBuf,
  size_t jpgLen
) {

  if (
    !jpgBuf ||
    jpgLen ==
    0
  ) {

    LOG_PRINTLN(
      "[Error]: Invalid image buffer"
    );


    return "";
  }


  size_t b64Len =
    0;


  char* b64 =
    base64Encode(
      jpgBuf,
      jpgLen,
      &b64Len
    );


  if (!b64) {

    LOG_PRINTLN(
      "[Error]: base64 encode failed"
    );


    return "";
  }


  char* sysEsc =
    jsonEscape(
      systemPrompt
    );


  char* qEsc =
    jsonEscape(
      question
    );


  if (
    !sysEsc ||
    !qEsc
  ) {

    LOG_PRINTLN(
      "[Error]: Vision JSON escaping allocation failed"
    );


    free(b64);


    if (sysEsc) {
      free(sysEsc);
    }


    if (qEsc) {
      free(qEsc);
    }


    return "";
  }


  size_t bodyCap =
    strlen(sysEsc) +
    strlen(qEsc) +
    b64Len +
    1024;


  char* bodyBuf =
    (char*)ps_malloc(
      bodyCap
    );


  if (!bodyBuf) {

    LOG_PRINTLN(
      "[Error]: PSRAM alloc failed for vision body"
    );


    free(b64);

    free(sysEsc);

    free(qEsc);


    return "";
  }


  size_t pos =
    0;


  pos =
    appendStr(
      bodyBuf,
      pos,
      "{\"model\":\""
    );


  pos =
    appendStr(
      bodyBuf,
      pos,
      GROQ_CHAT_MODEL
    );


  pos =
    appendStr(
      bodyBuf,
      pos,
      "\",\"temperature\":0.1,"
      "\"max_tokens\":400,"
      "\"reasoning_effort\":\"none\","
      "\"messages\":["
      "{\"role\":\"system\",\"content\":\""
    );


  pos =
    appendStr(
      bodyBuf,
      pos,
      sysEsc
    );


  pos =
    appendStr(
      bodyBuf,
      pos,
      "\"},"
      "{\"role\":\"user\",\"content\":["
      "{\"type\":\"text\",\"text\":\""
    );


  pos =
    appendStr(
      bodyBuf,
      pos,
      qEsc
    );


  pos =
    appendStr(
      bodyBuf,
      pos,
      "\"},"
      "{\"type\":\"image_url\","
      "\"image_url\":{\"url\":\"data:image/jpeg;base64,"
    );


  pos =
    appendStr(
      bodyBuf,
      pos,
      b64
    );


  pos =
    appendStr(
      bodyBuf,
      pos,
      "\"}}]}]}"
    );


  bodyBuf[pos] =
    '\0';


  free(b64);

  free(sysEsc);

  free(qEsc);


  printMemoryDebug(
    "BEFORE_VISION_REQUEST"
  );


  WiFiClientSecure client;

  client.setInsecure();


  HTTPClient https;


  if (
    !https.begin(
      client,
      "https://api.groq.com/openai/v1/chat/completions"
    )
  ) {

    free(bodyBuf);

    return "";
  }


  https.addHeader(
    "Content-Type",
    "application/json"
  );


  https.addHeader(
    "Authorization",
    String("Bearer ") +
    GROQ_API_KEY
  );


  int code =
    https.POST(
      (uint8_t*)bodyBuf,
      pos
    );


  String responseText =
    "";


  if (
    code ==
    HTTP_CODE_OK
  ) {

    String payload =
      https.getString();


    JsonDocument respDoc;


    DeserializationError err =
      deserializeJson(
        respDoc,
        payload
      );


    if (!err) {

      responseText =
        String(
          respDoc["choices"][0]["message"]["content"] |
          ""
        );

    } else {

      LOG_PRINT(
        "[Error]: Vision JSON parse: "
      );


      LOG_PRINTLN(
        err.c_str()
      );
    }

  } else {

    LOG_PRINTF(
      "[Error]: Groq vision HTTP %d\n",
      code
    );


    LOG_PRINTLN(
      https.getString()
    );
  }


  https.end();


  free(bodyBuf);


  historyCount =
    0;


  if (
    responseText.length() >
    0
  ) {

    addToHistory(
      "user",
      question
    );


    addToHistory(
      "assistant",
      responseText
    );
  }


  return responseText;
}
