#include<stdio.h>
#include<netinet/in.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h> // for close
#include<arpa/inet.h> // for inet_ntop()
#include<sys/types.h>
#include<sys/socket.h>
#include<errno.h>

#define PORT 6969
#define SAFE_MODE 1
#define BUFFER_SIZE 64
#define MAX_MESSAGES 100
#define BAN_LIMIT 600.0
#define MESSAGE_RATE 1.0
#define STRIKE_LIMIT 10

typedef enum {
	CLIENT_CONNECTED = 1,
	CLIENT_DISCONNECTED,
	NEW_MESSAGE
} MessageType;

typedef struct {
	MessageType type;
	int conn_fd;
	char text[BUFFER_SIZE];
	struct sockaddr_in addr;
} Message;

typedef struct Client {
  int conn_fd;
  time_t last_message_time;
  int strike_count;
  struct sockaddr_in addr;
} Client;

typedef struct ClientNode {
  Client client;
  struct ClientNode *next;
} ClientNode;

typedef struct BannedIP {
  char ip[INET_ADDRSTRLEN];
  time_t banned_at;
  struct BannedIP *next;
} BannedIP;

typedef struct {
	Message messages[MAX_MESSAGES];
	int start;
	int end;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} MessageQueue;

// Global variables
MessageQueue message_queue;
ClientNode *clients_head = NULL;
BannedIP *banned_ips_head = NULL;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t banned_ips_mutex = PTHREAD_MUTEX_INITIALIZER;

void init_message_queue(MessageQueue *queue);
void enqueue_message(MessageQueue *queue, Message *message);
int dequeue_message(MessageQueue *queue, Message *message);
int is_ip_banned(const char *ip, time_t *banned_at);
void remove_banned_ip(const char *ip);
const char *sensitive(const char *message);
Client *find_client(int conn_fd);
void add_banned_ip(const char *ip, time_t banned_at);
void add_client(Client *client);
void remove_client(int conn_fd);
void *server_thread(void *arg);
void *client_thread(void *arg);

int main()
{
	int listen_fd;
	struct sockaddr_in serv_addr;

	init_message_queue(&message_queue);

	pthread_t server_tid;
	pthread_create(&server_tid, NULL, server_thread, (void *)&message_queue);

  // create socket
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // set socket options to reuse address
  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // bind socket
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(PORT);

  if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("bind");
    close(listen_fd);
    exit(EXIT_FAILURE);
  }

  // listen
  if (listen(listen_fd, 10) < 0) {
    perror("listen");
    close(listen_fd);
    exit(EXIT_FAILURE);
  }

  printf("Listeing to TCP connections on port %d ...\n", PORT);

  while(1) {
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    int *conn_fd_ptr = malloc(sizeof(int));
    if (conn_fd_ptr == NULL) {
      perror("maloc");
      continue;
    }

    *conn_fd_ptr = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
    if (*conn_fd_ptr < 0) {
      perror("accept");
      free(conn_fd_ptr);
      continue;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(cli_addr.sin_addr), ip, INET_ADDRSTRLEN);
    printf("Accepted connection from %s\n", sensitive(ip));

    // send client connected message to server
    Message msg;
    msg.type = CLIENT_CONNECTED;
    msg.conn_fd = *conn_fd_ptr;
    msg.addr = cli_addr;
    enqueue_message(&message_queue, &msg);

    // start client thread
    pthread_t client_tid;
    pthread_create(&client_tid, NULL, client_thread, conn_fd_ptr);
    pthread_detach(client_tid); // detach thread to avoid memory leek
  }

  close(listen_fd);
	return 0;
}

void init_message_queue(MessageQueue *queue)
{
  queue->start = 0;
  queue->end = 0;
  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->cond, NULL);
}

const char *sensitive(const char *message)
{
  if (SAFE_MODE) {
    return "[REDACTED]";
  } else {
    return message;
  }
}

