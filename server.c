/*
A server that is connected to a p2p system using TCP/IP connection 
The server can:
1) handle the chat commands
2) keep the list of connected users/peers
3) create global consistent states and recover the system
4) handle the signals of a peer departure

The server is built using:
- as many threads as the peers (1 new peer = 1 new thread)
- signals for exiting of the peers are implemented
- locking of shared_id of the connected peers and shared_buffer for sending messages to all peers

menu of commands: /help --> /msg (message), /edit (message) - (number of entry you want to edit), /list
Consistent states: everytime the local txt file of a peer is changed (/msg or /edit) the local states are sent to the server.
The server must check the time of arrival to all peer (TS) and the state of the key-edit.

Protocols used: Total Order Multicast (chat) , 2 PC (edit), consistent global states (server)
Technologies used: Ubuntu 18.04, gcc 7.5, Coded in C
compile: gcc server.c -o server -lpthread
Run: ./server
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>

#define SIZE 256

void *client_thread(void*);
void signal_handler(int);

int sockfd, shared_id = 258, clisockfds[SIZE], climax, k=0, sec[SIZE], rec[SIZE], l=0, j=0, keys[SIZE], edit_keys[SIZE];
char shared_buffer[SIZE*4], shared_users[SIZE][SIZE], *shared_user, archive[SIZE][SIZE], edit[SIZE][SIZE];
pthread_mutex_t lock;

struct thread_args { /*contains the information about the thread*/
    int sock;
    int id;
};

int main(int argc, char *argv[])
{
    int clisockfd, n, i = 0;;
    socklen_t clilen; /*for the accept*/
    pthread_t thread_id;
    struct sockaddr_in serv_addr, cli_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0); /*TCP/IP conection*/
    if (sockfd < 0){
        perror("Error on opening socket");
        exit(1);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(6000);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
        perror("Error on binding");
        exit(1);
    }

    listen(sockfd, 1); /*from one machine so the number of the backlog doesn't matter*/

    clilen = sizeof(cli_addr);

    if (pthread_mutex_init(&lock, NULL) < 0){ //mutex initialization
        perror("Error on initializing mutex");
        exit(1);
    }

    /* zero out shared_users */
    for (n = 0; n < SIZE; n++) {
        memset(shared_users[n], 0, SIZE);
    }

    signal(SIGINT,signal_handler); //signals for the peer departure

    printf("Running Messenger Server...\n");

    /* infinite loop*/
    while (1) {
        // accepting all peer connection requests
        clisockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (clisockfd < 0){
            perror("Error on accept");
            exit(1);
        }

        /* create new thread for client */
        struct thread_args args;
        args.sock = clisockfd;
        j++; // number of peers
        if(strlen(shared_users[j-1]) == 0){
                args.id = j-1;
        }

        /* set default username to shared_users*/
        bzero(shared_users[args.id], SIZE);
        sprintf(shared_users[args.id], "%d", args.id);

        if (pthread_create(&thread_id, NULL, client_thread, (void*) &args) < 0) { /*client_thread= pointer to function*/ 
            perror("Error on creating thread");
            exit(1);
        }

    }

    pthread_join(thread_id, NULL);
    pthread_mutex_destroy(&lock);
    close(sockfd);
    return 0;
}

