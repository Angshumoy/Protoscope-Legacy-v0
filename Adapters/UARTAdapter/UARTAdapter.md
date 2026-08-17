# UARTAdapter — UART Telemetry Protocol (v0.2)

`UARTAdapter` reads newline-delimited ASCII telemetry from a serial device and
converts each record into a canonical `protoscope::Observation`. It is one
concrete `IObservationSource`.

```text
GY-521 (MPU6050) --I2C--> ESP8266 --UART--> CP2102 --USB--> /dev/ttyUSB0
        --> UARTAdapter --> Observation --> main.cpp
```

The protocol is **self-describing** and **resynchronizable**. The adapter holds
no sensor-specific knowledge: it never hardcodes `ax/ay/az`, `temperature`,
GPS, or any field name or count. Everything is learned from the stream.

---

## 1. Wire format

The stream is CSV text. There are two kinds of line:

- **Header line** — names the columns, e.g. `timestamp,ax,ay,az`
- **Data line** — one value per column, e.g. `58288,-19.61,-19.61,19.61`

Lines are terminated by `\n`; a trailing `\r` (CRLF, as the ESP sends) is
stripped. Blank lines are ignored.

Example stream:

```text
timestamp,ax,ay,az        <- header (schema announcement)
1000,1.0,2.0,9.8
1500,1.1,2.1,9.7
...
timestamp,ax,ay,az        <- SAME header, re-announced periodically
10500,1.5,2.4,9.7
...
```

### The header is not assumed to be first, and is repeated

A valid header **may appear at any point in the stream**, and the sender
**re-announces it periodically** (e.g. every 20 data records). This is what
makes the stream robust to startup and reconnection ordering:

- the sender may already be transmitting before this receiver attaches;
- the receiver (Protoscope) may restart;
- the USB serial device may reconnect;
- the sender may reset independently;
- the link may drop and recover.

Because the header keeps coming back, a receiver that missed the first one only
has to wait for the next repetition to synchronize. There is no out-of-band
handshake, no configuration file, and no fixed "the first line is the header"
assumption.

---

## 2. How the adapter interprets the stream

1. **Header vs. data is decided by content, not position.** A line is treated
   as a header exactly when it carries a column named literally `timestamp`.
   Telemetry rows are numeric and never contain that token, so this reliably
   separates the two with no sensor-specific rules and no line counting.
2. **Data before any header is ignored.** Until a valid header has been seen,
   the adapter cannot interpret data, so it skips those lines (emitting a
   single "waiting for schema" notice, not per-line spam).
3. **A header is recognized wherever it appears** and (re)sets the active
   schema: the column names (in wire order) and the index of the `timestamp`
   column.
4. **A repeated header is a schema announcement, not telemetry.** No
   `Observation` is produced for a header line. Re-announcing the *same* schema
   is silent; a genuine *change* (or the first discovery) logs one line.
5. **`timestamp` is found by name**, not by assuming column 0. Any column order
   works, e.g. `temperature,timestamp,humidity`.
6. **Every non-`timestamp` column becomes a `Measurement{name, value}`.** The
   number of measurements is whatever the header declares — fully dynamic.

### Data flow

```text
UART bytes
    |
    v
line buffering        (accumulate until '\n'; strip trailing '\r')
    |
    v
header detection       (does the line carry a "timestamp" column?)
    |
    +--- header ---> schema discovery / update ---> (no Observation)
    |
    +--- data -----> generic field parsing (using the current schema)
                             |
                             v
                        Observation  ---> main.cpp
```

---

## 3. Validation (malformed input is rejected, not guessed)

**Header** is rejected (with one diagnostic, then the adapter keeps waiting for
a good one) if:

- a column name is empty;
- a column name is duplicated;
- there is no `timestamp` column.

**Data line** is rejected (one diagnostic, line skipped, stream continues) if:

- its field count does not match the current header;
- the `timestamp` field does not parse as `uint64_t`;
- any measurement field does not parse as `double`.

A rejected line never yields an invalid `Observation`; the poll loop simply
moves on to the next line.

---

## 4. Scope (v0.2)

- Timestamp is stored source-native as `std::uint64_t` (see the DataModel docs);
  no cross-source clock normalization yet.
- Measurement values are `double`; a generalized value type is deferred.
- No threads, queues, factories, configuration files, JSON, or external parsing
  libraries — a single polled reader plus a self-describing, repeatable header.

---

## 5. Adding a new sensor without touching `UARTAdapter.cpp`

Because the schema comes from the stream, a different sensor just sends a
different header. For example, a temperature node:

```text
timestamp,temperature
1000,25.4
1500,25.6
```

or a GPS node:

```text
timestamp,lat,lon,alt,fix
1000,12.9716,77.5946,915.0,1
```

both produce correct `Observation`s with no code change. The only protocol
requirement is a column named `timestamp`.