void *server_thread(void *arg)
{
  MessageQueue *queue = (MessageQueue *)arg;

  while(1) {
    Message msg;
    dequeue_message(queue, &msg);
    switch(msg.type) {
      case CLIENT_CONNECTED: {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(msg.addr.sin_addr), ip, INET_ADDRSTRLEN);
        time_t banned_at;
        int is_banned = is_ip_banned(ip, &banned_at);
        time_t now = time(NULL);

        if (is_banned) {
          double elapsed = difftime(now, banned_at);
          if (elapsed >= BAN_LIMIT) {
            remove_banned_ip(ip);
            is_banned = 0;
          }
        }

        if (!is_banned) {
          printf("Client %s connected\n", sensitive(ip));
          Client client;
          client.conn_fd = msg.conn_fd;
          client.last_message_time = now;
          client.strike_count = 0;
          client.addr = msg.addr;
          add_client(&client);
        } else {
          double remaining = BAN_LIMIT - difftime(now, banned_at);
          char buffer[128];
          snprintf(buffer, sizeof(buffer), "You are banned MF: %f secs left\n", remaining);

          send(msg.conn_fd, buffer, strlen(buffer), 0);
          close(msg.conn_fd);
        }
        break;
      }
      case CLIENT_DISCONNECTED: {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(msg.addr.sin_addr), ip, INET_ADDRSTRLEN);
        printf("Client %s disconnected\n", sensitive(ip));
        remove_client(msg.conn_fd);
        break;
      }
      case NEW_MESSAGE: {
        Client *author = find_client(msg.conn_fd);
        time_t now = time(NULL);
        if (author != NULL) {
          double seconds_since_last_message = difftime(now, author->last_message_time);

          if (seconds_since_last_message >= MESSAGE_RATE) {
            author->last_message_time = now;
            author->strike_count = 0;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(author->addr.sin_addr), ip, INET_ADDRSTRLEN);
            printf("Client %s sent message %s", sensitive(ip), msg.text);

            // forward message to other clients
            pthread_mutex_lock(&clients_mutex);
            ClientNode *curr = clients_head;
            while(curr != NULL) {
              if (curr->client.conn_fd != msg.conn_fd) {
                int n = send(curr->client.conn_fd, msg.text, strlen(msg.text), 0);
                if (n < 0) {
                  printf("Cloud not send data to %s: %s\n", sensitive(ip), strerror(errno));
                }
              }
              curr = curr->next;
            }
            pthread_mutex_unlock(&clients_mutex);
          } else {
            author->strike_count++;
            if (author->strike_count >= STRIKE_LIMIT) {
              char ip[INET_ADDRSTRLEN];
              inet_ntop(AF_INET, &(author->addr.sin_addr), ip, INET_ADDRSTRLEN);
              add_banned_ip(ip, now);
              send(author->conn_fd, "You are banned\n", 16, 0);
              close(author->conn_fd);
              remove_client(author->conn_fd);
            }
          }
        } else {
          // close connection
          close(msg.conn_fd);
        }
        break;
      }
      default:
        break;
    }
  }
  return NULL;
}

void *client_thread(void *arg)
{
  int conn_fd = *(int *)arg;
  free(arg);
  char buffer[BUFFER_SIZE];

  while(1) {
    int n = recv(conn_fd, buffer, BUFFER_SIZE, 0);
    if (n <= 0) {
      if (n < 0) {
        perror("recv");
      }
      // send client disconnected message to server
      Message msg;
      msg.type = CLIENT_DISCONNECTED;
      msg.conn_fd = conn_fd;
      // get client's addr
      Client *client = find_client(conn_fd);
      if (client != NULL) {
        msg.addr = client->addr;
      }
      enqueue_message(&message_queue, &msg);
      break;
    }
    buffer[n] = '\0';
    // sen new message to server
    Message msg;
    msg.type = NEW_MESSAGE;
    msg.conn_fd = conn_fd;
    strncpy(msg.text, buffer, BUFFER_SIZE);
    enqueue_message(&message_queue, &msg);
  }

  return NULL;
}