// created with every accept
void *client_thread(void *args_ptr) {

    struct thread_args *args = (struct thread_args *) args_ptr;
    int sock, id, n, i;
    char buffer[SIZE*2], message[SIZE], command[SIZE], msg_token, *amessage;

    // individual socket info
    sock = args->sock;
    id = args->id;
	
    /* send welcome message to user */
    bzero(buffer, SIZE);
    sprintf(buffer, "Welcome to the Messenger Server: type /help for available commands\n");
    n = write(sock, buffer, SIZE);
    if (n < 0) 
        return NULL;

    /* write new connections to shared buffer*/
    printf("%s has joined\n", shared_users[id]);
	
    while (1) {

        /* read new message into buffer */
        bzero(buffer, SIZE);
        recv(sock, buffer, SIZE-1, MSG_DONTWAIT); /* non-blocking read */
        n = 0;
        msg_token = buffer[0];
        if (msg_token == '/') {

            /* read buffer into command and message strings */
            bzero(command, SIZE);
            bzero(message, SIZE);
            sscanf(buffer, "%s %[^\n]", command, message);

            /* process chat commands */
            if (strcmp(command, "/help") == 0) {
                bzero(buffer, SIZE);

                /* send list of commands */
                sprintf(buffer, "Commands: /msg, /edit, /list, /help, /exit\n");
                n = write(sock, buffer, SIZE);
                if (n < 0 ) break;
            } 
            else if (strcmp(command, "/list") == 0) { //list of connected users
                bzero(buffer, SIZE);
                strncat(buffer, "Peers: ", SIZE); 
                for (n = 0; n < SIZE; n++) {
                    if (strlen(shared_users[n]) > 0) {
                        strncat(buffer, shared_users[n], SIZE); // shared_users--> socket id
                        strncat(buffer, " ", SIZE);
                    }
                }
                strncat(buffer, "\n", SIZE);
                printf("%s\n", buffer);
                n = write(sock, buffer, strlen(buffer)); //write message to peer
                if (n < 0)
                    break;
                strcpy(buffer,shared_buffer);
            } else if (strcmp(command, "/exit") == 0) { // in case a peer wants to depart

                /* send final confirmation to client */
                sprintf(buffer, "You have been disconnected");
                n = write(sock, buffer, strlen(buffer));
                break;
                // global consistent states
            } 
            else if (strcmp(command, "/msg") == 0){ // for when multiple users try to send messages at the same time
                amessage = strtok(message, "|");
                strcpy(archive[l], amessage);
                rec[l] = atoi(strtok(NULL, "|"));
                keys[l] = atoi(strtok(NULL, "|"));
                printf("message received: %s with TS: %d and key: %d\n", archive[l], rec[l], keys[l]);
                if(l>0){
                    if((strcmp(archive[l-1],archive[l])!=0)&(rec[l-1]==rec[l])) // two peers try to access at the same time
                        printf(" %s goes first and %s goes second\n", archive[l-1], archive[l]);
                }
                l = l+1;
            } else if (strcmp(command, "/edit") == 0){ // for when multiple users try to edit a message at the same time
                amessage = strtok(message, "|");
                strcpy(edit[k], amessage);
                sec[k] = atoi(strtok(NULL, "|"));
                edit_keys[k] = atoi(strtok(NULL, "|"));
                printf("message received: %s with TS: %d and key: %d\n", edit[k], sec[k], edit_keys[k]);
                if(k>0){
                    if((strcmp(edit[k-1],edit[k])!=0)&(sec[k-1]==sec[k])&(edit_keys[k-1]==edit_keys[k])) // two peers try to access at the same time
                        printf(" %s goes first and %s goes second\n", edit[k-1], edit[k]);
                }
                k = k+1;
            }else { // if the command is not found in /help
                bzero(buffer, SIZE);
                sprintf(buffer, "%s: command not found, try /help\n", command);
                n = write(sock, buffer, strlen(buffer));
                if (n < 0)
                    break;
            }
        }

    }
    printf("%s has exited\n", shared_users[id]);
    pthread_mutex_lock(&lock);
    memset(shared_users[id], 0, SIZE);  // critical region, all threads have access
	
    pthread_mutex_unlock(&lock);
    close(sock);
    return NULL;
}

// server departing
void signal_handler(int signnum){

        int i;

        printf("\nServer shutting down.. \n");  //the handler for the signal SIGINT ( ctrl-c )
		
        pthread_mutex_destroy(&lock);                                              
        close(sockfd);
        sleep(1);
        exit(0);
}
