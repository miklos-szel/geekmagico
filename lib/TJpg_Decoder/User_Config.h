#if defined (ESP32) || defined (ARDUINO_ARCH_ESP8266) || defined (ARDUINO_ARCH_RP2040)
  #define TJPGD_LOAD_FFS
#endif

// GeekMagicO: TJPGD_LOAD_SD_LIBRARY is deliberately NOT defined.
// Upstream defines it unconditionally, which makes TJpg_Decoder.h pull in <SD.h>.
// That drags the SD -> SDFS -> ESP8266SdFat stack (~18KB of .irom0.text) into the
// image via SD.cpp's global `SDClass SD;`, which --gc-sections cannot drop.
// This board has no SD slot. See lib/TJpg_Decoder/readme.md.
