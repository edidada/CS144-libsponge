#include "logging.hh"

#include <ctime>
#include <iomanip>
#include <iostream>

using namespace std;

Logger::Logger() : mutex_(), file_(), terminal_(true), min_level_(LogLevel::DEBUG) {}

Logger &Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::open_file(const string &path) {
    lock_guard<mutex> lock(mutex_);
    file_.close();
    if (not path.empty()) {
        file_.open(path, ios::trunc);
    }
}

void Logger::set_min_level(const LogLevel level) {
    lock_guard<mutex> lock(mutex_);
    min_level_ = level;
}

string Logger::timestamp() {
    const auto now = chrono::system_clock::now();
    const auto millis = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const time_t t = chrono::system_clock::to_time_t(now);
    tm tmv{};
    localtime_r(&t, &tmv);  // thread-safe on POSIX
    ostringstream ss;
    ss << put_time(&tmv, "%Y-%m-%d %H:%M:%S") << '.' << setw(3) << setfill('0') << millis;
    return ss.str();
}

const char *Logger::level_tag(const LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
    }
    return "?????";
}

Logger::Line::Line(Logger &logger, const LogLevel level, const char *file, const int line, const char *func)
    : logger_(logger), level_(level), file_(file), line_(line), func_(func), buffer_() {}

Logger::Line::Line(Line &&other) noexcept
    : logger_(other.logger_), level_(other.level_), file_(other.file_), line_(other.line_), func_(other.func_),
      buffer_(move(other.buffer_)) {}

Logger::Line &Logger::Line::operator<<(std::ostream &(*manip)(std::ostream &)) {
    buffer_ << manip;
    return *this;
}

Logger::Line::~Line() {
    lock_guard<mutex> lock(logger_.mutex_);
    if (level_ < logger_.min_level_) {
        return;
    }

    ostringstream record;
    record << '[' << Logger::timestamp() << "] [" << left << setw(5) << Logger::level_tag(level_) << "] "
           << "[thread " << this_thread::get_id() << "] [" << file_ << ':' << line_ << ' ' << func_ << "] "
           << buffer_.str() << '\n';

    if (logger_.terminal_) {
        cerr.write(record.str().data(), record.str().size());
    }
    if (logger_.file_.is_open()) {
        logger_.file_ << record.str();
        logger_.file_.flush();
    }
}
