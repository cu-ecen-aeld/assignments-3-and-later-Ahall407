#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <pthread.h>

#define PORT 9000
#define BACKLOG 5
#define DATA_FILE "/var/tmp/aesdsocket"
#define TSTAMP_INTERVAL 10

volatile sig_atomic_t run = 1; // Flag for main loop

struct ThreadNode {
    pthread_t tid;           // Thread ID
    struct ThreadNode *next; // Pointer to the next node
};

int server_socket;

// Define a mutex
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
struct ThreadNode *head = NULL; // Global variable to store the head of thread linked list

// Function Delcarations
void cleanup();
void signal_handler();
void create_daemon();
int write_to_file(int client_socket);


int main(int argc, char *argv[]) {
    int daemonize = 0;
    // check for daemon mode
    if ((argc == 2) && (strcmp(argv[1], "-d") == 0)) {
        daemonize = 1;
    }
    
    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);
    

    openlog("aesdsocket", 0, LOG_USER); /* Initialize syslog */
    syslog(LOG_INFO, "Starting up AESD Socket");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create Server Socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) { //socket error handling
        syslog(LOG_ERR, "Error creating socket");
        cleanup(server_socket);
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT); // Replace with your desired port

    // Set SO_REUSEADDR option
    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        syslog(LOG_ERR, "ERROR setting SO_REUSEADDR");
        cleanup(server_socket);
        exit(EXIT_FAILURE);
    }

    // Bind socket to address
    if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "ERROR on binding");
        cleanup(server_socket);
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_socket, BACKLOG) < 0) {
        syslog(LOG_ERR, "Error listening on socket.");
        close(server_socket);
        cleanup(server_socket);
        exit(EXIT_FAILURE);
    }

    if (daemonize) {
        create_daemon();
    }

    pthread_t thread_id, timestamp_thread;
    // Create Timing Thread
    if (pthread_create(&timestamp_thread, NULL, append_timestamp, NULL) != 0) {
        syslog(LOG_ERR, "Failure during timestamp thread creation.");
        cleanup(server_socket);
        exit(EXIT_FAILURE);
    }

    while( run > 0 ) {
        int client_socket = malloc(sizeof(int));
        // Accepting client connections
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, (socklen_t*)&client_len)) < 0) {
            syslog(LOG_ERR, "Failed to accept connection.");
            exit(EXIT_FAILURE);
        }

        // Log accepted connection
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        int result = write_to_file(client_socket);
        if (result < 0) {
            syslog(LOG_ERR, "Error handling request from %s", client_ip);
        } else if (result == 0) {
            // Indicate that the client has disconnected properly
            syslog(LOG_INFO, "Closed connection from %s", client_ip);
        }

        close(client_socket);
        client_socket = -1;
    }

    syslog(LOG_INFO, "The end of the script has completed gracefully");
    cleanup(server_socket);
    return 0;

}

//cleanup steps
void cleanup(int temp_socket) {
    // Clean up and exit
    syslog(LOG_INFO, "Cleaning up and exiting");

    // Close server socket
    if (shutdown(temp_socket, SHUT_RDWR) == -1) {
        syslog(LOG_ERR, "Error shutting down server socket");
    }

    if (close(temp_socket) == -1) {
        syslog(LOG_ERR, "Error closing server socket");
    }

    unlink(DATA_FILE); // Delete the data file
    syslog(LOG_INFO, "Cleanup was reached!");
    closelog();        // Close syslog
    exit(EXIT_SUCCESS);
}

//Signal Handler
void signal_handler(int sign) {
    if (sign == SIGINT || sign == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        run = 0;
        cleanup(server_socket);
        return;
    }
}

void create_daemon() {
    pid_t pid = fork();
        if ( pid < 0 ) {
            syslog(LOG_ERR, "Fork Failed.");
            cleanup(server_socket);
            exit(EXIT_FAILURE);
        }
        if ( pid > 0 ) {
            syslog(LOG_INFO, "Successfully created daemon.");
            exit(EXIT_SUCCESS);
        }

        //stop output to the terminal
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
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

// Function to append timestamp every 10 seconds
void *append_timestamp(void *args) {
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
    pthread_exit(NULL);
}

// Rec and Send file data from server and client
int write_to_file(int clientfd) {
    int valread;
    char buffer[1024];

    // Open File to write or append to
    FILE *fp = fopen(DATA_FILE, "a+");
    if (fp == NULL) {
        syslog(LOG_ERR, "Server failed to open file.");
        exit(EXIT_FAILURE);
    }

    // Write string to file until eol is reached
    while ((valread = recv(clientfd, buffer, sizeof(buffer) -1, 0)) > 0) {
        buffer[valread] = '\0';
        fputs(buffer, fp);
        fflush(fp); 
        if (strchr(buffer, '\n')) break;
    }

    // Send file back to client until eof is reached
    fseek(fp, 0, SEEK_SET);
    while ((valread = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(clientfd, buffer, valread, 0);
    }
    fclose(fp);
    return 0;
}