Client *find_client(int conn_fd)
{
  pthread_mutex_lock(&clients_mutex);
  ClientNode *curr = clients_head;
  while(curr != NULL) {
    if (curr->client.conn_fd == conn_fd) {
      pthread_mutex_unlock(&clients_mutex);
      return &curr->client;
    }
    curr = curr->next;
  }
  pthread_mutex_unlock(&clients_mutex);
  return NULL;
}

void add_client(Client *client)
{
  pthread_mutex_lock(&clients_mutex);
  ClientNode *node = (ClientNode *)malloc(sizeof(ClientNode));
  if (node == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  node->client = *client;
  node->next = clients_head;
  clients_head = node;
  pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int conn_fd)
{
  pthread_mutex_lock(&clients_mutex);
  ClientNode *prev = NULL;
  ClientNode *curr = clients_head;
  while(curr != NULL) {
    if (curr->client.conn_fd == conn_fd) {
      if (prev == NULL) {
        clients_head = curr->next;
      } else {
        prev->next = curr->next;
      }
      close(curr->client.conn_fd);
      free(curr);
      break;
    }
    prev = curr;
    curr = curr->next;
  }
  pthread_mutex_unlock(&clients_mutex);
}

void enqueue_message(MessageQueue *queue, Message *message)
{
  pthread_mutex_lock(&queue->mutex);
  int next_end = (queue->end + 1) % MAX_MESSAGES;
  if (next_end == queue->start) {
    // queue is full, drop the message
    printf("Message queue is full, dropping message\n");
  } else {
    queue->messages[queue->end] = *message;
    queue->end = next_end;
    pthread_cond_signal(&queue->cond);
  }
  pthread_mutex_unlock(&queue->mutex);
}

int dequeue_message(MessageQueue *queue, Message *message)
{
  pthread_mutex_lock(&queue->mutex);
  while(queue->start == queue->end) {
    pthread_cond_wait(&queue->cond, &queue->mutex);
  }

  *message = queue->messages[queue->start];
  queue->start = (queue->start + 1) % MAX_MESSAGES;
  pthread_mutex_unlock(&queue->mutex);
  return 1;
}

int is_ip_banned(const char *ip, time_t *banned_at)
{
  pthread_mutex_lock(&banned_ips_mutex);
  BannedIP *curr = banned_ips_head;
  while(curr != NULL) {
    if (strcmp(curr->ip, ip) == 0) {
      *banned_at = curr->banned_at;
      pthread_mutex_unlock(&banned_ips_mutex);
      return 1;
    }
    curr = curr->next;
  }
  pthread_mutex_unlock(&banned_ips_mutex);
  return 0;
}

void add_banned_ip(const char *ip, time_t banned_at)
{
  pthread_mutex_lock(&banned_ips_mutex);
  BannedIP *node = (BannedIP *)malloc(sizeof(BannedIP));
  if (node == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  strncpy(node->ip, ip, INET_ADDRSTRLEN);
  node->banned_at = banned_at;
  node->next = banned_ips_head;
  banned_ips_head = node;
  pthread_mutex_unlock(&banned_ips_mutex);
}

void remove_banned_ip(const char *ip)
{
  pthread_mutex_lock(&banned_ips_mutex);
  BannedIP *prev = NULL;
  BannedIP *curr = banned_ips_head;
  while(curr != NULL) {
    if (strcmp(curr->ip, ip) == 0) {
      if (prev == NULL) {
        banned_ips_head = curr->next;
      } else {
        prev->next = curr->next;
      }
      free(curr);
      break;
    }
    prev = curr;
    curr = curr->next;
  }
  pthread_mutex_unlock(&banned_ips_mutex);
}
