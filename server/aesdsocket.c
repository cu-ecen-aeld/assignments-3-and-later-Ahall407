#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>


#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BACKLOG 10
#define TSTAMP_INTERVAL 10
#define BUFFER_SIZE 1024

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE (1)
#endif

volatile sig_atomic_t run = 1;

struct ThreadNode {
    pthread_t tid;           // Thread ID
    struct ThreadNode *next; // Pointer to the next node
};

// Define a structure to pass necessary arguments to the thread
struct ThreadArgs {
    int client_socket;
    int server_socket;
};

// Define a mutex
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
struct ThreadNode *head = NULL; // Global variable to store the head of thread linked list

// Function Declarations
void add_thread(pthread_t tid);
void remove_thread(pthread_t tid);
void join_threads();
void signal_handler(int sign);
void cleanup(int server_socket);
void create_daemon();
void handle_client(int client_socket, int server_socket);
void *client_thread(void *args);
void *timestamp_thread(void *args);

int main(int argc, char *argv[]) {
    int request_daemon = 0;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        request_daemon = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Starting AESD socket");
    printf("Starting AESD Socket on port %d\n", PORT);

    // Set up signal handlers using sigaction for better control
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Error setting up signal handlers");
        exit(EXIT_FAILURE);
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        syslog(LOG_ERR, "Error creating socket");
        cleanup(server_socket);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        syslog(LOG_ERR, "setsockopt(SO_REUSEADDR) failed");
        cleanup(server_socket);
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Error binding socket");
        cleanup(server_socket);
    }

    if (listen(server_socket, BACKLOG) < 0) {
        syslog(LOG_ERR, "Error listening for connections");
        cleanup(server_socket);
    }

    if (request_daemon) {
        create_daemon();
    }

    #ifndef USE_AESD_CHAR_DEVICE
        struct ThreadArgs *thread_data_timer = (struct ThreadArgs *)malloc(sizeof(struct ThreadArgs));
            thread_data_timer->server_socket = server_socket;

        pthread_t timestamp_tid;
        if (pthread_create(&timestamp_tid, NULL, timestamp_thread, (void *)thread_data_timer) != 0) {
            syslog(LOG_ERR, "Error creating timestamp thread");
        }
        add_thread(timestamp_tid); // Add the new thread to the linked list
    #endif

    while (run) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            syslog(LOG_ERR, "Error accepting connection");
            cleanup(server_socket);
        }

        // Create a new thread to handle the connection
        pthread_t tid;
        struct ThreadArgs *thread_data = (struct ThreadArgs *)malloc(sizeof(struct ThreadArgs));
        thread_data->client_socket = client_socket;
        thread_data->server_socket = server_socket;
        if (pthread_create(&tid, NULL, client_thread, (void *)thread_data) != 0) {
            syslog(LOG_ERR, "Error creating thread");
            close(client_socket);
            continue;
        }
        add_thread(tid); // Add the new thread to the linked list
    }

    join_threads(); // Join all completed threads before exiting
    cleanup(server_socket); // Cleanup if the loop exits
    syslog(LOG_INFO, "Reached end of sequence. Gracefully shutting down.");

    return 0;
}


// Function to add a new thread to the linked list
void add_thread(pthread_t tid) {
    struct ThreadNode *new_node = (struct ThreadNode *)malloc(sizeof(struct ThreadNode));
    if (new_node == NULL) {
        syslog(LOG_ERR, "Error creating thread node");
        exit(EXIT_FAILURE);
    }
    new_node->tid = tid;
    new_node->next = head;
    head = new_node;
}

