#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <syslog.h>
#include <errno.h>

#define PORT 9000
#define BACKLOG 3
#define FILE_IO "/var/tmp/aesdsocket"

int run = 1; // Flag for main loop
int serverfd = -1, clientfd = -1; // File descriptors

int write_to_file(int clientfd);
void cleanup();
void signal_handler();


int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);
    int opt = 1;

    openlog(NULL, 0, LOG_USER); /* Initialize syslog */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) { //socket error handling
        syslog(LOG_ERR, "Error creating socket");
        cleanup();
        exit(1);
    }

    // Set SO_REUSEADDR option
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "ERROR setting SO_REUSEADDR");
        cleanup();
        exit(1);
    }

    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = IN_ADDR_ANY;
    server_addr.sin_port = htons(PORT); // Replace with your desired port

    // Bind socket to address
    if (bind(serverfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "ERROR on binding");
        exit(1);
    }

    // Listen for connections
    if (listen(serverfd, BACKLOG) < 0) {
        syslog(LOG_ERR, "Error listening on socket.");
        close(serverfd);
        cleanup();
        exit(1);
    }

    while( run > 0 ) {

        // Accepting client connections
        if ((clientfd = accept(serverfd, (struct sockaddr *)&client_addr, (socklen_t*)&client_len)) < 0) {
            syslog(LOG_ERR, "Failed to accept connection.");
            exit(1);
        }

        // Log accepted connection
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        int result = write_to_file(clientfd);
        if (result < 0) {
            syslog(LOG_ERR, "Error handling request from %s", client_ip);
        } else if (result == 0) {
            // Indicate that the client has disconnected properly
            syslog(LOG_INFO, "Client %s disconnected", client_ip);
        }

        close(clientfd);
        clientfd = -1;
    }

    syslog(LOG_INFO, "The end of the script has completed gracefully");
    cleanup();
    return 0;

}

//cleanup steps
void cleanup() {
    if (serverfd != -1) close(serverfd);
    if (clientfd != -1) close(clientfd);
    if (remove(FILE_IO) != 0) syslog(LOG_ERR, "Failed to remove file: %s", strerror(errno));
    syslog(LOG_INFO, "Cleanup was reached!");
    closelog();
}

//Signal Handler
void signal_handler(int sign) {
    if (sign == SIGINT || sign == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        run = 0;
        cleanup();
        return;
    }
}

// Rec and Send file data from server and client
int write_to_file(int clientfd) {
    int valread;
    char buffer[1024] = {0};

    // Open File to write or append to
    FILE *fp = fopen(FILE_IO, "a+");
    if (fp == NULL) {
        syslog(LOG_ERR, "Server failed to open file.");
        exit(1);
    }

    // Write string to file until eol is reached
    while ((valread = read(clientfd, buffer, 1024)) > 0) {
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