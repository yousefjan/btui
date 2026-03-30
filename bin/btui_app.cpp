#include "btui_app.hpp"
#include "btui_model.hpp"

#include <binlog/EntryStream.hpp>
#include <binlog/Severity.hpp>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

namespace {

ftxui::Color severityColor(binlog::Severity sev)
{
  switch (sev)
  {
  case binlog::Severity::trace:    return ftxui::Color::GrayDark;
  case binlog::Severity::debug:    return ftxui::Color::Cyan;
  case binlog::Severity::info:     return ftxui::Color::White;
  case binlog::Severity::warning:  return ftxui::Color::Yellow;
  case binlog::Severity::error:    return ftxui::Color::Red;
  case binlog::Severity::critical: return ftxui::Color::RedLight;
  default:                         return ftxui::Color::White;
  }
}

ftxui::Decorator severityStyle(binlog::Severity sev)
{
  auto color = severityColor(sev);
  if (sev == binlog::Severity::critical)
  {
    return ftxui::color(color) | ftxui::bold;
  }
  if (sev == binlog::Severity::trace)
  {
    return ftxui::color(color) | ftxui::dim;
  }
  return ftxui::color(color);
}

std::string sortOrderStr(btui::SortOrder order)
{
  switch (order)
  {
  case btui::SortOrder::none:       return "OFF";
  case btui::SortOrder::ascending:  return "ASC";
  case btui::SortOrder::descending: return "DESC";
  }
  return "OFF";
}

std::string severityFilterStr(binlog::Severity sev)
{
  if (sev == binlog::Severity::trace) { return "ALL"; }
  auto s = binlog::severityToString(sev);
  return std::string(s.data(), s.size()) + "+";
}

// Truncate or pad a string to exactly `width` characters
std::string fitWidth(const std::string& s, std::size_t width)
{
  if (s.size() >= width) { return s.substr(0, width); }
  return s + std::string(width - s.size(), ' ');
}

constexpr std::size_t kPageSize = 20;

} // namespace