// Function to remove a thread from the linked list
void remove_thread(pthread_t tid) {
    struct ThreadNode *prev = NULL;
    struct ThreadNode *current = head;

    while (current != NULL) {
        if (pthread_equal(current->tid, tid)) {
            if (prev == NULL) {
                head = current->next;
            }
            else {
                prev->next = current->next;
            }
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }
}

// Function to join completed threads
void join_threads() {
    struct ThreadNode *current = head;
    while (current != NULL) {
        pthread_join(current->tid, NULL);
        struct ThreadNode *temp = current;
        current = current->next;
        free(temp);
    }
    head = NULL; // Reset the head of the list after joining all threads
}


void signal_handler(int sign) {
    if (sign == SIGINT || sign == SIGTERM) {
        run = 0;
        syslog(LOG_INFO, "Caught signal, exiting");
    }
}

void cleanup(int server_socket) {
    // Clean up and exit
    syslog(LOG_INFO, "Cleaning up and exiting");

    // Close server socket
    if (shutdown(server_socket, SHUT_RDWR) == -1) {
        syslog(LOG_ERR, "Error shutting down server socket");
    }

    if (close(server_socket) == -1) {
        syslog(LOG_ERR, "Error closing server socket");
    }

    #ifndef USE_AESD_CHAR_DEVICE
        unlink(DATA_FILE); // Delete the data file
    #endif
    closelog();        // Close syslog
    exit(EXIT_SUCCESS);
}

void create_daemon() {
    pid_t pid = fork();

    if (pid < 0) {
        syslog(LOG_ERR, "Error creating fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS); // Parent process exits
    }

    // Child process continues
    umask(0);
    if (setsid() < 0) {
        syslog(LOG_ERR, "Error setting session ID");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void handle_client(int client_socket, int server_socket) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int data_fd = -1;

    if (getpeername(client_socket, (struct sockaddr *)&client_addr, &client_addr_len) == 0) {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    }

    pthread_mutex_lock(&file_mutex);

    #ifdef USE_AESD_CHAR_DEVICE
        data_fd = open("/dev/aesdchar", O_RDWR);
    #else
        data_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    #endif
    if (data_fd < 0) {
        printf("Error opening file.\n");
        syslog(LOG_ERR, "Error opening data file");
        pthread_mutex_unlock(&file_mutex);
        cleanup(server_socket);
    }

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        if (write(data_fd, buffer, bytes_received) != bytes_received) {
            syslog(LOG_ERR, "Error writing to data file");
            pthread_mutex_unlock(&file_mutex);
            close(data_fd);
            cleanup(server_socket);
        }

        if (memchr(buffer, '\n', bytes_received)) {
            #ifdef USE_AESD_CHAR_DEVICE
                lseek(data_fd, 0, SEEK_SET);
            #endif
            char read_buf[BUFFER_SIZE];
            ssize_t bytes_read;
            while ((bytes_read = read(data_fd, read_buf, sizeof(read_buf))) > 0) {
                send(client_socket, read_buf, bytes_read, 0);
            }
            break;
        }
    }

    close(data_fd);
    pthread_mutex_unlock(&file_mutex);

    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
    close(client_socket);
}

void *client_thread(void *args) {
    struct ThreadArgs *thread_args = (struct ThreadArgs *)args;
    int client_socket = thread_args->client_socket;
    int server_socket = thread_args->server_socket;

    // Handle the client as before
    handle_client(client_socket, server_socket);

    // Free allocated memory and exit the thread
    remove_thread(pthread_self()); // Remove the completed thread from the linked list
    free(thread_args);
    pthread_exit(NULL);
}

// Function to append timestamp every 10 seconds
void *timestamp_thread(void *args) {
    struct ThreadArgs *thread_args = (struct ThreadArgs *)args;
    int server_socket = thread_args->server_socket;
    while (run) {
        // Get current time
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %T %z\n", tm_info);

        // Append timestamp to the file
        pthread_mutex_lock(&file_mutex);
        int data_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        if (data_fd < 0) {
            syslog(LOG_ERR, "Error opening data file");
            cleanup(server_socket);
        }
        write(data_fd, timestamp, strlen(timestamp));
        close(data_fd);
        pthread_mutex_unlock(&file_mutex);

        // Sleep for 10 seconds
        sleep(TSTAMP_INTERVAL);
    }
    remove_thread(pthread_self()); // Remove the completed thread from the linked list
    free(thread_args);
    pthread_exit(NULL);
}