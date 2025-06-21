#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd, client_fd;
FILE *data_file;
volatile sig_atomic_t exit_requested = 0;

void cleanup() {
    syslog(LOG_INFO, "Caught signal, exiting");
    if (client_fd) close(client_fd);
    if (server_fd) close(server_fd);
    if (data_file) fclose(data_file);
    data_file = NULL;
    remove(FILE_PATH);
    closelog();
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    int run_as_daemon = 0;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        run_as_daemon = 1;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Error creating socket: %s", strerror(errno));
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;


    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Bind error: %s", strerror(errno));
        cleanup();
        return -1;
    } 

    if (run_as_daemon) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Error al hacer fork: %s", strerror(errno));
            close(server_fd);
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            // exit from the parent process
            exit(EXIT_SUCCESS);
        }
    }

    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Listening failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            syslog(LOG_ERR, "Connection acceptance failed");
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        data_file = fopen(FILE_PATH, "a+");
        if (!data_file) {
            syslog(LOG_ERR, "File operation failed");
            close(client_fd);
            continue;
        }

        ssize_t received;
        while ((received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            fwrite(buffer, 1, received, data_file);
            fflush(data_file);

            if (strchr(buffer, '\n')) {
                fseek(data_file, 0, SEEK_SET);
                while (fgets(buffer, BUFFER_SIZE, data_file)) {
                    send(client_fd, buffer, strlen(buffer), 0);
                }
                break;
            }
        }

        fclose(data_file);
        data_file = NULL;
        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
        close(client_fd);
    }

    return 0;
}
