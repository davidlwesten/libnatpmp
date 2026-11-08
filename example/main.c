// Asks a NAT-PMP gateway for its external address:
//
//   natpmpc 192.168.1.1
//   natpmpc openwrt.lan

#include <byo/natpmp/natpmp.h>

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    // e.g. ECONNREFUSED when nothing listens on the gateway, the
    // retransmissions keep trying
    perror("recv");
    return;
  }

  natpmp_session_on_datagram(session, (size_t)size, data);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <gateway>\n", argv[0]);
    fprintf(stderr, "e.g.   %s 192.168.1.1\n", argv[0]);
    fprintf(stderr, "       %s openwrt.lan\n", argv[0]);
    return EXIT_FAILURE;
  }

  int const fd = connect_to_gateway(argv[1]);

  natpmp_session_t *const session = natpmp_session_begin((natpmp_config_t){});
  if (!session) {
    fprintf(stderr, "out of memory\n");
    return EXIT_FAILURE;
  }

  auto const request = natpmp_session_address_request(session);
  if (!request.has_value) {
    fprintf(stderr, "natpmp_session_address_request: %s\n",
            strerror(request.error));
    return EXIT_FAILURE;
  }

  int status = EXIT_FAILURE;
  for (bool running = true; running;) {
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
      status = EXIT_SUCCESS;
      running = false;
      break;
    }
    case NATPMP_EVENT_FAILED:
      fprintf(stderr, "the gateway didn't give its external address\n");
      running = false;
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
