#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <socket.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

static int failures;
static volatile sig_atomic_t signals;
static pthread_t main_thread;
static void caught(int sig) { signals++; }
static void check(int condition, const char *name) {
  if (!condition) {
    failures++;
    logkf("IOPOLL FAIL: %s errno=%d\n", name, errno);
  }
}
static uint64_t now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
typedef struct {
  int fd;
  bool interrupt;
} action_t;
static void *delayed(void *argument) {
  action_t *action = argument;
  struct timespec pause = {0, 30000000};
  nanosleep(&pause, NULL);
  if (action->interrupt)
    pthread_kill(main_thread, SIGUSR1);
  else
    write(action->fd, "x", 1);
  return NULL;
}

int io_poll_tests(void) {
  failures = 0;
  int pipefd[2];
  if (pipe2(pipefd, O_NONBLOCK | O_CLOEXEC))
    return 1;
  struct stat st;
  check(fstat(pipefd[0], &st) == 0 && S_ISFIFO(st.st_mode),
        "pipe descriptor type");
  check(fcntl(pipefd[0], F_GETFL) == (O_RDONLY | O_NONBLOCK) &&
            fcntl(pipefd[1], F_GETFL) == (O_WRONLY | O_NONBLOCK) &&
            fcntl(pipefd[0], F_GETFD) == FD_CLOEXEC,
        "pipe descriptor flags");
  char byte;
  check(read(pipefd[0], &byte, 1) == -1 && errno == EAGAIN,
        "empty nonblocking pipe");
  check(write(pipefd[0], "x", 1) == -1 && errno == EBADF,
        "unidirectional pipe");
  check(lseek(pipefd[0], 0, SEEK_SET) == -1 && errno == ESPIPE,
        "pipe seek rejected");
  struct pollfd fds[4] = {{pipefd[0], POLLIN, 0},
                          {pipefd[1], POLLOUT, 0},
                          {-1, POLLIN, 7},
                          {999999, POLLIN, 0}};
  check(poll(fds, 4, 0) == 2 && fds[1].revents == POLLOUT && !fds[0].revents &&
            !fds[2].revents && fds[3].revents == POLLNVAL,
        "mixed ready, negative and invalid descriptors");
  uint64_t start = now();
  check(poll(fds, 1, 35) == 0 && now() - start >= 35000000, "finite timeout");
  start = now();
  check(poll(NULL, 0, 20) == 0 && now() - start >= 20000000,
        "empty descriptor timeout");
  pthread_t thread;
  action_t action = {pipefd[1], false};
  check(!pthread_create(&thread, NULL, delayed, &action), "producer thread");
  check(poll(fds, 1, 1000) == 1 && fds[0].revents == POLLIN,
        "pipe readiness wake");
  pthread_join(thread, NULL);
  int available;
  check(ioctl(pipefd[0], FIONREAD, &available) == 0 && available == 1 &&
            read(pipefd[0], &byte, 1) == 1 && byte == 'x',
        "pipe byte stream and FIONREAD");
  char block[4096];
  memset(block, 0x5a, sizeof(block));
  unsigned total = 0;
  ssize_t written;
  while ((written = write(pipefd[1], block, sizeof(block))) > 0)
    total += written;
  check(total && written == -1 && errno == EAGAIN && poll(fds + 1, 1, 0) == 0,
        "full pipe backpressure");
  check(read(pipefd[0], block, sizeof(block)) == sizeof(block) &&
            poll(fds + 1, 1, 0) == 1,
        "space wakes writer readiness");
  close(pipefd[1]);
  check(poll(fds, 1, 0) == 1 && (fds[0].revents & POLLHUP),
        "hangup with buffered bytes");
  while (read(pipefd[0], block, sizeof(block)) > 0) {
  }
  check(read(pipefd[0], &byte, 1) == 0, "EOF after drain");
  close(pipefd[0]);

  check(pipe2(pipefd, O_CLOEXEC) == 0, "blocking pipe");
  int child = fork();
  if (!child) {
    fcntl(pipefd[0], F_SETFD, 0);
    close(pipefd[1]);
    int result = read(pipefd[0], &byte, 1) == 1 && byte == 'f';
    close(pipefd[0]);
    _exit(!result);
  }
  check(write(pipefd[1], "f", 1) == 1 && child > 0 && waittid(child) == 0,
        "pipe is shared across fork");
  check(fcntl(pipefd[0], F_GETFD) == FD_CLOEXEC,
        "descriptor flags are private across fork");
  close(pipefd[0]);
  close(pipefd[1]);
  check(pipe(pipefd) == 0, "signal pipe");
  signal(SIGPIPE, caught);
  close(pipefd[0]);
  signals = 0;
  check(write(pipefd[1], "x", 1) == -1 && errno == EPIPE && signals == 1,
        "broken pipe sends SIGPIPE");
  close(pipefd[1]);
  signal(SIGPIPE, SIG_DFL);
  signal(SIGUSR1, caught);
  main_thread = pthread_self();
  action = (action_t){-1, true};
  pthread_create(&thread, NULL, delayed, &action);
  check(poll(NULL, 0, -1) == -1 && errno == EINTR,
        "signal interrupts infinite wait");
  pthread_join(thread, NULL);
  signal(SIGUSR1, SIG_DFL);

  int sockets[2];
  check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0 &&
            sockets[0] != sockets[1],
        "local socket pair has distinct endpoints");
  check(pipe(pipefd) == 0, "mixed selector wakeup pipe");
  fds[0] = (struct pollfd){sockets[0], POLLIN, 0};
  fds[1] = (struct pollfd){pipefd[0], POLLIN, 0};
  action = (action_t){sockets[1], false};
  pthread_create(&thread, NULL, delayed, &action);
  check(poll(fds, 2, 1000) == 1 && fds[0].revents == POLLIN && !fds[1].revents,
        "socket and pipe share poll wait");
  pthread_join(thread, NULL);
  check(read(sockets[0], &byte, 1) == 1, "socket read after readiness");
  int closed = close(sockets[1]);
  int ready = poll(fds, 2, 1000);
  int received = read(sockets[0], &byte, 1);
  if (closed || ready != 1 || !(fds[0].revents & POLLHUP) || received)
    logkf("IOPOLL socket EOF close=%d poll=%d events=%x,%x read=%d\n", closed,
          ready, fds[0].revents, fds[1].revents, received);
  check(closed == 0 && ready == 1 && (fds[0].revents & POLLHUP) &&
            received == 0,
        "socket close and EOF readiness");
  close(sockets[0]);
  close(pipefd[0]);
  close(pipefd[1]);
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in address = {.sin_family = AF_INET};
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  socklen_t length = sizeof(address);
  check(listener >= 0 &&
            bind(listener, (void *)&address, sizeof(address)) == 0 &&
            getsockname(listener, (void *)&address, &length) == 0,
        "reserve ephemeral TCP port");
  close(listener);
  int client = socket(AF_INET, SOCK_STREAM, 0);
  check(client >= 0 && fcntl(client, F_SETFL, O_NONBLOCK) == 0 &&
            connect(client, (void *)&address, sizeof(address)) == -1 &&
            errno == EINPROGRESS,
        "nonblocking refused connection starts asynchronously");
  fds[0] = (struct pollfd){client, POLLOUT, 0};
  int error = 0;
  length = sizeof(error);
  check(poll(fds, 1, 5000) == 1 && (fds[0].revents & POLLERR) &&
            getsockopt(client, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
            error == ECONNREFUSED,
        "failed connection readiness and SO_ERROR");
  close(client);
  if (!failures)
    logk("IOPOLL PASS\n");
  return failures != 0;
}
