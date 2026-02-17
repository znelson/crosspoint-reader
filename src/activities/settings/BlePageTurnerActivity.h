#pragma once

#include <functional>
#include <string>
#include <vector>

#include "activities/ActivityWithSubactivity.h"
#include "util/ButtonNavigator.h"

class BlePageTurner;

class BlePageTurnerActivity final : public ActivityWithSubactivity {
 public:
  explicit BlePageTurnerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 BlePageTurner& blePageTurner, const std::function<void()>& goBack)
      : ActivityWithSubactivity("BlePageTurner", renderer, mappedInput),
        blePageTurner(blePageTurner),
        goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;

 private:
  enum class Screen { Main, Scanning };

  Screen screen = Screen::Main;
  BlePageTurner& blePageTurner;
  const std::function<void()> goBack;

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  struct MenuItem {
    std::string label;
    std::string value;
  };
  std::vector<MenuItem> menuItems;

  void buildMenuItems();
  void handleMainAction();
};
