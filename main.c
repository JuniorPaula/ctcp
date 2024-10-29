#include<stdio.h>
#include<netinet/in.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h> // for close
#include<arpa/inet.h> // for inet_ntop()

#define PORT 6969
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

MessageQueue message_queue;
BannedIP *banned_ips_head = NULL;
pthread_mutex_t banned_ips_mutex = PTHREAD_MUTEX_INITIALIZER;

void init_message_queue(MessageQueue *queue);
int dequeue_message(MessageQueue *queue, Message *message);
int is_ip_banned(const char *ip, time_t *banned_at);
void remove_banned_ip(const char *ip);
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
      }
    }
  }
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
