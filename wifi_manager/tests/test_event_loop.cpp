#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>

#include "test.hpp"
#include "wifimgr/event_loop.hpp"

using namespace wifimgr;

TEST(timers_fire_in_order_and_cancel_works) {
  EventLoop loop;
  std::vector<int> order;
  loop.startTimer(Millis(30), [&] { order.push_back(3); });
  loop.startTimer(Millis(10), [&] { order.push_back(1); });
  auto id = loop.startTimer(Millis(20), [&] { order.push_back(2); });
  loop.cancelTimer(id);
  loop.startTimer(Millis(40), [&] { loop.stop(); });
  loop.run();
  REQUIRE(order.size() == 2);
  CHECK_EQ(order[0], 1);
  CHECK_EQ(order[1], 3);
}

TEST(timer_handle_restart_and_rearm_from_callback) {
  EventLoop loop;
  Timer t(loop);
  int fired = 0;
  std::function<void()> tick = [&] {
    if (++fired < 3) t.start(Millis(1), tick);
    else loop.stop();
  };
  t.start(Millis(1000), [] {});
  t.start(Millis(1), tick);  // replaces the 1 s one
  auto begin = std::chrono::steady_clock::now();
  loop.run();
  CHECK_EQ(fired, 3);
  CHECK(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(500));
  CHECK(!t.active());
}

TEST(fd_callbacks_and_removal) {
  EventLoop loop;
  int fds[2];
  REQUIRE(pipe(fds) == 0);
  std::string got;
  loop.addFd(fds[0], EPOLLIN, [&](uint32_t) {
    char buf[16];
    ssize_t n = read(fds[0], buf, sizeof buf);
    if (n > 0) got.append(buf, static_cast<size_t>(n));
    loop.removeFd(fds[0]);
    loop.stop();
  });
  REQUIRE(write(fds[1], "hi", 2) == 2);
  loop.run();
  CHECK_EQ(got, std::string("hi"));
  close(fds[0]);
  close(fds[1]);
}

TEST(child_processes_are_reaped_with_status) {
  EventLoop loop;
  pid_t pid = spawnShell("exit 7");
  REQUIRE(pid > 0);
  int code = -1;
  loop.watchChild(pid, [&](pid_t, int st) {
    code = WIFEXITED(st) ? WEXITSTATUS(st) : -2;
    loop.stop();
  });
  loop.startTimer(Millis(5000), [&] { loop.stop(); });
  loop.run();
  CHECK_EQ(code, 7);
}

TEST(spawned_children_get_environment_and_unblocked_signals) {
  EventLoop loop;
  // The daemon blocks SIGTERM for its signalfd; children must not inherit
  // that, or `kill` would never stop the DHCP client.
  loop.watchSignals({SIGTERM}, [](const signalfd_siginfo&) {});
  pid_t pid = spawnShell("test \"$FOO\" = bar && sleep 30", {"FOO=bar"});
  REQUIRE(pid > 0);
  int sig = -1;
  loop.watchChild(pid, [&](pid_t, int st) {
    sig = WIFSIGNALED(st) ? WTERMSIG(st) : 100 + WEXITSTATUS(st);
    loop.stop();
  });
  loop.startTimer(Millis(200), [&] { kill(-pid, SIGTERM); });
  loop.startTimer(Millis(5000), [&] { loop.stop(); });
  loop.run();
  CHECK_EQ(sig, SIGTERM);
}
