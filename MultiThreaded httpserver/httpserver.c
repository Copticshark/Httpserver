#include "asgn4_helper_funcs.h"
#include "connection.h"
#include "debug.h"
#include "request.h"
#include "response.h"
#include "queue.h"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/file.h>

void handle_connection(int);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);

// Global vars------------------------------
queue_t *conn_queue; // Connection queue
pthread_mutex_t
    response_lock; // This lock is to be held when logging and sending a response (like it's atomic)

// Helpers -----------------------------------------

// Audit log function
void audit_log(char *oper, char *uri, int status_code, char *request_id_hval) {
    fprintf(stderr, "%s, %s, %d, %s\n", oper, uri, status_code, request_id_hval);
    fflush(stderr);
}

bool isDir(const char *dirName) {
    struct stat path;
    stat(dirName, &path);
    return S_ISDIR(path.st_mode);
}

// ----------------------------------------------------

// Thread function (worker thread)
void *threadFunc(void *arg) {
    (void) arg;

    while (1) {
        // Get connection fd from the queue
        uintptr_t connfd_in;
        queue_pop(conn_queue, (void **) &connfd_in);
        int connfd = (int) connfd_in;

        // Handle the connection
        handle_connection(connfd);
        close(connfd);
    }
}

int main(int argc, char **argv) {

    if (argc < 2) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    size_t threads = 4;

    int option;
    while ((option = getopt(argc, argv, "t:")) != -1) {
        if (option == 't') {
            char *endptr = NULL;
            threads = (size_t) strtoull(optarg, &endptr, 10);
        } else {
            warnx("wrong option");
            fprintf(stderr, "invalid option\n");
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    size_t port = (size_t) strtoull(argv[optind], &endptr, 10);

    if (endptr && *endptr != '\0') {
        warnx("invalid port number: %s", argv[optind]);
        return EXIT_FAILURE;
    }

    // Initialize socket and deal with signals
    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    int portfail = listener_init(&sock, port);
    if (portfail == -1) {
        warnx("unable to listen on port %s", argv[optind]);
        return EXIT_FAILURE;
    }

    // Initialize queue
    conn_queue = queue_new((int) threads + 1);

    // Initialize the response lock
    pthread_mutex_init(&response_lock, NULL);

    // Initialize the temp file used as a mutex lock with flock()
    int tmp_fd = open(".temp_lock.tmp", O_WRONLY | O_CREAT, 0600);

    // Somehow can't open temp file
    if (tmp_fd < 0) {
        warnx("unable to create temp file");
        return EXIT_FAILURE;
    }

    close(tmp_fd);

    // Spawn worker threads
    pthread_t threadArr[threads];
    for (int i = 0; i < (int) threads; i++) {
        pthread_create(&threadArr[i], NULL, threadFunc, NULL);
    }

    while (1) {
        // Accept connection
        int connfd = listener_accept(&sock);
        if (connfd < 0) {
            continue;
        }

        // Push it to a worker thread
        queue_push(conn_queue, (void *) (uintptr_t) connfd);
    }

    return EXIT_SUCCESS;
}

void handle_connection(int connfd) {
    conn_t *conn = conn_new(connfd);
    const Response_t *res = conn_parse(conn);

    if (res != NULL) {
        //debug("%s\n",conn_str(conn)); fflush(stdout);
        conn_send_response(conn, res);

    } else {

        //debug("%s", conn_str(conn));
        const Request_t *req = conn_get_request(conn);

        if (req == &REQUEST_GET) {
            handle_get(conn);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn);
        } else {
            handle_unsupported(conn);
        }
    }

    conn_delete(&conn);
    return;
}

void handle_get(conn_t *conn) {
    // Get the URI
    char *uri = conn_get_uri(conn);

    // Get the request id
    char *rqid_in = conn_get_header(conn, "Request-Id");
    char *rqid = rqid_in ? rqid_in : "0";

    // Open the temp file
    int tmp_fd = open(".temp_lock.tmp", O_WRONLY | O_CREAT, 0600);

    if (tmp_fd < 0) {
        pthread_mutex_lock(&response_lock);
        audit_log("GET", uri, 500, rqid);
        conn_send_response(conn, &RESPONSE_INTERNAL_SERVER_ERROR);
        pthread_mutex_unlock(&response_lock);

        flock(tmp_fd, LOCK_UN);
        return;
    }

    // Use temp file as a mutex to serialize opening files
    flock(tmp_fd, LOCK_EX);

    // Check if URI is a directory
    if (isDir(uri)) {
        pthread_mutex_lock(&response_lock);
        audit_log("GET", uri, 403, rqid);
        conn_send_response(conn, &RESPONSE_FORBIDDEN);
        pthread_mutex_unlock(&response_lock);

        flock(tmp_fd, LOCK_UN);
        return;
    }

    // Open the file
    int fd = open(uri, O_RDONLY, 0);

    // Can't open file, or doesnt exist
    if (fd < 0) {
        pthread_mutex_lock(&response_lock);
        audit_log("GET", uri, 404, rqid);
        conn_send_response(conn, &RESPONSE_NOT_FOUND);
        pthread_mutex_unlock(&response_lock);

        flock(tmp_fd, LOCK_UN);
        return;
    }

    // Acquire shared flock on the file
    flock(fd, LOCK_SH);

    // Release exclusive flock on the temp file so others can open/check existence
    flock(tmp_fd, LOCK_UN);
    close(tmp_fd);

    // Get the size of the file
    int fileSize = lseek(fd, 0, SEEK_END);
    if (fileSize < 0) {
        pthread_mutex_lock(&response_lock);
        audit_log("GET", uri, 500, rqid);
        conn_send_response(conn, &RESPONSE_INTERNAL_SERVER_ERROR);
        pthread_mutex_unlock(&response_lock);

        flock(tmp_fd, LOCK_UN);
        return;
    }

    // Return the head to the beginning
    int seek_err = lseek(fd, 0, SEEK_SET);
    if (seek_err < 0) {
        pthread_mutex_lock(&response_lock);
        audit_log("GET", uri, 500, rqid);
        conn_send_response(conn, &RESPONSE_INTERNAL_SERVER_ERROR);
        pthread_mutex_unlock(&response_lock);

        flock(tmp_fd, LOCK_UN);
        return;
    }

    // Acquire the response lock
    pthread_mutex_lock(&response_lock);

    // Read file and send to client
    const Response_t *res = conn_send_file(conn, fd, fileSize);
    if (res != NULL) {
        conn_send_response(conn, res);
        audit_log("GET", uri, response_get_code(res), rqid);
    }

    // Log the request
    audit_log("GET", uri, 200, rqid);

    // Release the response lock
    pthread_mutex_unlock(&response_lock);

    // Release shared flock on the file
    flock(fd, LOCK_UN);
    close(fd);

    return;
}

void handle_put(conn_t *conn) {

    // Get the URI
    char *uri = conn_get_uri(conn);

    // Get the request id
    char *rqid_in = conn_get_header(conn, "Request-Id");
    char *rqid = rqid_in ? rqid_in : "0";

    // Open the temp file
    int tmp_fd = open(".temp_lock.tmp", O_WRONLY | O_CREAT, 0600);

    // Use temp file as a mutex to serialize opening files
    flock(tmp_fd, LOCK_EX);

    // Check if URI is a directory
    if (isDir(uri)) {
        pthread_mutex_lock(&response_lock);
        conn_send_response(conn, &RESPONSE_FORBIDDEN);
        audit_log("PUT", uri, 403, rqid);
        pthread_mutex_unlock(&response_lock);

        flock(tmp_fd, LOCK_UN);
        return;
    }

    // Check if file exists
    bool created = false;
    if (access(uri, F_OK) == -1) {
        // File doesn't exist yet
        created = true;
    }

    // Open the file
    int fd = open(uri, O_WRONLY | O_CREAT, 0664);

    // Can't open file for some reason
    if (fd < 0) {
        pthread_mutex_lock(&response_lock);
        conn_send_response(conn, &RESPONSE_INTERNAL_SERVER_ERROR);
        audit_log("PUT", uri, 500, rqid);
        pthread_mutex_unlock(&response_lock);

        flock(tmp_fd, LOCK_UN);
        return;
    }

    // Acquire exclusive flock on the file, prevents reads or writes from occurring
    flock(fd, LOCK_EX);
    // Release exclusive flock on the temp file so others can open/check existence
    flock(tmp_fd, LOCK_UN);

    ftruncate(fd, 0);

    // Write file
    const Response_t *res = conn_recv_file(conn, fd);

    // Send Response
    pthread_mutex_lock(&response_lock);
    if (res != NULL) {
        conn_send_response(conn, res);
        audit_log("PUT", uri, response_get_code(res), rqid);
    } else {
        conn_send_response(conn, (created) ? &RESPONSE_CREATED : &RESPONSE_OK);
        audit_log("PUT", uri, (created) ? 201 : 200, rqid);
    }
    pthread_mutex_unlock(&response_lock);

    // Release the exclusive flock on the file
    flock(fd, LOCK_UN);
    close(fd);

    return;
}

void handle_unsupported(conn_t *conn) {
    debug("Unsupported request");
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
    return;
}
