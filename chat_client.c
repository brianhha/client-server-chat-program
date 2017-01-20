#include "chat.h"
#include <limits.h>
#include <string.h>
#include <curses.h>
#include <sys/ioctl.h>
#include <assert.h>

#ifdef CHAT_PROGRAM_DEBUG
#define DEBUG_DISPLAY(screen, _f, _a...) \
    do { \
        wprintw(screen, "[debug]<%s> " _f "\n", __func__, ## _a);\
		wrefresh(screen);\
    } while (0)
#else
#define DEBUG_DISPLAY(_f, _a...) do {} while (0)
#endif

#define DISPLAY(screen, _f, _a...) \
    do { \
        wprintw(screen,  _f "\n", ## _a);\
        wrefresh(screen);\
    } while (0)

int sockfd;

int send_msg_to_server(int sockfd, char *msg, int command)
{
    struct exchg_msg mbuf;
    int msg_len = 0;
    
    memset(&mbuf, 0, sizeof(struct exchg_msg));
    mbuf.instruction = htonl(command);
    if (command == CMD_CLIENT_DEPART) {
        mbuf.private_data = htonl(-1);
    } else if ( (command == CMD_CLIENT_JOIN) ||
                (command == CMD_CLIENT_SEND) ) {
        memcpy(mbuf.content, msg, strlen(msg));
        msg_len = strlen(msg) + 1;
        msg_len = (msg_len < CONTENT_LENGTH) ? msg_len : CONTENT_LENGTH;
        mbuf.content[msg_len-1] = '\0';
        mbuf.private_data = htonl(msg_len);
    }
    
    if (send(sockfd, &mbuf, sizeof(mbuf), 0) == -1) {
        perror("Server socket sending error");
        return -1;
    }
    
    return 0;
}

int join_server(int sockfd, struct sockaddr_in server_addr, char *user_name, WINDOW *screen)
{
    struct exchg_msg mbuf;
    int reply_instruction;
    int error_code = 0;

    // make a connection to the remote host
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        DISPLAY(screen, "socket connect error (%s)", strerror(errno));
        return -1;
	}
    
    // send a JOIN message
    if (send_msg_to_server(sockfd, user_name, CMD_CLIENT_JOIN) != 0) {
        DISPLAY(screen, "socket send error (%s)", strerror(errno));
        return -1;
    }
    
    // get the response from the server
    if (recv(sockfd, &mbuf, sizeof(mbuf), 0) == -1) {
        DISPLAY(screen, "socket receive error (%s)", strerror(errno));
        return -1;
	}

    reply_instruction = ntohl(mbuf.instruction);
    if (reply_instruction == CMD_SERVER_JOIN_OK) {
        return 0;
    } else if (reply_instruction == CMD_SERVER_FAIL) {
        error_code = ntohl(mbuf.private_data);
        if (error_code == ERR_JOIN_DUP_NAME)
            DISPLAY(screen, "connection failure - your name has been used, please change your name.");
        else if (error_code == ERR_JOIN_ROOM_FULL)
            DISPLAY(screen, "connection failure - the room is full");
        else
            DISPLAY(screen, "connection failure - unknown error");
        return -1;
    } else {
        DISPLAY(screen, "receive unknown reply");
        return -1;
	}

    return 0;
}

/*
 * A separate thread to listen the broadcast message from the server
 * Input parameter: window
 */
void *chat_thread_fn(void *arg)
{
    WINDOW *mywin = (WINDOW *)arg;		
    struct exchg_msg mbuf;
    int instuction;

    DEBUG_DISPLAY(mywin, "Listen thread started");

    // listen to broadcast message until user quits
    while (1) {
        if (recv(sockfd, &mbuf, sizeof(mbuf), 0) <= 0) {
            DISPLAY(mywin, "recv error occurs, exit");
            endwin();
            exit(0);
        }
 
        DEBUG_DISPLAY(mywin, "Listen thread: message received (%d)", ntohl(mbuf.instruction));

        instuction = ntohl(mbuf.instruction);
        if (instuction == CMD_SERVER_BROADCAST) {
            assert(ntohl(mbuf.private_data) <= CONTENT_LENGTH);
            DISPLAY(mywin, "%s", mbuf.content);
        } else if (instuction == CMD_SERVER_CLOSE) {
            DISPLAY(mywin, "******Exit: the chat server closes.******");
            endwin();
            exit(0);
        } else {
            DISPLAY(mywin, "Listen thread got a wrong message");
            break;
        }
    }
 
    DEBUG_DISPLAY(mywin, "chat thread terminates..");
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}


int test_input_error(char *user_command, char *parameter)
{
    int ret = 0;
 
    if (strcasecmp(user_command, "CLEAR") == 0) {
        ret = (parameter == NULL) ? 0 : -1;
    } else if (strcasecmp(user_command, "EXIT") == 0) {
        ret = (parameter == NULL) ? 0 : -1;
    } else if (strcasecmp(user_command, "DEPART") == 0) {
        ret = (parameter == NULL) ? 0 : -1;
    } else if (strcasecmp(user_command, "USER") == 0) {
        ret = (parameter == NULL) ? -1 : 0;
    } else if (strcasecmp(user_command, "JOIN") == 0) {
        ret = (parameter == NULL) ? -1 : 0;
    } else if (strcasecmp(user_command, "SEND") == 0) {
        ret = (parameter == NULL) ? -1 : 0;
    }
    
    return ret;
}

/*
 * The main client function
 */
int main(int argc, char *argv[])
{
    char MENU[] = "[CLEAR] [USER] [JOIN] [SEND] [DEPART] [EXIT]"; // menu title
    char input_buffer[CONTENT_LENGTH * 2];      // input buffer
    char *line, *user_command, *parameter;      // temporary strings
    char user_name[CLIENTNAME_LENGTH];          // the client user_name
    char server_name[HOSTNAME_LENGTH];          // the name of the remote server
    
    WINDOW *cmd_window, *msg_window;            // command and message windows
    int win_width, win_height;                  // window dimension
    int i, j;                                   // some integer variables
	
    pthread_t chat_thread;                      // chat thread

    int is_connected = 0;                       // the connection status 
    int port;
	
    initscr();
    win_height = LINES;
    win_width = COLS;
    cmd_window = newwin( (win_height-3)/2, win_width, (win_height+3)/2, 0);
    msg_window = newwin( (win_height-3)/2, win_width, 0, 0);
    if ((cmd_window == NULL) || (msg_window == NULL)) {
        perror("Error in create window\n");
        endwin();
        exit(0);
    }

    scrollok(cmd_window, TRUE);
    idlok(cmd_window, TRUE);
    echo();
    scrollok(msg_window, TRUE);
    idlok(msg_window, TRUE);
    werase(cmd_window);
    werase(msg_window);

    line = (char *)malloc(win_width);
    if (line == NULL) {
        printf("malloc error\n");
        exit(0);
    }
    for (i = 0; i < win_width; i++) {
        line[i] = '*';
    }

    mvaddstr((win_height-3) / 2, 0, line);
    memset(line, ' ', win_width);
    j = (win_width - strlen(MENU))/2;
    memcpy(line+j, MENU, strlen(MENU));
    mvaddstr(((win_height-3)/2)+1, 0, line);	
    refresh();
	/******************************************************************/
    
    memset(user_name, '\0', CLIENTNAME_LENGTH);
    memset(server_name, '\0', HOSTNAME_LENGTH);
               
    // loop continuously until user departs or exits
    while (1) {	
        wprintw(cmd_window,"@=  ");         //get the user input

        wgetnstr(cmd_window, input_buffer, CONTENT_LENGTH);

        user_command = strtok(input_buffer, " ");

        if (!user_command) 
            continue;
        parameter = strtok(NULL, "\n");

        if (test_input_error(user_command, parameter) != 0) {
            if (parameter == NULL)
                DISPLAY(cmd_window, "Incorrect input: no parameter");
            else
                DISPLAY(cmd_window, "Incorrect input: confusing parameter");
            continue;
        }

        if (strcasecmp(user_command, "CLEAR") == 0) { 
            werase(cmd_window);
        } else if (strcasecmp(user_command, "USER") == 0) {
            if (strlen(parameter) >= CLIENTNAME_LENGTH) {
                DISPLAY(cmd_window, "Your name is too long");
                continue;
            }
            if (is_connected) {
                DISPLAY(cmd_window, "You cannot change your name: Connected.");
                continue;
            }
            memset(user_name, '\0', CLIENTNAME_LENGTH);
            memcpy(user_name, parameter, strlen(parameter));
            DISPLAY(cmd_window, "Your name is %s", user_name);
        } else if (strcasecmp(user_command, "JOIN") == 0) { 
            if (strlen(user_name) == 0) {
                DISPLAY(cmd_window, "Use USER command to register yourself first");
                continue;
            }
            
            if (is_connected) {
                DISPLAY(cmd_window, "Already connected to a server");
                continue;
            } else {
                struct hostent *remote_host;    
                struct sockaddr_in server_addr;    
                char *input_server_name, *input_port;

                input_server_name = strtok(parameter, " ");
                DEBUG_DISPLAY(msg_window, "input_server_name %s", input_server_name);
                strcpy(server_name, input_server_name);
                
                input_port = strtok(NULL, " ");
                DEBUG_DISPLAY(msg_window, "input_port %s", input_port);
                if (input_port == NULL) {
                    DISPLAY(cmd_window, "port number is missed");
                    continue;
                } else {
                    port = atoi(input_port);
                }
                
                if ( (remote_host = gethostbyname(server_name)) == NULL) {  
                    DISPLAY(cmd_window, "Join: cannot resolve the remote host name, %s", server_name);
                    continue;
                }
                
                if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                    DISPLAY(cmd_window, "client socket creation error");
                    goto END;
                }
                
                server_addr.sin_family = AF_INET;		
                server_addr.sin_port = htons(port);
                server_addr.sin_addr = *((struct in_addr *)remote_host->h_addr);
                memset(&(server_addr.sin_zero), '\0', 8);

                if (join_server(sockfd, server_addr, user_name, msg_window) == 0) {
                    is_connected = 1;
                    DISPLAY(cmd_window, "Successfully connected to chat server");
                } else {
                    is_connected = 0;
                    DISPLAY(cmd_window, "Fail to connect to server");
                    close(sockfd);
                    continue;
                }

                if (pthread_create(&chat_thread, NULL, chat_thread_fn, (void *)msg_window) != 0) {
                    DISPLAY(cmd_window, "Fail to start the background thread");
                    close(sockfd);
                    goto END;
                }
            }
        } else if (strcasecmp(user_command, "SEND") == 0) {
            if (!is_connected) {
                DISPLAY(cmd_window, "Not connected, join a server first");
                continue;
        }
            if (send_msg_to_server(sockfd, parameter, CMD_CLIENT_SEND) != 0) {
                DISPLAY(cmd_window, "send message fails");
                close(sockfd);
                goto END;
            }
        } else if (strcasecmp(user_command, "DEPART") == 0) {
            if (is_connected) {
                pthread_cancel(chat_thread);
                pthread_join(chat_thread, NULL);
                
                if (send_msg_to_server(sockfd, NULL, CMD_CLIENT_DEPART) != 0) {
                    DISPLAY(cmd_window, "depart fails");
                }
                close(sockfd);
                is_connected = 0;
                
                DISPLAY(msg_window, "You have left the chat room.");
                DISPLAY(cmd_window, "Disconnected");
            } else {
                DISPLAY(cmd_window, "Meaningless, you are not connected");
            }
        } else if (strcasecmp(user_command, "EXIT") == 0) {
            if (is_connected) {
                pthread_cancel(chat_thread);
                pthread_join(chat_thread, NULL);
                
                if (send_msg_to_server(sockfd, NULL, CMD_CLIENT_DEPART) != 0) {
                    DISPLAY(cmd_window, "depart fails");
                }
                close(sockfd);
                is_connected = 0;
                
                DISPLAY(msg_window, "You have left the chat room.");
                DISPLAY(cmd_window, "Disconnected, exit soon.");
            }
            goto END;
        } else {
            DISPLAY(cmd_window, "Undefined command\n");
        }
    }

END:
    endwin();

    return 0;
} 
