/*
A peer that is connected to a server using TCP/IP connection and to other peers using UDP connection.
The peer can:
1) write messages to a chat
2) write these messages to a DB
3) edit an entry of the DB

The peer is built using:
- 4 threads: 1 for sending messages to the server and editiing a DB entry, 1 for sending messages to 
other peers, 1 for receiving messages from other peers, 1 for receiving server messages.
- signals for exiting are implemented
- locking of keys for DB entry editing

menu of commands: /help --> /msg (message), /edit (message) - (number of entry you want to edit), /list, /exit
All users must be active since the beggining.
The DB is implemented using local txt files. The connections are made using Stream and Datagram sockets.
Before sending messages to the chat, or editing you have to run /list from the menu.

Protocols used: Total Order Multicast (chat) , 2 PC (edit), consistent global states (server)
Technologies used: Ubuntu 18.04, gcc 7.5, Coded in C
compile: gcc peer.c -o peer -lpthread
Run: ./peer 9000 (...9256)
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
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>

#define PORT 9000 // first user ever to entry
#define SIZE 256

void edit_wrapper(int critical);
void *server_peer(void*);
void *client_thread(void*);
void parsed_args(int argc, char **argv);
void chat(int argc, char **argv, char message[SIZE]);
void edit_DB_entry(int argc, char **argv, char message[SIZE]);
void DB_write();
void update();
void DB_write_edit();
void *get_messages(void*);
void *send_message(char *msg);
void signal_handler(int);
void consistent(char* message, char* entry, int options);

char mess[SIZE], DB[SIZE][SIZE], file_name[SIZE][2], edit_mess[SIZE], sec[SIZE], serv_message[SIZE], locked_key[SIZE] = "locked: ";
int sockfd, ports[SIZE], number_of_users, serv_port, id, key =0, edit=0, edit_port, k;
pthread_mutex_t lock, lock_edit;
FILE * fp[SIZE];
time_t seconds;
pthread_t client_id;

int main(int argc, char *argv[]) {
    int n, sock;
    struct sockaddr_in serv_addr, peer_addr;
    pthread_t thread_id, serv_id;
    char buffer[SIZE], message[SIZE], command[SIZE], msg_token;
    
    // TCP/IP connection with server
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){
        perror("Error on opening socket");
        exit(1);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(6000);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    printf("Connecting to Messenger Server...\n");
    
    //request to connect
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
        perror("Error on connecting");
        exit(1);
    }

    // signals used for server connection
    static struct sigaction act, act2; 
    act.sa_handler = signal_handler; 
    sigfillset(&(act.sa_mask));  
    sigaction(SIGTSTP, &act, 0);
    signal(SIGINT,signal_handler);

    printf("Connected !\n");
    
    //PORT of this particular peer used a server
    serv_port = atoi(argv[1]);
    if (pthread_create(&serv_id, NULL, server_peer, (void*) &argv) < 0) { /*server_peer= pointer to function*/ 
        perror("Error on creating thread");
        exit(1);
    }
    /* create thread for receiving chat messages from the server */
    if (pthread_create(&thread_id, NULL, get_messages, (void*) &sockfd) < 0){
        perror("Error on creating thread");
        exit(1);
    }
    
    
    while (1) {
        // infinite loop to write messages to server or to chat
        bzero(buffer, SIZE);
        fgets(buffer, SIZE-1, stdin);
        if(strcmp(buffer,"clear\n")==0){
            system("clear");
            bzero(buffer, SIZE);
        }
        n = 0;
        msg_token = buffer[0];
        if (msg_token == '/') {

            /* read buffer into command and message strings */
            bzero(command, SIZE);
            bzero(message, SIZE);
            bzero(sec, SIZE);
            bzero(serv_message, SIZE);
            sscanf(buffer, "%s %[^\n]", command, message);

            /* process chat commands */
            if (strcmp(command, "/msg") == 0) {
                // chat wrapper function
                chat(argc, argv, message);
                sprintf(locked_key, "%d", key);
                consistent(message, locked_key, 1);
            }else if(strcmp(command, "/edit") == 0) {
                k=0;
                // edit the array of ALL PEERS function
                edit_DB_entry(argc, argv, message);
            }else if((strcmp(command, "/ABORT") == 0)||(strcmp(command, "/GO") == 0)){
                // send /GO or /ABORT to peer that requests to edit
                sock = socket(AF_INET, SOCK_DGRAM, 0);
                if (sock < 0){
                    perror("Error on opening socket");
                    exit(1);
                }

                bzero((char *) &peer_addr, sizeof(peer_addr));
                memset(&peer_addr, 0, sizeof(peer_addr));
                peer_addr.sin_family = AF_INET;
                peer_addr.sin_port = htons(edit_port);
                peer_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                if(strcmp(command, "/ABORT") == 0){
                    pthread_mutex_lock(&lock); //-->Total order multicast chat
                    sendto(sock, "/ABORT", sizeof("/ABORT"), 
                    MSG_CONFIRM, (const struct sockaddr *) &peer_addr,
                    sizeof(peer_addr));
                    pthread_mutex_unlock(&lock);
                }
                else if((strcmp(command, "/GO") == 0))
                {   pthread_mutex_lock(&lock); //-->Total order multicast chat
                    sendto(sock, "/GO", sizeof("/GO"), 
                    MSG_CONFIRM, (const struct sockaddr *) &peer_addr,
                    sizeof(peer_addr));
                    pthread_mutex_unlock(&lock);
                }
            }else{
                /* write to server the command*/
                n = write(sockfd, buffer, strlen(buffer));
                if (n < 0)
                    break;
            }
        }
        else {
                printf("Command not found try /help\n");
            }
    }
    // terminating threads
    pthread_join(client_id, NULL);
    pthread_join(serv_id, NULL);
    pthread_join(thread_id, NULL);
    printf("Lost connection to server\n");
    pthread_exit(NULL);
    close(sockfd);
    return 0;
}

