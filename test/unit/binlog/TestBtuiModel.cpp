#include "btui_model.hpp"

#include <binlog/Entries.hpp>
#include <binlog/Severity.hpp>

#include <mserialize/make_template_serializable.hpp>
#include <mserialize/serialize.hpp>

#include "test_utils.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <tuple>

namespace {

template <typename... Args>
struct BtuiTestEvent
{
  std::uint64_t eventSourceId;
  std::uint64_t clockValue;
  std::tuple<Args...> args;
};

binlog::EventSource makeEventSource(
  std::uint64_t id,
  binlog::Severity severity,
  const std::string& category,
  const std::string& formatStr,
  std::string argumentTags = {}
)
{
  return binlog::EventSource{
    id, severity, category, "testFunc", "/src/test.cpp", 42, formatStr, std::move(argumentTags)
  };
}

} // namespace

MSERIALIZE_MAKE_TEMPLATE_SERIALIZABLE(
  (typename... Args), (BtuiTestEvent<Args...>), eventSourceId, clockValue, args
)

TEST_CASE("btui_load_records")
{
  const binlog::EventSource src = makeEventSource(1, binlog::Severity::info, "app", "Hello {}!", "i");
  const binlog::WriterProp wp{10, "main_thread", 0};
  const binlog::ClockSync cs{0, 1000000000, 0, 3600, "CET"};
  const BtuiTestEvent<int> ev{1, 1000000000, std::make_tuple(42)};

  TestStream stream;
  binlog::serializeSizePrefixedTagged(src, stream);
  binlog::serializeSizePrefixedTagged(wp, stream);
  binlog::serializeSizePrefixedTagged(cs, stream);

  // serialize event (size-prefixed, no tag for events - tag is the source id)
  {
    const std::uint32_t size = std::uint32_t(mserialize::serialized_size(ev));
    mserialize::serialize(size, stream);
    mserialize::serialize(ev, stream);
  }

  btui::BtuiModel model("%S %m", "%H:%M:%S");
  model.loadFromEntryStream(stream);

  REQUIRE(model.totalRecordCount() == 1);
  REQUIRE(model.recordCount() == 1);

  const btui::LogRecord& rec = model.record(0);
  CHECK(rec.severity == binlog::Severity::info);
  CHECK(rec.severityStr == "INFO");
  CHECK(rec.category == "app");
  CHECK(rec.writerName == "main_thread");
  CHECK(rec.writerId == 10);
  CHECK(rec.file == "test.cpp");
  CHECK(rec.fileFull == "/src/test.cpp");
  CHECK(rec.line == 42);
  CHECK(rec.function == "testFunc");
  CHECK(rec.clockValue == 1000000000);
  CHECK(rec.message == "Hello 42!");
  CHECK(rec.formatString == "Hello {}!");
  CHECK(rec.argumentTags == "i");
}

TEST_CASE("btui_load_multiple_records")
{
  const binlog::EventSource src1 = makeEventSource(1, binlog::Severity::info, "app", "msg1");
  const binlog::EventSource src2 = makeEventSource(2, binlog::Severity::error, "net", "msg2");
  const binlog::ClockSync cs{0, 1000000000, 0, 0, "UTC"};
  const BtuiTestEvent<> ev1{1, 100, {}};
  const BtuiTestEvent<> ev2{2, 200, {}};

  TestStream stream;
  binlog::serializeSizePrefixedTagged(src1, stream);
  binlog::serializeSizePrefixedTagged(src2, stream);
  binlog::serializeSizePrefixedTagged(cs, stream);

  const std::uint32_t size1 = std::uint32_t(mserialize::serialized_size(ev1));
  mserialize::serialize(size1, stream);
  mserialize::serialize(ev1, stream);

  const std::uint32_t size2 = std::uint32_t(mserialize::serialized_size(ev2));
  mserialize::serialize(size2, stream);
  mserialize::serialize(ev2, stream);

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream);

  REQUIRE(model.totalRecordCount() == 2);
  CHECK(model.record(0).severity == binlog::Severity::info);
  CHECK(model.record(0).message == "msg1");
  CHECK(model.record(1).severity == binlog::Severity::error);
  CHECK(model.record(1).message == "msg2");
}

