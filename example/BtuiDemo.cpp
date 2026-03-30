#include <binlog/binlog.hpp>

#include <fstream>
#include <iostream>

int main()
{
  BINLOG_INFO_C(startup, "Application starting up, version {}.{}.{}", 1, 4, 2);
  BINLOG_DEBUG_C(startup, "Loading configuration from {}", "config.yaml");
  BINLOG_TRACE_C(startup, "Parsed {} config keys", 37);

  BINLOG_INFO_C(network, "Listening on port {}", 8080);
  BINLOG_DEBUG_C(network, "Max connections set to {}", 1024);
  BINLOG_INFO_C(network, "Accepted connection from {}", "192.168.1.42");
  BINLOG_WARN_C(network, "Connection pool at {}% capacity", 85);
  BINLOG_INFO_C(network, "Accepted connection from {}", "10.0.0.7");
  BINLOG_ERROR_C(network, "Connection from {} timed out after {}s", "192.168.1.42", 30);

  BINLOG_INFO_C(database, "Connected to database at {}", "db.internal:5432");
  BINLOG_DEBUG_C(database, "Connection pool size: {}", 10);
  BINLOG_INFO_C(database, "Query executed in {}ms: SELECT * FROM users", 12);
  BINLOG_WARN_C(database, "Slow query detected: {}ms", 2500);
  BINLOG_ERROR_C(database, "Failed to acquire lock on table {}", "orders");
  BINLOG_INFO_C(database, "Retrying lock acquisition, attempt {}/{}", 2, 3);
  BINLOG_INFO_C(database, "Lock acquired on attempt {}", 2);

  BINLOG_INFO_C(auth, "User {} logged in from {}", "alice", "10.0.0.7");
  BINLOG_WARN_C(auth, "Failed login attempt for user {} ({} of {})", "bob", 3, 5);
  BINLOG_CRITICAL_C(auth, "Account {} locked after {} failed attempts", "bob", 5);
  BINLOG_INFO_C(auth, "User {} granted admin role", "alice");
  BINLOG_DEBUG_C(auth, "Session token refreshed for user {}", "alice");

  BINLOG_INFO_C(worker, "Background job scheduler started with {} threads", 4);
  BINLOG_DEBUG_C(worker, "Processing job #{}: {}", 1001, "generate_report");
  BINLOG_INFO_C(worker, "Job #{} completed in {}ms", 1001, 340);
  BINLOG_DEBUG_C(worker, "Processing job #{}: {}", 1002, "send_notifications");
  BINLOG_WARN_C(worker, "Job #{} retrying, attempt {}/{}", 1002, 2, 3);
  BINLOG_ERROR_C(worker, "Job #{} failed: {}", 1002, "SMTP server unreachable");

  BINLOG_INFO_C(metrics, "CPU usage: {}%", 42);
  BINLOG_INFO_C(metrics, "Memory usage: {}/{} MB", 1847, 4096);
  BINLOG_WARN_C(metrics, "Disk usage at {}%, consider cleanup", 91);
  BINLOG_CRITICAL_C(metrics, "Out of memory: allocation of {} bytes failed", 67108864);

  BINLOG_INFO_C(startup, "Graceful shutdown initiated");
  BINLOG_INFO_C(network, "Closing {} active connections", 3);
  BINLOG_INFO_C(database, "Flushing write-ahead log");
  BINLOG_INFO_C(startup, "Shutdown complete");

  std::ofstream logfile("btui_demo.blog", std::ofstream::out | std::ofstream::binary);
  binlog::consume(logfile);

  if (!logfile)
  {
    std::cerr << "Failed to write btui_demo.blog\n";
    return 1;
  }

  std::cout << "Binary log written to btui_demo.blog\n";
  return 0;
}
