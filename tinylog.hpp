// tinylog.hpp - header-only logger for Windows, C++20+
// ASCII only. No external deps.
// Features:
// - Console and file sinks
// - Per-sink runtime level filters (console_level, file_level)
// - Background worker, thread-safe
// - std::format front end
// - Compile-time stripping with LOG_ACTIVE_LEVEL
// - Owns Windows console if console=true at init
// - Windows-native datetime formatting (GetDateFormatEx, GetTimeFormatEx)
//
// VS2022 (no CMake):
//   Debug   -> Preprocessor Definitions: LOG_ACTIVE_LEVEL=LOG_LEVEL_TRACE
//   Release -> Preprocessor Definitions: LOG_ACTIVE_LEVEL=LOG_LEVEL_OFF
//
// Usage:
//   #include "tinylog.hpp"
//   int main() {
//       tinylog::init({
//           true,                   // console
//           "app.log",              // file path
//           tinylog::Level::Debug,  // console_level
//           tinylog::Level::Trace,  // file_level
//           false,                  // utc (false = local time, true = UTC)
//           false,                  // flush_each
//           L"MM'-'dd",             // date_format (Windows pattern)
//           L"HH':'mm':'ss"         // time_format (Windows pattern)
//       });
//       LOG_I("hello {}", 123);
//       tinylog::shutdown();
//   }

#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <format>
#include <tuple>
#include <vector>

#define NOMINMAX
#include <windows.h>

namespace tinylog {
    enum class Level : int {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Critical,
        Off
    };

#define LOG_LEVEL_TRACE    0
#define LOG_LEVEL_DEBUG    1
#define LOG_LEVEL_INFO     2
#define LOG_LEVEL_WARN     3
#define LOG_LEVEL_ERROR    4
#define LOG_LEVEL_CRITICAL 5
#define LOG_LEVEL_OFF      6

#ifndef LOG_ACTIVE_LEVEL
#define LOG_ACTIVE_LEVEL LOG_LEVEL_TRACE
#endif