TEST_CASE("btui_filter_severity")
{
  const binlog::EventSource src1 = makeEventSource(1, binlog::Severity::debug, "app", "debug msg");
  const binlog::EventSource src2 = makeEventSource(2, binlog::Severity::info, "app", "info msg");
  const binlog::EventSource src3 = makeEventSource(3, binlog::Severity::warning, "app", "warn msg");
  const binlog::EventSource src4 = makeEventSource(4, binlog::Severity::error, "app", "error msg");
  const binlog::ClockSync cs{0, 1000000000, 0, 0, "UTC"};
  const BtuiTestEvent<> ev1{1, 100, {}};
  const BtuiTestEvent<> ev2{2, 200, {}};
  const BtuiTestEvent<> ev3{3, 300, {}};
  const BtuiTestEvent<> ev4{4, 400, {}};

  TestStream stream;
  binlog::serializeSizePrefixedTagged(src1, stream);
  binlog::serializeSizePrefixedTagged(src2, stream);
  binlog::serializeSizePrefixedTagged(src3, stream);
  binlog::serializeSizePrefixedTagged(src4, stream);
  binlog::serializeSizePrefixedTagged(cs, stream);

  auto serializeEv = [&stream](const auto& ev) {
    const std::uint32_t size = std::uint32_t(mserialize::serialized_size(ev));
    mserialize::serialize(size, stream);
    mserialize::serialize(ev, stream);
  };
  serializeEv(ev1);
  serializeEv(ev2);
  serializeEv(ev3);
  serializeEv(ev4);

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream);

  REQUIRE(model.totalRecordCount() == 4);
  CHECK(model.recordCount() == 4); // all shown by default (min=trace)

  // Set severity filter to warning+
  model.filterState().minSeverity = binlog::Severity::warning;
  model.applyFilters();

  CHECK(model.recordCount() == 2);
  CHECK(model.record(0).severity == binlog::Severity::warning);
  CHECK(model.record(1).severity == binlog::Severity::error);
}

TEST_CASE("btui_filter_category")
{
  const binlog::EventSource src1 = makeEventSource(1, binlog::Severity::info, "network", "msg1");
  const binlog::EventSource src2 = makeEventSource(2, binlog::Severity::info, "database", "msg2");
  const binlog::EventSource src3 = makeEventSource(3, binlog::Severity::info, "Network", "msg3");
  const binlog::ClockSync cs{0, 1000000000, 0, 0, "UTC"};
  const BtuiTestEvent<> ev1{1, 100, {}};
  const BtuiTestEvent<> ev2{2, 200, {}};
  const BtuiTestEvent<> ev3{3, 300, {}};

  TestStream stream;
  binlog::serializeSizePrefixedTagged(src1, stream);
  binlog::serializeSizePrefixedTagged(src2, stream);
  binlog::serializeSizePrefixedTagged(src3, stream);
  binlog::serializeSizePrefixedTagged(cs, stream);

  auto serializeEv = [&stream](const auto& ev) {
    const std::uint32_t size = std::uint32_t(mserialize::serialized_size(ev));
    mserialize::serialize(size, stream);
    mserialize::serialize(ev, stream);
  };
  serializeEv(ev1);
  serializeEv(ev2);
  serializeEv(ev3);

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream);

  model.filterState().categoryFilter = "net";
  model.applyFilters();

  // "network" and "Network" both match "net" case-insensitively
  CHECK(model.recordCount() == 2);
  CHECK(model.record(0).category == "network");
  CHECK(model.record(1).category == "Network");
}

TEST_CASE("btui_filter_message")
{
  const binlog::EventSource src1 = makeEventSource(1, binlog::Severity::info, "app", "Connection established");
  const binlog::EventSource src2 = makeEventSource(2, binlog::Severity::info, "app", "Request processed");
  const binlog::EventSource src3 = makeEventSource(3, binlog::Severity::info, "app", "connection lost");
  const binlog::ClockSync cs{0, 1000000000, 0, 0, "UTC"};
  const BtuiTestEvent<> ev1{1, 100, {}};
  const BtuiTestEvent<> ev2{2, 200, {}};
  const BtuiTestEvent<> ev3{3, 300, {}};

  TestStream stream;
  binlog::serializeSizePrefixedTagged(src1, stream);
  binlog::serializeSizePrefixedTagged(src2, stream);
  binlog::serializeSizePrefixedTagged(src3, stream);
  binlog::serializeSizePrefixedTagged(cs, stream);

  auto serializeEv = [&stream](const auto& ev) {
    const std::uint32_t size = std::uint32_t(mserialize::serialized_size(ev));
    mserialize::serialize(size, stream);
    mserialize::serialize(ev, stream);
  };
  serializeEv(ev1);
  serializeEv(ev2);
  serializeEv(ev3);

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream);

  model.filterState().messageFilter = "connection";
  model.applyFilters();

  CHECK(model.recordCount() == 2);
  CHECK(model.record(0).message == "Connection established");
  CHECK(model.record(1).message == "connection lost");
}

