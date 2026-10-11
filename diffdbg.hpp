// diffdbg.hpp
//
// Header-only differential-debugging framework.
//
// Usage pattern:
//   1. Run your app once in "record" mode -> writes a reference file.
//   2. Run the (optimized) app again in "check" mode -> compares every
//      registered variable against the reference. On the first mismatch it
//      prints the location and traps into the debugger (SIGTRAP), or aborts
//      if no debugger is attached.
//
// Minimal integration (flat loop):
//
//   #include "diffdbg.hpp"
//
//   int main(int argc, char** argv) {
//       diffdbg::init(diffdbg::modeFromEnv(), diffdbg::fileFromEnv(), diffdbg::strategyFromEnv());
//       ...
//       for (int step = 0; step < nSteps; ++step) {
//           ...
//           DIFFDBG_CHECK_VEC(velocity, step);
//           DIFFDBG_CHECK_VEC(pressure, step);
//       }
//       diffdbg::finalize();
//   }
//
// A single "step" isn't enough for every code shape. Two generalizations:
//
// (a) Multi-dimensional context: pass several indices instead of one.
//       DIFFDBG_CHECK_VEC(velocity, step, substep);
//       DIFFDBG_CHECK_VEC(field, step, rank, iter);
//
// (b) Scoped context (for checks buried deep in a call chain, recursive
//     solvers, or nested loops where threading the index through every
//     function signature is impractical). Push once per loop level with an
//     RAII guard; every check() below it in the call stack is automatically
//     tagged, with no extra parameters needed at the call site itself:
//
//       for (int step = 0; step < nSteps; ++step) {
//           diffdbg::ScopedContext ctxStep(step);
//           for (int substep = 0; substep < nSub; ++substep) {
//               diffdbg::ScopedContext ctxSub(substep);
//               solveInnerKernel(...);   // many frames deep, no index param
//           }
//       }
//
//       void solveInnerKernel(...) {
//           DIFFDBG_CHECK_VEC(residual);   // tagged with (step, substep) automatically
//       }
//
// Explicit indices passed to a check call are appended after whatever is
// currently on the scoped-context stack, so the two mechanisms can be mixed.
//
// Validation strategies (selected with DIFFDBG_STRATEGY at record time):
//
//   full   (default) Store every element; compare element-wise with
//          relTol/absTol for floating point. Pinpoints the first bad element.
//          Largest reference file.
//
//   first  Store/compare only the first element of each array. Tiny reference
//          file, cheap, but blind to differences in the remaining elements.
//
//   hash   Store a 64-bit FNV-1a hash of the raw bytes. Tiny reference file,
//          detects *any* difference, but the comparison is BIT-EXACT (no
//          tolerance) and cannot tell you which element differs. Best for
//          integer data or codes that are bitwise reproducible. For
//          floating-point data that legitimately differs in the last bits
//          after optimization, use `stats` or `full` instead. Note that
//          padding bytes inside structs are hashed too.
//
//   stats  Store min, max, mean (and the count of non-finite values) and
//          compare them within tolerance later. Tiny reference file, robust
//          to rounding noise, but can miss e.g. permuted elements. Only for
//          arithmetic element types; other types fall back to `hash`.
//
// The strategy is stored per entry in the reference file, so check mode
// always uses whatever strategy the reference was recorded with (a warning is
// printed if DIFFDBG_STRATEGY disagrees).
//
// Then:
//   DIFFDBG_MODE=record DIFFDBG_STRATEGY=stats DIFFDBG_FILE=ref.bin ./app   # reference run
//   DIFFDBG_MODE=check  DIFFDBG_FILE=ref.bin gdb ./app                      # comparison run
//
// No code changes needed to switch modes or strategies between binaries/runs
// -- just the environment variables:
//   DIFFDBG_MODE     = off | record | check        (default: off)
//   DIFFDBG_FILE     = path to reference file      (default: diffdbg_reference.bin)
//   DIFFDBG_STRATEGY = full | first | hash | stats (default: full)