namespace btui {

BtuiApp::BtuiApp(BtuiModel& model, std::vector<std::string> filePaths, bool tailMode)
  :_model(model),
   _filePaths(std::move(filePaths)),
   _tailRunning(false)
{
  _state.tailMode = tailMode;
}

BtuiApp::~BtuiApp()
{
  stopTailThread();
}

void BtuiApp::run()
{
  auto screen = ftxui::ScreenInteractive::Fullscreen();

  auto component = ftxui::Renderer([&] {
    // Terminal size for layout calculations
    const int termHeight = ftxui::Terminal::Size().dimy;
    const int headerHeight = 1;
    const int filterHeight = 1;
    const int statusHeight = 1;
    const int detailHeight = _state.showDetail ? 14 : 0;
    const int helpHeight = _state.showHelp ? 18 : 0;
    int tableHeight = termHeight - headerHeight - filterHeight - statusHeight - detailHeight - helpHeight - 2;
    if (tableHeight < 1) { tableHeight = 1; }
    const std::size_t visibleRows = static_cast<std::size_t>(tableHeight);

    const std::size_t count = _model.recordCount();

    // Clamp selection and scroll
    if (count > 0 && _state.selectedIndex >= count)
    {
      _state.selectedIndex = count - 1;
    }
    if (_state.selectedIndex < _state.scrollOffset)
    {
      _state.scrollOffset = _state.selectedIndex;
    }
    if (_state.selectedIndex >= _state.scrollOffset + visibleRows)
    {
      _state.scrollOffset = _state.selectedIndex - visibleRows + 1;
    }

    // --- Header ---
    std::string fileInfo;
    if (_filePaths.size() == 1)
    {
      fileInfo = _filePaths[0];
    }
    else
    {
      fileInfo = std::to_string(_filePaths.size()) + " files";
    }

    auto header = ftxui::hbox({
      ftxui::text(" btui") | ftxui::bold,
      ftxui::text(" - " + fileInfo),
      ftxui::filler(),
      ftxui::text(_state.tailMode ? " [TAIL:ON] " : " [TAIL:OFF] ") |
        (_state.tailMode ? ftxui::color(ftxui::Color::Green) : ftxui::nothing),
      ftxui::text(" [SORT:" + sortOrderStr(_model.sortOrder()) + "] "),
      ftxui::text(" [?]help ") | ftxui::dim,
    }) | ftxui::bgcolor(ftxui::Color::Blue) | ftxui::color(ftxui::Color::White);

    // --- Log Table ---
    ftxui::Elements tableRows;

    // Column header
    bool multiFile = _filePaths.size() > 1;
    {
      ftxui::Elements headerCols;
      headerCols.push_back(ftxui::text(fitWidth("Sev", 4)) | ftxui::bold);
      headerCols.push_back(ftxui::text(" "));
      headerCols.push_back(ftxui::text(fitWidth("Category", 10)) | ftxui::bold);
      headerCols.push_back(ftxui::text(" "));
      headerCols.push_back(ftxui::text(fitWidth("Time", 19)) | ftxui::bold);
      headerCols.push_back(ftxui::text(" "));
      headerCols.push_back(ftxui::text(fitWidth("Writer", 10)) | ftxui::bold);
      headerCols.push_back(ftxui::text(" "));
      headerCols.push_back(ftxui::text(fitWidth("File:Line", 20)) | ftxui::bold);
      headerCols.push_back(ftxui::text(" "));
      if (multiFile)
      {
        headerCols.push_back(ftxui::text(fitWidth("Source", 12)) | ftxui::bold);
        headerCols.push_back(ftxui::text(" "));
      }
      headerCols.push_back(ftxui::text("Message") | ftxui::bold);
      tableRows.push_back(
        ftxui::hbox(std::move(headerCols)) | ftxui::bgcolor(ftxui::Color::GrayDark)
      );
    }

    // Data rows
    if (count == 0)
    {
      tableRows.push_back(
        ftxui::text("  No events found") | ftxui::dim | ftxui::center
      );
    }
    else
    {
      for (std::size_t vi = 0; vi < visibleRows && _state.scrollOffset + vi < count; ++vi)
      {
        std::size_t idx = _state.scrollOffset + vi;
        const LogRecord& rec = _model.record(idx);
        bool isSelected = (idx == _state.selectedIndex);

        std::string fileLine = rec.file + ":" + std::to_string(rec.line);
        const std::string& timestamp = _state.showUtcTime ? rec.timestampUtc : rec.timestampLocal;

        ftxui::Elements cols;
        cols.push_back(ftxui::text(fitWidth(rec.severityStr, 4)));
        cols.push_back(ftxui::text(" "));
        cols.push_back(ftxui::text(fitWidth(rec.category, 10)));
        cols.push_back(ftxui::text(" "));
        cols.push_back(ftxui::text(fitWidth(timestamp, 19)));
        cols.push_back(ftxui::text(" "));
        cols.push_back(ftxui::text(fitWidth(rec.writerName, 10)));
        cols.push_back(ftxui::text(" "));
        cols.push_back(ftxui::text(fitWidth(fileLine, 20)));
        cols.push_back(ftxui::text(" "));
        if (multiFile)
        {
          cols.push_back(ftxui::text(fitWidth(rec.sourceFilename, 12)));
          cols.push_back(ftxui::text(" "));
        }
        cols.push_back(ftxui::text(rec.message));

        auto rowElement = ftxui::hbox(std::move(cols));

        // Apply severity color
        rowElement = rowElement | severityStyle(rec.severity);

        // Highlight selected row
        if (isSelected)
        {
          rowElement = rowElement | ftxui::inverted;
        }

        tableRows.push_back(std::move(rowElement));
      }
    }

    auto table = ftxui::vbox(std::move(tableRows)) | ftxui::flex;

    // --- Filter Bar ---
    auto filterBar = ftxui::hbox({
      ftxui::text(" Filter: ") | ftxui::bold,
      ftxui::text("[sev:" + severityFilterStr(_model.filterState().minSeverity) + "] "),
      ftxui::text("[cat:" + (_model.filterState().categoryFilter.empty() ? "*" : _model.filterState().categoryFilter) + "] "),
      ftxui::text("[writer:" + (_model.filterState().writerFilter.empty() ? "*" : _model.filterState().writerFilter) + "] "),
      ftxui::text("[msg:" + (_model.filterState().messageFilter.empty() ? "*" : _model.filterState().messageFilter) + "] "),
      _state.filterInputActive
        ? ftxui::hbox({
            ftxui::text(" > ") | ftxui::bold | ftxui::color(ftxui::Color::Yellow),
            ftxui::text(_state.filterInputBuffer + "_") | ftxui::color(ftxui::Color::Yellow),
          })
        : ftxui::text(""),
    });

    // --- Status Bar ---
    std::string posStr;
    if (count > 0)
    {
      posStr = "event " + std::to_string(_state.selectedIndex + 1) + "/" + std::to_string(count);
    }
    else
    {
      posStr = "no events";
    }

    auto statusBar = ftxui::hbox({
      ftxui::text(" " + std::to_string(_model.totalRecordCount()) + " events") | ftxui::dim,
      ftxui::text(" | ") | ftxui::dim,
      ftxui::text(std::to_string(count) + " shown") | ftxui::dim,
      ftxui::text(" | ") | ftxui::dim,
      ftxui::text(posStr) | ftxui::dim,
      ftxui::filler(),
      ftxui::text("[q]quit [h]help ") | ftxui::dim,
    }) | ftxui::bgcolor(ftxui::Color::GrayDark);

    // --- Detail Panel ---
    ftxui::Element detailPanel = ftxui::text("");
    if (_state.showDetail && count > 0)
    {
      const LogRecord& rec = _model.record(_state.selectedIndex);
      detailPanel = ftxui::vbox({
        ftxui::text("Event Detail") | ftxui::bold,
        ftxui::separator(),
        ftxui::hbox({ftxui::text("Severity:  ") | ftxui::bold, ftxui::text(rec.severityStr) | severityStyle(rec.severity)}),
        ftxui::hbox({ftxui::text("Category:  ") | ftxui::bold, ftxui::text(rec.category)}),
        ftxui::hbox({ftxui::text("Writer:    ") | ftxui::bold, ftxui::text(rec.writerName + " (id:" + std::to_string(rec.writerId) + ")")}),
        ftxui::hbox({ftxui::text("File:      ") | ftxui::bold, ftxui::text(rec.fileFull)}),
        ftxui::hbox({ftxui::text("Line:      ") | ftxui::bold, ftxui::text(std::to_string(rec.line))}),
        ftxui::hbox({ftxui::text("Function:  ") | ftxui::bold, ftxui::text(rec.function)}),
        ftxui::hbox({ftxui::text("Timestamp: ") | ftxui::bold, ftxui::text(rec.timestampLocal)}),
        ftxui::hbox({ftxui::text("UTC:       ") | ftxui::bold, ftxui::text(rec.timestampUtc)}),
        ftxui::hbox({ftxui::text("Raw clock: ") | ftxui::bold, ftxui::text(std::to_string(rec.clockValue))}),
        ftxui::hbox({ftxui::text("Format:    ") | ftxui::bold, ftxui::text(rec.formatString)}),
        ftxui::hbox({ftxui::text("Arg tags:  ") | ftxui::bold, ftxui::text(rec.argumentTags)}),
        ftxui::hbox({ftxui::text("Message:   ") | ftxui::bold, ftxui::text(rec.message)}),
      }) | ftxui::border;
    }

    // --- Help Overlay ---
    ftxui::Element helpPanel = ftxui::text("");
    if (_state.showHelp)
    {
      helpPanel = ftxui::vbox({
        ftxui::text("Keyboard Shortcuts") | ftxui::bold,
        ftxui::separator(),
        ftxui::text("j/Down      Move down"),
        ftxui::text("k/Up        Move up"),
        ftxui::text("g/Home      Jump to first event"),
        ftxui::text("G/End       Jump to last event"),
        ftxui::text("PgUp/Ctrl-B Page up"),
        ftxui::text("PgDn/Ctrl-F Page down"),
        ftxui::text("Enter/Space Open/close detail panel"),
        ftxui::text("/           Filter by message"),
        ftxui::text("s           Cycle severity filter"),
        ftxui::text("c           Filter by category"),
        ftxui::text("w           Filter by writer"),
        ftxui::text("o           Toggle sort order"),
        ftxui::text("t           Toggle live tail mode"),
        ftxui::text("d           Toggle local/UTC time"),
        ftxui::text("r           Reset all filters"),
        ftxui::text("Escape      Clear filter / close panel"),
        ftxui::text("h/?         Toggle this help"),
        ftxui::text("q/Ctrl-C    Quit"),
      }) | ftxui::border;
    }

    // --- Compose Layout ---
    ftxui::Elements layout;
    layout.push_back(header);
    layout.push_back(table);
    layout.push_back(ftxui::separator());
    layout.push_back(filterBar);
    layout.push_back(statusBar);
    if (_state.showDetail)
    {
      layout.push_back(detailPanel);
    }
    if (_state.showHelp)
    {
      layout.push_back(helpPanel);
    }

    return ftxui::vbox(std::move(layout));
  });

  // Key event handler
  component = ftxui::CatchEvent(component, [&](ftxui::Event event) -> bool {
    const std::size_t count = _model.recordCount();

    // Filter input mode: capture text
    if (_state.filterInputActive)
    {
      if (event == ftxui::Event::Escape)
      {
        _state.filterInputActive = false;
        _state.filterInputBuffer.clear();
        return true;
      }
      if (event == ftxui::Event::Return)
      {
        // Apply filter
        switch (_state.activeFilterField)
        {
        case 0: _model.filterState().messageFilter = _state.filterInputBuffer; break;
        case 1: _model.filterState().categoryFilter = _state.filterInputBuffer; break;
        case 2: _model.filterState().writerFilter = _state.filterInputBuffer; break;
        }
        _model.applyFilters();
        _state.filterInputActive = false;
        _state.filterInputBuffer.clear();
        _state.selectedIndex = 0;
        _state.scrollOffset = 0;
        return true;
      }
      if (event == ftxui::Event::Backspace)
      {
        if (!_state.filterInputBuffer.empty())
        {
          _state.filterInputBuffer.pop_back();
        }
        return true;
      }
      if (event.is_character())
      {
        _state.filterInputBuffer += event.character();
        return true;
      }
      return false;
    }

    // Normal mode keybindings

    // Quit
    if (event == ftxui::Event::Character('q'))
    {
      screen.Exit();
      return true;
    }

    // Navigation
    if (event == ftxui::Event::Character('j') || event == ftxui::Event::ArrowDown)
    {
      if (count > 0 && _state.selectedIndex < count - 1)
      {
        _state.selectedIndex++;
      }
      return true;
    }
    if (event == ftxui::Event::Character('k') || event == ftxui::Event::ArrowUp)
    {
      if (_state.selectedIndex > 0)
      {
        _state.selectedIndex--;
      }
      return true;
    }
    if (event == ftxui::Event::Character('g') || event == ftxui::Event::Home)
    {
      _state.selectedIndex = 0;
      _state.scrollOffset = 0;
      return true;
    }
    if (event == ftxui::Event::Character('G') || event == ftxui::Event::End)
    {
      if (count > 0)
      {
        _state.selectedIndex = count - 1;
      }
      return true;
    }
    if (event == ftxui::Event::PageUp)
    {
      if (_state.selectedIndex > kPageSize)
      {
        _state.selectedIndex -= kPageSize;
      }
      else
      {
        _state.selectedIndex = 0;
      }
      return true;
    }
    if (event == ftxui::Event::PageDown)
    {
      _state.selectedIndex += kPageSize;
      if (count > 0 && _state.selectedIndex >= count)
      {
        _state.selectedIndex = count - 1;
      }
      return true;
    }
    // Ctrl-B (page up) and Ctrl-F (page down)
    if (event == ftxui::Event::Character('\x02')) // Ctrl-B
    {
      if (_state.selectedIndex > kPageSize)
      {
        _state.selectedIndex -= kPageSize;
      }
      else
      {
        _state.selectedIndex = 0;
      }
      return true;
    }
    if (event == ftxui::Event::Character('\x06')) // Ctrl-F
    {
      _state.selectedIndex += kPageSize;
      if (count > 0 && _state.selectedIndex >= count)
      {
        _state.selectedIndex = count - 1;
      }
      return true;
    }

    // Detail panel
    if (event == ftxui::Event::Return || event == ftxui::Event::Character(' '))
    {
      _state.showDetail = !_state.showDetail;
      return true;
    }

    // Help
    if (event == ftxui::Event::Character('h') || event == ftxui::Event::Character('?'))
    {
      _state.showHelp = !_state.showHelp;
      return true;
    }

    // Filters
    if (event == ftxui::Event::Character('/'))
    {
      _state.filterInputActive = true;
      _state.activeFilterField = 0; // message
      _state.filterInputBuffer = _model.filterState().messageFilter;
      return true;
    }
    if (event == ftxui::Event::Character('c'))
    {
      _state.filterInputActive = true;
      _state.activeFilterField = 1; // category
      _state.filterInputBuffer = _model.filterState().categoryFilter;
      return true;
    }
    if (event == ftxui::Event::Character('w'))
    {
      _state.filterInputActive = true;
      _state.activeFilterField = 2; // writer
      _state.filterInputBuffer = _model.filterState().writerFilter;
      return true;
    }
    if (event == ftxui::Event::Character('s'))
    {
      _model.cycleSeverityFilter();
      _state.selectedIndex = 0;
      _state.scrollOffset = 0;
      return true;
    }
    if (event == ftxui::Event::Character('r'))
    {
      _model.resetFilters();
      _state.selectedIndex = 0;
      _state.scrollOffset = 0;
      return true;
    }

    // Sort
    if (event == ftxui::Event::Character('o'))
    {
      _model.cycleSortOrder();
      return true;
    }

    // Tail
    if (event == ftxui::Event::Character('t'))
    {
      _state.tailMode = !_state.tailMode;
      if (_state.tailMode)
      {
        startTailThread();
      }
      else
      {
        stopTailThread();
      }
      return true;
    }

    // Time display toggle
    if (event == ftxui::Event::Character('d'))
    {
      _state.showUtcTime = !_state.showUtcTime;
      return true;
    }

    // Escape: close panels
    if (event == ftxui::Event::Escape)
    {
      if (_state.showDetail) { _state.showDetail = false; return true; }
      if (_state.showHelp) { _state.showHelp = false; return true; }
      return true;
    }

    return false;
  });

  if (_state.tailMode)
  {
    startTailThread();
  }

  screen.Loop(component);

  stopTailThread();
}

void BtuiApp::startTailThread()
{
  if (_tailRunning.load()) { return; }
  if (_filePaths.empty() || _filePaths[0] == "-") { return; }

  _tailRunning.store(true);
  const std::string path = _filePaths[0];

  _tailThread = std::thread([this, path] {
    std::ifstream file(path, std::ios::binary);
    if (!file) { return; }

    // Seek to end (past already-loaded data)
    file.seekg(0, std::ios::end);

    while (_tailRunning.load())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // Clear EOF flag so we can try reading more
      file.clear();

      // Check current position vs end
      auto pos = file.tellg();
      file.seekg(0, std::ios::end);
      auto end = file.tellg();

      if (end > pos)
      {
        // New data available
        file.seekg(pos);

        std::lock_guard<std::mutex> lock(_model.mutex());
        std::size_t oldCount = _model.totalRecordCount();
        _model.loadFromStream(file, path);

        if (_model.totalRecordCount() > oldCount)
        {
          // Auto-scroll to bottom if we were at the last event
          std::size_t filteredCount = _model.recordCount();
          if (filteredCount > 0 &&
              _state.selectedIndex >= filteredCount - (_model.totalRecordCount() - oldCount) - 1)
          {
            _state.selectedIndex = filteredCount - 1;
          }
        }
      }
    }
  });
}

void BtuiApp::stopTailThread()
{
  _tailRunning.store(false);
  if (_tailThread.joinable())
  {
    _tailThread.join();
  }
}

} // namespace btui
