#pragma once

#include<optional>              // Provides std::optional for values that may or may not exist. Allows TryReadObservation() to represent "no observation available".
#include<string>

#include"Observation.hpp"

/*
     IObservationSource:

     Abstract interface representing any source capable of producing
     Observation objects.

     An Observation Source can be:
       - CSV file
       - JSON file
       - MATLAB (.mat) file
       - UART stream
       - SPI/I2C device
       - ZeroMQ publisher
       - CAN Bus
       - Any future telemetry source

     The responsibility of an Observation Source is NOT to expose
     protocol-specific details. Instead, it provides a common interface
     for opening a source, reading observations one at a time, and
     closing the source.

     Each concrete adapter is responsible for converting its native
     data format into the canonical Observation data model.
 */

namespace protoscope {

class IObservationSource
{
    // It's a contract for future adapters.

    public:

        // Virtual destructor ensures proper cleanup when deleting
        // derived objects through a base class pointer.
        virtual ~IObservationSource() = default;

        // Adapters may own non-copyable OS resources (serial ports, file
        // handles, sockets), so the interface forbids copying. Move semantics
        // are left to concrete adapters to define if they need them.
        // This basically means: Don't allow accidental copying.
        IObservationSource(const IObservationSource&) = delete;
        IObservationSource& operator=(const IObservationSource&) = delete;

        // Identifies the source (e.g. "UART(/dev/ttyUSB0)").
        virtual std::string GetSourceName() const = 0;

        /*
            Opens or initializes the underlying data source.

            Examples:
            - Open a CSV file.
            - Connect to a UART port.
            - Open a TCP socket.
            - Subscribe to a ZeroMQ publisher.

            Returns:
            true  - Opened successfully.
            false - Failed to initialize the source.
        */
        // Opens or initializes the data source.
        // Returns true on success, false on failure.
        virtual bool Open() = 0;

        /*
            Reads the next available Observation.

            Not const: reading advances internal buffer/stream state.

            Returns:
              A populated Observation when one is available.
              std::nullopt when none is currently available — end-of-source
              for finite sources, or "nothing yet" for streaming sources.
              (There is deliberately no separate HasNext(): absence is
              signalled by the empty optional.)
        */

        // Tries to read the next Observation.
        // Not const because reading advances the adapter's internal state.
        // Returns std::nullopt if no Observation is currently available.
        virtual std::optional<Observation> TryReadObservation() = 0;

        /*
            Releases all resources associated with the source.

            Examples:
              - Close file handles.
              - Disconnect serial ports.
              - Release sockets.
              - Free allocated resources.
        */
        virtual void Close() = 0;

    protected:
        // Declaring the deleted copy operations suppresses the implicit
        // default constructor; provide one so derived classes stay
        // default-constructible.
        IObservationSource() = default;

};

} // namespace protoscope
