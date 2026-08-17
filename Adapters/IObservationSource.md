# IObservationSource — Layman's Notes

## 1. What is `IObservationSource`?

`IObservationSource` is the **common rulebook for anything that brings data into Protoscope**.

Protoscope may receive data from:

- UART
- CSV
- JSON
- CAN
- SPI/I2C
- ZeroMQ
- Other future sources

Each source works differently, but Protoscope wants all of them to eventually produce the same thing:

```text
Observation
```

So instead of teaching the rest of Protoscope how every protocol works, we give every source the same basic set of operations.

```text
                 IObservationSource
                        |
          +-------------+-------------+
          |             |             |
          v             v             v
     UARTAdapter    CSVAdapter    JSONAdapter
          |             |             |
          +-------------+-------------+
                        |
                        v
                   Observation
```

The interface is basically saying:

> "I don't care where your data comes from. If you want to be a data source for Protoscope, you must know how to identify yourself, open yourself, provide observations, and close yourself."

---

# 2. Why do we need this?

Imagine we didn't have `IObservationSource`.

The main program might need to know:

```text
If UART:
    read UART
    parse UART
    create Observation

If CSV:
    open file
    read row
    parse CSV
    create Observation

If JSON:
    parse JSON
    create Observation

If CAN:
    read CAN frame
    parse frame
    create Observation
```

The main program would become full of protocol-specific code.

That makes the project difficult to extend.

Instead, Protoscope can work with the common interface:

```text
Source
  |
  v
IObservationSource
  |
  v
Observation
```

The adapter handles the messy source-specific details.

The rest of Protoscope only deals with `Observation`.

---

# 3. Why is it called an Interface?

C++ does not have a separate `interface` keyword like some other languages.

Instead, we create an **abstract class**.

The class contains functions that derived classes must implement.

For example:

```cpp
virtual bool Open() = 0;
```

The `= 0` means:

> "I am not providing the implementation here. The child/derived class must provide it."

So:

```text
IObservationSource
        |
        | gives rules
        v
UARTAdapter
        |
        | implements rules
        v
UART-specific code
```

---

# 4. The four main functions

The current interface provides four important operations.

```text
GetSourceName()
Open()
TryReadObservation()
Close()
```

Think of them as a conversation with the data source.

```text
"Who are you?"
      ↓
GetSourceName()

"Can you start?"
      ↓
Open()

"Do you have an Observation?"
      ↓
TryReadObservation()

"Okay, we're finished."
      ↓
Close()
```

---

# 5. `GetSourceName()`

```cpp
virtual std::string GetSourceName() const = 0;
```

This asks the source:

> "What are you?"

For example:

```text
UART(/dev/ttyUSB0)
```

or:

```text
CSV(imu.csv)
```

or:

```text
CAN(can0)
```

This is mainly useful for identification, logging, and debugging.

The `const` means:

> "Getting my name does not change me."

---

# 6. `Open()`

```cpp
virtual bool Open() = 0;
```

This asks the source to initialize itself.

For a CSV adapter:

```text
Open()
  ↓
Open the CSV file
```

For UART:

```text
Open()
  ↓
Open /dev/ttyUSB0
  ↓
Configure serial communication
```

For another source, "open" could mean connecting or initializing something else.

It returns a `bool`:

```text
true
 ↓
Successfully opened

false
 ↓
Failed to open
```

So the caller can check:

```cpp
if (source.Open())
{
    // Start reading
}
else
{
    // Handle failure
}
```

---

# 7. `TryReadObservation()`

```cpp
virtual std::optional<Observation> TryReadObservation() = 0;
```

This is the most important function.

It means:

> "Try to give me the next Observation."

If data is available:

```text
UART
 ↓
58288,-19.61,-19.61,19.61
 ↓
UARTAdapter
 ↓
Observation
```

The function returns that `Observation`.

If there is currently no data:

```text
UART
 ↓
Nothing available
 ↓
std::nullopt
```

`std::optional` is simply a C++ way of saying:

> "There might be a value, or there might be nothing."

---

# 8. Why don't we have `HasNext()`?

An earlier design considered:

```cpp
HasNext();
NextObservation();
```

This works nicely for a finite file.

For example:

```text
CSV
 |
 +-- Row 1
 +-- Row 2
 +-- Row 3
 +-- END
```

You can ask:

> "Are there more rows?"

But a UART stream can continue indefinitely:

```text
ESP
 ↓
data
 ↓
data
 ↓
data
 ↓
data
 ↓
...
```

There may be nothing available at one particular moment, but that does not mean the source has ended.

