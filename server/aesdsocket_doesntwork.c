#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT 9000
#define BUFFER_SIZE 1024
#define TSTAMP_INTERVAL 10
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocket"

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
void signal_handler(int sig);
void cleanup_all(int server_socket);
void daemonize();
void add_thread(pthread_t thread_id);
void join_threads();
void remove_thread(pthread_t tid);
void *timestamp_thread(void *args);
void *client_thread(void *args);
void handle_client_IO(int client_socket);
void handle_client(int client_socket);


// Main function
int main(int argc, char *argv[]) {
    int daemon_mode = 0;

    // Parse command-line arguments for daemon mode
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    // Daemonize if the -d flag is received
    if (daemon_mode) {
        daemonize();
    }

    // Set up signal handlers using sigaction for better control
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Error setting up signal handlers");
        exit(EXIT_FAILURE);
    }

    // Open syslog
    openlog("aesd_socket", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Starting up AESD Socket");

    // Initialize mutex
    if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        syslog(LOG_ERR, "Error: Failed to initialize Mutex.");
        exit(EXIT_FAILURE);
    }

    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        syslog(LOG_ERR, "Error: Failed to create the socket.");
        exit(EXIT_FAILURE);
    }

    // Define server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        syslog(LOG_ERR, "setsockopt(SO_REUSEADDR) failed");
        close(server_socket);
    }

    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "Error: Failed to bind the socket.");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, BACKLOG) < 0) {
        syslog(LOG_ERR, "Error: Failed to listen on socket.");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    
    struct ThreadArgs *thread_data_timer = (struct ThreadArgs *)malloc(sizeof(struct ThreadArgs));
        thread_data_timer->server_socket = server_socket;

    pthread_t timestamp_tid;
    if (pthread_create(&timestamp_tid, NULL, timestamp_thread, (void *)thread_data_timer) != 0) {
        syslog(LOG_ERR, "Error creating timestamp thread");
    }
    add_thread(timestamp_tid); // Add the new thread to the linked list
    
    printf("Running aesd socket on port %d \n", PORT);
    syslog(LOG_INFO, "Server listening on port %d...", PORT);

    // Accept incoming connections continuously until interrupted
    while (run) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            syslog(LOG_ERR, "Error accepting connection");
            close(client_socket);
        }

        syslog(LOG_INFO, "Client connected");

        // Create a new thread to handle the client connection
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

    // The server will clean up after the signal handler sets run = 0
    join_threads();
    cleanup_all(server_socket);
    printf("Finished main function and exited completely\n");
    return 0;
}


// Function to handle signals and perform graceful shutdown
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Server interrupted by signal %d. Closing server...", sig);
        run = 0;
        printf("Caught signal and exiting.\n");
        // pthread_mutex_destroy(&file_mutex);
    }
}

void cleanup_all(int server_socket){
    syslog(LOG_INFO, "Cleaning up server socket, closing file and exiting.");
    printf("In the cleanup all function.\n");
    // Close server socket
    if (shutdown(server_socket, SHUT_RDWR) == -1) {
        syslog(LOG_ERR, "Error shutting down server socket");
    }

    if (close(server_socket) == -1) {
        syslog(LOG_ERR, "Error closing server socket");
    }

    printf("Closed aesd socket on port %d \n", PORT);

    if (pthread_mutex_destroy(&file_mutex) != 0) {
        printf("Failed to destroy mutex.\n");
    }
    
    if (remove(DATA_FILE) != 0) {
        printf("Failed to remove the file.\n");
    }
    else {
        printf("Just removed the file.\n");
    }
    
    closelog();
    exit(EXIT_SUCCESS);
}


// Function to daemonize the application
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed. Exiting.");
        exit(EXIT_FAILURE);
    }

    // Parent process exits, allowing the child to continue as a daemon
    if (pid > 0) {
        exit(0);
    }

    // Child process becomes the session leader
    if (setsid() < 0) {
        syslog(LOG_ERR, "Failed to create new session. Exiting.");
        exit(EXIT_FAILURE);
    }

    // Change the current working directory to root to detach from the terminal
    if (chdir("/") < 0) {
        syslog(LOG_ERR, "Failed to change working directory. Exiting.");
        exit(EXIT_FAILURE);
    }

    // Close all open file descriptors
    for (int i = 0; i < 1024; i++) {
        close(i);
    }

    // Redirect standard output and error to /dev/null
    open("/dev/null", O_RDWR); // stdin
    dup(0);  // stdout
    dup(0);  // stderr
}


// Function to add a new thread to the linked list
void add_thread(pthread_t tid) {
    struct ThreadNode *new_node = (struct ThreadNode *)malloc(sizeof(struct ThreadNode));
    if (new_node == NULL) {
        syslog(LOG_ERR, "Error: Failed to allocate memory for thread node.");
        exit(EXIT_FAILURE);
    }
    new_node->tid = tid;
    new_node->next = head;
    head = new_node;
}

