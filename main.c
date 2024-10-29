#include<stdio.h>
#include<netinet/in.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h> // for close
#include<arpa/inet.h> // for inet_ntop()
#include<sys/types.h>
#include<sys/socket.h>

#define PORT 6969
#define SAFE_MODE 1
#define BUFFER_SIZE 64
#define MAX_MESSAGES 100
#define BAN_LIMIT 600.0

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
int dequeue_message(MessageQueue *queue, Message *message);
int is_ip_banned(const char *ip, time_t *banned_at);
void remove_banned_ip(const char *ip);
const char *sensitive(const char *message);
void add_client(Client *client);
void remove_client(int conn_fd);
void *server_thread(void *arg);

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
    }
  }
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
