#include "btui_model.hpp"

#include <binlog/Entries.hpp>
#include <binlog/EntryStream.hpp>
#include <binlog/EventStream.hpp>
#include <binlog/PrettyPrinter.hpp>
#include <binlog/Severity.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

// Extract basename from a file path (same logic as PrettyPrinter printFilename)
std::string baseName(const std::string& path)
{
  std::size_t i = path.size();
  while (i != 0)
  {
    if (path[i-1] == '/' || path[i-1] == '\\') { break; }
    --i;
  }
  return path.substr(i);
}

bool containsSubstringCaseInsensitive(const std::string& haystack, const std::string& needle)
{
  if (needle.empty()) { return true; }
  if (haystack.size() < needle.size()) { return false; }

  auto toLower = [](char c) -> char {
    if (c >= 'A' && c <= 'Z') { return static_cast<char>(c + ('a' - 'A')); }
    return c;
  };

  for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i)
  {
    bool match = true;
    for (std::size_t j = 0; j < needle.size(); ++j)
    {
      if (toLower(haystack[i + j]) != toLower(needle[j]))
      {
        match = false;
        break;
      }
    }
    if (match) { return true; }
  }
  return false;
}

} // namespace

namespace btui {

BtuiModel::BtuiModel(std::string eventFormat, std::string dateFormat)
  :_eventFormat(std::move(eventFormat)),
   _dateFormat(std::move(dateFormat))
{}

void BtuiModel::loadFromStream(std::istream& input, const std::string& sourceFilename)
{
  binlog::IstreamEntryStream entryStream(input);
  loadFromEntryStream(entryStream, sourceFilename);
}

void BtuiModel::loadFromEntryStream(binlog::EntryStream& input, const std::string& sourceFilename)
{
  binlog::EventStream eventStream;

  // PrettyPrinters for extracting individual fields
  binlog::PrettyPrinter ppTimestampLocal("%d", _dateFormat);
  binlog::PrettyPrinter ppTimestampUtc("%u", _dateFormat);
  binlog::PrettyPrinter ppMessage("%m", _dateFormat);

  while (const binlog::Event* event = eventStream.nextEvent(input))
  {
    LogRecord rec;

    // Direct fields from EventSource
    rec.severity = event->source->severity;
    const auto sevStr = binlog::severityToString(event->source->severity);
    rec.severityStr.assign(sevStr.data(), sevStr.size());
    rec.category = event->source->category;
    rec.fileFull = event->source->file;
    rec.file = baseName(event->source->file);
    rec.line = event->source->line;
    rec.function = event->source->function;
    rec.formatString = event->source->formatString;
    rec.argumentTags = event->source->argumentTags;

    // Fields from WriterProp
    rec.writerName = eventStream.writerProp().name;
    rec.writerId = eventStream.writerProp().id;

    // Clock value for sorting
    rec.clockValue = event->clockValue;

    // Pre-render timestamps and message via PrettyPrinter
    {
      std::ostringstream oss;
      ppTimestampLocal.printEvent(oss, *event, eventStream.writerProp(), eventStream.clockSync());
      rec.timestampLocal = oss.str();
    }
    {
      std::ostringstream oss;
      ppTimestampUtc.printEvent(oss, *event, eventStream.writerProp(), eventStream.clockSync());
      rec.timestampUtc = oss.str();
    }
    {
      std::ostringstream oss;
      ppMessage.printEvent(oss, *event, eventStream.writerProp(), eventStream.clockSync());
      rec.message = oss.str();
    }

    rec.sourceFilename = sourceFilename;

    _allRecords.push_back(std::move(rec));
  }

  applyFilters();
}

void BtuiModel::applyFilters()
{
  _filteredIndices.clear();

  for (std::size_t i = 0; i < _allRecords.size(); ++i)
  {
    const LogRecord& rec = _allRecords[i];

    // Severity filter
    if (rec.severity < _filterState.minSeverity)
    {
      continue;
    }

    // Category filter (case-insensitive substring)
    if (!containsSubstringCaseInsensitive(rec.category, _filterState.categoryFilter))
    {
      continue;
    }

    // Writer filter (case-insensitive substring)
    if (!containsSubstringCaseInsensitive(rec.writerName, _filterState.writerFilter))
    {
      continue;
    }

    // Message filter (case-insensitive substring)
    if (!containsSubstringCaseInsensitive(rec.message, _filterState.messageFilter))
    {
      continue;
    }

    _filteredIndices.push_back(i);
  }

  applySorting();
}

void BtuiModel::applySorting()
{
  if (_sortOrder == SortOrder::none)
  {
    return;
  }

  const bool ascending = (_sortOrder == SortOrder::ascending);
  std::stable_sort(_filteredIndices.begin(), _filteredIndices.end(),
    [this, ascending](std::size_t a, std::size_t b)
    {
      if (ascending)
      {
        return _allRecords[a].clockValue < _allRecords[b].clockValue;
      }
      return _allRecords[a].clockValue > _allRecords[b].clockValue;
    }
  );
}

std::size_t BtuiModel::recordCount() const
{
  return _filteredIndices.size();
}

std::size_t BtuiModel::totalRecordCount() const
{
  return _allRecords.size();
}

const LogRecord& BtuiModel::record(std::size_t filteredIdx) const
{
  return _allRecords[_filteredIndices[filteredIdx]];
}

const std::vector<LogRecord>& BtuiModel::allRecords() const
{
  return _allRecords;
}

const std::vector<std::size_t>& BtuiModel::filteredIndices() const
{
  return _filteredIndices;
}

FilterState& BtuiModel::filterState()
{
  return _filterState;
}

const FilterState& BtuiModel::filterState() const
{
  return _filterState;
}

void BtuiModel::setSortOrder(SortOrder order)
{
  _sortOrder = order;
}

SortOrder BtuiModel::sortOrder() const
{
  return _sortOrder;
}

void BtuiModel::cycleSortOrder()
{
  switch (_sortOrder)
  {
  case SortOrder::none:       _sortOrder = SortOrder::ascending; break;
  case SortOrder::ascending:  _sortOrder = SortOrder::descending; break;
  case SortOrder::descending: _sortOrder = SortOrder::none; break;
  }
  applySorting();
}

void BtuiModel::cycleSeverityFilter()
{
  switch (_filterState.minSeverity)
  {
  case binlog::Severity::trace:    _filterState.minSeverity = binlog::Severity::debug; break;
  case binlog::Severity::debug:    _filterState.minSeverity = binlog::Severity::info; break;
  case binlog::Severity::info:     _filterState.minSeverity = binlog::Severity::warning; break;
  case binlog::Severity::warning:  _filterState.minSeverity = binlog::Severity::error; break;
  case binlog::Severity::error:    _filterState.minSeverity = binlog::Severity::critical; break;
  case binlog::Severity::critical: _filterState.minSeverity = binlog::Severity::trace; break;
  default:                         _filterState.minSeverity = binlog::Severity::trace; break;
  }
  applyFilters();
}

void BtuiModel::resetFilters()
{
  _filterState = FilterState{};
  applyFilters();
}

std::mutex& BtuiModel::mutex()
{
  return _mutex;
}

} // namespace btui
