
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
#include <sys/ioctl.h>
#include <cassert>
#include <fcntl.h>
#include <vector>
#include <errno.h>
#include <poll.h>
//Target host details:
#define PORT 1234
#define HOST "0.0.0.0"
const size_t k_max_msg = 4096;
enum {
    STATE_REQ = 0, // for reading requests
    STATE_RES = 1, // for sending responses
    STATE_END = 2,  // mark the connection for deletion
};

static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        perror("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        perror("fcntl error");
    }
}

struct Conn {
    int fd = -1;
    uint32_t state = 0;     // either STATE_REQ or STATE_RES
    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};
static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        perror("accept() error");
        return -1;  // error
    }

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);
    // creating the struct Conn
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn) {
        close(connfd);
        return -1;
    }
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}
static void state_req(Conn *conn);
static void state_res(Conn *conn);
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


static bool try_one_request(Conn *conn) {
    // try to parse a request from the buffer
    if (conn->rbuf_size < 4) {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        perror("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }

    // got one request, do something with it
    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // generating echoing response
    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    // remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // change state
    conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn) {
    // try to fill the buffer
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN) {
        // got EAGAIN, stop.
        return false;
    }
    if (rv < 0) {
	perror("read() error");
        conn->state = STATE_END;
        return false;
    }
    if (rv == 0) {
        if (conn->rbuf_size > 0) {
            perror("unexpected EOF");
        } else {
            perror("EOF");
        }
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    // Try to process requests one by one.
    // Why is there a loop? Please read the explanation of "pipelining".
    while (try_one_request(conn)) {}
    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn) {
    while (try_fill_buffer(conn)) {}
}

static bool try_flush_buffer(Conn *conn) {
    ssize_t rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN) {
        // got EAGAIN, stop.
        return false;
    }
    if (rv < 0) {
        perror("write() error");
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        // response was fully sent, change state back
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    // still got some data in wbuf, could try to write again
    return true;
}

static void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {}
}
static void connection_io(Conn *conn) {
    if (conn->state == STATE_REQ) {
        state_req(conn);
    } else if (conn->state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);  // not expected
    }
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
void accept_loop(int domain, int type, int protocol) {
    int sock = make_accept_sock(domain, type, protocol);
    if (sock == -1) {
        perror("Failed to create socket");
        return;
    }

    // A map of all client connections
    std::vector<Conn *> fd2conn;
    fd_set_nb(sock); // Assuming this sets the socket to non-blocking

    // The event loop
    std::vector<struct pollfd> poll_args;

    while (true) {
        // Prepare the argument for poll()
        poll_args.clear();

        // Listening fd in the first position
        struct pollfd pfd = {sock, POLLIN, 0};
        poll_args.push_back(pfd);

        // Connection fds
        for (Conn *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events |= POLLERR;
            poll_args.push_back(pfd);
        }

        // Poll for active fds
        int rv = poll(poll_args.data(), static_cast<nfds_t>(poll_args.size()), 1000);
        if (rv < 0) {
            perror("poll");
            continue; // Handle error by continuing the loop
        }

        // Process active connections
        for (size_t i = 1; i < poll_args.size(); ++i) { // Start from 1 to skip the listening socket
            if (poll_args[i].revents) {
                Conn *conn = fd2conn[i - 1]; // Map poll_args index to fd2conn
                connection_io(conn);
                if (conn->state == STATE_END) {
                    // Client closed normally or something bad happened; destroy this connection
                    fd2conn[i - 1] = nullptr; // Clear the connection from the map
                    close(conn->fd);
                    free(conn);
                }
            }
        }

        // Try to accept a new connection if the listening fd is active
        if (poll_args[0].revents & POLLIN) {
            struct sockaddr_in client_addr{};
            socklen_t addrlen = sizeof(client_addr);
            int new_sock = accept(sock, (struct sockaddr*)&client_addr, &addrlen);
            if (new_sock == -1) {
                perror("accept failed");
                continue; // Retry on error
            }
            // Create a new connection object (assuming the constructor initializes the connection)
            Conn *new_conn = new Conn();
            new_conn->fd = new_sock;
            new_conn->state = STATE_REQ; // Assuming initial state is STATE_REQ

            // Add new connection to fd2conn
            fd2conn.push_back(new_conn);
        }
    }
}

int main(int argc,char* argv[])
{

	accept_loop(AF_INET, SOCK_STREAM, 0);
	

		//   the protocol i will use
		//   +-----+------+-----+------+--------
		//   | len | msg1 | len | msg2 | more...
		//   +-----+------+-----+------+--------

	return 0;
}

