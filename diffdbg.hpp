// diffdbg.hpp
//
// Copyright (c) 2026       Chrsitoph Niethammer <christoph.niethammer@gamil.com>
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Header-only differential-debugging framework.
//
// Usage:
//   1. Run your app once in "record" mode. This will a reference file.
//   2. Run the (modified) app again in "check" mode. This compares every
//      registered variable against the reference. On the first mismatch it
//      prints the location and traps into the debugger (SIGTRAP), or aborts
//      if no debugger is attached.
//
// Minimal integration:
//
//   #include "diffdbg.hpp"
//
//   int main(int argc, char** argv) {
//       diffdbg::init(diffdbg::modeFromEnv(), diffdbg::fileFromEnv());
//       ...
//       for (int step = 0; step < nSteps; ++step) {
//           ...
//           DIFFDBG_CHECK_VEC(velocity, step);
//           DIFFDBG_CHECK_VEC(pressure, step);
//       }
//       diffdbg::finalize();
//   }
//
// Then:
//   DIFFDBG_MODE=record DIFFDBG_FILE=ref.bin ./app        # reference run
//   DIFFDBG_MODE=check  DIFFDBG_FILE=ref.bin gdb ./app     # comparison run
//
// No code changes needed to switch modes between binaries/runs -- just the
// environment variables.

#pragma once

#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace diffdbg {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

enum class Mode { Off, Record, Check };

struct Config {
  Mode mode = Mode::Off;
  std::string referenceFile = "diffdbg_reference.bin";
  double relTol = 1e-9;   // relative tolerance for floating point compare
  double absTol = 1e-12;  // absolute tolerance floor
  bool breakOnMismatch = true;
  bool stopAtFirstElement = true;  // stop scanning a vector at first bad element
  bool verboseOk = false;          // log every successful check too (noisy)
};

inline Config& config() {
  static Config cfg;
  return cfg;
}

// Read mode/file from environment variables, useful so the *same* binary
// can be run once to record and once to check without recompiling.
//   DIFFDBG_MODE = off | record | check   (default: off)
//   DIFFDBG_FILE = path to reference file (default: diffdbg_reference.bin)
inline Mode modeFromEnv(const char* var = "DIFFDBG_MODE") {
  const char* v = std::getenv(var);
  if (!v) return Mode::Off;
  std::string s(v);
  if (s == "record" || s == "RECORD") return Mode::Record;
  if (s == "check" || s == "CHECK") return Mode::Check;
  return Mode::Off;
}

inline std::string fileFromEnv(const char* var = "DIFFDBG_FILE",
                               const std::string& fallback = "diffdbg_reference.bin") {
  const char* v = std::getenv(var);
  return v ? std::string(v) : fallback;
}

// ---------------------------------------------------------------------------
// Internal storage
// ---------------------------------------------------------------------------

struct Key {
  std::string label;
  long long step;
  bool operator==(const Key& o) const { return step == o.step && label == o.label; }
};

struct KeyHash {
  size_t operator()(const Key& k) const {
    size_t h1 = std::hash<std::string>()(k.label);
    size_t h2 = std::hash<long long>()(k.step);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
  }
};

struct Record {
  std::vector<unsigned char> bytes;
  size_t elemSize = 0;
  size_t count = 0;
  std::string typeName;
};

// ---------------------------------------------------------------------------
// Debugger singleton
// ---------------------------------------------------------------------------

class Debugger {
 public:
  static Debugger& instance() {
    static Debugger d;
    return d;
  }

  void init(Mode mode, const std::string& file) {
    std::lock_guard<std::mutex> lock(mutex_);
    config().mode = mode;
    config().referenceFile = file;
    mismatchCount_ = 0;
    switch (mode) {
      case Mode::Check:
        loadReference(file);
        break;
      case Mode::Record:
        records_.clear();
        break;
      case Mode::Off:
        break;
    }
    if (config().mode != Mode::Off) {
      std::cerr << "[diffdbg] mode=" << modeName(mode) << " file=" << file << "\n";
    }
  }

  // Call at program exit (or via atexit) when in Record mode.
  void finalize() {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (config().mode) {
      case Mode::Record:
        saveReference(config().referenceFile);
        std::cerr << "[diffdbg] wrote " << records_.size() << " reference entries to " << config().referenceFile
                  << "\n";
        break;
      case Mode::Check:
        std::cerr << "[diffdbg] check run finished, mismatches=" << mismatchCount_ << "\n";
        break;
      case Mode::Off:
        break;
    }
  }

  template <typename T>
  void check(const std::string& label, long long step, const T* data, size_t count) {
    if (config().mode == Mode::Off) return;

    Key key{label, step};

    if (config().mode == Mode::Record) {
      Record rec;
      rec.elemSize = sizeof(T);
      rec.count = count;
      rec.typeName = typeid(T).name();
      rec.bytes.resize(count * sizeof(T));
      if (count) std::memcpy(rec.bytes.data(), data, rec.bytes.size());
      std::lock_guard<std::mutex> lock(mutex_);
      records_[key] = std::move(rec);
      return;
    }

    // Check mode
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = reference_.find(key);
    if (it == reference_.end()) {
      std::cerr << "[diffdbg] WARNING: no reference entry for label='" << label << "' step=" << step << " (skipping)\n";
      return;
    }
    const Record& ref = it->second;
    if (ref.count != count || ref.elemSize != sizeof(T)) {
      std::cerr << "[diffdbg] MISMATCH (shape) label='" << label << "' step=" << step << " ref.count=" << ref.count
                << " cur.count=" << count << " ref.elemSize=" << ref.elemSize << " cur.elemSize=" << sizeof(T) << "\n";
      ++mismatchCount_;
      triggerBreak();
      return;
    }

    const T* refData = reinterpret_cast<const T*>(ref.bytes.data());
    size_t firstBad = static_cast<size_t>(-1);
    for (size_t i = 0; i < count; ++i) {
      if (!approxEqual(refData[i], data[i])) {
        if (firstBad == static_cast<size_t>(-1)) firstBad = i;
        std::cerr << "[diffdbg] MISMATCH label='" << label << "' step=" << step << " index=" << i
                  << " ref=" << toStr(refData[i]) << " cur=" << toStr(data[i]) << "\n";
        if (config().stopAtFirstElement) break;
      }
    }
    if (firstBad != static_cast<size_t>(-1)) {
      ++mismatchCount_;
      triggerBreak();
    } else if (config().verboseOk) {
      std::cerr << "[diffdbg] OK label='" << label << "' step=" << step << " (" << count << " elems)\n";
    }
  }

