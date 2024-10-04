
#include <asm-generic/socket.h>
#include <cstdint>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <thread>
#include <sys/ioctl.h>
#include <cassert>

//Target host details:
#define PORT 1234
#define HOST "0.0.0.0"
const size_t k_max_msg = 4096;


static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error, or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t one_request(int connfd) {
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            perror("EOF");
        } else {
            perror("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        perror("too long");
        return -1;
    }

    // request body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        perror("read() error");
        return err;
    }

    // do something
    rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);

    // reply using the same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}
int make_accept_sock(int domain, int type, int protocol )
{
	//Create socket
	int fd = socket(domain, type, protocol);
	if(fd == -1)
	{
		perror("socket problem: ");
		return -1;
	}

	//Enable socket reuse opion
	int value = 1;
	if(setsockopt(fd,SOL_SOCKET , SO_REUSEADDR , &value , sizeof(value)) == -1)
	{
		perror("socket configuration problem: ");
		close(fd);
		return -1;
	};

	//Parse server adress structure
	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	inet_pton(AF_INET,HOST, &addr.sin_addr);

	//Bind socket to address and port
	int rv = bind(fd, (const sockaddr *) &addr, sizeof(addr));
	if(rv == -1)
	{
		perror("bind failed");
      		close(fd);
      		return -1;
	}

	//Listen for incoming connection
	if (listen(fd, SOMAXCONN) == -1) {
		perror("listen failed");
		close(fd);
		return -1;
	} 

	printf("Server is listening on port %d\n", PORT);
;

	return (fd);
}



// Function to check if a socket is closed

bool isclosed(int sock) {
    fd_set rfd;
    FD_ZERO(&rfd);
    FD_SET(sock, &rfd);
    timeval tv = { 0, 0 };  // Initialize both tv_sec and tv_usec to 0
    select(sock + 1, &rfd, 0, 0, &tv);
    if (!FD_ISSET(sock, &rfd)) return false;
    int n = 0;
    ioctl(sock, FIONREAD, &n);
    return n == 0;
}



void new_connection(int connfd)
{
	while (true) {
            int32_t err = one_request(connfd);
            if (err) {
                break;
            }
	}
	close(connfd);

}

void accept_loop(int domain, int type, int protocol ) {
    int sock = make_accept_sock(domain, type, protocol );
    if(sock == -1)
	{
	perror("Failed to create socket");
	return;
	}

    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int new_sock = accept(sock, (struct sockaddr*)&client_addr, &addrlen);

        if (new_sock == -1) {
            perror("accept failed");
            continue; // Retry on error
		}
	//create new thread
	std::thread t(new_connection, new_sock);
        t.detach();
	
}}


int main(int argc,char* argv[])
{

	accept_loop(AF_INET, SOCK_STREAM, 0);
		//   the protocol i will use
		//   +-----+------+-----+------+--------
		//   | len | msg1 | len | msg2 | more...
		//   +-----+------+-----+------+--------

	return 0;
}

