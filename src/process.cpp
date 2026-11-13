#include "process.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace yati {

namespace {

// Builds the argv vector handed to execvp(). Done before fork() so the child
// does not have to allocate.
std::vector<char*> BuildArgv(const std::vector<std::string>& argv) {
  std::vector<char*> result;
  result.reserve(argv.size() + 1);
  for (const auto& item : argv) {
    result.push_back(const_cast<char*>(item.c_str()));
  }
  result.push_back(nullptr);
  return result;
}

void SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

// Drains whatever is available without blocking. Returns false on EOF.
bool DrainAvailable(int fd, std::string* out) {
  char buffer[8192];
  for (;;) {
    const ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n > 0) {
      out->append(buffer, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) {
      return false;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
  }
}

}  // namespace

bool CommandExists(const std::string& program) {
  if (program.empty()) {
    return false;
  }
  if (program.find('/') != std::string::npos) {
    return access(program.c_str(), X_OK) == 0;
  }
  const char* path_env = std::getenv("PATH");
  if (!path_env) {
    return false;
  }
  std::string paths = path_env;
  size_t start = 0;
  while (start <= paths.size()) {
    const size_t end = paths.find(':', start);
    const std::string dir =
        paths.substr(start, end == std::string::npos ? std::string::npos
                                                     : end - start);
    if (!dir.empty()) {
      std::string candidate = dir + "/" + program;
      if (access(candidate.c_str(), X_OK) == 0) {
        return true;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return false;
}

ProcessResult RunProcess(const std::vector<std::string>& argv,
                         const std::string& input,
                         int timeout_ms) {
  ProcessResult result;
  if (argv.empty()) {
    result.spawn_failed = true;
    return result;
  }

  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  int in_pipe[2] = {-1, -1};

  if (pipe(out_pipe) != 0) {
    result.spawn_failed = true;
    return result;
  }
  if (pipe(err_pipe) != 0) {
    close(out_pipe[0]);
    close(out_pipe[1]);
    result.spawn_failed = true;
    return result;
  }
  if (!input.empty() && pipe(in_pipe) != 0) {
    close(out_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[0]);
    close(err_pipe[1]);
    result.spawn_failed = true;
    return result;
  }

  std::vector<char*> child_argv = BuildArgv(argv);

  const pid_t pid = fork();
  if (pid < 0) {
    close(out_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[0]);
    close(err_pipe[1]);
    if (in_pipe[0] != -1) {
      close(in_pipe[0]);
      close(in_pipe[1]);
    }
    result.spawn_failed = true;
    return result;
  }

  if (pid == 0) {
    // Child: wire up stdio and exec. No allocation happens here.
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    if (in_pipe[0] != -1) {
      dup2(in_pipe[0], STDIN_FILENO);
    } else {
      const int dev_null = open("/dev/null", O_RDONLY);
      if (dev_null >= 0) {
        dup2(dev_null, STDIN_FILENO);
      }
    }
    close(out_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[0]);
    close(err_pipe[1]);
    if (in_pipe[0] != -1) {
      close(in_pipe[0]);
      close(in_pipe[1]);
    }
    execvp(child_argv[0], child_argv.data());
    _exit(127);
  }

  close(out_pipe[1]);
  close(err_pipe[1]);
  if (in_pipe[0] != -1) {
    close(in_pipe[0]);
  }

  if (!input.empty()) {
    size_t offset = 0;
    while (offset < input.size()) {
      const ssize_t n = write(in_pipe[1], input.data() + offset,
                              input.size() - offset);
      if (n <= 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      offset += static_cast<size_t>(n);
    }
  }
  if (in_pipe[1] != -1) {
    close(in_pipe[1]);
  }

  SetNonBlocking(out_pipe[0]);
  SetNonBlocking(err_pipe[0]);

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  bool out_open = true;
  bool err_open = true;
  bool killed = false;

  while (out_open || err_open) {
    pollfd fds[2];
    int count = 0;
    if (out_open) {
      fds[count++] = pollfd{out_pipe[0], POLLIN, 0};
    }
    if (err_open) {
      fds[count++] = pollfd{err_pipe[0], POLLIN, 0};
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline && !killed) {
      kill(pid, SIGKILL);
      killed = true;
      result.timed_out = true;
    }
    int wait_ms = 200;
    if (!killed) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 deadline - now)
                                 .count();
      if (remaining < wait_ms) {
        wait_ms = static_cast<int>(remaining > 0 ? remaining : 0);
      }
    }

    const int ready = poll(fds, static_cast<nfds_t>(count), wait_ms);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    // Drain both pipes on every iteration (one may be saturating while the
    // other has data buffered).
    if (out_open) {
      if (!DrainAvailable(out_pipe[0], &result.out)) {
        close(out_pipe[0]);
        out_open = false;
      }
    }
    if (err_open) {
      if (!DrainAvailable(err_pipe[0], &result.err)) {
        close(err_pipe[0]);
        err_open = false;
      }
    }
  }

  if (out_open) {
    close(out_pipe[0]);
  }
  if (err_open) {
    close(err_pipe[0]);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) == pid) {
    if (WIFEXITED(status)) {
      result.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      result.code = 128 + WTERMSIG(status);
    }
  }
  return result;
}

bool SpawnDetached(const std::vector<std::string>& argv) {
  if (argv.empty()) {
    return false;
  }
  std::vector<char*> child_argv = BuildArgv(argv);
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    setsid();
    const int dev_null = open("/dev/null", O_RDWR);
    if (dev_null >= 0) {
      dup2(dev_null, STDIN_FILENO);
      dup2(dev_null, STDOUT_FILENO);
      dup2(dev_null, STDERR_FILENO);
    }
    execvp(child_argv[0], child_argv.data());
    _exit(127);
  }
  return true;
}

}  // namespace yati
