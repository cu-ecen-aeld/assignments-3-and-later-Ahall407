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
void cleanup_all();
void daemonize();
void add_thread(pthread_t thread_id);
void join_threads();
void remove_thread(pthread_t tid);
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

    // Set up signal handlers using sigaction for better control
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

    // Start the timestamp thread
    if (pthread_create(&timestamp_thread, NULL, append_timestamp, NULL) != 0) {
        syslog(LOG_ERR, "Error: Failed to create Timestamp thread.");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    else {
        add_thread(timestamp_thread); // Add the thread to the linked list
    }

    printf("Running aesd socket on port %d \n", PORT);
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
            add_thread(thread_id); // Add the thread to the linked list
        } 
    }

    // The server will clean up after the signal handler sets run = 0
    join_threads();
    cleanup_all();
    printf("Finished main function and exited completely\n");
    return 0;
}


// Function to handle signals and perform graceful shutdown
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Server interrupted by signal %d. Closing server...", sig);
        printf("Caught signal and exiting.\n");
        run = 0;
        exit(0);
    }
}

void cleanup_all(){
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

void join_threads() {
    // Join all active threads in the linked list
    printf("In joining threads function.\n");
    thread_node_t* current = thread_list;
    while (current != NULL) {
        pthread_join(current->thread_id, NULL);
        thread_node_t* temp = current;
        current = current->next;
        free(temp);
    }
    thread_list = NULL; // Reset the head of the list after joining all threads
}

// Function to remove a thread from the linked list
void remove_thread(pthread_t tid) {
    thread_node_t* prev = NULL;
    thread_node_t* current = thread_list;

    while (current != NULL) {
        if (pthread_equal(current->thread_id, tid)) {
            if (prev == NULL) {
                thread_list = current->next;
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


// Function to append a timestamp to the file every 10 seconds
void* append_timestamp(void* arg) {
    char timestamp[256];
    time_t raw_time;
    struct tm* time_info;

    while (run > 0) {
        // Get the current time
        sleep(TSTAMP_INTERVAL); 
        time(&raw_time);
        time_info = localtime(&raw_time);

        // Generate timestamp in RFC 2822 format
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %T %z\n", time_info);
        
        // Lock the mutex to write timestamp atomically
        pthread_mutex_lock(&file_mutex);
        // Open the file in append mode
        int write_file = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
        if (write_file < 0) {
            printf("Failed to open file for writing timestamp.\n");
            syslog(LOG_ERR, "Error: Failed to open file for writing client data.");
            pthread_mutex_unlock(&file_mutex);
            cleanup_all();
        }

        // Write the timestamp to the file, unlock mutex and sleep
        write(write_file, timestamp, strlen(timestamp));
        close(write_file);
        printf("Just wrote timestamp %s to file.\n", timestamp);
        pthread_mutex_unlock(&file_mutex);
        // sleep(TSTAMP_INTERVAL);  
    }
    remove_thread(pthread_self());
    pthread_exit(NULL);
}


// Function to handle each client in a separate thread
void* handle_client_IO(void* arg) {
    int client_socket = *((int*)arg);
    char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
    size_t buffer_size = BUFFER_SIZE;
    int bytes_received;
    int total_received = 0;

    free(arg);  // Free the allocated memory for client_socket

    // Synchronize access to the file using the mutex
    pthread_mutex_lock(&file_mutex);
    int write_file = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
    if (write_file < 0) {
        printf("Failed to open file for writing data.\n");
        syslog(LOG_ERR, "Error: Failed to open file for writing client data.");
        pthread_mutex_unlock(&file_mutex);
        free(buffer);
        close(client_socket);
        cleanup_all();
    }
    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes_received <= 0) {
            printf("Failed to read data from the client.");
            syslog(LOG_ERR, "Error: Failed to Read from Client.");
            pthread_mutex_unlock(&file_mutex);
            close(client_socket);
            cleanup_all();
        }
        
        // realloc buffer size if data larger that 1024 bytes
        total_received += bytes_received;
        if (total_received >= buffer_size) {
            buffer_size += 512;
            buffer = (char *)realloc(buffer, buffer_size * sizeof(char));
        }

        printf("Just recieved a new byte = %s \n", buffer);
        // Write received message to the file
        write(write_file, buffer, total_received);
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
                cleanup_all();
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
                    cleanup_all();
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
    remove_thread(pthread_self());
    pthread_exit(NULL);
    return 0;
}