// read messages from srever
void *get_messages(void *sock_ptr) {
    int sock, len, n, i=0;
    char buffer[SIZE], cache[SIZE], msg_token, *a, command[SIZE], message[SIZE];
    char *ptr,*shared_users[SIZE];;
    sock = *(int *) sock_ptr;
    
    bzero(cache, SIZE);
    while(1) {
        /* read message into buffer*/
        bzero(buffer, SIZE);
        n = recv(sock, buffer, SIZE-1, MSG_DONTWAIT); /* non-blocking read */

        len = strlen(buffer);

        /* only display new messages */
        if (len > 0 && strcmp(buffer, cache) != 0) {
            printf("\n-%s \n", buffer);
            if(buffer[0]=='P'){ // if message is Peers: ... then this is the list of active peers
                // initialization of file names
                number_of_users = 0;
                for(i=0;i<SIZE;i++){
                    bzero(file_name[i], 2);
                }
                ptr = strtok(buffer, " ");
                while(ptr!=NULL){
                    //shared_users = array with peers id
                    ptr = strtok(NULL, " ");
                    shared_users[number_of_users] = ptr;
                    number_of_users++;
                }
                for(i=0;i<number_of_users-2;i++){
                    // ports  = array with peers id 
                    ports[i] = atoi(shared_users[i]) + PORT;
                }
            }
            if(strcmp(buffer,"You have been disconnected") == 0){
                exit(0);
            }
            bzero(cache, SIZE);
        }
    }
    
    close(sock);
    return NULL;
}

// signal handler for exit
void signal_handler(int signum){
    printf("\nType /exit in order to disconnect. \n");
    return;
}


// chat wrapper function
void chat(int argc, char **argv, char message[SIZE])
{   bzero(mess, SIZE);
    pthread_t thread_id;
    strcpy(mess, message);
    // new thread for sending messages to other peers
    if (pthread_create(&client_id, NULL, client_thread, (void*) &argv) < 0) { /*client_thread= pointer to function*/ 
        perror("Error on creating thread");
        exit(1);
    }
}

