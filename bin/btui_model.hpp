#ifndef BINLOG_BIN_BTUI_MODEL_HPP
#define BINLOG_BIN_BTUI_MODEL_HPP

#include <binlog/EntryStream.hpp>
#include <binlog/Severity.hpp>

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>

namespace btui {

/** A single log event with all fields pre-rendered for display. */
struct LogRecord
{
  binlog::Severity severity = binlog::Severity::info;
  std::string severityStr;      /**< "INFO", "WARN", etc. */
  std::string category;
  std::string writerName;
  std::uint64_t writerId = 0;
  std::string file;             /**< basename only */
  std::string fileFull;         /**< full path */
  std::uint64_t line = 0;
  std::string function;
  std::uint64_t clockValue = 0;
  std::string timestampLocal;   /**< pre-rendered local timestamp */
  std::string timestampUtc;     /**< pre-rendered UTC timestamp */
  std::string message;          /**< format string with arguments substituted */
  std::string formatString;
  std::string argumentTags;
  std::string sourceFilename;   /**< originating .blog file (for multi-file) */
};

/** Current filter criteria. */
struct FilterState
{
  binlog::Severity minSeverity = binlog::Severity::trace;
  std::string categoryFilter;   /**< substring match */
  std::string writerFilter;     /**< substring match */
  std::string messageFilter;    /**< substring match */
};

enum class SortOrder { none, ascending, descending };

/**
 * Data model for the btui TUI log viewer.
 *
 * Holds all parsed log records, filter state, and filtered/sorted
 * index. Independent of any UI framework.
 */
class BtuiModel
{
public:
  BtuiModel(std::string eventFormat, std::string dateFormat);

  /**
   * Parse events from an istream and append to the record list.
   *
   * @param input a binary stream containing binlog entries
   * @param sourceFilename tag records with this filename (for multi-file)
   */
  void loadFromStream(std::istream& input, const std::string& sourceFilename = {});

  /**
   * Parse events from an EntryStream and append to the record list.
   * Useful for unit testing with TestStream.
   *
   * @param input an EntryStream containing binlog entries
   * @param sourceFilename tag records with this filename
   */
  void loadFromEntryStream(binlog::EntryStream& input, const std::string& sourceFilename = {});

  /** Recompute filtered indices based on current filter state. */
  void applyFilters();

  /** Sort filtered indices by clockValue according to current sort order. */
  void applySorting();

  /** Number of records after filtering. */
  std::size_t recordCount() const;

  /** Total number of records before filtering. */
  std::size_t totalRecordCount() const;

  /** Access a record by filtered index. */
  const LogRecord& record(std::size_t filteredIdx) const;

  /** Access all records (unfiltered). */
  const std::vector<LogRecord>& allRecords() const;

  /** Access the filtered indices. */
  const std::vector<std::size_t>& filteredIndices() const;

  /** Mutable access to filter state. */
  FilterState& filterState();

  /** Const access to filter state. */
  const FilterState& filterState() const;

  /** Set the sort order. Does not re-sort; call applySorting(). */
  void setSortOrder(SortOrder order);

  /** Get current sort order. */
  SortOrder sortOrder() const;

  /** Toggle sort order: none -> ascending -> descending -> none. */
  void cycleSortOrder();

  /**
   * Cycle the severity floor filter.
   * trace -> debug -> info -> warning -> error -> critical -> trace
   * Then calls applyFilters().
   */
  void cycleSeverityFilter();

  /** Reset all filters to defaults. Calls applyFilters(). */
  void resetFilters();

  /** Mutex for thread-safe access (tail mode). */
  std::mutex& mutex();

private:
  std::string _eventFormat;
  std::string _dateFormat;
  std::vector<LogRecord> _allRecords;
  std::vector<std::size_t> _filteredIndices;
  FilterState _filterState;
  SortOrder _sortOrder = SortOrder::none;
  mutable std::mutex _mutex;
};

} // namespace btui

#endif // BINLOG_BIN_BTUI_MODEL_HPP
