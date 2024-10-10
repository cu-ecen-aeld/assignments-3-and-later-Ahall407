#include <stdio.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char **argv) {

    openlog(NULL, 0, LOG_USER);

    if (argc < 3) {
        syslog(LOG_ERR, "Invalid Number of arguments.");
        syslog(LOG_ERR, "Total number of arguments should be 2.");
        syslog(LOG_ERR, "The order of arguments should be:");
        syslog(LOG_ERR, "1) Full path to a file (including filename)");
        syslog(LOG_ERR, "2) Text string to be written to file.");
        return 1;
    }

    const char* file = argv[1];
    const char* text = argv[2];
    syslog(LOG_DEBUG, "Writing %s to %s", text, file);
    
    FILE *f_temp = fopen(file, "w");
    // Check if the file was opened successfully 
    if (f_temp == NULL) { 
        syslog(LOG_ERR, "Could not open the file.");
        return 1; 
    } 
     
    // Write the string to the file 
    fprintf(f_temp, "%s\n", text); 
     
    // Close the file 
    fclose(f_temp); 
     
    return 0; 
}