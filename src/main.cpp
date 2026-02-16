#include <Arduino.h>
#include <Epub.h>
#include <FontCache.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <cstring>
#include <string>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/home/HomeActivity.h"
#include "activities/home/MyLibraryActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"
#include "embedded_fonts.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

HalDisplay display;
HalGPIO gpio;
MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
Activity* currentActivity;
FontCache fontCache;

// UI fonts (compiled-in bitmaps)
EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  currentActivity = activity;
  currentActivity->onEnter();
}

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    gpio.startDeepSleep();
  }
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  APP_STATE.lastSleepFromReader = currentActivity && currentActivity->isReaderActivity();
  APP_STATE.saveToFile();
  exitActivity();
  enterNewActivity(new SleepActivity(renderer, mappedInputManager));

  display.deepSleep();
  LOG_DBG("MAIN", "Power button press calibration value: %lu ms", t2 - t1);
  LOG_DBG("MAIN", "Entering deep sleep");

  gpio.startDeepSleep();
}

void onGoHome();
void onGoToMyLibraryWithPath(const std::string& path);
void onGoToRecentBooks();
bool ensureReaderFont();
void onGoToReader(const std::string& initialEpubPath) {
  exitActivity();
  enterNewActivity(
      new ReaderActivity(renderer, mappedInputManager, initialEpubPath, onGoHome, onGoToMyLibraryWithPath));
}

void onGoToFileTransfer() {
  exitActivity();
  enterNewActivity(new CrossPointWebServerActivity(renderer, mappedInputManager, onGoHome));
}

void onGoToSettings() {
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome,
      [](int oldFontId, int newFontId) {
        renderer.removeFont(oldFontId);
        if (!ensureReaderFont()) {
          LOG_ERR("MAIN", "Failed to prepare reader font %d", newFontId);
        }
      }));
}

void onGoToMyLibrary() {
  exitActivity();
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome, onGoToReader));
}

void onGoToRecentBooks() {
  exitActivity();
  enterNewActivity(new RecentBooksActivity(renderer, mappedInputManager, onGoHome, onGoToReader));
}

void onGoToMyLibraryWithPath(const std::string& path) {
  exitActivity();
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome, onGoToReader, path));
}

void onGoToBrowser() {
  exitActivity();
  enterNewActivity(new OpdsBookBrowserActivity(renderer, mappedInputManager, onGoHome));
}

void onGoHome() {
  exitActivity();
  enterNewActivity(new HomeActivity(renderer, mappedInputManager, onGoToReader, onGoToMyLibrary, onGoToRecentBooks,
                                    onGoToSettings, onGoToFileTransfer, onGoToBrowser));
}

/// Embedded Bookerly TTF pointers (regular, bold, italic, bolditalic)
static const uint8_t* kBookerlyTtfPtrs[4] = {
    bookerly_regular_ttf, bookerly_bold_ttf, bookerly_italic_ttf, bookerly_bolditalic_ttf};
static const size_t kBookerlyTtfSizes[4] = {
    bookerly_regular_ttf_size, bookerly_bold_ttf_size, bookerly_italic_ttf_size, bookerly_bolditalic_ttf_size};

/// Resolve TTF pointers for a font family. For Bookerly, returns embedded pointers.
/// For SD card fonts, reads from SD and attempts malloc; if that fails, stages to flash.
/// @param family       The font family to resolve.
/// @param outPtrs      Output: array of 4 pointers (may be malloc'd or mmap'd).
/// @param outSizes     Output: array of 4 sizes.
/// @param mallocBufs   Output: array of 4 pointers that were malloc'd (caller must free).
/// @return true if all 4 styles were resolved.
static bool resolveTtfPointers(CrossPointSettings::FONT_FAMILY family,
                               const uint8_t* outPtrs[4], size_t outSizes[4],
                               uint8_t* mallocBufs[4]) {
    for (int i = 0; i < 4; i++) {
        mallocBufs[i] = nullptr;
        outPtrs[i] = nullptr;
        outSizes[i] = 0;
    }

    if (family == CrossPointSettings::BOOKERLY) {
        // Check SD card first for override
        std::string sdPath = std::string("/fonts/Bookerly/") + CrossPointSettings::getTtfFilename(family, 0);
        if (Storage.exists(sdPath.c_str())) {
            // SD override for Bookerly -- fall through to SD loading below
        } else {
            // Use embedded
            for (int s = 0; s < 4; s++) {
                outPtrs[s] = kBookerlyTtfPtrs[s];
                outSizes[s] = kBookerlyTtfSizes[s];
            }
            return true;
        }
    }

    // Load from SD card
    const char* familyName = nullptr;
    switch (family) {
        case CrossPointSettings::BOOKERLY: familyName = "Bookerly"; break;
        case CrossPointSettings::NOTOSANS: familyName = "NotoSans"; break;
        case CrossPointSettings::OPENDYSLEXIC: familyName = "OpenDyslexic"; break;
        default: return false;
    }

    for (int s = 0; s < 4; s++) {
        std::string path = std::string("/fonts/") + familyName + "/" + CrossPointSettings::getTtfFilename(family, s);

        FsFile file;
        if (!Storage.openFileForRead("FONT", path.c_str(), file)) {
            LOG_ERR("FONT", "Cannot open %s", path.c_str());
            // Fall back to embedded Bookerly if available
            if (family == CrossPointSettings::BOOKERLY) {
                outPtrs[s] = kBookerlyTtfPtrs[s];
                outSizes[s] = kBookerlyTtfSizes[s];
            }
            continue;
        }

        size_t fileSize = file.size();
        if (fileSize == 0) {
            file.close();
            continue;
        }

        // Try malloc first
        uint8_t* buf = static_cast<uint8_t*>(malloc(fileSize));
        if (buf) {
            size_t bytesRead = file.read(buf, fileSize);
            file.close();
            if (bytesRead != fileSize) {
                free(buf);
                LOG_ERR("FONT", "Short read for %s", path.c_str());
                continue;
            }
            outPtrs[s] = buf;
            outSizes[s] = fileSize;
            mallocBufs[s] = buf;
        } else {
            // malloc failed -- stage to flash partition via FontCache
            LOG_DBG("FONT", "malloc(%u) failed for %s, staging to flash", (unsigned)fileSize, path.c_str());

            // Read in chunks to a temporary buffer, writing to flash staging
            // We'll need to read the whole file to stage it
            uint8_t* tmpBuf = static_cast<uint8_t*>(malloc(4096));
            if (!tmpBuf) {
                file.close();
                LOG_ERR("FONT", "Cannot allocate even 4KB for staging read");
                continue;
            }

            // Read entire file into staging via chunks
            std::vector<uint8_t> fileData;
            fileData.reserve(fileSize);
            size_t remaining = fileSize;
            while (remaining > 0) {
                size_t chunkSize = std::min(remaining, (size_t)4096);
                size_t bytesRead = file.read(tmpBuf, chunkSize);
                if (bytesRead == 0) break;
                fileData.insert(fileData.end(), tmpBuf, tmpBuf + bytesRead);
                remaining -= bytesRead;
            }
            free(tmpBuf);
            file.close();

            if (fileData.size() != fileSize) {
                LOG_ERR("FONT", "Short staged read for %s", path.c_str());
                continue;
            }

            const uint8_t* staged = fontCache.stageToFlash(fileData.data(), fileData.size());
            if (staged) {
                outPtrs[s] = staged;
                outSizes[s] = fileSize;
            } else {
                LOG_ERR("FONT", "Flash staging failed for %s", path.c_str());
            }
        }
    }

    // Ensure at least regular style is available
    return outPtrs[0] != nullptr && outSizes[0] > 0;
}

