#ifndef YATI_PROCESS_H_
#define YATI_PROCESS_H_

#include <string>
#include <vector>

namespace yati {

struct ProcessResult {
  int code = -1;            // exit code, or -1 when killed / not started
  std::string out;          // captured stdout
  std::string err;          // captured stderr
  bool timed_out = false;
  bool spawn_failed = false;
};

// Runs |argv| (argv[0] looked up on PATH) with an optional stdin payload and
// captures stdout/stderr. Blocks, so callers on the UI thread should only use
// it for short-lived commands.
ProcessResult RunProcess(const std::vector<std::string>& argv,
                         const std::string& input = std::string(),
                         int timeout_ms = 5000);

// Starts |argv| and detaches immediately (no output capture). Used for
// fire-and-forget launches such as shell.openExternal.
bool SpawnDetached(const std::vector<std::string>& argv);

// True when the program exists on PATH.
bool CommandExists(const std::string& program);

}  // namespace yati

#endif  // YATI_PROCESS_H_
