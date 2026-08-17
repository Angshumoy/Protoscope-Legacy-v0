#include"UARTAdapter.hpp"

#include<charconv>
#include<cstring>
#include<iostream>
#include<string_view>
#include<vector>

#include<cerrno>
#include<fcntl.h>
#include<termios.h>
#include<unistd.h>

namespace protoscope {

namespace {

// Split `line` on ',' into fields that view into `line` (no allocation per
// field). Empty fields are preserved so callers can reject them, and a
// trailing comma yields a trailing empty field. Used for both the header and
// data lines so the two always agree on field boundaries.
std::vector<std::string_view> SplitFields(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            fields.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return fields;
}

// A stream line is a schema HEADER (rather than telemetry) exactly when it
// carries a column named literally "timestamp". Telemetry rows are numeric and
// never contain that token, so this is a reliable, sensor-agnostic way to tell
// a (possibly repeated) header apart from data — no line counting, no assuming
// the header is first. Full validation still happens in ParseHeader.
bool LooksLikeHeader(std::string_view line)
{
    constexpr std::string_view kTimestamp = "timestamp";
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            if (line.substr(start, i - start) == kTimestamp) {
                return true;
            }
            start = i + 1;
        }
    }
    return false;
}

// Parse an entire string_view as one number, rejecting trailing garbage,
// empty fields, and surrounding whitespace. Locale-independent (from_chars).
template<typename T>
bool ParseWhole(std::string_view sv, T& out)
{
    const char* first = sv.data();
    const char* last  = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
}

// Map a numeric baud rate to its termios speed constant.
bool BaudToSpeed(unsigned int baud, speed_t& out)
{
    switch (baud) {
        case 9600:   out = B9600;   return true;
        case 19200:  out = B19200;  return true;
        case 38400:  out = B38400;  return true;
        case 57600:  out = B57600;  return true;
        case 115200: out = B115200; return true;
        case 230400: out = B230400; return true;
        default:                    return false;
    }
}

} // namespace

UARTAdapter::UARTAdapter(std::string devicePath, unsigned int baudRate)
    : m_devicePath(std::move(devicePath)), m_baudRate(baudRate)
{
}

UARTAdapter::~UARTAdapter()
{
    Close();
}

std::string UARTAdapter::GetSourceName() const
{
    return "UART(" + m_devicePath + ")";
}

bool UARTAdapter::Open()
{
    speed_t speed{};
    if (!BaudToSpeed(m_baudRate, speed)) {
        std::cerr << "UARTAdapter: unsupported baud rate " << m_baudRate << '\n';
        return false;
    }

    // O_NONBLOCK so TryReadObservation() can report "nothing yet" instead of
    // blocking the single-threaded poll loop. O_NOCTTY: this port must not
    // become the process's controlling terminal.
    m_fd = ::open(m_devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        std::cerr << "UARTAdapter: cannot open " << m_devicePath << ": "
                  << std::strerror(errno) << '\n';
        return false;
    }

    struct termios tty{};
    if (::tcgetattr(m_fd, &tty) != 0) {
        std::cerr << "UARTAdapter: tcgetattr failed on " << m_devicePath << ": "
                  << std::strerror(errno) << '\n';
        Close();
        return false;
    }

    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    // 8N1, no hardware flow control, receiver enabled, ignore modem lines.
    tty.c_cflag &= ~PARENB;            // no parity
    tty.c_cflag &= ~CSTOPB;            // one stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;                // 8 data bits
    tty.c_cflag &= ~CRTSCTS;           // no RTS/CTS flow control
    tty.c_cflag |= (CREAD | CLOCAL);

    // Raw input: no canonical line editing, echo, or signal generation.
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);

    // No software flow control; do not translate/strip incoming bytes so the
    // '\n' framing and numeric payload arrive verbatim.
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_oflag &= ~OPOST;             // raw output
    tty.c_oflag &= ~ONLCR;

    // With O_NONBLOCK set, read() returns immediately; VMIN/VTIME = 0 keeps it
    // that way even if the descriptor's non-blocking flag is later cleared.
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(m_fd, TCSANOW, &tty) != 0) {
        std::cerr << "UARTAdapter: tcsetattr failed on " << m_devicePath << ": "
                  << std::strerror(errno) << '\n';
        Close();
        return false;
    }

    return true;
}