/// Ensure the current reader font is cached and registered with the renderer.
/// Shows a progress bar popup if rasterization is needed.
/// @return true if the font was successfully made available.
bool ensureReaderFont() {
    const int fontId = SETTINGS.getReaderFontId();
    const int pixelSize = SETTINGS.getReaderPixelSize();
    const auto family = static_cast<CrossPointSettings::FONT_FAMILY>(SETTINGS.fontFamily);

    if (!fontCache.hasFont(fontId)) {
        const uint8_t* ttfPtrs[4];
        size_t ttfSizes[4];
        uint8_t* mallocBufs[4];

        if (!resolveTtfPointers(family, ttfPtrs, ttfSizes, mallocBufs)) {
            // Font files not available -- show error and fall back to Bookerly
            LOG_ERR("FONT", "Cannot resolve TTF for family %d", (int)family);

            // Free any malloc'd buffers
            for (int i = 0; i < 4; i++) {
                if (mallocBufs[i]) { free(mallocBufs[i]); mallocBufs[i] = nullptr; }
            }

            // If we're already trying Bookerly, nothing more we can do
            if (family == CrossPointSettings::BOOKERLY) return false;

            // Fall back to embedded Bookerly
            SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
            SETTINGS.saveToFile();
            return ensureReaderFont();
        }

        auto popupRect = GUI.drawPopup(renderer, "Preparing fonts...");
        renderer.displayBuffer();

        bool success = fontCache.cacheFont(fontId, ttfPtrs, ttfSizes, pixelSize,
            [&popupRect](int progress) {
                GUI.fillPopupProgress(renderer, popupRect, progress);
                renderer.displayBuffer();
            });

        // Free any malloc'd buffers
        for (int i = 0; i < 4; i++) {
            if (mallocBufs[i]) { free(mallocBufs[i]); mallocBufs[i] = nullptr; }
        }

        if (!success) {
            LOG_ERR("FONT", "Rasterization failed for font %d", fontId);
            return false;
        }
    }

    // Remove old reader font if present, then insert cached version
    renderer.removeFont(fontId);
    renderer.insertFont(fontId, fontCache.buildEpdFontFamily(fontId));
    return true;
}

void setupDisplayAndFonts() {
  display.begin();
  renderer.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize FontCache (raw flash partition for rasterized font data)
  fontCache.init();

  // Ensure the currently selected reader font is cached and registered
  ensureReaderFont();

  // Register UI fonts (compiled-in bitmaps -- always available)
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  t1 = millis();

  gpio.begin();

  // Only start serial if USB connected
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    // Wait up to 3 seconds for Serial to be ready to catch early logs
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) {
      delay(10);
    }
  }

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts();
    exitActivity();
    enterNewActivity(new FullScreenMessageActivity(renderer, mappedInputManager, "SD card error", EpdFontFamily::BOLD));
    return;
  }

  SETTINGS.loadFromFile();
  KOREADER_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
      // For normal wakeups, verify power button press duration
      LOG_DBG("MAIN", "Verifying power button press duration");
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      gpio.startDeepSleep();
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  setupDisplayAndFonts();

  exitActivity();
  enterNewActivity(new BootActivity(renderer, mappedInputManager));

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
  // crashed (indicated by readerActivityLoadCount > 0)
  if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
      mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    onGoHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    onGoToReader(path);
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes", ESP.getFreeHeap(), ESP.getHeapSize(),
            ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        logSerial.printf("SCREENSHOT_START:%d\n", HalDisplay::BUFFER_SIZE);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, HalDisplay::BUFFER_SIZE);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();  // Reset inactivity timer
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  const unsigned long activityStartTime = millis();
  if (currentActivity) {
    currentActivity->loop();
  }
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();  // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // 3 seconds
    if (millis() - lastActivityTime >= IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