#pragma once

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace diffdbg {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

enum class Mode { Off, Record, Check };

// Validation strategy. Values are persisted in the reference file; do not renumber.
enum class Strategy : uint32_t { Full = 0, First = 1, Hash = 2, Stats = 3 };

struct Config {
  Mode mode = Mode::Off;
  Strategy strategy = Strategy::Full;  // strategy used when *recording*
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

// Read mode/file/strategy from environment variables, useful so the *same*
// binary can be run once to record and once to check without recompiling.
//   DIFFDBG_MODE     = off | record | check          (default: off)
//   DIFFDBG_FILE     = path to reference file        (default: diffdbg_reference.bin)
//   DIFFDBG_STRATEGY = full | first | hash | stats   (default: full)
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

inline const char* strategyName(Strategy s) {
  switch (s) {
    case Strategy::Full:
      return "full";
    case Strategy::First:
      return "first";
    case Strategy::Hash:
      return "hash";
    case Strategy::Stats:
      return "stats";
  }
  return "?";
}

inline Strategy strategyFromEnv(const char* var = "DIFFDBG_STRATEGY", Strategy fallback = Strategy::Full) {
  const char* v = std::getenv(var);
  if (!v) return fallback;
  std::string s(v);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s == "full") return Strategy::Full;
  if (s == "first") return Strategy::First;
  if (s == "hash") return Strategy::Hash;
  if (s == "stats") return Strategy::Stats;
  std::cerr << "[diffdbg] WARNING: unknown " << var << "='" << v << "', using '" << strategyName(fallback)
            << "' (valid: full, first, hash, stats)\n";
  return fallback;
}

// ---------------------------------------------------------------------------
// Internal storage
// ---------------------------------------------------------------------------

// A key is a label plus an arbitrary-length tuple of integer indices, e.g.
// {} for a one-off checkpoint, {step} for a flat loop, {step, substep},
// {step, rank, iter}, etc. Two checks are the "same" registration point iff
// label and the full index tuple match exactly.
struct Key {
  std::string label;
  std::vector<long long> idx;
  bool operator==(const Key& o) const { return idx == o.idx && label == o.label; }
};

struct KeyHash {
  size_t operator()(const Key& k) const {
    size_t h = std::hash<std::string>()(k.label);
    for (long long v : k.idx) {
      size_t hv = std::hash<long long>()(v);
      h ^= hv + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
  }
};

inline std::string idxToStr(const std::vector<long long>& idx) {
  std::ostringstream oss;
  oss << "(";
  for (size_t i = 0; i < idx.size(); ++i) {
    if (i) oss << ",";
    oss << idx[i];
  }
  oss << ")";
  return oss.str();
}

// ---------------------------------------------------------------------------
// Scoped context stack
//
// Lets nested loops (or recursive calls) tag every check() beneath them in
// the call stack without passing indices explicitly at each call site.
// Thread-local so multi-threaded codes (e.g. one context stack per OpenMP
// thread) don't collide.
// ---------------------------------------------------------------------------

inline std::vector<long long>& contextStack() {
  thread_local std::vector<long long> stack;
  return stack;
}

class ScopedContext {
 public:
  explicit ScopedContext(long long value) { contextStack().push_back(value); }
  ~ScopedContext() { contextStack().pop_back(); }
  ScopedContext(const ScopedContext&) = delete;
  ScopedContext& operator=(const ScopedContext&) = delete;
};

// One checkpoint. `bytes` is the strategy-specific payload:
//   Full  : count * elemSize raw bytes
//   First : elemSize raw bytes (empty if count == 0)
//   Hash  : 8 bytes (uint64_t FNV-1a hash of the raw data)
//   Stats : sizeof(StatsPayload) bytes
// `elemSize` and `count` always describe the *original* array so the shape
// can be verified for every strategy.
struct Record {
  Strategy strategy = Strategy::Full;
  std::vector<unsigned char> bytes;
  size_t elemSize = 0;
  size_t count = 0;
  std::string typeName;
};

// Summary statistics of an array of arithmetic values. Non-finite values
// (NaN/Inf) are excluded from min/max/mean and only counted.
struct StatsPayload {
  double min = 0.0;
  double max = 0.0;
  double mean = 0.0;
  uint64_t nonFinite = 0;
};

// ---------------------------------------------------------------------------
// Helpers for the strategies
// ---------------------------------------------------------------------------

// 64-bit FNV-1a over raw bytes.
inline uint64_t fnv1a64(const void* data, size_t nBytes) {
  const unsigned char* p = static_cast<const unsigned char*>(data);
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < nBytes; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

inline std::string hex64(uint64_t v) {
  std::ostringstream oss;
  oss << "0x" << std::hex << std::setw(16) << std::setfill('0') << v;
  return oss.str();
}

template <typename T>
StatsPayload computeStats(const T* data, size_t count) {
  StatsPayload s;
  if constexpr (std::is_arithmetic<T>::value) {
    long double sum = 0.0L;
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
      double v = static_cast<double>(data[i]);
      if (!std::isfinite(v)) {
        ++s.nonFinite;
        continue;
      }
      if (n == 0) {
        s.min = s.max = v;
      } else {
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
      }
      sum += v;
      ++n;
    }
    s.mean = n ? static_cast<double>(sum / static_cast<long double>(n)) : 0.0;
  }
  return s;
}

// Tolerance comparison of two doubles; `scale` is the magnitude the relative
// tolerance is applied to.
inline bool closeEnough(double a, double b, double scale) {
  if (a == b) return true;  // also handles equal infinities
  if (std::isnan(a) && std::isnan(b)) return true;
  double tol = config().absTol + config().relTol * std::fabs(scale);
  return std::fabs(a - b) <= tol;
}

// ---------------------------------------------------------------------------
// Debugger singleton
// ---------------------------------------------------------------------------

class Debugger {
 public:
  static Debugger& instance() {
    static Debugger d;
    return d;
  }

  void init(Mode mode, const std::string& file, Strategy strategy) {
    std::lock_guard<std::mutex> lock(mutex_);
    config().mode = mode;
    config().referenceFile = file;
    config().strategy = strategy;
    mismatchCount_ = 0;
    strategyMismatchWarned_ = false;
    if (mode == Mode::Check) {
      loadReference(file);
    } else if (mode == Mode::Record) {
      records_.clear();
    }
    if (config().mode != Mode::Off) {
      std::cerr << "[diffdbg] mode=" << modeName(mode) << " file=" << file;
      if (mode == Mode::Record) std::cerr << " strategy=" << strategyName(strategy);
      std::cerr << "\n";
    }
  }

  // Call at program exit (or via atexit) when in Record mode.
  void finalize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config().mode == Mode::Record) {
      saveReference(config().referenceFile);
      std::cerr << "[diffdbg] wrote " << records_.size() << " reference entries to " << config().referenceFile
                << " (strategy=" << strategyName(config().strategy) << ")\n";
    } else if (config().mode == Mode::Check) {
      std::cerr << "[diffdbg] check run finished, mismatches=" << mismatchCount_ << "\n";
    }
  }

  template <typename T>
  void check(const std::string& label, const std::vector<long long>& idx, const T* data, size_t count) {
    if (config().mode == Mode::Off) return;

    Key key{label, idx};
    std::string idxStr = idxToStr(idx);

    if (config().mode == Mode::Record) {
      Record rec = makeRecord(data, count);
      std::lock_guard<std::mutex> lock(mutex_);
      records_[key] = std::move(rec);
      return;
    }

    // Check mode
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = reference_.find(key);
    if (it == reference_.end()) {
      std::cerr << "[diffdbg] WARNING: no reference entry for label='" << label << "' idx=" << idxStr
                << " (skipping)\n";
      return;
    }
    const Record& ref = it->second;

    if (ref.strategy != config().strategy && !strategyMismatchWarned_) {
      std::cerr << "[diffdbg] NOTE: reference was recorded with strategy='" << strategyName(ref.strategy)
                << "'; using that for checking (DIFFDBG_STRATEGY='" << strategyName(config().strategy)
                << "' only applies to record runs)\n";
      strategyMismatchWarned_ = true;
    }

    // The shape is stored for every strategy, so always verify it first.
    if (ref.count != count || ref.elemSize != sizeof(T)) {
      std::cerr << "[diffdbg] MISMATCH (shape) label='" << label << "' idx=" << idxStr << " ref.count=" << ref.count
                << " cur.count=" << count << " ref.elemSize=" << ref.elemSize << " cur.elemSize=" << sizeof(T) << "\n";
      ++mismatchCount_;
      triggerBreak();
      return;
    }

    bool bad = false;
    switch (ref.strategy) {
      case Strategy::Full:
        bad = compareElements(label, idxStr, reinterpret_cast<const T*>(ref.bytes.data()), data, count);
        break;
      case Strategy::First:
        bad = compareElements(label, idxStr, reinterpret_cast<const T*>(ref.bytes.data()), data,
                              count ? size_t(1) : size_t(0));
        break;
      case Strategy::Hash:
        bad = compareHash(label, idxStr, ref, data, count);
        break;
      case Strategy::Stats:
        bad = compareStats(label, idxStr, ref, data, count);
        break;
    }

    if (bad) {
      ++mismatchCount_;
      triggerBreak();
    } else if (config().verboseOk) {
      std::cerr << "[diffdbg] OK label='" << label << "' idx=" << idxStr << " (" << count << " elems, "
                << strategyName(ref.strategy) << ")\n";
    }
  }

  size_t mismatchCount() const { return mismatchCount_; }

 private:
  std::mutex mutex_;
  std::unordered_map<Key, Record, KeyHash> records_;
  std::unordered_map<Key, Record, KeyHash> reference_;
  size_t mismatchCount_ = 0;
  bool strategyMismatchWarned_ = false;

  static constexpr char kMagic[8] = {'D', 'I', 'F', 'F', 'D', 'B', 'G', '2'};

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

  // ---- Record construction (record mode) ---------------------------------

  // `stats` only makes sense for arithmetic types; anything else (structs,
  // enums of class type, ...) falls back to `hash`.
  template <typename T>
  Strategy effectiveStrategy() const {
    Strategy s = config().strategy;
    if (s == Strategy::Stats && !std::is_arithmetic<T>::value) {
      static bool warned = false;
      if (!warned) {
        std::cerr << "[diffdbg] WARNING: strategy 'stats' needs arithmetic element types; falling back to "
                     "'hash' for type "
                  << typeid(T).name() << "\n";
        warned = true;
      }
      return Strategy::Hash;
    }
    return s;
  }

  template <typename T>
  Record makeRecord(const T* data, size_t count) const {
    Record rec;
    rec.elemSize = sizeof(T);
    rec.count = count;
    rec.typeName = typeid(T).name();
    rec.strategy = effectiveStrategy<T>();

    switch (rec.strategy) {
      case Strategy::Full:
        rec.bytes.resize(count * sizeof(T));
        if (count) std::memcpy(rec.bytes.data(), data, rec.bytes.size());
        break;
      case Strategy::First:
        if (count) {
          rec.bytes.resize(sizeof(T));
          std::memcpy(rec.bytes.data(), data, sizeof(T));
        }
        break;
      case Strategy::Hash: {
        uint64_t h = fnv1a64(data, count * sizeof(T));
        rec.bytes.resize(sizeof(h));
        std::memcpy(rec.bytes.data(), &h, sizeof(h));
        break;
      }
      case Strategy::Stats: {
        StatsPayload s = computeStats(data, count);
        rec.bytes.resize(sizeof(s));
        std::memcpy(rec.bytes.data(), &s, sizeof(s));
        break;
      }
    }
    return rec;
  }

  // ---- Comparison (check mode). Each returns true on MISMATCH ------------

  // Element-wise comparison of the first `n` elements (used by Full and First).
  template <typename T>
  bool compareElements(const std::string& label, const std::string& idxStr, const T* refData, const T* data,
                       size_t n) const {
    bool bad = false;
    for (size_t i = 0; i < n; ++i) {
      if (!approxEqual(refData[i], data[i])) {
        bad = true;
        std::cerr << "[diffdbg] MISMATCH label='" << label << "' idx=" << idxStr << " index=" << i
                  << " ref=" << toStr(refData[i]) << " cur=" << toStr(data[i]) << "\n";
        if (config().stopAtFirstElement) break;
      }
    }
    return bad;
  }

  template <typename T>
  bool compareHash(const std::string& label, const std::string& idxStr, const Record& ref, const T* data,
                   size_t count) const {
    uint64_t refHash = 0;
    if (ref.bytes.size() != sizeof(refHash)) {
      std::cerr << "[diffdbg] ERROR: corrupt hash entry for label='" << label << "' idx=" << idxStr << "\n";
      return true;
    }
    std::memcpy(&refHash, ref.bytes.data(), sizeof(refHash));
    uint64_t curHash = fnv1a64(data, count * sizeof(T));
    if (refHash == curHash) return false;
    std::cerr << "[diffdbg] MISMATCH (hash) label='" << label << "' idx=" << idxStr << " ref=" << hex64(refHash)
              << " cur=" << hex64(curHash) << " (" << count
              << " elems; re-record with DIFFDBG_STRATEGY=full to locate the element)\n";
    return true;
  }

  template <typename T>
  bool compareStats(const std::string& label, const std::string& idxStr, const Record& ref, const T* data,
                    size_t count) const {
    StatsPayload r;
    if (ref.bytes.size() != sizeof(r)) {
      std::cerr << "[diffdbg] ERROR: corrupt stats entry for label='" << label << "' idx=" << idxStr << "\n";
      return true;
    }
    std::memcpy(&r, ref.bytes.data(), sizeof(r));
    StatsPayload c = computeStats(data, count);

    bool bad = false;
    auto cmp = [&](const char* what, double a, double b, double scale) {
      if (!closeEnough(a, b, scale)) {
        bad = true;
        std::cerr << "[diffdbg] MISMATCH (stats) label='" << label << "' idx=" << idxStr << " " << what
                  << " ref=" << std::setprecision(17) << a << " cur=" << b << "\n";
      }
    };
    // The mean is sensitive to summation order, so scale its tolerance by the
    // largest magnitude in the data rather than by the (possibly ~0) mean.
    double scale = std::max(std::fabs(r.min), std::fabs(r.max));
    cmp("min", r.min, c.min, r.min);
    cmp("max", r.max, c.max, r.max);
    cmp("mean", r.mean, c.mean, scale);
    if (r.nonFinite != c.nonFinite) {
      bad = true;
      std::cerr << "[diffdbg] MISMATCH (stats) label='" << label << "' idx=" << idxStr
                << " non-finite count ref=" << r.nonFinite << " cur=" << c.nonFinite << "\n";
    }
    return bad;
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

  // ---- Reference file I/O ------------------------------------------------
  //
  // Layout:
  //   char[8]  magic "DIFFDBG2"
  //   u64      number of records
  //   per record:
  //     string label            (u64 length + bytes)
  //     u64 nIdx, i64[nIdx]     index tuple
  //     string typeName
  //     u32 strategy
  //     u64 elemSize, u64 count (shape of the original array)
  //     u64 payloadBytes, payload

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
    out.write(kMagic, sizeof(kMagic));
    uint64_t n = records_.size();
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    for (auto& kv : records_) {
      const Key& key = kv.first;
      const Record& rec = kv.second;
      writeString(out, key.label);
      uint64_t nIdx = key.idx.size();
      out.write(reinterpret_cast<const char*>(&nIdx), sizeof(nIdx));
      if (nIdx)
        out.write(reinterpret_cast<const char*>(key.idx.data()),
                  static_cast<std::streamsize>(nIdx * sizeof(long long)));
      writeString(out, rec.typeName);
      uint32_t strat = static_cast<uint32_t>(rec.strategy);
      out.write(reinterpret_cast<const char*>(&strat), sizeof(strat));
      uint64_t elemSize = rec.elemSize, count = rec.count, payload = rec.bytes.size();
      out.write(reinterpret_cast<const char*>(&elemSize), sizeof(elemSize));
      out.write(reinterpret_cast<const char*>(&count), sizeof(count));
      out.write(reinterpret_cast<const char*>(&payload), sizeof(payload));
      if (payload) out.write(reinterpret_cast<const char*>(rec.bytes.data()), static_cast<std::streamsize>(payload));
    }
  }

  void loadReference(const std::string& file) {
    reference_.clear();
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      std::cerr << "[diffdbg] ERROR: cannot open reference file '" << file << "'\n";
      return;
    }
    char magic[sizeof(kMagic)] = {};
    in.read(magic, sizeof(magic));
    if (!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
      std::cerr << "[diffdbg] ERROR: '" << file
                << "' is not a diffdbg reference file (or was written by an older, incompatible version)\n";
      return;
    }
    uint64_t n = 0;
    in.read(reinterpret_cast<char*>(&n), sizeof(n));
    for (uint64_t i = 0; i < n && in; ++i) {
      Key key;
      key.label = readString(in);
      uint64_t nIdx = 0;
      in.read(reinterpret_cast<char*>(&nIdx), sizeof(nIdx));
      key.idx.resize(nIdx);
      if (nIdx)
        in.read(reinterpret_cast<char*>(key.idx.data()), static_cast<std::streamsize>(nIdx * sizeof(long long)));
      Record rec;
      rec.typeName = readString(in);
      uint32_t strat = 0;
      in.read(reinterpret_cast<char*>(&strat), sizeof(strat));
      rec.strategy = static_cast<Strategy>(strat);
      uint64_t elemSize = 0, count = 0, payload = 0;
      in.read(reinterpret_cast<char*>(&elemSize), sizeof(elemSize));
      in.read(reinterpret_cast<char*>(&count), sizeof(count));
      in.read(reinterpret_cast<char*>(&payload), sizeof(payload));
      rec.elemSize = elemSize;
      rec.count = count;
      rec.bytes.resize(payload);
      if (payload) in.read(reinterpret_cast<char*>(rec.bytes.data()), static_cast<std::streamsize>(payload));
      reference_[key] = std::move(rec);
    }
    if (!in) std::cerr << "[diffdbg] WARNING: reference file '" << file << "' appears truncated\n";
  }
};