    // Convert wide to UTF-8 string.
    inline std::string WideToUtf8(const wchar_t* w, int len) {
        if (!w || len <= 0) return {};
        int bytes = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
        std::string out;
        out.resize(bytes);
        if (bytes > 0) {
            WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), bytes, nullptr, nullptr);
        }
        return out;
    }
    inline std::string WideToUtf8(const std::wstring& ws) {
        return WideToUtf8(ws.c_str(), static_cast<int>(ws.size()));
    }

    struct Options {
        bool        console = true;                // allocate/use console
        std::string file_path;                     // empty => no file sink
        Level       console_level = Level::Info;   // runtime min for console
        Level       file_level = Level::Trace;  // runtime min for file
        bool        utc = false;                   // true => UTC, false => local
        bool        flush_each = false;            // flush file every message
        std::wstring date_format = L"yyyy'-'MM'-'dd"; // Windows pattern
        std::wstring time_format = L"HH':'mm':'ss";    // Windows pattern
    };

    struct Msg {
        Level level;
        std::string text;  // formatted UTF-8 payload
        std::thread::id tid;
        std::chrono::system_clock::time_point tp;
    };

    class Logger {
    public:
        static Logger& instance() {
            static Logger inst;
            return inst;
        }

        void init(Options opts) {
            std::lock_guard<std::mutex> lk(m_);
            if (running_) return;
            opts_ = std::move(opts);

            auto disable_quick_edit = [] {
                HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
                if (hIn == INVALID_HANDLE_VALUE || hIn == nullptr) return;
                DWORD mode = 0;
                if (!GetConsoleMode(hIn, &mode)) return;

                mode |= ENABLE_EXTENDED_FLAGS;
                mode &= ~ENABLE_QUICK_EDIT_MODE; // disable freeze-on-select
                mode &= ~ENABLE_INSERT_MODE;     // optional: stop accidental paste
                mode |= ENABLE_MOUSE_INPUT;      // keep scrollwheel support

                SetConsoleMode(hIn, mode);
            };

            if (opts_.console) {
                if (AllocConsole()) {
                    owns_console_ = true;
                    FILE* fp = nullptr;
                    freopen_s(&fp, "CONOUT$", "w", stdout);
                    freopen_s(&fp, "CONOUT$", "w", stderr);
                    std::ios::sync_with_stdio(false);
                    std::cout.clear();
                    std::cerr.clear();
                    disable_quick_edit();
                }
                else {
                    owns_console_ = false;
                    disable_quick_edit();
                }
            }

            if (!opts_.file_path.empty()) {
                file_.emplace(opts_.file_path, std::ios::out | std::ios::app | std::ios::binary);
                if (!*file_) {
                    file_.reset();
                }
            }

            console_level_.store(static_cast<int>(opts_.console_level), std::memory_order_relaxed);
            file_level_.store(static_cast<int>(opts_.file_level), std::memory_order_relaxed);

            running_ = true;
            worker_ = std::jthread([this](std::stop_token st) { this->run(st); });
            initialized_.store(true, std::memory_order_release);

            std::atexit([] {
                if (Logger::instance().is_initialized()) {
                    Logger::instance().shutdown();
                }
            });
        }

        void shutdown() {
            std::unique_lock<std::mutex> lk(m_);
            if (!running_) return;
            running_ = false;
            lk.unlock();
            cv_.notify_all();
            if (worker_.joinable()) worker_.join();

            std::lock_guard<std::mutex> lk2(m_);
            if (file_) {
                file_->flush();
                file_->close();
                file_.reset();
            }

            if (owns_console_) {
                std::cout.flush();
                std::cerr.flush();
                FreeConsole();
                owns_console_ = false;
            }
            initialized_.store(false, std::memory_order_release);
        }

        bool is_initialized() const noexcept {
            return initialized_.load(std::memory_order_acquire);
        }

        void set_console_level(Level lvl) { console_level_.store(static_cast<int>(lvl), std::memory_order_relaxed); }
        void set_file_level(Level lvl) { file_level_.store(static_cast<int>(lvl), std::memory_order_relaxed); }

        template <class... Args>
        void logf(Level lvl, std::string_view fmt, Args&&... args) {
            if (!is_initialized()) return;

            Msg m;
            m.level = lvl;
            m.tid = std::this_thread::get_id();
            m.tp = std::chrono::system_clock::now();

            if constexpr (sizeof...(Args) == 0) {
                m.text = std::string(fmt);
            }
            else {
                auto storage = std::make_tuple(std::forward<Args>(args)...);
                m.text = std::apply(
                    [&](auto&... lrefs) {
                    return std::vformat(fmt, std::make_format_args(lrefs...));
                },
                    storage
                );
            }

            {
                std::lock_guard<std::mutex> lk(m_);
                q_.emplace_back(std::move(m));
            }
            cv_.notify_one();
        }

    private:
        Logger() = default;
        ~Logger() = default;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        static const char* level_tag(Level lvl) {
            switch (lvl) {
            case Level::Trace:    return "TRACE";
            case Level::Debug:    return "DEBUG";
            case Level::Info:     return "INFO";
            case Level::Warn:     return "WARN";
            case Level::Error:    return "ERROR";
            case Level::Critical: return "CRIT";
            default:              return "OFF";
            }
        }

        WORD get_color_for_level(Level lvl) const {
            switch (lvl) {
            case Level::Trace:    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // White/Grey
            case Level::Debug:    return FOREGROUND_GREEN | FOREGROUND_BLUE;                  // Cyan
            case Level::Info:     return FOREGROUND_GREEN;                                    // Green
            case Level::Warn:     return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Yellow
            case Level::Error:    return FOREGROUND_RED | FOREGROUND_INTENSITY;               // Bright Red
            case Level::Critical: return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // Magenta
            default:              return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            }
        }

        // Build timestamp prefix using WinAPI format strings and UTF-8 output.
        std::string make_prefix(const Msg& m) {
            SYSTEMTIME st;
            if (opts_.utc) GetSystemTime(&st);
            else           GetLocalTime(&st);

            wchar_t dateBuf[128]{};
            wchar_t timeBuf[128]{};

            // LOCALE_NAME_USER_DEFAULT ensures user locale. Patterns are Windows-style.
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st,
                opts_.date_format.empty() ? L"yyyy'-'MM'-'dd" : opts_.date_format.c_str(),
                dateBuf, static_cast<int>(std::size(dateBuf)), nullptr);

            GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st,
                opts_.time_format.empty() ? L"HH':'mm':'ss" : opts_.time_format.c_str(),
                timeBuf, static_cast<int>(std::size(timeBuf)));

            // Assemble datetime + ms in UTF-8.
            std::wstring wdt = std::wstring(dateBuf) + L" " + std::wstring(timeBuf);
            std::string dt = WideToUtf8(wdt);
            char msbuf[8];
            int n = std::snprintf(msbuf, sizeof(msbuf), ".%03u", static_cast<unsigned>(st.wMilliseconds));
            if (n < 0) n = 0;

            std::ostringstream oss;
            oss << dt << msbuf
                << " [" << level_tag(m.level) << "] "
                << "[tid " << m.tid << "] ";
            return oss.str();
        }

        void run(std::stop_token st) {
            std::deque<Msg> local;
            for (;;) {
                {
                    std::unique_lock<std::mutex> lk(m_);
                    cv_.wait(lk, [this] { return !q_.empty() || !running_; });
                    if (!running_ && q_.empty()) break;
                    local.swap(q_);
                }

                while (!local.empty()) {
                    const Msg& m = local.front();
                    std::string line = make_prefix(m) + m.text + '\n';

                    if (opts_.console && static_cast<int>(m.level) >= console_level_.load(std::memory_order_relaxed)) {
                        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                        CONSOLE_SCREEN_BUFFER_INFO csbi;
                        WORD oldColor = 0;
                        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
                            oldColor = csbi.wAttributes;
                        }

                        // Find the [LEVEL] portion
                        std::string::size_type levelStart = line.find('[');
                        std::string::size_type levelEnd = std::string::npos;
                        if (levelStart != std::string::npos) {
                            levelEnd = line.find(']', levelStart);
                        }

                        if (levelStart != std::string::npos && levelEnd != std::string::npos) {
                            std::cout.write(line.data(), levelStart);

                            SetConsoleTextAttribute(hOut, get_color_for_level(m.level));
                            std::cout.write(line.data() + levelStart, (levelEnd - levelStart + 1));

                            // Restore color
                            SetConsoleTextAttribute(hOut, oldColor);

                            std::cout.write(line.data() + levelEnd + 1, line.size() - (levelEnd + 1));
                        }
                        else {
                            // Fallback: print normally if tags not found
                            std::cout << line;
                        }
                    }
                    if (file_ && static_cast<int>(m.level) >= file_level_.load(std::memory_order_relaxed)) {
                        file_->write(line.data(), static_cast<std::streamsize>(line.size()));
                        if (opts_.flush_each) file_->flush();
                    }

                    local.pop_front();
                }

                if (st.stop_requested()) break;
            }

            if (file_) file_->flush();
            if (opts_.console) { std::cout.flush(); std::cerr.flush(); }
        }

    private:
        std::mutex m_;
        std::condition_variable cv_;
        std::deque<Msg> q_;
        std::jthread worker_;
        std::optional<std::ofstream> file_;
        Options opts_;
        std::atomic<bool> initialized_{ false };
        std::atomic<bool> running_{ false };
        std::atomic<int>  console_level_{ static_cast<int>(Level::Info) };
        std::atomic<int>  file_level_{ static_cast<int>(Level::Trace) };
        bool owns_console_ = false;
    };

    // public API
