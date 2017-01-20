#include "chat.h"
#include "chat_server.h"
#include <string.h>
#include <signal.h>
#include <assert.h> 

static char banner[] =
"Brian Ha";


void server_init(void);
void server_run(void);

void *broadcast_thread_fn(void *);
void *client_thread_fn(void *);

void shutdown_handler(int); 


struct chat_server  chatserver; //local chat_server variable to be used
struct client_queue clientqueue; //local client_queue variable to be used
int port = DEFAULT_LISTEN_PORT; //variable holding default communication port number


/*
 * Use add_client to add clients to CLIENTQUEUE variable. The COUNT variable in its
 * struct is incremented accordingly. Depending on whther CLIENTQUEUE exists or not
 * the HEAD and TAIL are created or modified accordingly.
 */
void add_client(struct chat_client *cli) 
{	
	clientqueue.count++;

	if (clientqueue.head == NULL) {
		clientqueue.head = (struct chat_client *)malloc(sizeof(struct chat_client));		
		memcpy(clientqueue.head, cli, sizeof(struct chat_client));
		clientqueue.head->next = NULL;
		clientqueue.tail = clientqueue.head;
	}
	else {
		clientqueue.tail->next = (struct chat_client *)malloc(sizeof(struct chat_client));
		memcpy(clientqueue.tail->next, cli, sizeof(struct chat_client));
		clientqueue.tail->next->next = NULL;
		clientqueue.tail = clientqueue.tail->next;
	}
}

/*
 * Use client_dup to check if CLIENTQUEUE has a client with the same name as the client that the
 * input argument points to.
 */
int client_dup(char *comp) {
	sem_wait(&clientqueue.cq_lock);
	struct chat_client *temp;
	for (temp = clientqueue.head; temp != NULL; temp = temp->next) {
		if (strcmp(temp->client_name, comp) == 0) {
			sem_post(&clientqueue.cq_lock);
			return 0;
		}
	}
	sem_post(&clientqueue.cq_lock);
	return 1;
}

/*
 * The main server process
 */
int main(int argc, char **argv)
{
    printf("%s\n", banner);
    
    if (argc > 1) {
        port = atoi(argv[1]);
    } else {
        port = DEFAULT_LISTEN_PORT;
    }

    printf("Starting chat server ...\n");
    
    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);

	// Initilize the server
    server_init();
    // Run the server
    server_run();

    return 0;
}


/*
 * Initilize the chatserver
 */
void server_init(void)
{
    // Initilize all related data structures
    // 1. semaphores
    // 2. create the broadcast_thread
	sem_init(&(chatserver.room.chatmsgQ.buffer_empty), 0, MAX_QUEUE_MSG);
	sem_init(&(chatserver.room.chatmsgQ.buffer_full), 0, 0);
	sem_init(&(chatserver.room.chatmsgQ.mq_lock), 0, 1);
	sem_init(&(clientqueue.cq_lock), 0, 1);
	
	pthread_create(&chatserver.room.broadcast_thread, NULL, (void *) &broadcast_thread_fn, (void *) &chatserver.room);
} 

/*
 * Run the chat server 
 */