 private:
  std::mutex mutex_;
  std::unordered_map<Key, Record, KeyHash> records_;
  std::unordered_map<Key, Record, KeyHash> reference_;
  size_t mismatchCount_ = 0;

  static const char* modeName(Mode m) {
    switch (m) {
      case Mode::Off:
        return "off";
      case Mode::Record:
        return "record";
      case Mode::Check:
        return "check";
    }
    return "?";
  }

  template <typename T>
  static std::string toStr(const T& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
  }

  template <typename T>
  bool approxEqual(const T& a, const T& b) const {
    if constexpr (std::is_floating_point<T>::value) {
      double da = static_cast<double>(a);
      double db = static_cast<double>(b);
      double diff = std::fabs(da - db);
      double tol = config().absTol + config().relTol * std::fabs(da);
      return diff <= tol;
    } else {
      return a == b;
    }
  }

  void triggerBreak() {
    if (!config().breakOnMismatch) return;
    std::cerr << "[diffdbg] trapping into debugger (attach with a debugger, or run "
                 "the whole process under gdb/rr, to catch this)\n";
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__GNUC__) || defined(__clang__)
    raise(SIGTRAP);
#else
    std::abort();
#endif
  }

  static void writeString(std::ofstream& out, const std::string& s) {
    uint64_t len = s.size();
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    out.write(s.data(), static_cast<std::streamsize>(len));
  }

  static std::string readString(std::ifstream& in) {
    uint64_t len = 0;
    in.read(reinterpret_cast<char*>(&len), sizeof(len));
    std::string s(len, '\0');
    if (len) in.read(&s[0], static_cast<std::streamsize>(len));
    return s;
  }

  void saveReference(const std::string& file) {
    std::ofstream out(file, std::ios::binary);
    if (!out) {
      std::cerr << "[diffdbg] ERROR: cannot open '" << file << "' for writing\n";
      return;
    }
    uint64_t n = records_.size();
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    for (auto& [key, rec] : records_) {
      writeString(out, key.label);
      long long step = key.step;
      out.write(reinterpret_cast<const char*>(&step), sizeof(step));
      writeString(out, rec.typeName);
      uint64_t elemSize = rec.elemSize, count = rec.count;
      out.write(reinterpret_cast<const char*>(&elemSize), sizeof(elemSize));
      out.write(reinterpret_cast<const char*>(&count), sizeof(count));
      if (!rec.bytes.empty())
        out.write(reinterpret_cast<const char*>(rec.bytes.data()), static_cast<std::streamsize>(rec.bytes.size()));
    }
  }

  void loadReference(const std::string& file) {
    reference_.clear();
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      std::cerr << "[diffdbg] ERROR: cannot open reference file '" << file << "'\n";
      return;
    }
    uint64_t n = 0;
    in.read(reinterpret_cast<char*>(&n), sizeof(n));
    for (uint64_t i = 0; i < n; ++i) {
      Key key;
      key.label = readString(in);
      in.read(reinterpret_cast<char*>(&key.step), sizeof(key.step));
      Record rec;
      rec.typeName = readString(in);
      uint64_t elemSize = 0, count = 0;
      in.read(reinterpret_cast<char*>(&elemSize), sizeof(elemSize));
      in.read(reinterpret_cast<char*>(&count), sizeof(count));
      rec.elemSize = elemSize;
      rec.count = count;
      rec.bytes.resize(elemSize * count);
      if (!rec.bytes.empty())
        in.read(reinterpret_cast<char*>(rec.bytes.data()), static_cast<std::streamsize>(rec.bytes.size()));
      reference_[key] = std::move(rec);
    }
  }
};

// ---------------------------------------------------------------------------
// Free functions / public API
// ---------------------------------------------------------------------------

inline void init(Mode mode, const std::string& file = "diffdbg_reference.bin") {
  Debugger::instance().init(mode, file);
}

inline void finalize() { Debugger::instance().finalize(); }

template <typename T>
inline void checkVector(const std::string& label, long long step, const std::vector<T>& v) {
  Debugger::instance().check(label, step, v.data(), v.size());
}

template <typename T>
inline void checkArray(const std::string& label, long long step, const T* data, size_t count) {
  Debugger::instance().check(label, step, data, count);
}

template <typename T>
inline void checkScalar(const std::string& label, long long step, const T& value) {
  Debugger::instance().check(label, step, &value, 1);
}

}  // namespace diffdbg

// Convenience macros: capture the variable name as the label automatically.
#define DIFFDBG_CHECK_VEC(vec, step) diffdbg::checkVector(#vec, (step), (vec))
#define DIFFDBG_CHECK_ARR(ptr, count, step) diffdbg::checkArray(#ptr, (step), (ptr), (count))
#define DIFFDBG_CHECK_SCALAR(val, step) diffdbg::checkScalar(#val, (step), (val))
