#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

#define FILE_READ_SIZE 4096
#define REQUEST_MAX_SIZE 8192
struct fd_keep{
  int proxy;
  int client;
  int signal_received;
};
/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;
pthread_t *threads;

char * stitch(char *s1, char*s2){
  char *res = malloc(strlen(s1)+strlen(s2)+1);
  strcpy(res, s1);
  strcat(res, s2);
  return res;
}

void send_not_found(int fd){
  http_start_response(fd, 404);
  http_send_header(fd, "Content-type", http_get_mime_type("i.html"));
  http_send_header(fd, "Server", "httpserver/1.0");
  http_end_headers(fd);
  http_send_string(fd, "<center><h1>404 Not Found<h1/><center/>");
}

void send_file(int fd, char *path)
{
  int file;
  int bytes_read;
  char file_content[FILE_READ_SIZE];

  if ((file = open(path, O_RDONLY)) < 0){
    send_not_found(fd);
    return;
  }
  http_start_response(fd, 200);
  http_send_header(fd, "Content-type", http_get_mime_type(path));
  http_send_header(fd, "Server", "httpserver/1.0");
  http_end_headers(fd);
  while ((bytes_read = read(file, file_content, FILE_READ_SIZE))){
    http_send_data(fd, file_content, bytes_read);
  }
}

char *create_html_link(char *link, char *name){
  char *res = malloc(strlen(name) + strlen(link) + strlen("<a href = \"\"></ a><br/>\n") + 1);
  sprintf(res, "<a href = \"%s\">%s</ a><br/>\n", link , name);
  return res;
}

char *list_dir(char * dir_name, DIR *directory){
  struct dirent *dir_ent;

  char *link;
  char *temp;
  char *html;
  link = create_html_link("../", "Parent directory");
  html = stitch("<html><center>\n", link);
  free(link);
  while ((dir_ent = readdir(directory)))
  {
    if (strcmp(dir_ent->d_name, "..") && strcmp(dir_ent->d_name,"." )){
      link = create_html_link(dir_ent->d_name, dir_ent->d_name);
      temp = html;
      html = stitch(html, link);
      free(temp);
      free(link);
    }
  }
  temp = html;
  html = stitch(html, "<center/><html/>\n");
  free(temp);
  return html;
}

void send_dir(int fd, char *path){
  DIR *directory;
  char *temp;
  if (!(directory = opendir(path))){
    send_not_found(fd);
    return;
  }
  http_start_response(fd, 200);
  http_send_header(fd, "Content-type", http_get_mime_type("i.html"));
  http_send_header(fd, "Server", "httpserver/1.0");
  http_end_headers(fd);
  temp = list_dir(path, directory);
  http_send_string(fd, temp);
  closedir(directory);
  free(temp);
}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
void handle_files_request(int fd) {

  /*
   * TODO: Your solution for Task 1 goes here! Feel free to delete/modify *
   * any existing code.
   */
 
  struct stat statbuf;
  char *file_path;
  char *temp;
  
  struct http_request *request = http_request_parse(fd);

  //  stitch filename
  file_path = stitch(server_files_directory, request->path);
  if (!stat(file_path, &statbuf))
  {
    if (S_ISREG(statbuf.st_mode)) //if file is regular file
      send_file(fd, file_path);
    else if (S_ISDIR(statbuf.st_mode)){ //if file is a directory
      temp = stitch(file_path, "index.html");
      if (!stat(temp, &statbuf))
        send_file(fd, temp);
      else
        send_dir(fd, file_path);
      free(temp);
    }
    else{
      send_not_found(fd);
    }
  }else{
    send_not_found(fd);
  }
  close(fd);
  free(file_path);
  free(request->method);
  free(request->path);
  free(request);
  printf("request handled\n");
}



int write_data(int fd, char *data, size_t size) {
  ssize_t bytes_sent;
  while (size > 0) {
    bytes_sent = write(fd, data, size);
    if (bytes_sent < 0)
      return -1;
    size -= bytes_sent;
    data += bytes_sent;
  }
  //printf("test: file sent to fd : %d \nsize: %d \n", fd, size);
  return 1;
}



void *reroute(void *var)
{
  int input = ((struct fd_keep *)var)->proxy;
  int output = ((struct fd_keep *)var)->client;
  char buf[REQUEST_MAX_SIZE + 1];

  int bytes_read;

  while ((bytes_read = read(input, buf, REQUEST_MAX_SIZE)))
  {
    buf[bytes_read] = 0;
    printf("\nfile read from client / size: %d\n", bytes_read);
    if (write_data(output, buf, bytes_read) < 0)
    {
      printf("writing to proxy failed\n");
      break;
    }
  }
  return NULL;
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {
  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */
  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int client_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(client_socket_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;

  }
  /* 
  * TODO: Your solution for task 3 belongs here! 
  */
  pthread_t proxy_thread;
  pthread_t client_thread;
  struct fd_keep fds;
  struct fd_keep fds2;

  fds.proxy = fd;
  fds.client = client_socket_fd;
  fds.signal_received = 1;

  pthread_create(&client_thread, NULL, reroute, &fds);
  fds2.client = fd;
  fds2.proxy = client_socket_fd;
  pthread_create(&proxy_thread, NULL, reroute, &fds2);

  pthread_join(proxy_thread, NULL);

  pthread_join(client_thread, NULL);
  close(client_socket_fd);
  close(fd);
}

void *thread_handler(void *request_handler)
{
  int client_socket_number;
  while(1){
    client_socket_number = wq_pop(&work_queue);
    ((void *(*)(int))request_handler)(client_socket_number);
  }
  return NULL;
}

void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  /*
   * TODO: Part of your solution for Task 2 goes here!
   */
  wq_init(&work_queue);
  printf("num thdeads %d\n", num_threads);
  //if (server_files_directory)
  //  chdir(server_files_directory);
  threads = malloc(sizeof(pthread_t) * num_threads);//malloc
  for (int i = 0; i < num_threads;i++){
    pthread_create(threads + i, NULL, thread_handler, request_handler);
  }

}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);
  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    // TODO: Change me?
    wq_push(&work_queue, client_socket_number);
    //((void *(*)(int))request_handler)(client_socket_number);
    //close(client_socket_number);

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 --num-threads 5\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 --num-threads 5\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  num_threads = 0;
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if ((server_files_directory == NULL && server_proxy_hostname == NULL) || num_threads == 0){
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
