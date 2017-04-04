#include "control_loop.h"

#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <win32.h>
#else
#include <pthread.h>
#endif

namespace franka {

ControlLoop::ControlLoop(Robot::Impl& robot_impl,
                         ControlCallback control_callback)
    : robot_impl_(robot_impl), control_callback_(std::move(control_callback)) {
  if (control_callback_) {
    setCurrentThreadToRealtime();
    robot_impl_.startController();
  }
}

ControlLoop::~ControlLoop() {
  if (control_callback_) {
    robot_impl_.stopController();
  }
}

void ControlLoop::operator()() {
  while (robot_impl_.update()) {
    if (!spinOnce()) {
      break;
    }
  }
}

bool ControlLoop::spinOnce() {
  if (control_callback_) {
    Torques control_output = control_callback_(robot_impl_.robotState());
    if (&control_output == &Stop) {
      return false;
    }
    convertTorques(control_output, &robot_impl_.controllerCommand());
  }

  return true;
}

void ControlLoop::convertTorques(
    const Torques& torques,
    research_interface::ControllerCommand* command) {
  command->tau_J_d = torques.tau_J;
}

void ControlLoop::setCurrentThreadToRealtime() {
#ifdef _WIN32
  // TODO: test on WINDOWS
  auto get_last_windows_error = []() -> std::string {
    DWORD error_id = GetLastError();
    LPSTR buffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&buffer, 0, NULL);
    return std::string(buffer, size);
  };

  if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
    if (robot_impl_.realtimeConfig() == RealtimeConfig::kEnforce) {
      throw RealTimeException(
          std::string{
              "libfranka: unable to set realtime priority for the process: "} +
          get_last_windows_error());
    } else {
      return;
    }
  }
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
    if (robot_impl_.realtimeConfig() == RealtimeConfig::kEnforce) {
      throw RealTimeException(
          std::string{
              "libfranka: unable to set realtime priority for the thread: "} +
          get_last_windows_error());
    }
  }
#else
  int policy = SCHED_FIFO;
  struct sched_param thread_param;
  constexpr int thread_priority = 20;
  thread_param.sched_priority = thread_priority;
  if (pthread_setschedparam(pthread_self(), policy, &thread_param) != 0) {
    if (robot_impl_.realtimeConfig() == RealtimeConfig::kEnforce) {
      throw RealTimeException(
          std::string{"libfranka: unable to set realtime scheduling: "} +
          strerror(errno));
    }
  }
#endif
}

}  // namespace franka