void server_run(void)
{
	//initialize varaibles to be used throughout this function
	int sockfd, new_fd;
	struct exchg_msg in, out;
	socklen_t sin_size;
	clientqueue.head = NULL;
	clientqueue.tail = NULL;
	clientqueue.count = 0;
	chatserver.room.chatmsgQ.head = 0;
	chatserver.room.chatmsgQ.tail = 0;
	struct sockaddr_in add;
	
	//socket function
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	//set up chatserver's address
	chatserver.address.sin_family = AF_INET;
	chatserver.address.sin_port = htons(port);
	chatserver.address.sin_addr.s_addr = htonl(INADDR_ANY);
	memset(&(chatserver.address.sin_zero), '\0', 8);

	//bind function
	bind(sockfd, (struct sockaddr *)&chatserver.address, sizeof(struct sockaddr));

	//listen function
	listen(sockfd, BACKLOG);

    while (1) {
        // Listen for new connections
        // 1. if it is a CMD_CLIENT_JOIN, try to create a new client_thread
        //  1.1) check whether the room is full or not
        //  1.2) check whether the username has been used or not
        // 2. otherwise, return ERR_UNKNOWN_CMD

		sin_size = sizeof(struct sockaddr_in);

		//accept function
		new_fd = accept(sockfd, (struct sockaddr *)&add, &sin_size);

		//receive function
		recv(new_fd, &in, sizeof(in), 0);

		//if it is a CMD_CLIENT_JOIN, try to create a new client_thread
		if (ntohl(in.instruction) == 100) {
			//check whether the room is full or not
			if (clientqueue.count == MAX_ROOM_CLIENT) {
				out.instruction = htonl(106);
				out.private_data = htonl(201);
				send(new_fd, &out, sizeof(out), 0);
			}
			//check whether the username has been used or not
			else if (client_dup(in.content) == 0){
				out.instruction = htonl(106);
				out.private_data = htonl(200);
				send(new_fd, &out, sizeof(out), 0);
			}
			//client can be added
			else {
				struct chat_client cli;			
				strcpy(cli.client_name, in.content);
				cli.socketfd = new_fd;
				sem_wait(&clientqueue.cq_lock);
				add_client(&cli);
				sem_post(&clientqueue.cq_lock);
				pthread_create(&cli.client_thread, NULL, client_thread_fn, (void *) &cli);
			}
		} else { //unknown command
			out.instruction = htonl(106);
			out.private_data = htonl(202);
			//send function
			send(new_fd, &out, sizeof(out), 0);
		}
    }
}


void *client_thread_fn(void *arg)
{

    // Put one message into the bounded buffer "$client_name$ just joins, welcome!"
    	struct exchg_msg in, out;
	struct chat_client cli = *(struct chat_client *) arg;

	out.instruction = htonl(103);
	out.private_data = htonl(-1);
	
	//send function
	send(cli.socketfd, &out, sizeof(out), 0);

	out.instruction = htonl(104);
	strcpy(out.content, cli.client_name);
	strcat(out.content, " just joins the chat room, welcome!");

	sem_wait(&(chatserver.room.chatmsgQ.buffer_empty));
	sem_wait(&(chatserver.room.chatmsgQ.mq_lock));
	chatserver.room.chatmsgQ.slots[chatserver.room.chatmsgQ.tail] = out.content; //fills SLOTS with join message
	chatserver.room.chatmsgQ.tail = (chatserver.room.chatmsgQ.tail + 1) % MAX_QUEUE_MSG; //increments TAIL accordingly
	sem_post(&(chatserver.room.chatmsgQ.mq_lock));
	sem_post(&(chatserver.room.chatmsgQ.buffer_full));

   while (1) {
        // Wait for incomming messages from this client
        // 1. if it is CMD_CLIENT_SEND, put the message to the bounded buffer
        // 2. if it is CMD_CLIENT_DEPART: 
        //  2.1) send a message "$client_name$ leaves, goodbye!" to all other clients
        //  2.2) free/destroy the resources allocated to this client
        //  2.3) terminate this thread
	
	//receive function
	recv(cli.socketfd, &in, sizeof(in), 0);
	
	//if it is CMD_CLIENT_SEND, put the message to the bounded buffer
	if (ntohl(in.instruction) == 102) {
		out.instruction = htonl(104);
		strcpy(out.content, cli.client_name);
		strcat(out.content, ": ");
		strcat(out.content, in.content);
		sem_wait(&chatserver.room.chatmsgQ.buffer_empty);
		sem_wait(&chatserver.room.chatmsgQ.mq_lock);
		chatserver.room.chatmsgQ.slots[chatserver.room.chatmsgQ.tail] = out.content; //fills SLOTS with send message
		chatserver.room.chatmsgQ.tail = (chatserver.room.chatmsgQ.tail + 1) % MAX_QUEUE_MSG; //increments TAIL accordingly
		sem_post(&chatserver.room.chatmsgQ.mq_lock);
		sem_post(&chatserver.room.chatmsgQ.buffer_full);
	}

	//if it is CMD_CLIENT_DEPART: 
	if (ntohl(in.instruction) == 101) {
		struct chat_client *temp;
		struct chat_client *prev;

		out.instruction = htonl(104);
		strcpy(out.content, cli.client_name);
		strcat(out.content, " leaves, goodbye!");
		sem_wait(&chatserver.room.chatmsgQ.buffer_empty);
		sem_wait(&chatserver.room.chatmsgQ.mq_lock);
		chatserver.room.chatmsgQ.slots[chatserver.room.chatmsgQ.tail] = out.content; //fills SLOTS with depart message
		chatserver.room.chatmsgQ.tail = (chatserver.room.chatmsgQ.tail + 1) % MAX_QUEUE_MSG; //increments TAIL accordingly
		sem_post(&chatserver.room.chatmsgQ.mq_lock);
		sem_post(&chatserver.room.chatmsgQ.buffer_full);
		sem_wait(&clientqueue.cq_lock);
		prev = NULL;
		
		int i = cli.socketfd;
	
		//removes existing client from CLIENTQUEUE as it departs
		for (temp = clientqueue.head; temp != NULL; prev = temp, temp = temp->next) {
			if (strcmp(temp->client_name, cli.client_name) == 0) {
				if (prev == NULL) 
					clientqueue.head = clientqueue.head->next;					
				else 
					prev->next = temp->next;
				clientqueue.count--;
				free(temp); //freed resources here
				break;
			}
		}
		sem_post(&clientqueue.cq_lock);

		//close socket id
		close(i);
		//destroy thread upon exit
		pthread_detach(pthread_self());
		pthread_exit(0);
	}
    }
} 




