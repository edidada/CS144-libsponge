#include "logging.hh"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>

using namespace std;

Logger::Logger()
    : mutex_(), file_(), path_(), max_size_(512 * 1024), max_lines_(3000), line_count_(0), terminal_(true),
      min_level_(LogLevel::DEBUG) {}

Logger &Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::open_file(const string &path, const size_t max_size_bytes, const size_t max_lines) {
    lock_guard<mutex> lock(mutex_);
    file_.close();
    path_ = path;
    max_size_ = max_size_bytes;
    max_lines_ = max_lines;
    line_count_ = 0;
    if (not path_.empty()) {
        const auto parent = filesystem::path(path_).parent_path();
        if (not parent.empty()) {
            filesystem::create_directories(parent);
        }
        file_.open(path_, ios::trunc);
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

string Logger::compact_timestamp() {
    const auto now = chrono::system_clock::now();
    const auto millis = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const time_t t = chrono::system_clock::to_time_t(now);
    tm tmv{};
    localtime_r(&t, &tmv);  // thread-safe on POSIX
    ostringstream ss;
    ss << put_time(&tmv, "%Y%m%d-%H%M%S") << '-' << setw(3) << setfill('0') << millis;
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

bool Logger::file_exists(const string &name) {
    ifstream probe(name);
    return probe.good();
}

void Logger::rotate_locked() {
    if (not file_.is_open() or path_.empty()) {
        return;
    }
    file_.close();

    // archive as <path>.<YYYYMMDD-HHMMSS.mmm>; append .N if the name is taken
    string archived = path_ + "." + compact_timestamp();
    for (int seq = 2; file_exists(archived); ++seq) {
        archived = path_ + "." + compact_timestamp() + "." + to_string(seq);
    }
    rename(path_.c_str(), archived.c_str());

    line_count_ = 0;
    file_.open(path_, ios::trunc);
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

        // rotate when the active file is too large or too long
        ++logger_.line_count_;
        const auto size_ok = logger_.file_.tellp() >= 0 && logger_.file_.tellp() < static_cast<streamoff>(logger_.max_size_);
        if (not size_ok or logger_.line_count_ >= logger_.max_lines_) {
            logger_.rotate_locked();
        }
    }
}