// ---------------------------------------------------------------------------
// Free functions / public API
// ---------------------------------------------------------------------------

// `strategy` defaults to DIFFDBG_STRATEGY from the environment, so existing
// call sites `init(modeFromEnv(), fileFromEnv())` pick it up automatically.
// It is only used when recording; check runs use the strategy stored in the
// reference file.
inline void init(Mode mode, const std::string& file = "diffdbg_reference.bin",
                 Strategy strategy = strategyFromEnv()) {
  Debugger::instance().init(mode, file, strategy);
}

inline void finalize() { Debugger::instance().finalize(); }

// Builds the full index tuple for a check: whatever is currently on the
// scoped-context stack (outer loops/RAII guards), followed by any indices
// given explicitly at the call site. Either side can be empty.
template <typename... Indices>
inline std::vector<long long> makeIdx(Indices... explicitIdx) {
  std::vector<long long> idx = contextStack();  // copy
  (idx.push_back(static_cast<long long>(explicitIdx)), ...);
  return idx;
}

template <typename T, typename... Indices>
inline void checkVector(const std::string& label, const std::vector<T>& v, Indices... idx) {
  Debugger::instance().check(label, makeIdx(idx...), v.data(), v.size());
}

template <typename T, typename... Indices>
inline void checkArray(const std::string& label, const T* data, size_t count, Indices... idx) {
  Debugger::instance().check(label, makeIdx(idx...), data, count);
}