// edit wrapper function
void edit_DB_entry(int argc, char **argv, char message[SIZE])
{   char request[SIZE] = "edit request - ";
    strcat(request,argv[1]);
    strcpy(edit_mess, message); //who is sending the edit request
    chat(argc, argv, request);
}

//function to edit DB entry
void edit_wrapper(int critical)
{   //critical --> if all /GO = 0, if one /ABORT =  1
    if( critical == 0){
        strtok(edit_mess, "-");
        char* edit_c = strtok(NULL, "-");
        
        /* key lock for entry*/
        pthread_mutex_lock(&lock_edit);
        edit = atoi(edit_c); // what entry to edit
        sprintf(locked_key, "locked key= %d", edit);
        strcpy(DB[edit], edit_mess); //write new message to entry "edit"
        pthread_mutex_unlock(&lock_edit);
        consistent(edit_mess, locked_key, 2);
        edit=0;
        DB_write_edit(); // edit entry to all peer files
    }
    else if( critical == 1)
        printf("Edit Aborted\n"); // /ABORT
    critical = 0;
}


// edit DB entry in all file copies
void DB_write_edit()
{   int i=0, j=0;
    for(j=0;j<number_of_users-2;j++){
        sprintf(file_name[j], "%d", ports[j]); //filenames=ports.txt
        strcat(file_name[j],".txt");
        fp[j] = fopen (file_name[j],"wb"); //open txt file
        for(i=0;i<key;i++){ // key = #entries
            fprintf (fp[j], DB[i], sizeof(DB[i]));
            fprintf(fp[j], "\n");
        }
        fclose(fp[j]);
    }
}

//write message to DB
void DB_write()
{   int i=0, j=0;
    FILE *f;
    char filename[SIZE];
    sprintf(filename, "%d", serv_port); //file of every peer
    strcat(filename,".txt");

    f = fopen (filename,"wb");
    for(i=0;i<key;i++){  // key = #entries
        fprintf (f, DB[i], sizeof(DB[i]));
        fprintf(f, "\n");
    }
    fclose(f);
}

// updates the DB array from the file
void update()
{   int i=0, j=0;
    FILE *file;
    char filename[SIZE];
    sprintf(filename, "%d", serv_port);
    strcat(filename,".txt");
    int numProgs=0;
    char* programs[50];
    char line[50];

    bzero(DB, SIZE);
    file = fopen (filename,"r");
    while(fgets(line, sizeof(line), file)!=NULL) {
        if(strcmp(line, "\n") != 0){
            line[strlen(line) - 1] = '\0';
            strcpy(DB[i], line);  
            i++;
        }
    }
    fclose(file);
}

// write messages to all peers in chat
void *client_thread(void *args_ptr)
{   int sockfd[SIZE], i=0;
    struct sockaddr_in serv_addr[SIZE];
    char buffer[SIZE];;
    
    
    for(i=0;i<number_of_users-2;i++)
    {   //socket for every peer connection
        sockfd[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd[i] < 0){
            perror("Error on opening socket");
            exit(1);
        }

        bzero((char *) &serv_addr[i], sizeof(serv_addr[i]));
        memset(&serv_addr[i], 0, sizeof(serv_addr[i]));
        serv_addr[i].sin_family = AF_INET;
        serv_addr[i].sin_port = htons(ports[i]);
        serv_addr[i].sin_addr.s_addr = inet_addr("127.0.0.1");

        pthread_mutex_lock(&lock); //-->Total order multicast chat
        sendto(sockfd[i], (const char *)mess, SIZE-1, 
            MSG_CONFIRM, (const struct sockaddr *) &serv_addr[i],
                sizeof(serv_addr[i]));
        pthread_mutex_unlock(&lock);
    }
}

