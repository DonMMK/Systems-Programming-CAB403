#define _GNU_SOURCE
#include <stdio.h>   /* standard I/O routines                     */
#include <pthread.h> /* pthread functions and data structures     */
#include <stdlib.h>  /* rand() and srand() functions              */
#include <time.h>
#include <unistd.h>

/* number of threads used to service requests */
#define NUM_HANDLER_THREADS 3

/* global mutex for our program. */
pthread_mutex_t request_mutex;
pthread_mutex_t quit_mutex;

/* global condition variable for our program. */
pthread_cond_t got_request;

int num_requests = 0; /* number of pending requests, initially none */
int quit = 0;

/* format of a single request. */
struct request
{
    void (*func)(void *);
    void *data;
    struct request *next; /* pointer to next request, NULL if none. */
};

struct request *requests = NULL;     /* head of linked list of requests. */
struct request *last_request = NULL; /* pointer to last request.         */

/*
 * function add_request(): add a request to the requests list
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    none.
 */
void add_request(void (*func)(void *),
                 void *data,
                 pthread_mutex_t *p_mutex,
                 pthread_cond_t *p_cond_var)
{
    struct request *a_request; /* pointer to newly added request.     */

    /* create structure with new request */
    a_request = (struct request *)malloc(sizeof(struct request));
    if (!a_request)
    { /* malloc failed?? */
        fprintf(stderr, "add_request: out of memory\n");
        exit(1);
    }
    a_request->func = func;
    a_request->data = data;
    a_request->next = NULL;

    /* lock the mutex, to assure exclusive access to the list */
    pthread_mutex_lock(p_mutex);

    /* add new request to the end of the list, updating list */
    /* pointers as required */
    if (num_requests == 0)
    { /* special case - list is empty */
        requests = a_request;
        last_request = a_request;
    }
    else
    {
        last_request->next = a_request;
        last_request = a_request;
    }

    /* increase total number of pending requests by one. */
    num_requests++;

    /* unlock mutex */
    pthread_mutex_unlock(p_mutex);

    /* signal the condition variable - there's a new request to handle */
    pthread_cond_signal(p_cond_var);
}

/*
 * function get_request(): gets the first pending request from the requests list
 *                         removing it from the list.
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * output:    pointer to the removed request, or NULL if none.
 * memory:    the returned request need to be freed by the caller.
 */
struct request *get_request()
{
    struct request *a_request; /* pointer to request.                 */

    if (num_requests > 0)
    {
        a_request = requests;
        requests = a_request->next;
        if (requests == NULL)
        { /* this was the last request on the list */
            last_request = NULL;
        }
        /* decrease the total number of pending requests */
        num_requests--;
    }
    else
    { /* requests list is empty */
        a_request = NULL;
    }

    /* return the request to the caller. */
    return a_request;
}

/*
 * function handle_request(): handle a single given request.
 * algorithm: prints a message stating that the given thread handled
 *            the given request.
 * input:     request pointer, id of calling thread.
 * output:    none.
 */
void handle_request(struct request *a_request, int thread_id)
{
    a_request->func(a_request->data);
}

/*
 * function handle_requests_loop(): infinite loop of requests handling
 * algorithm: forever, if there are requests to handle, take the first
 *            and handle it. Then wait on the given condition variable,
 *            and when it is signaled, re-do the loop.
 *            increases number of pending requests by one.
 * input:     id of thread, for printing purposes.
 * output:    none.
 */
void *handle_requests_loop(void *data)
{
    struct request *a_request;      /* pointer to a request.               */
    int thread_id = *((int *)data); /* thread identifying number           */

    /* lock the mutex, to access the requests list exclusively. */
    pthread_mutex_lock(&request_mutex);

    /* while still running.... */
    int running = 1;
    while (running)
    {
        if (num_requests > 0)
        { /* a request is pending */
            a_request = get_request();
            if (a_request)
            { /* got a request - handle it and free it */
                /* unlock mutex - so other threads would be able to handle */
                /* other requests waiting in the queue paralelly.          */
                pthread_mutex_unlock(&request_mutex);

                //TO DO - UNLOCCK MUTEX, CALL FUNCTION TO HANDLE REQUEST AND RELOCK MUTEX
                // DYNAMIC MEMORY ALLOCATED _ HOW DO WE RECLAIM MEMORY
                handle_request(a_request, thread_id);

                free(a_request);

                /* and lock the mutex again. */
                pthread_mutex_lock(&request_mutex);
            }
        }
        else
        {
            /* wait for a request to arrive. note the mutex will be */
            /* unlocked here, thus allowing other threads access to */
            /* requests list.                                       */

            pthread_cond_wait(&got_request, &request_mutex);
            /* and after we return from pthread_cond_wait, the mutex  */
            /* is locked again, so we don't need to lock it ourselves */
        }

        pthread_mutex_lock(&quit_mutex);
        if (quit) running = 0;
        pthread_mutex_unlock(&quit_mutex);
    }
    pthread_mutex_unlock(&request_mutex);
    return NULL;
}

void test_func(void *arg)
{
    printf("Called\n");
}

void squareValue(void *arg)
{
    int *value = arg;
    *value = *value * *value;
}

/* like any C program, program's execution begins in main */
int main(int argc, char *argv[])
{
    int i;                                    /* loop counter          */
    int thr_id[NUM_HANDLER_THREADS];          /* thread IDs            */
    pthread_t p_threads[NUM_HANDLER_THREADS]; /* thread's structures   */
    struct timespec delay;                    /* used for wasting time */

    int array[] = {1, 12, 21323, 12, 31312, 1, 13, 3,5 ,7, 8, 9,943};

    pthread_mutex_init(&request_mutex, NULL);
    pthread_mutex_init(&quit_mutex, NULL);
    pthread_cond_init(&got_request, NULL);

    /* create the request-handling threads */
    for (i = 0; i < NUM_HANDLER_THREADS; i++)
    {
        thr_id[i] = i;
        pthread_create(&p_threads[i], NULL, handle_requests_loop, &thr_id[i]);
    }

    /* run a loop that generates requests */
    int array_size = sizeof(array) / sizeof(array[0]);
    for (i = 0; i < array_size; i++)
    {
        add_request(squareValue, &array[i], &request_mutex, &got_request);
        /* pause execution for a little bit, to allow      */
        /* other threads to run and handle some requests.  */
        if (rand() > 3 * (RAND_MAX / 4))
        { /* this is done about 25% of the time */
            delay.tv_sec = 0;
            delay.tv_nsec = 10;
            nanosleep(&delay, NULL);
        }
    }

    /* now wait till there are no more requests to process */
    
    pthread_mutex_lock(&quit_mutex);
    quit = 1;
    pthread_mutex_unlock(&quit_mutex);
    pthread_cond_broadcast(&got_request);

    for (i = 0; i < NUM_HANDLER_THREADS; i++)
    {
        pthread_join(p_threads[i], NULL);
    }

    printf("Glory,  we are done.\n");

    for (i = 0; i < array_size; i++)
    {
        printf("%d ", array[i]);
    }
    printf("\n");

    return 0;
}
