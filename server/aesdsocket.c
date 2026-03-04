#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>

#define LISTEN_PORT 9000
#define BACKLOG 10
#define DATAFILE "/var/tmp/aesdsocketdata"
#define IO_CHUNK 4096

static volatile sig_atomic_t g_exit_requested = 0;

static void handle_signal(int signo)
{
    (void)signo;
    g_exit_requested = 1;
}

static int set_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    // Do NOT set SA_RESTART so accept()/recv() can return EINTR and we can exit promptly
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) != 0) return -1;
    if (sigaction(SIGTERM, &sa, NULL) != 0) return -1;
    return 0;
}

static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int send_all(int sockfd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(sockfd, p + off, len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int send_file_to_client(int client_fd)
{
    int fd = open(DATAFILE, O_RDONLY);
    if (fd < 0) return -1;

    char buf[IO_CHUNK];
    for (;;) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (r == 0) break; // EOF
        if (send_all(client_fd, buf, (size_t)r) != 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}

/*
 * Handles one client connection:
 * - recv stream
 * - each newline-terminated packet appended to DATAFILE
 * - after each packet completion, send full file to client
 */
static void handle_client(int client_fd)
{
    char recvbuf[IO_CHUNK];

    char *packet = NULL;
    size_t packet_len = 0;

    for (;;) {
        if (g_exit_requested) {
            // "complete any open connection operations" -> stop reading new data,
            // then return to close connection.
            break;
        }

        ssize_t n = recv(client_fd, recvbuf, sizeof(recvbuf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) {
            // Client closed connection
            break;
        }

        // Process received bytes; may contain 0, 1, or multiple '\n'
        size_t i = 0;
        while (i < (size_t)n) {
            // Find next newline in the remaining recvbuf
            void *nlp = memchr(recvbuf + i, '\n', (size_t)n - i);
            size_t chunk_len = nlp ? (size_t)((char *)nlp - (recvbuf + i) + 1)
                                   : ((size_t)n - i);

            // Grow packet buffer and append this chunk
            if (chunk_len > 0) {
                char *newpkt = realloc(packet, packet_len + chunk_len);
                if (!newpkt) {
                    syslog(LOG_ERR, "malloc/realloc failed");
                    free(packet);
                    packet = NULL;
                    packet_len = 0;
                    return;
                }
                packet = newpkt;
                memcpy(packet + packet_len, recvbuf + i, chunk_len);
                packet_len += chunk_len;
            }

            i += chunk_len;

            // If this chunk ended with newline, packet is complete
            if (nlp) {
                int fd = open(DATAFILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
                if (fd < 0) {
                    syslog(LOG_ERR, "open(%s) failed: %s", DATAFILE, strerror(errno));
                    free(packet);
                    return;
                }

                if (write_all(fd, packet, packet_len) != 0) {
                    syslog(LOG_ERR, "write(%s) failed: %s", DATAFILE, strerror(errno));
                    close(fd);
                    free(packet);
                    return;
                }
                close(fd);

                // Send the full contents back to the client (streaming; no heap-size assumption)
                if (send_file_to_client(client_fd) != 0) {
                    syslog(LOG_ERR, "sending file to client failed: %s", strerror(errno));
                    free(packet);
                    return;
                }

                // Reset for next packet
                free(packet);
                packet = NULL;
                packet_len = 0;
            }
        }
    }

    free(packet);
}

static int daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        // Parent exits
        _exit(0);
    }

    // Child continues
    if (setsid() < 0) {
        return -1;
    }

    // Optional but typical
    if (chdir("/") != 0) {
        return -1;
    }

    // Redirect stdin/out/err to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) return -1;
    (void)dup2(fd, STDIN_FILENO);
    (void)dup2(fd, STDOUT_FILENO);
    (void)dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);

    return 0;
}

int main(int argc, char *argv[])
{
	bool run_as_daemon = false;
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        run_as_daemon = true;
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return EXIT_FAILURE;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (set_signal_handlers() != 0) {
        syslog(LOG_ERR, "sigaction failed: %s", strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(listen_fd);
        closelog();
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(LISTEN_PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(listen_fd);
        closelog();
        return EXIT_FAILURE; // requirement (b): fail on socket connection steps
    }
    
    if (run_as_daemon) {
        if (daemonize() != 0) {
            syslog(LOG_ERR, "daemonize failed: %s", strerror(errno));
            close(listen_fd);
            closelog();
            return EXIT_FAILURE;
        }
    }

    if (listen(listen_fd, BACKLOG) != 0) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(listen_fd);
        closelog();
        return EXIT_FAILURE;
    }

    while (!g_exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR && g_exit_requested) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue; // keep server running
        }

        char ipstr[INET_ADDRSTRLEN];
        const char *ip = inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, sizeof(ipstr));
        if (!ip) ip = "unknown";

        syslog(LOG_INFO, "Accepted connection from %s", ip);

        handle_client(client_fd);

        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", ip);
    }

    syslog(LOG_INFO, "Caught signal, exiting");

    close(listen_fd);
    unlink(DATAFILE);

    closelog();
    return EXIT_SUCCESS;
}
