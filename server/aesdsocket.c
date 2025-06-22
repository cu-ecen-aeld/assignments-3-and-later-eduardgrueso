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
#include <pthread.h>

#include <time.h>

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd;
volatile sig_atomic_t exit_requested = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t timestamp_thread;

typedef struct thread_node {
    pthread_t thread_id;
    int client_fd;
    struct sockaddr_in client_addr;
    struct thread_node *next;
    int thread_complete;
} thread_node_t;

thread_node_t *thread_list_head = NULL;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_thread(thread_node_t *node) {
    pthread_mutex_lock(&thread_list_mutex);
    node->next = thread_list_head;
    thread_list_head = node;
    pthread_mutex_unlock(&thread_list_mutex);
}

void remove_completed_threads() {
    pthread_mutex_lock(&thread_list_mutex);
    thread_node_t **curr = &thread_list_head;
    while (*curr) {
        if ((*curr)->thread_complete) {
            pthread_join((*curr)->thread_id, NULL);
            thread_node_t *to_free = *curr;
            *curr = (*curr)->next;
            close(to_free->client_fd);
            free(to_free);
        } else {
            curr = &(*curr)->next;
        }
    }
    pthread_mutex_unlock(&thread_list_mutex);
}

void wait_for_all_threads() {
    pthread_mutex_lock(&thread_list_mutex);
    thread_node_t *curr = thread_list_head;
    while (curr) {
        pthread_join(curr->thread_id, NULL);
        close(curr->client_fd);
        thread_node_t *to_free = curr;
        curr = curr->next;
        free(to_free);
    }
    thread_list_head = NULL;
    pthread_mutex_unlock(&thread_list_mutex);
}

void cleanup(int signo) {
    syslog(LOG_INFO, "Caught signal %d, exiting", signo);
    exit_requested = 1;

    // Cierra el socket del servidor para desbloquear accept()
    if (server_fd > 0) close(server_fd);

    // Espera a que terminen los hilos de clientes y el de timestamp
    wait_for_all_threads();
    if (timestamp_thread) pthread_join(timestamp_thread, NULL);

    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&thread_list_mutex);

    remove(FILE_PATH);
    closelog();

    exit(0);
}

void *connection_handler(void *arg) {
    thread_node_t *node = (thread_node_t *)arg;
    char buffer[BUFFER_SIZE];
    ssize_t received;

    syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(node->client_addr.sin_addr));

    FILE *data_file = NULL;
    while ((received = recv(node->client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        pthread_mutex_lock(&file_mutex);
        data_file = fopen(FILE_PATH, "a+");
        if (!data_file) {
            pthread_mutex_unlock(&file_mutex);
            syslog(LOG_ERR, "File operation failed");
            break;
        }
        fwrite(buffer, 1, received, data_file);
        fflush(data_file);

        if (memchr(buffer, '\n', received)) {
            fseek(data_file, 0, SEEK_SET);
            while (fgets(buffer, BUFFER_SIZE, data_file)) {
                send(node->client_fd, buffer, strlen(buffer), 0);
            }
        }
        fclose(data_file);
        pthread_mutex_unlock(&file_mutex);

        if (memchr(buffer, '\n', received)) {
            break;
        }
    }

    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(node->client_addr.sin_addr));
    node->thread_complete = 1;
    return NULL;
}

void *timestamp_thread_func(void *arg) {
    (void)arg;
    char timestr[128];
    char line[160];
    FILE *data_file;

    while (!exit_requested) {
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        strftime(timestr, sizeof(timestr), "%a, %d %b %Y %H:%M:%S %z", &tm_now);
        snprintf(line, sizeof(line), "timestamp: %s\n", timestr);

        pthread_mutex_lock(&file_mutex);
        data_file = fopen(FILE_PATH, "a");
        if (data_file) {
            fwrite(line, 1, strlen(line), data_file);
            fclose(data_file);
        }
        pthread_mutex_unlock(&file_mutex);

        for (int i = 0; i < 10 && !exit_requested; ++i) {
            sleep(1);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
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
        close(server_fd);
        return -1;
    }

    if (run_as_daemon) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Error during fork: %s", strerror(errno));
            close(server_fd);
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
    }

    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Listening failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&timestamp_thread, NULL, timestamp_thread_func, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create the timestamp thread");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (!exit_requested) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (exit_requested) break;
            syslog(LOG_ERR, "Connection acceptance failed");
            continue;
        }

        thread_node_t *node = calloc(1, sizeof(thread_node_t));
        node->client_fd = client_fd;
        node->client_addr = client_addr;
        node->thread_complete = 0;
        add_thread(node);

        if (pthread_create(&node->thread_id, NULL, connection_handler, node) != 0) {
            syslog(LOG_ERR, "Failed to create thread");
            close(client_fd);
            node->thread_complete = 1;
        }

        remove_completed_threads();
    }

    wait_for_all_threads();
    pthread_join(timestamp_thread, NULL);
    close(server_fd);
    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&thread_list_mutex);
    remove(FILE_PATH);
    closelog();
    return 0;
}