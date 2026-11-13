// Asks a NAT-PMP gateway for its external address, and optionally to map a
// port, for 7200 s unless a lifetime is given:
//
//   natpmpc 192.168.1.1
//   natpmpc gateway tcp 8080
//   natpmpc gateway udp 5000 60

#include <byo/natpmp/natpmp.h>

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static natpmp_time_ms_t now() {
  struct timespec ts = {};
  [[maybe_unused]] int const result = clock_gettime(CLOCK_MONOTONIC, &ts);
  assert(result == 0);
  return (natpmp_time_ms_t)ts.tv_sec * 1000 +
         (natpmp_time_ms_t)ts.tv_nsec / 1000000;
}

static bool fill(void * /*ctx*/, size_t size, uint8_t data[]) {
  size_t filled = 0;
  while (filled < size) {
    ssize_t const result = getrandom(data + filled, size - filled, 0);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    filled += (size_t)result;
  }
  return true;
}

// a number from `min` to `max`, or exits
static unsigned long
parse(char const *text, unsigned long min, unsigned long max) {
  char *end = nullptr;
  errno = 0;
  unsigned long const value = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value < min || value > max) {
    fprintf(stderr, "%s: not a number from %lu to %lu\n", text, min, max);
    exit(EXIT_FAILURE);
  }
  return value;
}

// a UDP socket that only talks to the gateway's NAT-PMP port
static int connect_to_gateway(char const *gateway) {
  // NAT-PMP only knows IPv4
  struct addrinfo const hints = {
      .ai_family = AF_INET,
      .ai_socktype = SOCK_DGRAM,
      .ai_flags = AI_NUMERICSERV,
  };
  struct addrinfo *addresses = nullptr;
  int const error = getaddrinfo(gateway, "5351", &hints, &addresses);
  if (error != 0) {
    fprintf(stderr, "%s: %s\n", gateway, gai_strerror(error));
    exit(EXIT_FAILURE);
  }

  // the first address will do
  int const fd = socket(addresses->ai_family, addresses->ai_socktype,
                        addresses->ai_protocol);
  if (fd == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  if (connect(fd, addresses->ai_addr, addresses->ai_addrlen) == -1) {
    perror("connect");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(addresses);
  return fd;
}

// wait for a datagram until the deadline, if any, and hand it to the session
static void
receive(int fd, natpmp_session_t *session, natpmp_optional_time_ms_t deadline) {
  int timeout = -1;
  if (deadline.has_value) {
    auto const current = now();
    timeout = deadline.value > current ? (int)(deadline.value - current) : 0;
  }

  struct pollfd pfd = {.fd = fd, .events = POLLIN};
  int const ready = poll(&pfd, 1, timeout);
  if (ready == -1) {
    perror("poll");
    exit(EXIT_FAILURE);
  }

  if (ready == 0) {
    // the deadline passed
    return;
  }

  // larger than any response, so a truncated datagram can't pass for one
  uint8_t data[64];
  ssize_t const size = recv(fd, data, sizeof(data), 0);
  if (size == -1) {
    if (errno == ECONNREFUSED) {
      // an ICMP Port Unreachable, nothing listens on the gateway
      natpmp_session_on_host_unreachable(session);
    } else {
      perror("recv");
    }
    return;
  }

  natpmp_session_on_datagram(session, now(), (size_t)size, data);
}

int main(int argc, char *argv[]) {
  if (argc != 2 && argc != 4 && argc != 5) {
    fprintf(stderr, "usage: %s <gateway> [tcp|udp <port> [<lifetime>]]\n",
            argv[0]);
    fprintf(stderr, "e.g.   %s 192.168.1.1\n", argv[0]);
    fprintf(stderr, "       %s gateway tcp 8080\n", argv[0]);
    fprintf(stderr, "       %s gateway udp 5000 60\n", argv[0]);
    return EXIT_FAILURE;
  }

  bool const map = argc > 2;
  natpmp_protocol_t protocol = NATPMP_PROTOCOL_TCP;
  natpmp_port_t port = 0;
  natpmp_time_s_t lifetime = 7200;
  if (map) {
    if (strcmp(argv[2], "udp") == 0) {
      protocol = NATPMP_PROTOCOL_UDP;
    } else if (strcmp(argv[2], "tcp") != 0) {
      fprintf(stderr, "%s: not tcp or udp\n", argv[2]);
      return EXIT_FAILURE;
    }
    port = (natpmp_port_t)parse(argv[3], 1, UINT16_MAX);
    if (argc == 5) {
      lifetime = (natpmp_time_s_t)parse(argv[4], 1, UINT32_MAX);
    }
  }

  int const fd = connect_to_gateway(argv[1]);

  natpmp_session_t *const session =
      natpmp_session_begin((natpmp_config_t){.rng = {.fill = fill}});
  if (!session) {
    fprintf(stderr, "out of memory\n");
    return EXIT_FAILURE;
  }

  natpmp_session_address_request(session);
  int waiting = 1;

  if (map) {
    // the same external port, if the gateway can
    auto const mapping =
        natpmp_session_map_request(session, lifetime, protocol, port, port);
    if (!mapping.has_value) {
      fprintf(stderr, "natpmp_session_map_request: %s\n",
              strerror(mapping.error));
      return EXIT_FAILURE;
    }
    waiting += 1;
  }

  int status = EXIT_SUCCESS;
  while (waiting > 0) {
    auto const event = natpmp_session_poll(session, now());
    switch (event.type) {
    case NATPMP_EVENT_SEND:
      if (send(fd, event.send.data, event.send.size, 0) == -1) {
        // the retransmission is already scheduled
        perror("send");
      }
      break;
    case NATPMP_EVENT_ADDRESS: {
      auto const ip = event.address.external_ip.octets;
      printf("%u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
      waiting -= 1;
      break;
    }
    case NATPMP_EVENT_MAPPED:
      printf("%s %u -> %u for %u s\n", argv[2], port,
             event.mapped.external_port, event.mapped.lifetime);
      waiting -= 1;
      break;
    case NATPMP_EVENT_ADDRESS_FAILED:
      fprintf(stderr, "the gateway didn't give its external address\n");
      status = EXIT_FAILURE;
      waiting -= 1;
      break;
    case NATPMP_EVENT_FAILED:
      fprintf(stderr, "the gateway didn't map %s %u\n", argv[2], port);
      status = EXIT_FAILURE;
      waiting -= 1;
      break;
    case NATPMP_EVENT_TIMEOUT:
      receive(fd, session, event.timeout.deadline);
      break;
    }
  }

  natpmp_session_end(session);
  close(fd);
  return status;
}
