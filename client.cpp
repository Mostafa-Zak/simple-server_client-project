
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


int main(int argc, char* argv[])
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(8000);
	addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
	int result = connect(fd,  (const struct sockaddr *)  &addr,sizeof(addr));
	if(result == -1)
	{
		perror("connet failed: ");
	}
	char msg[] = "hello";
	write(fd, msg, strlen(msg));
	char rbuf[64] = {};
	ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
	printf("server says: %s\n", rbuf);
	close(fd);
	return 0;
}