Therefore the current design uses:

```cpp
TryReadObservation()
```

which can return:

```text
Observation
```

or:

```text
std::nullopt
```

This keeps the interface simple for both finite and streaming sources.

---

# 9. Why isn't `TryReadObservation()` `const`?

The function reads data and therefore changes the source's internal position/state.

For a CSV:

```text
Row 1 ← current
Row 2
Row 3
```

After reading:

```text
Row 1
Row 2 ← current
Row 3
```

The adapter has changed internally.

The same idea applies to UART buffers and other streams.

Therefore:

```cpp
TryReadObservation()
```

is not `const`.

Simple rule:

```text
Get information without changing the object
        → const

Read the next piece and advance internal state
        → not const
```

---

# 10. `Close()`

```cpp
virtual void Close() = 0;
```

This tells the source:

> "We're done. Release whatever resources you were using."

For UART:

```text
Close()
 ↓
Release /dev/ttyUSB0
```

For CSV:

```text
Close()
 ↓
Close file
```

For a socket:

```text
Close()
 ↓
Release connection
```

`Open()` and `Close()` therefore form a natural pair:

```text
Open
 ↓
Use source
 ↓
Read Observations
 ↓
Close
```

---

# 11. Why can't we copy an Observation Source?

The interface contains:

```cpp
IObservationSource(const IObservationSource&) = delete;
IObservationSource& operator=(const IObservationSource&) = delete;
```

This means:

> **Don't allow copying of Observation Sources.**

Why?

A future UART adapter may own:

```text
/dev/ttyUSB0
```

A file adapter may own:

```text
imu.csv
```

A socket adapter may own:

```text
network connection
```

If we accidentally copied the adapter, two objects could believe they own the same resource.

That can cause resource-management problems.

So we prevent accidental copying.

For now, remember:

```text
Observation Source → not copyable
```

Move semantics can be added by concrete adapters later if ownership transfer is actually required.

---

# 12. Virtual destructor

The interface contains:

```cpp
virtual ~IObservationSource() = default;
```

This is important because `IObservationSource` is a **polymorphic base class**.

For example:

```text
IObservationSource
       ↑
       |
 UARTAdapter
```

We can have:

```cpp
IObservationSource* source = new UARTAdapter();
```

The pointer is a base-class pointer, but the actual object is a `UARTAdapter`.

If we destroy the object through that base pointer:

```cpp
delete source;
```

the virtual destructor ensures that the derived adapter is properly destroyed.

Simple interview memory:

> "A polymorphic base class should have a virtual destructor when derived objects may be destroyed through base-class pointers."

---

# 13. `protected` constructor

The interface has:

```cpp
protected:

    IObservationSource() = default;
```

`IObservationSource` is abstract, so we don't directly create objects of it.

Instead, derived adapters inherit from it:

```text
IObservationSource
        ↑
        |
 UARTAdapter
```

The constructor allows the derived adapter to construct the base-class portion of itself.

The `protected` access means derived classes can use it, while normal outside code cannot directly call it.

For Version 0.1, there is no need to overthink this.

---

# 14. What does an Adapter actually do?

The interface does **not** contain UART code, CSV parsing code, or JSON parsing code.

It only defines the common behavior.

For example:

```text
UARTAdapter

Open()
  ↓
Open serial port

TryReadObservation()
  ↓
Read UART bytes
  ↓
Find complete message
  ↓
Parse values
  ↓
Validate message
  ↓
Create Observation

Close()
  ↓
Close serial port
```

A CSV adapter would do something different:

```text
CSVAdapter

Open()
  ↓
Open CSV file

TryReadObservation()
  ↓
Read next row
  ↓
Parse row
  ↓
Create Observation

Close()
  ↓
Close file
```

Both still follow the same interface.

---

# 15. The central idea

The most important thing to remember is:

```text
                    IObservationSource
                           |
             +-------------+-------------+
             |             |             |
             v             v             v
        UARTAdapter    CSVAdapter    JSONAdapter
             |             |             |
             +-------------+-------------+
                           |
                           v
                      Observation
                           |
                           v
                     Processing
```

The adapter knows:

> "How do I get data from this particular source?"

The `Observation` knows:

> "What does one telemetry record look like?"

The processing layer knows:

> "What should I do with the telemetry?"

They do not need to know each other's internal implementation details.

---

# 16. One-line mental model

If you forget everything else during an interview:

> **`IObservationSource` is the common contract that allows different data-source adapters to acquire and convert heterogeneous input into Protoscope's common `Observation` format without exposing protocol-specific details to the rest of the system.**