#if LOG_ACTIVE_LEVEL != LOG_LEVEL_OFF
    inline void init(const Options& opts) { Logger::instance().init(opts); }
    inline void shutdown() { Logger::instance().shutdown(); }
#else
    inline void init(const Options&) { }
    inline void shutdown() { }
#endif

    inline void set_console_level(Level lvl) { Logger::instance().set_console_level(lvl); }
    inline void set_file_level(Level lvl) { Logger::instance().set_file_level(lvl); }

    // single front-end used by macros
#if LOG_ACTIVE_LEVEL != LOG_LEVEL_OFF
    template <class... Args>
    inline void log(Level lvl, std::string_view fmt, Args&&... args) {
        Logger::instance().logf(lvl, fmt, std::forward<Args>(args)...);
    }
#else
    template <class... Args>
    inline void log(Level, std::string_view, Args&&...) { }
#endif

    // macros (compile-time stripping)
#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_TRACE
#define LOG_T(...) ::tinylog::log(::tinylog::Level::Trace, __VA_ARGS__)
#else
#define LOG_T(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_D(...) ::tinylog::log(::tinylog::Level::Debug, __VA_ARGS__)
#else
#define LOG_D(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_INFO
#define LOG_I(...) ::tinylog::log(::tinylog::Level::Info, __VA_ARGS__)
#else
#define LOG_I(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_WARN
#define LOG_W(...) ::tinylog::log(::tinylog::Level::Warn, __VA_ARGS__)
#else
#define LOG_W(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_ERROR
#define LOG_E(...) ::tinylog::log(::tinylog::Level::Error, __VA_ARGS__)
#else
#define LOG_E(...) (void)0
#endif

#if LOG_ACTIVE_LEVEL <= LOG_LEVEL_CRITICAL
#define LOG_C(...) ::tinylog::log(::tinylog::Level::Critical, __VA_ARGS__)
#else
#define LOG_C(...) (void)0
#endif
} // namespace tinylog