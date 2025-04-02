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

#define PORT 9000
#define BUFFER_SIZE 1024
#define TSTAMP_INTERVAL 10
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocket"

int run = 1;

int server_socket;
pthread_mutex_t file_mutex;

// Define a node for the singly linked list to store thread IDs
typedef struct thread_node {
    pthread_t thread_id;
    struct thread_node* next;
} thread_node_t;

// Global pointer to the head of the list
thread_node_t* thread_list = NULL;

// Function Declarations
void signal_handler(int sig);
void daemonize();
void add_thread(pthread_t thread_id);
void* append_timestamp(void* arg);
void* handle_client_IO(void* arg);


// Main function
int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id, timestamp_thread;
    int daemon_mode = 0;

    // Parse command-line arguments for daemon mode
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    // Daemonize if the -d flag is received
    if (daemon_mode) {
        daemonize();
    }

    // Set up signal handlers for SIGINT and SIGTERM
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Open syslog
    openlog("aesd_socket", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Starting up AESD Socket");

    // Initialize mutex
    if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        syslog(LOG_ERR, "Error: Failed to initialize Mutex.");
        exit(EXIT_FAILURE);
    }

    // Create socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "Error: Failed to create the socket.");
        exit(EXIT_FAILURE);
    }

    // Define server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

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

    printf("Running aesd socket on port %d", PORT);

    // Start the timestamp thread
    if (pthread_create(&timestamp_thread, NULL, append_timestamp, NULL) != 0) {
        syslog(LOG_ERR, "Error: Failed to create Timestamp thread.");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Server listening on port %d...", PORT);

    // Accept incoming connections continuously until interrupted
    while (run > 0) {
        int* client_socket = malloc(sizeof(int));
        if ((*client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            syslog(LOG_ERR, "Error: Failed to accept connection from the client.");
            free(client_socket);
            break;
        }

        syslog(LOG_INFO, "Client connected");

        // Create a new thread to handle the client connection
        if (pthread_create(&thread_id, NULL, handle_client_IO, (void*)client_socket) != 0) {
            syslog(LOG_ERR, "Error: Failed to create the client thread.");
            free(client_socket);
        } else {
            // Add the thread to the linked list
            add_thread(thread_id);
        }
    }

    // The server will clean up in the signal handler
    close(server_socket);
    pthread_mutex_destroy(&file_mutex);
    closelog();  // Close syslog
    printf("Closed aesd socket on port %d", PORT);
    return 0;
}


// Function to handle signals and perform graceful shutdown
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Server interrupted by signal %d. Closing server...", sig);
        close(server_socket);
        printf("Closed aesd socket on port %d", PORT);
        
        // Join all active threads in the linked list
        thread_node_t* current = thread_list;
        while (current != NULL) {
            pthread_join(current->thread_id, NULL);
            thread_node_t* temp = current;
            current = current->next;
            free(temp);
        }

        // Destroy the mutex and exit
        pthread_mutex_destroy(&file_mutex);
        syslog(LOG_INFO, "Server shut down successfully.");
        closelog();
        run = 0;
        exit(0);
    }
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
void add_thread(pthread_t thread_id) {
    thread_node_t* new_node = malloc(sizeof(thread_node_t));
    if (new_node == NULL) {
        syslog(LOG_ERR, "Error: Failed to allocate memory for thread node.");
        exit(EXIT_FAILURE);
    }
    new_node->thread_id = thread_id;
    new_node->next = thread_list;
    thread_list = new_node;
}


// Function to append a timestamp to the file every 10 seconds
void* append_timestamp(void* arg) {
    char timestamp[256];
    time_t raw_time;
    struct tm* time_info;

    while (run > 0) {
        // Get the current time
        time(&raw_time);
        time_info = localtime(&raw_time);

        // Generate timestamp in RFC 2822 format
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);
        
        // Lock the mutex to write timestamp atomically
        pthread_mutex_lock(&file_mutex);
        
        // Open the file in append mode
        FILE* file = fopen(DATA_FILE, "a");
        if (file == NULL) {
            syslog(LOG_ERR, "Error: Failed to open file while appending timestamp.");
            pthread_mutex_unlock(&file_mutex);
            continue;
        }

        // Write the timestamp to the file, unlock mutex and sleep
        fprintf(file, "timestamp:%s\n", timestamp);
        fclose(file);
        pthread_mutex_unlock(&file_mutex);
        sleep(TSTAMP_INTERVAL);
    }
    return NULL;
}


// Function to handle each client in a separate thread
void* handle_client_IO(void* arg) {
    int client_socket = *((int*)arg);
    // char buffer[BUFFER_SIZE];
    int bytes_received;
    int total_received = 0;

    free(arg);  // Free the allocated memory for client_socket

    char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
    size_t buffer_size = BUFFER_SIZE;

    while ((bytes_received = read(client_socket, buffer + total_received, sizeof(buffer) - total_received - 1)) > 0) {
        total_received += bytes_received;
        // buffer[total_received] = '\0'; // dont need
        // realloc buffer size
        if (total_received >= buffer_size) {
            buffer_size *= 2;
            buffer = (char *)realloc(buffer, buffer_size * sizeof(char));
        }
        printf("Just recieved a new byte = %c", bytes_received);
        // If newline is found, stop receiving
        if (strchr(buffer, '\n')) {
            break;
        }
    }

    if (bytes_received <= 0) {
        syslog(LOG_ERR, "Error: Failed to Read from Client.");
        close(client_socket);
        return NULL;
    }

    // Synchronize access to the file using the mutex
    pthread_mutex_lock(&file_mutex);

    // Write received message to the file
    int write_file = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND);
    if (write_file < 0) {
        printf("Failed to open file for writing data.");
        syslog(LOG_ERR, "Error: Failed to open file for writing client data.");
        pthread_mutex_unlock(&file_mutex);
        close(client_socket);
        return NULL;
    }

    printf("Writing bytes = %c", total_received);
    write(write_file, buffer, total_received);
    close(write_file);

    // Send the contents of the file back to the client
    // file = fopen(DATA_FILE, "r");
    int read_file = open(DATA_FILE, O_RDONLY);
    if (read_file < 0) {
        syslog(LOG_ERR, "Error: Failed to open file for reading file content.");
        pthread_mutex_unlock(&file_mutex);
        close(client_socket);
        return NULL;
    }

    // Send the contents of the file to the client
    while ((bytes_received = read(read_file, buffer, sizeof(buffer))) > 0) {
        printf("Byte to send back = %c", bytes_received);
        int send_success = send(client_socket, buffer, bytes_received, 0);
        if (send_success == -1) {
            syslog(LOG_ERR, "Error: Failed to Write while sending data to client.");
            close(read_file);
            pthread_mutex_unlock(&file_mutex);
            close(client_socket);
            return NULL;
        }
        // if (write(client_socket, buffer, bytes_received) == -1) {
        //     syslog(LOG_ERR, "Error: Failed to Write while sending data to client.");
        //     fclose(file);
        //     pthread_mutex_unlock(&file_mutex);
        //     close(client_socket);
        //     return NULL;
        // }
    }

    close(read_file);
    pthread_mutex_unlock(&file_mutex);
    close(client_socket);
    return NULL;
}