template <typename T, typename... Indices>
inline void checkScalar(const std::string& label, const T& value, Indices... idx) {
  Debugger::instance().check(label, makeIdx(idx...), &value, 1);
}

}  // namespace diffdbg

// Convenience macros: capture the variable name as the label automatically.
// Trailing indices are optional and variadic:
//   DIFFDBG_CHECK_VEC(velocity)                 // just whatever's on the scope stack
//   DIFFDBG_CHECK_VEC(velocity, step)            // + one explicit index
//   DIFFDBG_CHECK_VEC(velocity, step, substep)    // + several explicit indices
#define DIFFDBG_CHECK_VEC(vec, ...) diffdbg::checkVector(#vec, (vec), ##__VA_ARGS__)
#define DIFFDBG_CHECK_ARR(ptr, count, ...) diffdbg::checkArray(#ptr, (ptr), (count), ##__VA_ARGS__)
#define DIFFDBG_CHECK_SCALAR(val, ...) diffdbg::checkScalar(#val, (val), ##__VA_ARGS__)

// RAII helper macro for scoped context, with a unique variable name per call
// site so multiple scopes can be opened in the same block if needed.
#define DIFFDBG_SCOPE_CONCAT_(a, b) a##b
#define DIFFDBG_SCOPE_CONCAT(a, b) DIFFDBG_SCOPE_CONCAT_(a, b)
#define DIFFDBG_SCOPE(value) \
  diffdbg::ScopedContext DIFFDBG_SCOPE_CONCAT(diffdbg_scope_, __LINE__)(static_cast<long long>(value))
