# Protoscope Data Model: Observation and Measurement

## 1. Purpose

The `Observation` and `Measurement` structures form the **canonical data model** of Protoscope.

Protoscope receives telemetry from heterogeneous sources such as UART, CSV, JSON, CAN, SPI/I2C, and future transport mechanisms. Each source may have a different native representation, but after acquisition and parsing, the data is converted into the same internal representation.

The relationship is:

```text
External Data Source
        |
        v
     Adapter
        |
        v
   Observation
      /   \
     /     \
Timestamp  Measurements
              |
              +--> Measurement
              +--> Measurement
              +--> ...
```

The design deliberately keeps the data model independent of the source that produced it.

---

# 2. `Measurement`

Current implementation:

```cpp
#pragma once

#include <string>

namespace protoscope {

// A single named scalar sample within an Observation.
// v0.1: value stays a plain double — no std::variant / generalized value system.
struct Measurement
{
    std::string name;
    double value;
};

} // namespace protoscope
```

## 2.1 `#pragma once`

```cpp
#pragma once
```

This prevents the header from being processed multiple times during the same compilation.

C++ headers can be indirectly included through multiple files. Without an include guard mechanism, the compiler could encounter the same type definition repeatedly and report a redefinition error.

`#pragma once` therefore provides **header inclusion protection**.

It does not restrict the number of objects that can be created from the type.

For example, it is still valid to create:

```cpp
Measurement a;
Measurement b;
Measurement c;
```

The restriction applies to repeated processing of the header, not to object creation.

---

## 2.2 `<string>`

```cpp
#include <string>
```

`Measurement` contains:

```cpp
std::string name;
```

Therefore the standard string type must be made available.

`std::string` stores the semantic identifier of the measured quantity.

Examples:

```text
"ax"
"ay"
"temperature"
"pressure"
"motor_rpm"
```

The name allows the value to remain generic instead of requiring a dedicated C++ member for every possible sensor quantity.

---

## 2.3 Namespace

```cpp
namespace protoscope {
```

The project types are placed inside the `protoscope` namespace.

Therefore the fully qualified type is:

```cpp
protoscope::Measurement
```

Using a project namespace prevents generic names from occupying the global namespace and reduces the probability of name collisions with external libraries or future project components.

---

## 2.4 `struct Measurement`

```cpp
struct Measurement
```

`Measurement` is a lightweight data structure representing one named scalar measurement.

It intentionally contains data rather than complex behavior.

Conceptually:

```text
Measurement
|
+-- name
|
+-- value
```

For example:

```text
("ax", -6.04)
```

represents:

```text
name  = "ax"
value = -6.04
```

---

## 2.5 `std::string name`

```cpp
std::string name;
```

This identifies the semantic meaning of the value.

Without the name:

```text
-6.04
```

has no reliable interpretation.

With the name:

```text
ax = -6.04
```

the value has meaning within the telemetry schema.

This is important because Protoscope is designed to support dynamic measurement sets. Different observations or sources may contain different measurement names.

---

## 2.6 `double value`

```cpp
double value;
```

This stores the numerical value associated with the measurement.

Version 0.1 intentionally uses a plain `double`.

Examples:

```text
temperature = 25.8
pressure    = 101.3
ax          = -6.04
motor_rpm   = 1450.0
```

A `double` provides a common numerical representation suitable for the current telemetry-processing goals.

A generalized type system such as:

```cpp
std::variant<double, int, bool, std::string>
```

is intentionally deferred.

This is a deliberate scope decision for Version 0.1 rather than an architectural claim that all future telemetry must be represented by `double`.

---

# 3. `Observation`

Current implementation:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "Measurement.hpp"

namespace protoscope {

// The canonical, source-agnostic telemetry record.
struct Observation
{
    // Source-native timestamp, stored as received without normalization.
    // For the ESP8266 UART source this is the raw millis() value; a general
    // clock-normalization step is deferred until a second, differently-clocked
    // source (e.g. CSV with epoch timestamps) actually exists.
    std::uint64_t timestamp;

    std::vector<Measurement> measurements;
};

} // namespace protoscope
```

## 3.1 Purpose of `Observation`

An `Observation` represents one logical telemetry record.

For the current ESP8266 + MPU6050 experiment, one transmitted line such as:

```text
58288,-19.61,-19.61,19.61
```

can eventually become:

```text
Observation
|
+-- timestamp = 58288
|
+-- measurements
      |
      +-- ("ax", -19.61)
      +-- ("ay", -19.61)
      +-- ("az", 19.61)
