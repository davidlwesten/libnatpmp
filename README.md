# libnatpmp

[![CI](https://github.com/davidlwesten/libnatpmp/actions/workflows/ci.yml/badge.svg)](https://github.com/davidlwesten/libnatpmp/actions/workflows/ci.yml)

A client library for NAT Port Mapping Protocol ([RFC 6886]), written in C23.

NAT-PMP lets a host ask its router for the router's external IPv4 address and
to forward ports to it. This library implements the client side: it builds
requests, retransmits them and tells the application how the gateway answered.

So far it asks for the external address and maps ports, but doesn't renew or
delete mappings yet.

[RFC 6886]: https://www.rfc-editor.org/rfc/rfc6886

## Sans-IO

The library never touches the network or the clock. The application owns the
socket and the time, and the library is a state machine in between:

- the application hands it received datagrams and the current time
- the library tells it what to send and how long it can sleep

That keeps the library small and deterministic. It works with any event loop
or socket API, and the tests can drive it through minutes of retransmissions
without waiting for anything.

## Usage

The application makes requests, then calls `natpmp_session_poll` until it
returns `NATPMP_EVENT_TIMEOUT`, handles the events in between and waits for a
datagram or the deadline:

```c
natpmp_session_t *session = natpmp_session_begin((natpmp_config_t){});

auto const address = natpmp_session_address_request(session);
// TCP port 8080, for an hour, preferably on the same external port
auto const mapping =
    natpmp_session_map_request(session, 3600, NATPMP_PROTOCOL_TCP, 8080, 8080);

while (running) {
  natpmp_event_t event;
  do {
    event = natpmp_session_poll(session, now_ms());
    switch (event.type) {
    case NATPMP_EVENT_SEND:
      // to the gateway's port 5351
      send(socket, event.send.data, event.send.size, 0);
      break;
    case NATPMP_EVENT_ADDRESS:
      // event.address.handle answered with event.address.external_ip
      break;
    case NATPMP_EVENT_MAPPED:
      // event.mapped.handle got event.mapped.external_port for
      // event.mapped.lifetime seconds
      break;
    case NATPMP_EVENT_FAILED:
    case NATPMP_EVENT_TIMEOUT:
      break;
    }
  } while (event.type != NATPMP_EVENT_TIMEOUT);

  // wait for a datagram or until event.timeout.deadline, and hand any datagram
  // to natpmp_session_on_datagram(session, size, data), or call
  // natpmp_session_on_unreachable(session) if the gateway's port is unreachable
}

natpmp_session_end(session);
```

Requests go to the gateway one at a time, in the order they were made.

`example/` has a complete client using a UDP socket and `poll`.

## Building

libnatpmp needs a C23 compiler and CMake 3.23 or newer to build, or CMake 3.28
with the tests. CI builds with GCC 14 and Clang 20.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The tests download [Unity] and run with AddressSanitizer and
UndefinedBehaviorSanitizer. Without network access,
`-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS` uses an installed Unity instead.

[Unity]: https://github.com/ThrowTheSwitch/Unity

The wire format is generated from `lib/natpmp.pkt` by [pktc], along with a
libFuzzer harness for it. To build and run the harness (clang only):

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DNATPMP_BUILD_FUZZER=ON
cmake --build build
./build/test/natpmp_fuzz -max_total_time=60
```

[pktc]: https://github.com/davidlwesten/pktc

The example client takes the gateway's address or hostname, and optionally a
port to map:

```sh
./build/example/natpmpc 192.168.1.1
./build/example/natpmpc 192.168.1.1 tcp 8080
```

## License

MIT, see [LICENSE](LICENSE).