TEST_CASE("btui_sort_by_clock")
{
  const binlog::EventSource src = makeEventSource(1, binlog::Severity::info, "app", "msg");
  const binlog::ClockSync cs{0, 1000000000, 0, 0, "UTC"};
  const BtuiTestEvent<> ev1{1, 300, {}};
  const BtuiTestEvent<> ev2{1, 100, {}};
  const BtuiTestEvent<> ev3{1, 200, {}};

  TestStream stream;
  binlog::serializeSizePrefixedTagged(src, stream);
  binlog::serializeSizePrefixedTagged(cs, stream);

  auto serializeEv = [&stream](const auto& ev) {
    const std::uint32_t size = std::uint32_t(mserialize::serialized_size(ev));
    mserialize::serialize(size, stream);
    mserialize::serialize(ev, stream);
  };
  serializeEv(ev1);
  serializeEv(ev2);
  serializeEv(ev3);

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream);

  // Default: file order
  CHECK(model.record(0).clockValue == 300);
  CHECK(model.record(1).clockValue == 100);
  CHECK(model.record(2).clockValue == 200);

  // Ascending
  model.setSortOrder(btui::SortOrder::ascending);
  model.applySorting();
  CHECK(model.record(0).clockValue == 100);
  CHECK(model.record(1).clockValue == 200);
  CHECK(model.record(2).clockValue == 300);

  // Descending
  model.setSortOrder(btui::SortOrder::descending);
  model.applySorting();
  CHECK(model.record(0).clockValue == 300);
  CHECK(model.record(1).clockValue == 200);
  CHECK(model.record(2).clockValue == 100);
}

TEST_CASE("btui_multi_file_merge")
{
  const binlog::EventSource src1 = makeEventSource(1, binlog::Severity::info, "app", "file1 msg");
  const binlog::EventSource src2 = makeEventSource(1, binlog::Severity::info, "app", "file2 msg");
  const binlog::ClockSync cs{0, 1000000000, 0, 0, "UTC"};
  const BtuiTestEvent<> ev1{1, 200, {}};
  const BtuiTestEvent<> ev2{1, 100, {}};

  auto serializeEv = [](TestStream& s, const auto& ev) {
    const std::uint32_t size = std::uint32_t(mserialize::serialized_size(ev));
    mserialize::serialize(size, s);
    mserialize::serialize(ev, s);
  };

  TestStream stream1;
  binlog::serializeSizePrefixedTagged(src1, stream1);
  binlog::serializeSizePrefixedTagged(cs, stream1);
  serializeEv(stream1, ev1);

  TestStream stream2;
  binlog::serializeSizePrefixedTagged(src2, stream2);
  binlog::serializeSizePrefixedTagged(cs, stream2);
  serializeEv(stream2, ev2);

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream1, "file1.blog");
  model.loadFromEntryStream(stream2, "file2.blog");

  REQUIRE(model.totalRecordCount() == 2);
  CHECK(model.record(0).sourceFilename == "file1.blog");
  CHECK(model.record(1).sourceFilename == "file2.blog");

  // Sort ascending to merge by time
  model.setSortOrder(btui::SortOrder::ascending);
  model.applySorting();
  CHECK(model.record(0).clockValue == 100);
  CHECK(model.record(0).sourceFilename == "file2.blog");
  CHECK(model.record(1).clockValue == 200);
  CHECK(model.record(1).sourceFilename == "file1.blog");
}

