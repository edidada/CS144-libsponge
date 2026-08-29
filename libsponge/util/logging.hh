#ifndef SPONGE_LIBSPONGE_LOGGING_HH
#define SPONGE_LIBSPONGE_LOGGING_HH

#include <chrono>
#include <fstream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>

//! Log severity levels. Higher value = more severe.
enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

//! Industrial-grade logger shared by every app and library component.
//!
//! Every log line is emitted as a single, atomically-written record:
//!   [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL ] [thread 0x....] [file.cc:LINE function()] message
//!
//! Features:
//!  - millisecond-resolution timestamps
//!  - source location (file, line and full function signature incl. class) captured via macros
//!  - thread id, thread-safe (a mutex protects each line's assembly and write)
//!  - severity levels with a runtime filter
//!  - writes to an optional log file and always mirrors to stderr
//!  - log rotation: once the active file exceeds `max_size_bytes` or `max_lines`,
//!    it is archived as `<path>.<YYYYMMDD-HHMMSS.mmm>` and a fresh file is started
class Logger {
  public:
    //! Global singleton.
    static Logger &get();

    //! Open (truncating) the given log file; empty path closes any file.
    //! The file is rotated (archived under a timestamped name) once it grows past
    //! `max_size_bytes` bytes or `max_lines` lines. Terminal output stays enabled.
    void open_file(const std::string &path,
                   const size_t max_size_bytes = 512 * 1024,
                   const size_t max_lines = 3000);

    //! Set the minimum severity that will be emitted.
    void set_min_level(const LogLevel level);

    //! \brief Stream-like log record. Construct via the LOG_* macros, append
    //! with <<, and the complete line is flushed on destruction.
    class Line {
      public:
        Line(Logger &logger, const LogLevel level, const char *file, const int line, const char *func);
        Line(Line &&other) noexcept;
        Line(const Line &) = delete;
        Line &operator=(const Line &) = delete;
        ~Line();

        //! Append any streamable value to the message.
        template <typename T>
        Line &operator<<(const T &value) {
            buffer_ << value;
            return *this;
        }

        //! Accept stream manipulators (std::endl, std::flush, ...); ignored
        //! structurally since a Line is always terminated by its own newline.
        Line &operator<<(std::ostream &(*manip)(std::ostream &));

      private:
        Logger &logger_;
        const LogLevel level_;
        const char *file_;
        const int line_;
        const char *func_;
        std::ostringstream buffer_;
    };

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

  private:
    Logger();
    friend class Line;

    static std::string timestamp();
    static std::string compact_timestamp();
    static const char *level_tag(const LogLevel level);
    static bool file_exists(const std::string &name);

    //! Archive the current file under a timestamped name and start a fresh one.
    //! Must be called with mutex_ already held.
    void rotate_locked();

    std::mutex mutex_;
    std::ofstream file_;
    std::string path_;
    size_t max_size_{512 * 1024};
    size_t max_lines_{3000};
    size_t line_count_{0};
    bool terminal_{true};
    LogLevel min_level_{LogLevel::DEBUG};
};

//! Emit a DEBUG-level log record: LOG_DEBUG << "value = " << x;
#define LOG_DEBUG Logger::Line(Logger::get(), LogLevel::DEBUG, __FILE__, __LINE__, __PRETTY_FUNCTION__)
//! Emit an INFO-level log record.
#define LOG_INFO Logger::Line(Logger::get(), LogLevel::INFO, __FILE__, __LINE__, __PRETTY_FUNCTION__)
//! Emit a WARN-level log record.
#define LOG_WARN Logger::Line(Logger::get(), LogLevel::WARN, __FILE__, __LINE__, __PRETTY_FUNCTION__)
//! Emit an ERROR-level log record.
#define LOG_ERROR Logger::Line(Logger::get(), LogLevel::ERROR, __FILE__, __LINE__, __PRETTY_FUNCTION__)

#endif  // SPONGE_LIBSPONGE_LOGGING_HH
