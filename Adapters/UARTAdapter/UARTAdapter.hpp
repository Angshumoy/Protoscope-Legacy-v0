// UARTAdapter
//
// Reads newline-delimited ASCII telemetry from a serial (UART) device and
// converts each complete line into a canonical protoscope::Observation.
//
// Wire format (v0.2): a self-describing, resynchronizable CSV stream.
//   A HEADER line names the columns, e.g.
//       "timestamp,ax,ay,az"
//   and DATA lines carry one value per column, e.g.
//       "58288,-19.61,-19.61,19.61"
//
//   The header is NOT assumed to be the first line. A valid header may appear
//   at any point in the stream, and the sender re-announces it periodically:
//
//       timestamp,ax,ay,az   <- schema announcement
//       1000,1.0,2.0,9.8
//       ...
//       timestamp,ax,ay,az   <- same schema, re-announced
//       10500,1.5,2.4,9.7
//
//   This makes the stream robust to startup/reconnection ordering: the sender
//   may already be running before this receiver attaches, the receiver may
//   restart, or the USB serial device may reconnect. The adapter ignores
//   data-looking lines until it has seen a header, recognizes a header
//   wherever it appears, and treats a repeated header as a schema
//   announcement (no Observation is produced for it).
//
//   The header must contain a column literally named "timestamp" (in any
//   position); that column supplies Observation::timestamp and every other
//   column becomes a Measurement{name, value}. The adapter holds no
//   sensor-specific knowledge — a "timestamp,temperature,humidity" stream, or
//   any column order, works with no code change.
//
// This is the only telemetry leg implemented right now. It deliberately
// avoids threads, queues, and any schema/config machinery — a single polled
// reader plus a self-describing, repeatable header is all this needs.

#pragma once

#include<cstddef>
#include<cstdint>
#include<optional>
#include<string>
#include<vector>

#include"IObservationSource.hpp"
#include"Observation.hpp"

namespace protoscope {

class UARTAdapter final : public IObservationSource {           // UARTAdapter inherits from IObservationSource
    // final means: Nobody is allowed to inherit from UARTAdapter.
    public:
        // devicePath: POSIX device node, e.g. "/dev/ttyUSB0".
        // baudRate:   line speed; must be one of the standard rates handled in
        //             Open() (defaults to 115200).
        explicit UARTAdapter(std::string devicePath, unsigned int baudRate = 115200);
        ~UARTAdapter() override;

        std::string GetSourceName() const override;

        // Opens and configures the serial port (8N1, raw, non-blocking).
        bool Open() override;

        // Returns the next complete, well-formed Observation, or nullopt when
        // none is currently buffered/available. Complete-but-malformed lines
        // are rejected (one diagnostic to stderr) and skipped; incomplete
        // trailing bytes are retained until a newline completes them.
        std::optional<Observation> TryReadObservation() override;

        void Close() override;

    private:
        // Validates one line as a schema header and, on success, (re)stores the
        // column names + the "timestamp" index and marks the schema ready.
        // Returns false (after one diagnostic) for a malformed header — empty
        // column name, duplicate column, or no "timestamp" column. A repeated
        // header carrying the same schema is accepted silently; a first
        // discovery or genuine change emits one informational line.
        bool ParseHeader(const std::string& line);

        // Parses one complete data line into an Observation using the header
        // acquired earlier. Returns nullopt (after emitting one diagnostic)
        // when the line is structurally malformed. No field order or column
        // name is hardcoded here — everything comes from m_columns.
        std::optional<Observation> ParseLine(const std::string& line) const;

        std::string  m_devicePath;
        unsigned int m_baudRate;
        int          m_fd = -1;   // POSIX file descriptor; -1 when closed. fd means: File Descriptor
        std::string  m_buffer;    // Accumulates bytes until a newline arrives.

        // --- Schema, discovered from a header line at runtime (see file header) ---
        std::vector<std::string> m_columns;          // column names, in wire order
        std::size_t              m_timestampIndex = 0; // index of "timestamp" within m_columns
        bool                     m_headerReady = false; // true once a valid header is parsed
        bool                     m_warnedWaiting = false;// one-shot "ignoring data, no header yet" notice
};

} // namespace protoscope
