#include<stdio.h>
#include<netinet/in.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h> // for close

#define PORT 6969
#define BUFFER_SIZE 64
#define MAX_MESSAGES 100

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

typedef struct {
	Message messages[MAX_MESSAGES];
	int start;
	int end;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} MessageQueue;

MessageQueue message_queue;

void init_message_queue(MessageQueue *queue);
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
  printf("%d", queue->start);
}










