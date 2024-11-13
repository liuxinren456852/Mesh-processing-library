// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#ifndef MESH_PROCESSING_LIBHH_CONSOLEPROGRESS_H_
#define MESH_PROCESSING_LIBHH_CONSOLEPROGRESS_H_

#include <atomic>
#include <mutex>

#include "libHh/Hh.h"

#if 0
{
  ConsoleProgress cprogress;
  const int n = 10'000;
  for_int(i, n) {
    cprogress.update(float(i) / n);
    process(i);
  }
}
{
  const int n = 1000;
  ConsoleProgressInc cprogress(n, "Computing");
  parallel_for_each(range(n), [&](const int i) {
    cprogress.increment();
    process(i);
  });
}
#endif

namespace hh {

// Show incremental progress in the console window by writing an updating text percentage on std::cerr
//  using some backspace/carriage-return characters.
class ConsoleProgress : noncopyable {
 public:
  explicit ConsoleProgress(string task_name = "", bool set_silent = false);
  ~ConsoleProgress() { clear(); }
  void update(float f) {
    if (!_silent && min(int(f * 100.f), 99) > _last_val) update_i(f);
  }
  void clear();
  static bool set_all_silent(bool v) { return std::exchange(silent_instance(), v); }

 private:
  std::atomic<int> _last_val{-1};  // -1 if not yet printed
  string _task_name;
  bool _silent;
  void update_i(float f);
  static bool& silent_instance();
  static std::mutex& global_mutex_instance() {
    static auto& mutex = *new std::mutex;
    return mutex;
  }
};

class ConsoleProgressInc : public ConsoleProgress {
 public:
  ConsoleProgressInc(int total, string task_name = "") : ConsoleProgress(std::move(task_name)), _total(total) {}
  void increment() { update(float(_counter++) / _total); }

 private:
  int _total;
  std::atomic<int> _counter{0};
};

//----------------------------------------------------------------------------

inline ConsoleProgress::ConsoleProgress(string task_name, bool set_silent)
    : _task_name(std::move(task_name)), _silent(set_silent) {
  if (silent_instance()) _silent = true;
}

inline void ConsoleProgress::update_i(float f) {
  // Notes:
  // In cmd.exe (From HTest -quick):
  // - "\a" (7) rings bell.
  // - "\t" (9) is tab (\011).
  // - "\n" (10) is usual newline.
  // - "\b" (8) erases the last character; my emacs shell is modified to do the same.
  // - "\r" (13) moves cursor to beginning of line, but does *not* clear the line contents;
  //    my emacs shell is modified to have "\r" delete backwards to the line beginning.
  int val = clamp(int(f * 100.f), 0, 99);
  if (val <= _last_val) return;
  {  // synchronize in case multiple threads are updating the object or using ConsoleProgress
    std::lock_guard<std::mutex> lock(global_mutex_instance());
    if (!(val <= _last_val)) {
      int old_val = _last_val.exchange(val);
      string str;
      if (_task_name != "") {
        if (old_val >= 0) {
          if (0) {
            str += "\r";  // bad because it could erase shell prompt
          } else {
            const int n = narrow_cast<int>(_task_name.size()) + 6;
            str.append(n, '\b');
          }
        }
        str += "#" + _task_name + ":" + sform("%02d%% ", val);
      } else {
        if (old_val < 0)
          str += "#";
        else
          str += "\b\b\b";
        str += sform("%02d%%", val);
      }
      std::cerr << str << std::flush;  // write atomically so "prog1.exe | prog2.exe" is OK.
    }
  }
}

inline void ConsoleProgress::clear() {
  if (_silent) return;
  if (_last_val < 0) return;
  {  // synchronize in case multiple threads are using ConsoleProgress
    std::lock_guard<std::mutex> lock(global_mutex_instance());
    if (!(_last_val < 0)) {
      _last_val = -1;
      string str;
      if (_task_name != "") {
        const int n = narrow_cast<int>(_task_name.size()) + 6;
        if (0) {
          str += "\r";
        } else {
          str.append(n, '\b');
        }
        // For cmd.exe console, must erase remainder of line.
        str.append(n, ' ');
        str.append(n, '\b');
      } else {
        // For cmd.exe console, must overwrite "\b" with spaces.
        str = "\b\b\b\b    \b\b\b\b";
      }
      std::cerr << str << std::flush;
    }
  }
}

inline bool& ConsoleProgress::silent_instance() {
  // Singleton pattern;
  //  https://stackoverflow.com/a/18032418
  //  https://stackoverflow.com/questions/1661529/is-meyers-implementation-of-singleton-pattern-thread-safe
  static bool silent = getenv_bool("NO_CONSOLE_PROGRESS");
  return silent;
}

// From HTest -quick:
// Real-time overhead:
//  with name:    .025 sec in cmd.exe, .060 sec in tcsh.exe.
//  without name: .012 sec in cmd.exe, .032 sec in tcsh.exe.

}  // namespace hh

#endif  // MESH_PROCESSING_LIBHH_CONSOLEPROGRESS_H_
