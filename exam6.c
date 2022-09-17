#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

typedef struct s_client {
    int id;
    char buf[128*1024];
} t_client;

fd_set active, write_set, read_set;

int maxFd = 0;
int next_id = 0;

char bufRead[128*1024];
char bufWrite[128*1024];

t_client clients[10*1024];

void err(char *msg, int fd) {
    if (fd)
        close(fd);
    write(2, msg, strlen(msg));
    exit(1);
}

void sendToAll(int *senderFd) {
   for (int i = 0; i < maxFd; i++) {
       if (FD_ISSET(i, &write_set) && i != *senderFd)
        send(i, bufWrite, strlen(bufWrite), 0);
   }
}

int main(int argc, char* argv[]) {
    if (argc != 2)
        err("Wrong number of arguments\n", 0);

    int socketServ = socket(PF_INET, SOCK_STREAM, 0);
    if (socketServ == -1)
        err("Fatal error\n", 0);

    bzero(bufRead, strlen(bufRead));
    bzero(bufWrite, strlen(bufWrite));

    FD_ZERO(&active);
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);

    FD_SET(socketServ, &active);

    maxFd = socketServ + 1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = PF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addr_len = sizeof(addr);

    if ((bind(socketServ, (const struct sockaddr *)&addr, addr_len)) != 0)
        err("Fatal error\n", socketServ);
    if (listen(socketServ, 10) != 0)
        err("Fatal error\n", socketServ);
    
    while (1) {
        read_set = write_set = active;

        if (select(maxFd, &read_set, &write_set, NULL, NULL) < 0)
            continue;

        for (int fd = 0; fd < maxFd; fd++) {
            if (FD_ISSET(fd, &read_set) && fd == socketServ) {
                struct sockaddr_storage client_addr;
                socklen_t addr_size = sizeof(client_addr);
                int clientSock = accept(socketServ, (struct sockaddr *)&client_addr, &addr_size);
                if (clientSock < 0)
                    continue;
                maxFd = clientSock > maxFd - 1 ? clientSock + 1 : maxFd;
                clients[clientSock].id = next_id++;
                bzero(clients[clientSock].buf, 128*1024);
                FD_SET(clientSock, &active);
                bzero(bufWrite, 128*1024);
                sprintf(bufWrite, "server: client %d just arrived\n", clients[clientSock].id);
                sendToAll(&clientSock);
                break;//need to update active set
            }

            if (FD_ISSET(fd, &read_set) && fd != socketServ) {
                bzero(bufRead, strlen(bufRead));
                int bytes = recv(fd, bufRead, 128*1024, 0);

                if (bytes <= 0) { // 
                    bzero(bufWrite, 128*1024);
                    sprintf(bufWrite, "server: client %d just left\n", clients[fd].id);
                    sendToAll(&fd);
                    FD_CLR(fd, &active);
                    close(fd);
                    break;//need to update active set

                } else { //
                    for (int i = 0, j = strlen(clients[fd].buf); i < bytes; i++, j++) {
                        clients[fd].buf[j] = bufRead[i];
                        
                        if (clients[fd].buf[j] == '\n') {
                            if (strlen(clients[fd].buf) > 1) {
                                clients[fd].buf[j] = '\0';
                                bzero(bufWrite, 128*1024);
                                sprintf(bufWrite, "client %d: %s\n", clients[fd].id, clients[fd].buf);
                                sendToAll(&fd);
                            }
                            bzero(clients[fd].buf, 128*1024);
                            j = -1;
                        }
                    }
                }
            }
        }
    }
    close(socketServ);
    return 0;
}
