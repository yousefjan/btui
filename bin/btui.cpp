#include "btui_app.hpp"
#include "btui_model.hpp"
#include "getopt.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <io.h>
  #define ISATTY _isatty
  #define FILENO _fileno
#else
  #include <unistd.h>
  #define ISATTY isatty
  #define FILENO fileno
#endif

#define BTUI_DEFAULT_FORMAT "%S %C [%d] %n %m (%G:%L)"
#define BTUI_DEFAULT_DATE_FORMAT "%Y-%m-%d %H:%M:%S.%N"

namespace {

void showHelp()
{
  std::cout <<
    "btui -- interactive TUI viewer for binary logfiles\n"
    "\n"
    "Synopsis:\n"
    "  btui [-f format] [-d date-format] [-s] [-t] [filename...]\n"
    "\n"
    "Examples:\n"
    "  btui logfile.blog\n"
    "  btui -s logfile.blog\n"
    "  btui -t logfile.blog\n"
    "  btui file1.blog file2.blog\n"
    "\n"
    "Arguments:\n"
    "  filename       Path(s) to .blog file(s). If '-' or unspecified, read from stdin\n"
    "  format         Event format string, see bread -h for placeholders\n"
    "  date-format    Date format string, see bread -h for placeholders\n"
    "\n"
    "Allowed options:\n"
    "  -h             Show this help\n"
    "  -f             Set a custom event format string\n"
    "  -d             Set a custom date format string\n"
    "  -s             Pre-sort all events by clock value on load\n"
    "  -t             Enable live tail mode (poll file for new events)\n"
    "\n"
    "Keyboard shortcuts (in TUI):\n"
    "  j/k, arrows    Navigate up/down\n"
    "  g/G            Jump to first/last event\n"
    "  PgUp/PgDn      Page up/down\n"
    "  Enter/Space    Toggle detail panel\n"
    "  /              Filter by message\n"
    "  s              Cycle severity filter\n"
    "  c              Filter by category\n"
    "  w              Filter by writer\n"
    "  o              Toggle sort order\n"
    "  t              Toggle live tail\n"
    "  d              Toggle local/UTC timestamps\n"
    "  r              Reset all filters\n"
    "  h/?            Toggle help overlay\n"
    "  Escape         Close panel / clear filter\n"
    "  q              Quit\n"
    "\n"
    "Default event format:  \"" BTUI_DEFAULT_FORMAT "\"\n"
    "Default date format:   \"" BTUI_DEFAULT_DATE_FORMAT "\"\n";
}

} // namespace

int main(int argc, /*const*/ char* argv[])
{
  std::string format = BTUI_DEFAULT_FORMAT;
  std::string dateFormat = BTUI_DEFAULT_DATE_FORMAT;
  bool sorted = false;
  bool tail = false;

  int opt;
  while ((opt = getopt(argc, argv, "f:d:sth")) != -1) // NOLINT(concurrency-mt-unsafe)
  {
    switch (opt)
    {
    case 'f':
      format = optarg;
      break;
    case 'd':
      dateFormat = optarg;
      break;
    case 's':
      sorted = true;
      break;
    case 't':
      tail = true;
      break;
    case 'h':
      showHelp();
      return 0;
    default:
      showHelp();
      return 1;
    }
  }

  std::vector<std::string> inputPaths;
  for (int i = optind; i < argc; ++i)
  {
    inputPaths.push_back(argv[i]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  }

  if (inputPaths.empty())
  {
    inputPaths.push_back("-");
  }

  // Check if reading from stdin and it's a terminal
  if (inputPaths.size() == 1 && inputPaths[0] == "-")
  {
    if (ISATTY(FILENO(stdin))) // NOLINT(concurrency-mt-unsafe)
    {
      std::cerr << "[btui] stdin is a terminal. Pipe a .blog file or specify a filename.\n";
      return 2;
    }
  }

  btui::BtuiModel model(format, dateFormat);

  // Load all files
  // parseErrors is not currently displayed, but kept for potential future use
  for (const auto& path : inputPaths)
  {
    if (path == "-")
    {
      try
      {
        model.loadFromStream(std::cin, "stdin");
      }
      catch (const std::exception& ex)
      {
        std::cerr << "[btui] Warning: parse error in stdin: " << ex.what() << "\n";
        // continue with successfully parsed records
      }
    }
    else
    {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
        std::cerr << "[btui] Failed to open '" << path << "' for reading\n";
        return 2;
      }
      try
      {
        model.loadFromStream(file, path);
      }
      catch (const std::exception& ex)
      {
        std::cerr << "[btui] Warning: parse error in '" << path << "': " << ex.what() << "\n";
        // continue with successfully parsed records
      }
    }
  }

  if (sorted)
  {
    model.setSortOrder(btui::SortOrder::ascending);
    model.applySorting();
  }

  btui::BtuiApp app(model, inputPaths, tail);
  app.run();

  return 0;
}
