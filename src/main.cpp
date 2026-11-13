#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "include/cef_app.h"
#include "include/cef_path_util.h"
#include "include/cef_version.h"

#include "app.h"
#include "options.h"
#include "util.h"
#include "version.h"

namespace {

yati::Options g_options;

bool StartsWith(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string ValueOf(const std::string& argument) {
  const size_t pos = argument.find('=');
  return pos == std::string::npos ? std::string() : argument.substr(pos + 1);
}

int IntOf(const std::string& argument, int fallback) {
  const std::string value = ValueOf(argument);
  if (value.empty()) {
    return fallback;
  }
  return std::atoi(value.c_str());
}

void ParseCommandLine(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (StartsWith(argument, "--url=")) {
      g_options.url = ValueOf(argument);
    } else if (StartsWith(argument, "--title=")) {
      g_options.title = ValueOf(argument);
    } else if (StartsWith(argument, "--width=")) {
      g_options.width = IntOf(argument, g_options.width);
    } else if (StartsWith(argument, "--height=")) {
      g_options.height = IntOf(argument, g_options.height);
    } else if (argument == "--no-sandbox") {
      g_options.no_sandbox = true;
    } else if (argument == "--sandbox") {
      g_options.no_sandbox = false;
    } else if (argument == "--disable-gpu") {
      g_options.disable_gpu = true;
    } else if (argument == "--inspect") {
      g_options.inspect = true;
    } else if (StartsWith(argument, "--inspect-port=")) {
      g_options.inspect = true;
      g_options.inspect_port = IntOf(argument, g_options.inspect_port);
    } else if (argument == "--verbose") {
      g_options.verbose = true;
    }
  }
}

}  // namespace

namespace yati {

const Options& GetOptions() {
  return g_options;
}

void SetOptions(const Options& options) {
  g_options = options;
}

}  // namespace yati

// CEF warns loudly (and may share a singleton with an unrelated app) when
// root_cache_path is left at the default. Give the framework its own cache
// directory under the XDG cache home.
std::string ResolveCacheRoot() {
  const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
  if (xdg_cache && *xdg_cache) {
    return std::string(xdg_cache) + "/" + YATI_APP_ID;
  }
  const char* home = std::getenv("HOME");
  if (home && *home) {
    return std::string(home) + "/.cache/" + YATI_APP_ID;
  }
  return std::string("/tmp/") + YATI_APP_ID + "-cache";
}

int main(int argc, char* argv[]) {
  CefMainArgs main_args(argc, argv);
  CefRefPtr<yati::App> app(new yati::App());

  // If this process was spawned by CEF as a renderer/GPU/utility helper this
  // call never returns; the browser process falls through.
  const int exit_code = CefExecuteProcess(main_args, app, nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  ParseCommandLine(argc, argv);
  yati::Options options = yati::GetOptions();

  if (options.url.empty()) {
    CefString exe_dir;
    if (!CefGetPath(PK_DIR_EXE, exe_dir)) {
      std::fprintf(stderr, "[yati] unable to resolve the executable directory\n");
      return 1;
    }
    options.url = "file://" + yati::ToUtf8(exe_dir) + "/webui/index.html";
  }
  yati::SetOptions(options);

  const std::string cache_root = ResolveCacheRoot();
  std::error_code fs_error;
  std::filesystem::create_directories(cache_root, fs_error);

  CefSettings settings;
  settings.no_sandbox = options.no_sandbox;
  CefString(&settings.root_cache_path) = yati::ToCef(cache_root);
  settings.windowless_rendering_enabled = false;
  settings.multi_threaded_message_loop = false;
  settings.external_message_pump = false;
  settings.log_severity = options.verbose ? LOGSEVERITY_INFO : LOGSEVERITY_WARNING;
  if (options.inspect) {
    settings.remote_debugging_port = options.inspect_port;
  }

  std::printf("%s %s  |  CEF %s  |  Chromium %d\n", YATI_PRODUCT_NAME,
              YATI_VERSION, CEF_VERSION, CHROME_VERSION_MAJOR);
  std::fflush(stdout);

  if (!CefInitialize(main_args, settings, app, nullptr)) {
    std::fprintf(stderr, "[yati] CefInitialize failed\n");
    return 1;
  }

  CefRunMessageLoop();

  CefShutdown();
  return 0;
}
