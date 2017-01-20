# client-server-chat-program

This project was an exploration of my C programming skills and desire to practice networking and operating systems concepts, through feats such as: handling simultaneous TCP connections, inter-thread communication, and optimizing server performance through aspects such as a message queue and broadcast thread. Locks and semaphores were used.

--

Many users can register to the server with a unique username, send messages to each other, and depart after they are finished with the following commands (case-insensitive):

user [username] - messages are displayed with their associated usernames. usernames are necessary for connecting to the server. this command also allows for renaming usernames if used again

join [server name] [port number] - connect to a chat server if not already connected to one. a join message will be received as confirmation.

send [message] - sends a message to the server, to be broadcasted to every connected client on the server

depart - terminates current connection to the current chat server

exit - exit the entire program

clear - clear the command window text