```

The adapter is responsible for converting the source-specific payload into this representation.

---

# 4. `<cstdint>`

```cpp
#include <cstdint>
```

This provides fixed-width integer types.

The current timestamp uses:

```cpp
std::uint64_t
```

`uint64_t` represents an unsigned integer with a width of exactly 64 bits when the implementation provides that type.

The explicit width makes the intended storage capacity clear and avoids relying on the platform-dependent size of types such as `unsigned long`.

---

# 5. `<vector>`

```cpp
#include <vector>
```

`Observation` contains:

```cpp
std::vector<Measurement> measurements;
```

A vector is used because the number of measurements is not fixed.

For example, one source may provide:

```text
ax
ay
az
```

while another may provide:

```text
temperature
pressure
flow
level
rpm
```

The Observation therefore does not need to know the number of measurements at compile time.

The vector provides a dynamically sized contiguous collection.

---

# 6. Dependency on `Measurement`

```cpp
#include "Measurement.hpp"
```

`Observation` contains actual `Measurement` objects:

```cpp
std::vector<Measurement>
```

Therefore the compiler needs the definition of `Measurement`.

The relationship between the two types is composition:

```text
Observation
    |
    +-- Measurement
    +-- Measurement
    +-- Measurement
```

An Observation is therefore a higher-level data structure composed of multiple Measurement objects.

---

# 7. `std::uint64_t timestamp`

```cpp
std::uint64_t timestamp;
```

The timestamp identifies the temporal position of the observation.

In Version 0.1, the timestamp is deliberately **source-native**.

For the current ESP8266 source:

```cpp
millis()
```

is transmitted by the device and stored without conversion.

For example:

```text
58288
```

means approximately 58.288 seconds after the ESP's timer started.

It does not represent an absolute calendar time.

The current design intentionally does not normalize timestamps from different clock domains yet.

A future version may need a dedicated timestamp normalization strategy when sources with fundamentally different timestamp semantics are combined.

---

# 8. `std::vector<Measurement> measurements`

```cpp
std::vector<Measurement> measurements;
```

This is the core of the dynamic data model.

Instead of defining:

```cpp
double ax;
double ay;
double az;
double temperature;
double pressure;
```

the Observation contains a collection:

```cpp
std::vector<Measurement>
```

This makes the number and names of measurements independent of the `Observation` type itself.

For example:

```cpp
Observation imuObservation;
```

may contain:

```text
ax
ay
az
gx
gy
gz
```

while:

```cpp
Observation temperatureObservation;
```

may contain only:

```text
temperature
```

Both are valid `Observation` objects.

---

# 9. Relationship between the two structures

The model can be summarized as:

```text
Measurement
    |
    | represents
    v
One named scalar value

Observation
    |
    | contains
    v
Timestamp + zero or more Measurements
```

Example:

```text
Observation
timestamp = 58288

measurements:
    ax          = -19.61
    ay          = -19.61
    az          =  19.61
```

---

# 10. Why this is called a Canonical Data Model

Different external sources can represent the same physical information differently.

For example:

```text
UART:
58288,-19.61,-19.61,19.61

CSV:
58288,-19.61,-19.61,19.61

JSON:
{
    "time": 58288,
    "ax": -19.61,
    "ay": -19.61,
    "az": 19.61
}
```

The external representation is different, but the internal Protoscope representation can be identical:

```text
Observation
    |
    +-- timestamp
    |
    +-- Measurements
```

This is the key abstraction boundary:

```text
Source-specific representation
            |
            v
          Adapter
            |
            v
       Observation
            |
            v
 Source-independent processing
```

Processing components therefore do not need separate logic for UART, CSV, JSON, or CAN.

---

# 11. Design decisions for Version 0.1

The current data model intentionally makes the following decisions:

### Timestamp

- Stored as `std::uint64_t`.
- Source-native timestamp is retained.
- No timestamp normalization yet.

### Measurement count

- Dynamic.
- Stored using `std::vector`.

### Measurement identity

- Stored as `std::string name`.

### Measurement value

- Stored as `double`.
- Generalized heterogeneous value types are deferred.

### Data model behavior

- `Measurement` and `Observation` are currently simple data structures.
- Acquisition, parsing, validation, and transport logic do not belong inside these structures.

---

# 12. Architectural responsibility

The data model should remain simple.

It should **not** be responsible for:

- Opening UART devices.
- Reading CSV files.
- Parsing JSON.
- Validating UART frames.
- Communicating over CAN.
- Drawing graphs.
- Performing FFT.
- Detecting anomalies.
- Storing historical datasets.

Those responsibilities belong to other layers.

The intended separation is:

```text
External Source
      |
      v
   Adapter
      |
      | parse + validate
      v
 Observation
      |
      v
 Processing
      |
      +--> Analysis
      +--> Anomaly Detection
      +--> Storage
      +--> Visualization
```

This separation keeps `Observation` and `Measurement` independent of the source and downstream processing system.

---

# 13. Summary

`Measurement` is the smallest semantic unit of Protoscope telemetry:

```cpp
struct Measurement
{
    std::string name;
    double value;
};
```

`Observation` is the canonical telemetry record:

```cpp
struct Observation
{
    std::uint64_t timestamp;
    std::vector<Measurement> measurements;
};
```

Together they provide a flexible internal representation for heterogeneous telemetry.

The fundamental design principle is:

> **Adapters understand external formats; the canonical data model represents the resulting telemetry; downstream processing operates on the canonical model rather than on source-specific formats.**
