#ifndef BINLOG_BIN_BTUI_APP_HPP
#define BINLOG_BIN_BTUI_APP_HPP

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace btui {

class BtuiModel;

/** UI state for the btui application. */
struct AppState
{
  std::size_t selectedIndex = 0;
  std::size_t scrollOffset = 0;
  bool showDetail = false;
  bool showHelp = false;
  bool tailMode = false;
  bool showUtcTime = false;

  // Filter input mode
  bool filterInputActive = false;
  int activeFilterField = 0; // 0=message, 1=category, 2=writer
  std::string filterInputBuffer;
};

/**
 * FTXUI-based TUI application for viewing binlog files.
 *
 * Provides a scrollable log table with severity color coding,
 * filtering, sorting, detail panel, and live tail mode.
 */
class BtuiApp
{
public:
  BtuiApp(BtuiModel& model, std::vector<std::string> filePaths, bool tailMode);
  ~BtuiApp();

  BtuiApp(const BtuiApp&) = delete;
  BtuiApp& operator=(const BtuiApp&) = delete;

  /** Run the TUI event loop. Blocks until the user quits. */
  void run();

private:
  void startTailThread();
  void stopTailThread();

  BtuiModel& _model;
  AppState _state;
  std::vector<std::string> _filePaths;
  std::atomic<bool> _tailRunning{false};
  std::thread _tailThread;
};

} // namespace btui

#endif // BINLOG_BIN_BTUI_APP_HPP
