
#include <asm-generic/socket.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>


static void do_something(int connfd) {
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        perror("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);

    char wbuf[] = "world";

    write(connfd, wbuf, strlen(wbuf));
}


int main(int argc,char* argv[])
{
	int fd = socket(AF_INET,SOCK_STREAM,0);
	if(fd == -1)
	{
		perror("socket problem: ");
	}
	int value = 1;
	if(setsockopt(fd,SOL_SOCKET , SO_REUSEADDR , &value , sizeof(value)) == -1)
	{
		perror("socket configuration problem: ");
	};

	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(8000);
	inet_pton(AF_INET,"0.0.0.0", &addr.sin_addr);
	int rv = bind(fd, (const sockaddr *) &addr, sizeof(addr));
	if(rv == -1)
	{
		perror("bind failed");
      		close(fd);
      		exit(EXIT_FAILURE);
	}

	if (listen(fd, SOMAXCONN) == -1) {
		perror("listen failed");
		close(fd);
		exit(EXIT_FAILURE);
	} else {
		printf("Server is listening on port 8000\n");
}
	while(true)
	{

	struct sockaddr_in client_addr{};
	socklen_t addrlen = sizeof(client_addr);
	int connfd = accept(fd, (struct sockaddr *) &client_addr,&addrlen) ;
	if ( connfd == -1)
	{
		perror("listen failed");
      		close(fd);
      		exit(EXIT_FAILURE);
	};
		do_something(connfd);
		close(connfd);
		
	}

	return 0;
}

