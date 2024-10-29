#include<stdio.h>
#include<netinet/in.h>
#include<pthread.h>
#include<stdlib.h>

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

	return 0;
}
