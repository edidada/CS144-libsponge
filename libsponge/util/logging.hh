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
class Logger {
  public:
    //! Global singleton.
    static Logger &get();

    //! Open (truncating) the given log file; empty path closes any file.
    //! Terminal output stays enabled regardless.
    void open_file(const std::string &path);

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
    static const char *level_tag(const LogLevel level);

    std::mutex mutex_;
    std::ofstream file_;
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