TEST_CASE("btui_cycle_severity")
{
  btui::BtuiModel model("%m", "%H:%M:%S");

  // Default is trace
  CHECK(model.filterState().minSeverity == binlog::Severity::trace);

  model.cycleSeverityFilter();
  CHECK(model.filterState().minSeverity == binlog::Severity::debug);

  model.cycleSeverityFilter();
  CHECK(model.filterState().minSeverity == binlog::Severity::info);

  model.cycleSeverityFilter();
  CHECK(model.filterState().minSeverity == binlog::Severity::warning);

  model.cycleSeverityFilter();
  CHECK(model.filterState().minSeverity == binlog::Severity::error);

  model.cycleSeverityFilter();
  CHECK(model.filterState().minSeverity == binlog::Severity::critical);

  // Wraps back to trace
  model.cycleSeverityFilter();
  CHECK(model.filterState().minSeverity == binlog::Severity::trace);
}

TEST_CASE("btui_tail_append")
{
  const binlog::EventSource src = makeEventSource(1, binlog::Severity::info, "app", "msg");
  const binlog::ClockSync cs{0, 1000000000, 0, 0, "UTC"};
  const BtuiTestEvent<> ev1{1, 100, {}};
  const BtuiTestEvent<> ev2{1, 200, {}};

  auto serializeEv = [](TestStream& s, const auto& ev) {
    const std::uint32_t size = std::uint32_t(mserialize::serialized_size(ev));
    mserialize::serialize(size, s);
    mserialize::serialize(ev, s);
  };

  // Initial load with one event
  TestStream stream1;
  binlog::serializeSizePrefixedTagged(src, stream1);
  binlog::serializeSizePrefixedTagged(cs, stream1);
  serializeEv(stream1, ev1);

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream1);

  REQUIRE(model.totalRecordCount() == 1);
  CHECK(model.record(0).clockValue == 100);

  // Simulate tail: load more events from a new stream
  TestStream stream2;
  binlog::serializeSizePrefixedTagged(src, stream2);
  binlog::serializeSizePrefixedTagged(cs, stream2);
  serializeEv(stream2, ev2);

  model.loadFromEntryStream(stream2);

  REQUIRE(model.totalRecordCount() == 2);
  CHECK(model.record(0).clockValue == 100);
  CHECK(model.record(1).clockValue == 200);
}

TEST_CASE("btui_reset_filters")
{
  const binlog::EventSource src1 = makeEventSource(1, binlog::Severity::debug, "app", "debug msg");
  const binlog::EventSource src2 = makeEventSource(2, binlog::Severity::error, "net", "error msg");
  const binlog::ClockSync cs{0, 1000000000, 0, 0, "UTC"};
  const BtuiTestEvent<> ev1{1, 100, {}};
  const BtuiTestEvent<> ev2{2, 200, {}};

  TestStream stream;
  binlog::serializeSizePrefixedTagged(src1, stream);
  binlog::serializeSizePrefixedTagged(src2, stream);
  binlog::serializeSizePrefixedTagged(cs, stream);

  auto serializeEv = [&stream](const auto& ev) {
    const std::uint32_t size = std::uint32_t(mserialize::serialized_size(ev));
    mserialize::serialize(size, stream);
    mserialize::serialize(ev, stream);
  };
  serializeEv(ev1);
  serializeEv(ev2);

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream);

  // Apply restrictive filter
  model.filterState().minSeverity = binlog::Severity::error;
  model.filterState().categoryFilter = "net";
  model.applyFilters();
  CHECK(model.recordCount() == 1);

  // Reset
  model.resetFilters();
  CHECK(model.recordCount() == 2);
  CHECK(model.filterState().minSeverity == binlog::Severity::trace);
  CHECK(model.filterState().categoryFilter.empty());
}

TEST_CASE("btui_empty_stream")
{
  TestStream stream;

  btui::BtuiModel model("%m", "%H:%M:%S");
  model.loadFromEntryStream(stream);

  CHECK(model.totalRecordCount() == 0);
  CHECK(model.recordCount() == 0);
}

TEST_CASE("btui_cycle_sort_order")
{
  btui::BtuiModel model("%m", "%H:%M:%S");

  CHECK(model.sortOrder() == btui::SortOrder::none);

  model.cycleSortOrder();
  CHECK(model.sortOrder() == btui::SortOrder::ascending);

  model.cycleSortOrder();
  CHECK(model.sortOrder() == btui::SortOrder::descending);

  model.cycleSortOrder();
  CHECK(model.sortOrder() == btui::SortOrder::none);
}