// function for peer server
void* server_peer(void* args_ptr)
{   int sock, clisockfd, n, i = 0, j = 0, len, r=0, ans_m[SIZE], critical=0;
    char buffer[SIZE], cache[SIZE], answer[SIZE];
    socklen_t clilen; /*for the accept*/
    pthread_t thread_id;
    
    //connection for all peers
    struct sockaddr_in serv_addr, cli_addr;
    if ( (sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { // UDP connection
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(&cli_addr, 0, sizeof(cli_addr));
      
    // Filling server information
    serv_addr.sin_family    = AF_INET; // IPv4
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(serv_port);
      
    // Bind the socket with the server address
    if ( bind(sock, (const struct sockaddr *)&serv_addr, 
            sizeof(serv_addr)) < 0 )
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    clilen = sizeof(cli_addr);
    // mutex initialization
    if (pthread_mutex_init(&lock, NULL) < 0){
        perror("Error on initializing mutex");
        exit(1);
    }
  
    len = sizeof(cli_addr);  
    bzero(cache, SIZE);
    while(1) {
        /* read message into buffer*/
        bzero(buffer, SIZE);
        n = recvfrom(sock, (char *)buffer, SIZE-1, 
                MSG_DONTWAIT, ( struct sockaddr *) &cli_addr,
                &len); // non blocking receive
        
        len = strlen(buffer);
        buffer[n] = '\0';
        // read only new messages
        if (len > 0 && strcmp(buffer, cache) != 0) {
            strtok(buffer, "-");
            char* edit_c = strtok(NULL, "-"); 
            
            if(edit_c != NULL){
                edit_port = atoi(edit_c); // port of peer that requests to edit
                if(serv_port!=edit_port)
                    printf("Type /ABORT or /GO:\n"); //this message is printed only to those who dont request to edit this message
            }
            else if((strcmp(buffer,"/ABORT") == 0)||(strcmp(buffer,"/GO") == 0)){
                k = k+1; // #of replies to edit request
                if(strcmp(buffer,"/ABORT") == 0){
                    ans_m[k-1] = 1;
                }
                else if(strcmp(buffer,"/GO") == 0){
                    ans_m[k-1] = 0;
                     
                }
                critical = 0;
                if(k==number_of_users-3){ // check if we got replies from all users
                    for(r=0;r<k;r++){ // we run the whole array of ans_m for /ABORT messages
                        if(ans_m[r] == 1){
                            critical = 1; //ABORTING
                        }
                    }
                    edit_wrapper(critical);
                }
            }
            else{
                // the new messages of the chat
                printf("\n-%s \n", buffer);
                if(key>0){ // if one or more messages are received write it to DB
                    update();
                }
                pthread_mutex_lock(&lock_edit);
                // key lock for editing 
                strcpy(DB[key], buffer); // edit
                key = key +1;
                pthread_mutex_unlock(&lock_edit);

                DB_write();
                
                if(strcmp(buffer,"You have been disconnected") == 0){
                        exit(0);
                }
            }
            bzero(cache, SIZE);
        }
    }
    close(sock);
}

// sending to server the local state i order to create global consistent states
void consistent(char* message, char* entry, int option){
    char command[SIZE];
    int n=0;
    
    bzero(sec, SIZE); // sec --> TS in str
    bzero(serv_message, SIZE);
    bzero(command, SIZE);
    if(option==1){
        command[0]='/'; command[1]='m';command[2]='s';command[3]='g';command[4]= ' '; // command = /msg
    }
    else if(option==2){
        command[0]='/'; command[1]='e';command[2]='d';command[3]='i';command[4]= 't'; command[5]=' ';// command = /edit
    }
    
    sprintf(sec, "%d", time(NULL)); // TS
    strcpy(serv_message, message);
    strcat(serv_message, " | ");
    strcat(serv_message, sec);
    strcat(command, serv_message);
    strcat(command, " | ");
    strcat(command, entry);
    n = write(sockfd, command, strlen(command)); // we write the message we received to server with its TS
    if (n<0)
        exit(0);
}