std::optional<Observation> UARTAdapter::TryReadObservation()
{
    if (m_fd < 0) {
        return std::nullopt;
    }

    for (;;) {
        // Consume any complete line already sitting in the buffer.
        const std::size_t newline = m_buffer.find('\n');
        if (newline != std::string::npos) {
            std::string line = m_buffer.substr(0, newline);
            m_buffer.erase(0, newline + 1);

            // ESP output is CRLF-terminated; drop the trailing CR.
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // Blank lines carry no record (framing/reset artifacts); skip
            // them silently rather than reporting them as malformed.
            if (line.empty()) {
                continue;
            }

            // A schema header may appear at ANY point in the stream, not just
            // as the first line. The sender re-announces it periodically so a
            // receiver that attached mid-stream, restarted, or reconnected can
            // (re)synchronize. Headers (re)set the schema and never yield an
            // Observation — a repeated header is a schema announcement, not a
            // data record.
            if (LooksLikeHeader(line)) {
                ParseHeader(line);   // validates + (re)sets schema, or reports
                continue;
            }

            // Data line. Until a valid header has been discovered we cannot
            // interpret it, so skip it. Emit a single notice the first time so
            // an operator whose sender started before Protoscope can see we are
            // alive and waiting for the schema, without per-line spam.
            if (!m_headerReady) {
                if (!m_warnedWaiting) {
                    std::cerr << "UARTAdapter: no schema header yet on "
                              << m_devicePath << "; ignoring data until one "
                                 "arrives\n";
                    m_warnedWaiting = true;
                }
                continue;
            }

            if (auto obs = ParseLine(line)) {
                return obs;
            }
            // Malformed complete line: ParseLine already reported it. Keep
            // scanning for the next valid line rather than failing the call.
            continue;
        }

        // No complete line buffered — pull more bytes from the port.
        char chunk[256];
        const ssize_t n = ::read(m_fd, chunk, sizeof(chunk));
        if (n > 0) {
            m_buffer.append(chunk, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return std::nullopt;              // nothing available right now
        }
        // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::nullopt;              // non-blocking: no data yet
        }
        if (errno == EINTR) {
            continue;                         // interrupted syscall, retry
        }
        std::cerr << "UARTAdapter: read error on " << m_devicePath << ": "
                  << std::strerror(errno) << '\n';
        return std::nullopt;
    }
}

bool UARTAdapter::ParseHeader(const std::string& line)
{
    const std::vector<std::string_view> fields = SplitFields(line);

    std::vector<std::string> columns;
    columns.reserve(fields.size());
    std::optional<std::size_t> timestampIndex;

    for (std::size_t i = 0; i < fields.size(); ++i) {
        const std::string_view name = fields[i];

        // A column with no name can never identify a Measurement.
        if (name.empty()) {
            std::cerr << "UARTAdapter: invalid header (empty column name at index "
                      << i << "): \"" << line << "\"\n";
            return false;
        }

        // Duplicate names would make measurement identity ambiguous. Linear
        // scan is fine for the handful of columns a telemetry line carries.
        for (const std::string& seen : columns) {
            if (name == seen) {
                std::cerr << "UARTAdapter: invalid header (duplicate column \""
                          << seen << "\"): \"" << line << "\"\n";
                return false;
            }
        }

        if (name == "timestamp") {
            timestampIndex = i;
        }
        columns.emplace_back(name);
    }

    // Without a timestamp column we cannot populate Observation::timestamp;
    // reject rather than guessing which column is time. (Routing via
    // LooksLikeHeader normally guarantees one is present; this keeps
    // ParseHeader correct even when called directly.)
    if (!timestampIndex) {
        std::cerr << "UARTAdapter: invalid header (no \"timestamp\" column): \""
                  << line << "\"\n";
        return false;
    }

    // Valid header. Re-announcements of the SAME schema are expected (the
    // sender repeats the header periodically so late/reconnecting receivers can
    // sync) and are accepted silently. Only the first discovery or a genuine
    // schema change is worth a diagnostic.
    const bool firstHeader   = !m_headerReady;
    const bool schemaChanged = firstHeader || columns != m_columns;

    m_columns        = std::move(columns);
    m_timestampIndex = *timestampIndex;
    m_headerReady    = true;

    if (schemaChanged) {
        std::cerr << "UARTAdapter: "
                  << (firstHeader ? "discovered schema " : "schema changed to ")
                  << '"' << line << "\" (timestamp @ index " << m_timestampIndex
                  << ")\n";
    }
    return true;
}

std::optional<Observation> UARTAdapter::ParseLine(const std::string& line) const
{
    const std::vector<std::string_view> fields = SplitFields(line);

    // A data line must line up with the header exactly — no more, no fewer.
    if (fields.size() != m_columns.size()) {
        std::cerr << "UARTAdapter: malformed line (expected " << m_columns.size()
                  << " fields, got " << fields.size() << "): \"" << line << "\"\n";
        return std::nullopt;
    }

    std::uint64_t timestamp = 0;
    if (!ParseWhole(fields[m_timestampIndex], timestamp)) {
        std::cerr << "UARTAdapter: malformed line (bad timestamp): \""
                  << line << "\"\n";
        return std::nullopt;
    }

    Observation obs;
    obs.timestamp = timestamp;
    obs.measurements.reserve(m_columns.size() - 1);   // every column but timestamp

    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i == m_timestampIndex) {
            continue;                                 // timestamp is not a Measurement
        }
        double value = 0.0;
        if (!ParseWhole(fields[i], value)) {
            std::cerr << "UARTAdapter: malformed line (bad " << m_columns[i]
                      << "): \"" << line << "\"\n";
            return std::nullopt;
        }
        obs.measurements.push_back(Measurement{m_columns[i], value});
    }

    return obs;
}

void UARTAdapter::Close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_buffer.clear();

    // Drop the discovered schema so a subsequent Open() re-acquires the header
    // from the stream rather than reusing a stale layout.
    m_columns.clear();
    m_timestampIndex = 0;
    m_headerReady = false;
    m_warnedWaiting = false;
}

} // namespace protoscope
