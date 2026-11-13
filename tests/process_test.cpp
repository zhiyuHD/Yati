// Yubild 的测试框架用法示例：进程执行与输出采集。
//
// 这些用例覆盖 src/process.cpp 的全部对外行为——它是纯 POSIX 实现，
// 不依赖 CEF，所以可以直接链进测试可执行文件。

#include <string>

#include "process.h"
#include "yubild_test.h"

YB_TEST(command_exists_finds_shell) {
    YB_CHECK(yati::CommandExists("sh"));
    YB_CHECK(!yati::CommandExists("yati-definitely-not-a-real-command"));
}

YB_TEST(command_exists_rejects_empty) {
    YB_CHECK(!yati::CommandExists(""));
}

YB_TEST(run_process_captures_stdout) {
    const yati::ProcessResult result = yati::RunProcess({"echo", "hello"});
    YB_CHECK_EQ(result.code, 0);
    YB_CHECK_EQ(result.out, std::string("hello\n"));
    YB_CHECK_EQ(result.err, std::string(""));
    YB_CHECK(!result.timed_out);
}

YB_TEST(run_process_captures_stderr) {
    const yati::ProcessResult result =
        yati::RunProcess({"sh", "-c", "printf oops >&2"});
    YB_CHECK_EQ(result.code, 0);
    YB_CHECK_EQ(result.err, std::string("oops"));
}

YB_TEST(run_process_feeds_stdin) {
    const yati::ProcessResult result = yati::RunProcess({"cat"}, "roundtrip");
    YB_CHECK_EQ(result.code, 0);
    YB_CHECK_EQ(result.out, std::string("roundtrip"));
}

YB_TEST(run_process_reports_exit_code) {
    const yati::ProcessResult result = yati::RunProcess({"sh", "-c", "exit 3"});
    YB_CHECK_EQ(result.code, 3);
}

YB_TEST(run_process_kills_on_timeout) {
    const yati::ProcessResult result = yati::RunProcess({"sleep", "5"}, "", 300);
    YB_CHECK(result.timed_out);
	// 被 SIGKILL 后退出码是 128 + 9
    YB_CHECK_EQ(result.code, 137);
}

YB_TEST(run_process_missing_binary) {
    const yati::ProcessResult result =
        yati::RunProcess({"yati-definitely-not-a-real-command"});
    YB_CHECK_EQ(result.code, 127);
}

YB_TEST(spawn_detached_returns_true) {
    YB_CHECK(yati::SpawnDetached({"true"}));
}
