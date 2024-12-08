#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    int time_to_wait_sec = thread_func_args->wait_to_obtain_ms / 1000;
    int time_to_release_sec = thread_func_args->wait_to_release_ms / 1000;

    // Initial wait
    sleep(time_to_wait_sec);

    // obtain the mutex
    int rc = pthread_mutex_lock(thread_func_args->mutex);
    if ( rc != 0 ) {
        perror("Failed to implement lock.");
        thread_func_args->thread_complete_success = false;
        pthread_exit(thread_param);
    }

    // wait to release the mutex
    sleep(time_to_release_sec);

    // release the mutex
    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if ( rc != 0 ) {
        perror("Failed to release mutex");
        thread_func_args->thread_complete_success = false;
        pthread_exit(thread_param);
    }

    // mark that the thread has successfully completed
    thread_func_args->thread_complete_success = true;
    // Exit the thread and return the thread data
    pthread_exit(thread_param);

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *t_data; 
    
    t_data = (struct thread_data*) malloc(sizeof(struct thread_data));

    t_data->wait_to_obtain_ms = wait_to_obtain_ms;
    t_data->wait_to_release_ms = wait_to_release_ms;
    t_data->mutex = mutex;
    t_data->thread_complete_success = false;
    
    int rc = pthread_create(thread, NULL, threadfunc, t_data);
    if ( rc != 0) {
        perror("Failed to create pthread.");
        free(t_data);
        return false;
    }

    return true;
}