void join_threads() {
    // Join all active threads in the linked list
    printf("In joining threads function.\n");
    struct ThreadNode *current = head;
    int i = 1;
    while (current != NULL) {
        pthread_join(current->tid, NULL);
        printf("Joined %d thread.\n", i);
        struct ThreadNode *temp = current;
        current = current->next;
        free(temp);
        i++;
    }
    head = NULL; // Reset the head of the list after joining all threads
    printf("Finished joining threads.\n");
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
            pthread_mutex_unlock(&file_mutex);
            close(server_socket);
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

void *client_thread(void *args) {
    struct ThreadArgs *thread_args = (struct ThreadArgs *)args;
    int client_socket = thread_args->client_socket;

    // Handle the client as before
    handle_client(client_socket);

    // Free allocated memory and exit the thread
    remove_thread(pthread_self()); // Remove the completed thread from the linked list
    free(thread_args);
    pthread_exit(NULL);
}


// Function to handle each client in a separate thread
void handle_client_IO(int client_socket) {
    char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
    size_t buffer_size = BUFFER_SIZE;
    int bytes_received;
    int total_received = 0;

    // Synchronize access to the file using the mutex
    pthread_mutex_lock(&file_mutex);
    int write_file = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
    if (write_file < 0) {
        printf("Failed to open file for writing data.\n");
        syslog(LOG_ERR, "Error: Failed to open file for writing client data.");
        pthread_mutex_unlock(&file_mutex);
        free(buffer);
        close(client_socket);
    }
    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes_received <= 0) {
            printf("Failed to read data from the client.");
            syslog(LOG_ERR, "Error: Failed to Read from Client.");
            pthread_mutex_unlock(&file_mutex);
            free(buffer);
            close(client_socket);
        }
        
        // realloc buffer size if data larger that 1024 bytes
        total_received += bytes_received;
        if (total_received >= buffer_size) {
            buffer_size += 512;
            buffer = (char *)realloc(buffer, buffer_size * sizeof(char));
        }

        printf("Just recieved a new byte = %s \n", buffer);
        // Write received message to the file
        write(write_file, buffer, bytes_received);
        printf("Just wrote bytes to %s \n", DATA_FILE);

        if (memchr(buffer, '\n', bytes_received) != NULL) {
            close(write_file);
            // Send the contents of the file back to the client
            int read_file = open(DATA_FILE, O_RDONLY, S_IRUSR);
            if (read_file < 0) {
                printf("Failed to open the file for reading back client data.\n");
                syslog(LOG_ERR, "Error: Failed to open file for reading file content.");
                pthread_mutex_unlock(&file_mutex);
                free(buffer);
                close(client_socket);
            }

            int total_read = 0;
            char *read_buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
            size_t bytes_read = BUFFER_SIZE;
            printf("Starting to read the file contents back to the client.\n");
            // Send the contents of the file to the client
            while ((bytes_read = read(read_file, read_buffer, sizeof(read_buffer))) > 0) {
                total_read += bytes_read;
                if (total_read >= bytes_read) {
                    bytes_read += 512;
                    read_buffer = (char *)realloc(read_buffer, bytes_read * sizeof(char));
                }

                printf("Byte to send back = %s \n", read_buffer);
                int send_success = send(client_socket, read_buffer, bytes_read, 0);
                if (send_success == -1) {
                    printf("Failed to send byte back to client.\n");
                    syslog(LOG_ERR, "Error: Failed to Write while sending data to client.");
                    close(read_file);
                    pthread_mutex_unlock(&file_mutex);
                    free(read_buffer);
                    close(client_socket);
                }
            } 
            close(read_file);
            pthread_mutex_unlock(&file_mutex);
            free(read_buffer);
            break;
        }
    }
    close(client_socket);
    free(buffer);
}


void handle_client(int client_socket) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (getpeername(client_socket, (struct sockaddr *)&client_addr, &client_addr_len) == 0) {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    }

    char buffer[1024];
    ssize_t bytes_received;

    pthread_mutex_lock(&file_mutex);

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        int data_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        if (data_fd < 0) {
            syslog(LOG_ERR, "Error opening data file");
            close(client_socket);
        }

        write(data_fd, buffer, bytes_received);
        close(data_fd);

        if (memchr(buffer, '\n', bytes_received) != NULL) {
            int file_fd = open(DATA_FILE, O_RDONLY);
            if (file_fd < 0) {
                syslog(LOG_ERR, "Error opening data file for reading");
                close(client_socket);
            }

            char file_buffer[1024];
            ssize_t bytes_read;

            while ((bytes_read = read(file_fd, file_buffer, sizeof(file_buffer))) > 0) {
                send(client_socket, file_buffer, bytes_read, 0);
            }

            close(file_fd);
            pthread_mutex_unlock(&file_mutex);
            break;
        }
    }

    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
    close(client_socket);
}