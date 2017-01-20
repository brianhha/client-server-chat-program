#ifndef _CHSERVER_H_
#define _CHSERVER_H_

#include <semaphore.h>

#define DEFAULT_LISTEN_PORT 3500    // the default port number of server/client communication
#define BACKLOG 10                  // the queue length of waiting connections

struct chat_client {
    struct chat_client *next, *prev;
    int socketfd;                           // the socket to communicate with client
    struct sockaddr_in address;	            // remote client address
    char client_name[CLIENTNAME_LENGTH];    // remote client username
    pthread_t client_thread;                // the client thread to receive messages, and then put them in the bounded buffer
};

struct client_queue {
#define MAX_ROOM_CLIENT	20      // max. # of clients allowed
    volatile int count;
    struct chat_client *head, *tail;
    sem_t cq_lock; // mutex lock for accessing the link list
};

struct chatmsg_queue {
#define MAX_QUEUE_MSG	20              // size of the bounded buffer of the chat room
    char *slots[MAX_QUEUE_MSG];

    volatile int head;
    volatile int tail;
    
    sem_t buffer_empty; 
    sem_t buffer_full;  
    sem_t mq_lock;      // mutex lock for accessing the bounded buffer
};


struct chat_room {
    struct chatmsg_queue chatmsgQ;  // the message buffer to queue up chat message for broadcast
    struct client_queue clientQ;	// the corresponding slave thread for each client
    pthread_t broadcast_thread;     // the broadcast thread for sending out messages to all clients
};

struct chat_server {
    struct sockaddr_in address;     // the server's internet address 
    struct chat_room room;
};

#endif
