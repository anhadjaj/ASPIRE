// ============================================================
// CAMERA
// ============================================================

bool initCamera() {

  camera_config_t config =
    {};


  config.ledc_channel =
    LEDC_CHANNEL_0;


  config.ledc_timer =
    LEDC_TIMER_0;


  config.pin_d0 =
    Y2_GPIO_NUM;

  config.pin_d1 =
    Y3_GPIO_NUM;

  config.pin_d2 =
    Y4_GPIO_NUM;

  config.pin_d3 =
    Y5_GPIO_NUM;

  config.pin_d4 =
    Y6_GPIO_NUM;

  config.pin_d5 =
    Y7_GPIO_NUM;

  config.pin_d6 =
    Y8_GPIO_NUM;

  config.pin_d7 =
    Y9_GPIO_NUM;


  config.pin_xclk =
    XCLK_GPIO_NUM;

  config.pin_pclk =
    PCLK_GPIO_NUM;

  config.pin_vsync =
    VSYNC_GPIO_NUM;

  config.pin_href =
    HREF_GPIO_NUM;


  config.pin_sccb_sda =
    SIOD_GPIO_NUM;

  config.pin_sccb_scl =
    SIOC_GPIO_NUM;


  config.pin_pwdn =
    PWDN_GPIO_NUM;

  config.pin_reset =
    RESET_GPIO_NUM;


  config.xclk_freq_hz =
    20000000;


  config.pixel_format =
    PIXFORMAT_JPEG;


  if (
    psramFound()
  ) {

    // VGA = 640 x 480
    config.frame_size =
      FRAMESIZE_VGA;


    config.jpeg_quality =
      14;


    // One framebuffer only.
    config.fb_count =
      1;


    config.fb_location =
      CAMERA_FB_IN_PSRAM;


    // We take individual photos.
    config.grab_mode =
      CAMERA_GRAB_WHEN_EMPTY;

  } else {

    config.frame_size =
      FRAMESIZE_QVGA;


    config.jpeg_quality =
      16;


    config.fb_count =
      1;


    config.fb_location =
      CAMERA_FB_IN_DRAM;


    config.grab_mode =
      CAMERA_GRAB_WHEN_EMPTY;
  }


  LOG_PRINTLN(
    "[CAM] Initializing camera..."
  );


  printMemoryDebug(
    "BEFORE_CAMERA_INIT"
  );


  esp_err_t err =
    esp_camera_init(
      &config
    );


  if (
    err !=
    ESP_OK
  ) {

    LOG_PRINTF(
      "[Error]: Camera init failed: 0x%x\n",
      err
    );


    return false;
  }


  LOG_PRINTLN(
    "[CAM] Camera initialized successfully."
  );


  printMemoryDebug(
    "AFTER_CAMERA_INIT"
  );


  return true;
}


camera_fb_t* captureHighResFrame() {

  LOG_PRINTLN(
    "[System]: Capturing image..."
  );


  camera_fb_t* fb =
    esp_camera_fb_get();


  if (!fb) {

    LOG_PRINTLN(
      "[Error]: Camera capture failed"
    );


    return nullptr;
  }


  LOG_PRINTF(
    "[System]: Captured %dx%d, %u bytes\n",
    fb->width,
    fb->height,
    (unsigned)fb->len
  );


  return fb;
}