void *broadcast_thread_fn(void *arg)
{
	
	//necessary variables
	struct chat_room room = *(struct chat_room *) arg;
	struct exchg_msg out;
	struct chat_client *temp;
	char *msg; 

    while (1) {
        // Broadcast the messages in the bounded buffer to all clients, one by one

		sem_wait(&chatserver.room.chatmsgQ.buffer_full);
		sem_wait(&chatserver.room.chatmsgQ.mq_lock);
		msg = chatserver.room.chatmsgQ.slots[chatserver.room.chatmsgQ.head]; //place next message in bounded buffer in MSG variable
		chatserver.room.chatmsgQ.head = (chatserver.room.chatmsgQ.head + 1) % MAX_QUEUE_MSG; //increment HEAD
		sem_post(&chatserver.room.chatmsgQ.mq_lock);
		sem_post(&chatserver.room.chatmsgQ.buffer_empty);
		sem_wait(&clientqueue.cq_lock);
		for (temp = clientqueue.head; temp != NULL; temp = temp->next) { //message is sent to clients in CLIENTQUEUE
			out.instruction = htonl(104);
			strcpy(out.content, msg);
			out.private_data = htonl(strlen(out.content));
			//send function
			send(temp->socketfd, &out, sizeof(out), 0);
		}
		sem_post(&clientqueue.cq_lock);	
    }
	
}



/*
 * Signal handler
 */
void shutdown_handler(int signum)
{
    // Implement server shutdown here
    // 1. send CMD_SERVER_CLOSE message to all clients
    // 2. terminates all threads: broadcast_thread, client_thread(s)
    // 3. free/destroy all dynamically allocated resources: memory, mutex, semaphore, whatever.
	struct exchg_msg out;
	struct chat_client *temp;
	for (temp = clientqueue.head; temp != NULL; temp = temp->next) {
		out.instruction = htonl(105);
		out.private_data = htonl(-1);
		send(temp->socketfd, &out, sizeof(out), 0);
		pthread_cancel(temp->client_thread);
		pthread_join(temp->client_thread, NULL);
	}
    
    sem_destroy(&chatserver.room.chatmsgQ.buffer_empty);
	sem_destroy(&chatserver.room.chatmsgQ.buffer_full);
	sem_destroy(&chatserver.room.chatmsgQ.mq_lock);
	sem_destroy(&clientqueue.cq_lock);
	pthread_cancel(chatserver.room.broadcast_thread);
	pthread_join(chatserver.room.broadcast_thread, NULL);
    exit(0);
}
