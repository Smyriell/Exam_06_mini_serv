#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

typedef struct s_client { // each client has an id number and buf to save message in case of interrupt sig
    int id;
    char buf[128*1024];
} t_client;

fd_set active_set, write_set, read_set; // fd_sets for select

int maxFd = 0;
int next_id = 0; // value for the next client id

char bufToRead[128*1024]; //buf for recieving data from client to server 
char bufToWrite[128*1024]; //buf for sending data from server to clients

t_client clients[10*1024]; // buf for saving > 10.000 clients

void err(char *msg, int fd) { // print errors and close server fd(socket) if it exists
    if (fd)
        close(fd);
    write(2, msg, strlen(msg));
    exit(1);
}

void sendToAll(int *senderFd) { // send data from server to clients, exept of client, who wrote that message(sent data)
   for (int i = 0; i < maxFd; i++) {
       if (FD_ISSET(i, &write_set) && i != *senderFd)
        send(i, bufToWrite, strlen(bufToWrite), 0);
   }
}

int main(int argc, char* argv[]) {
    if (argc != 2)
        err("Wrong number of arguments\n", 0);

    int servSocket = socket(PF_INET, SOCK_STREAM, 0);// create listening socket for server 
    if (servSocket == -1)
        err("Fatal error\n", 0);

    bzero(bufToRead, sizeof(bufToRead)); //zeroing bufs for further using
    bzero(bufToWrite, sizeof(bufToWrite));

    FD_ZERO(&active_set); //zeroing sets for further using
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);

    FD_SET(servSocket, &active_set); // adding server socket to the active_set 

    maxFd = servSocket + 1; // maxFd for select, it should be always + 1 to the max value of using fd

    struct sockaddr_in addr; // struct for binding listenning socket
    memset(&addr, 0, sizeof(addr)); //zeroing struct

    addr.sin_family = PF_INET; // NOT AF_INET!!!!
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // probably it's forbidden to use INADDR_ANY in exam , so use: htonl(0x7f000001); It's == "127.0.0.0.1"
    socklen_t addr_size = sizeof(addr);

    struct sockaddr_storage client_addr; // struct for accepting client and creating a socket for him 
    memset(&client_addr, 0, sizeof(client_addr)); //zeroing struct
    socklen_t client_addr_size = sizeof(client_addr); 

    if ((bind(servSocket, (const struct sockaddr *)&addr, addr_size)) != 0) // binding server's socket
        err("Fatal error\n", servSocket);

    if (listen(servSocket, 10) != 0) // making server's socket as a listening, max 10 clients in a queue
        err("Fatal error\n", servSocket);
    
    while (1) {
        read_set = write_set = active_set; // after we finished all procedures after select, we need to update our fd_sets, it should have all active FDs(we could remove or add some clients) 

        if (select(maxFd, &read_set, &write_set, NULL, NULL) < 0)
            continue;

        for (int fd = 0; fd < maxFd; fd++) {
            if (FD_ISSET(fd, &read_set) && fd == servSocket) { // if there is a changing in read_set at servSocket, means we got a new client is awaiting to be connected 
                
                int clientSock = accept(servSocket, (struct sockaddr *)&client_addr, &client_addr_size);
                if (clientSock < 0)
                    continue;
                maxFd = clientSock > maxFd - 1 ? clientSock + 1 : maxFd; // changing maxFd
                clients[clientSock].id = next_id++; // next client will have an ++id
                bzero(clients[clientSock].buf, sizeof(clients[clientSock].buf)); // zeroing client's buf (in case there is some garbage) 
                FD_SET(clientSock, &active_set); // adding new fd to the active set
                bzero(bufToWrite, sizeof(bufToWrite)); // zeroing buf 
                sprintf(bufToWrite, "server: client %d just arrived\n", clients[clientSock].id); // saving the message to the buf
                sendToAll(&clientSock); // sending a message to everybody that we got a new client 
                break;// coz need to update our active set (got one more fd)
            }

            if (FD_ISSET(fd, &read_set) && fd != servSocket) { // if there is a changing in read_set NOT servSocket, means we got a data to recieve from client on socket == fd or this client has been disconnected 
                bzero(bufToRead, sizeof(bufToRead)); // zeroing buf to clean it from prev data
                int bytes = recv(fd, bufToRead, 128*1024, 0);

                if (bytes <= 0) { // means this client has been disconnected (by himself bytes == 0 or error happened < 0 )
                    bzero(bufToWrite, sizeof(bufToWrite)); // zeroing buf 
                    sprintf(bufToWrite, "server: client %d just left\n", clients[fd].id); // saving the message to the buf
                    sendToAll(&fd); // sending a message to everybody that the client has disconnected 
                    FD_CLR(fd, &active_set); // delete this fd(socket) from active_set, we don't need it anymore
                    close(fd); // close this fd
                    break; // coz need to update our active set (deleted fd)

                } else { // means we got some data from client
                    for (int i = 0, j = strlen(clients[fd].buf); i < bytes; i++, j++) { 
                        clients[fd].buf[j] = bufToRead[i]; // saving data from bufToRead to the client's buf. If it was there something not sent yet, we keep saving his message till the end (\n)
                        
                        // if (clients[fd].buf[j] == '\n') { 
                        //     if (strlen(clients[fd].buf) > 1) { // in real life server should't send to clients empty buffer, but in exame case traces will give you a mistake. So for exam use the block down  
                        //         clients[fd].buf[j] = '\0';
                        //         bzero(bufWrite, 128*1024);
                        //         sprintf(bufWrite, "client %d: %s\n", clients[fd].id, clients[fd].buf);
                        //         sendToAll(&fd);
                        //     }
                        //     bzero(clients[fd].buf, 128*1024);
                        //     j = -1;
                        // }

                        if (clients[fd].buf[j] == '\n') { // means this is the end of message. It is ready to be sent to every chat user 
                            clients[fd].buf[j] = '\0'; // swithing /n to /0 and sending to everybody
                            bzero(bufToWrite, sizeof(bufToWrite)); 
                            sprintf(bufToWrite, "client %d: %s\n", clients[fd].id, clients[fd].buf); // saving the message to the buf
                            sendToAll(&fd); // sending a message from client to everybody exept him 
                            bzero(clients[fd].buf, sizeof(clients[fd].buf)); // clearing client's buf for the next messages
                            j = -1;
                        }
                    }
                }
            }
        }
    }
    //coz while(1) it won't stop ever, but still:

    close(servSocket);
    return 0;
}
