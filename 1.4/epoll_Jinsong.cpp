#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>

#include <vector>
#include <algorithm>

#define MAXEVENTS 1000
#define VERBOSE 1
#define NONBLOCKING 1
const int kServerPort = 5703;
const int kServerBacklog = 128;
const size_t kTransferBufferSize = 64;

enum EConnState
{
	eConnStateReceiving,
	eConnStateSending
};

struct ConnectionData
{
	EConnState state;

	int sock;

	size_t bufferOffset, bufferSize;
	char buffer[kTransferBufferSize + 1];
};

static bool process_client_recv(ConnectionData &cd);
static bool process_client_send(ConnectionData &cd);
static bool set_socket_nonblocking(int fd);
static bool is_invalid_connection( const ConnectionData& cd );
static int setup_server_socket(short port);

int main(int argc, char *argv[])
{
	int serverPort = kServerPort;

	if (2 == argc)
	{
		serverPort = atoi(argv[1]);
	}

#if VERBOSE
	printf("Attempting to bind to port %d\n", serverPort);
#endif

	int listenfd = setup_server_socket(serverPort);

	if (-1 == listenfd)
		return 1;

	std::vector<ConnectionData> connections;

	// Declares epoll_event structure & size
	struct epoll_event *ep_events;
	struct epoll_event event;
	// Sets file descriptor (server own's socket) associated with events to be processed
	event.data.fd = listenfd;
	// Sets the type of events to be processed (read)
	event.events = EPOLLIN;

	// Allocates maximum size of operation events (EPOLL_CTL_ADD/MOD/DEL)
	ep_events = (epoll_event *)calloc(MAXEVENTS, sizeof(struct epoll_event));

	// Create epoll instance
	int epollfd = epoll_create(MAXEVENTS);
	printf("epollfd: %d\n", epollfd);
	if (epollfd == -1)
	{
		perror("epoll_create1() failed");
		return 1;
	}

	// Adds listenfd to epollfd
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event) == -1)
	{
		printf("Couldn't add listenfd to epoll descriptor: %s\n", strerror(errno));
		return 1;
	}

	// Number of file descriptors ready to be read
	int event_cnt;

	// Loop forever
	while (1)
	{
		// Wait for ready file descriptor list events
		event_cnt = epoll_wait(epollfd, ep_events, MAXEVENTS, -1);
		// Checks for errors
		if (event_cnt == -1)
		{
			printf("epoll_wait() failed: %s\n", strerror(errno));
			return 1;
		}

		//printf("event_cnt: %d\n", event_cnt);
		// Checks through all the available fd returned by epoll_wait
		for (int i = 0; i < event_cnt; ++i)
		{
			//printf("events no: %d\n", ep_events[i].events);
			if (ep_events[i].events & EPOLLERR) {
				printf("epoll_wait returned EPOLLERR");
			}
			// NEW INCOMING CLIENT
			if (ep_events[i].data.fd == listenfd)
			{
				sockaddr_in clientAddr;
				socklen_t addrSize = sizeof(clientAddr);

				// Accepts a single incoming connection
				int clientfd = accept(listenfd, (sockaddr *)&clientAddr, &addrSize);

				if (-1 == clientfd)
				{
					perror("accept() failed");
					continue; // Attempt to accept a different client.
				}
				else
				{
// Error while accepting client
#if VERBOSE
					// print some information about the new client
					char buff[128];
					printf("Connection from %s:%d -> socket %d\n",
						   inet_ntop(AF_INET, &clientAddr.sin_addr, buff, sizeof(buff)),
						   ntohs(clientAddr.sin_port),
						   clientfd);
					fflush(stdout);
#endif

#if NONBLOCKING
					// enable non-blocking sends and receives on this socket
					if (!set_socket_nonblocking(clientfd))
						continue;
#endif

					event.data.fd = clientfd;
					event.events = EPOLLIN;

					// Adds new client file desc. to epoll's list
					if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientfd, &event) == -1)
					{
						printf("epoll_ctl EPOLL_CTL_ADD error\n");
						continue;
					} else {
						// initialize connection data
					ConnectionData connData;
					memset(&connData, 0, sizeof(connData));

					connData.sock = clientfd;
					connData.state = eConnStateReceiving;

					// TODO: add connData in your data structure so that you can keep track of that socket.
					connections.push_back(connData);
					}
				}
			}
			// NEW DATA AVAILABLE
			else
			{

				for( size_t j = 0; j < connections.size(); ++j )
				{
					if (ep_events[i].data.fd == connections[j].sock) {
						int clientfd = ep_events[i].data.fd;

						if (ep_events[i].events & EPOLLOUT) {
							printf("socket %d at process_client_send\n", clientfd);
							if( !process_client_send(connections[j]))
							{
								continue;
								printf("socket %d closing\n", clientfd);
								if (epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL) < 0) {
								  printf("epoll_ctl EPOLL_CTL_DEL error\n");
								}
								close( clientfd );
								//I tried to erase it at here, but i'm not familiar with vector. 
								//So I manually made it invalid, is_invalid_connection() will return false, then it will be erased later. 
								connections[j].sock = -1;
							}
							if(connections[j].state == eConnStateReceiving){
								event.data.fd = clientfd;
								event.events = EPOLLIN;
								if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clientfd, &event) < 0) {
									printf("epoll_ctl EPOLL_CTL_MOD error\n");
								}
							}
						}
						else if(ep_events[i].events & EPOLLIN) {
							printf("socket %d at process_client_recv\n", clientfd);
							if( !process_client_recv(connections[j]))
							{
								printf("socket %d closing\n", clientfd);
								if (epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL) < 0) {
								  printf("epoll_ctl EPOLL_CTL_DEL error\n");
								}
								close( clientfd );
								//I tried to erase it at here, but i'm not familiar with vector. 
								//So I manually made it invalid, is_invalid_connection() will return false, then it will be erased later. 
								connections[j].sock = -1;
							}
							if(connections[j].state == eConnStateSending){
								event.data.fd = clientfd;
								event.events = EPOLLOUT;
								if (epoll_ctl(epollfd, EPOLL_CTL_MOD, clientfd, &event) < 0) {
									printf("epoll_ctl EPOLL_CTL_MOD error\n");
								}
							}
						}
					}
				}
/*

				// New data available
				ConnectionData connData;
				memset( &connData, 0, sizeof(connData) );
				connData.sock = ep_events[i].data.fd;
				connData.state = eConnStateReceiving;


				printf("Receiving data from socket %d\n", connData.sock);
				bool returnval;
				returnval = process_client_recv( connData );
				if (returnval == true) {
					printf("Sending back data to socket %d\n", connData.sock);
					process_client_send( connData );
				} else {
					// done - close connection
					epoll_ctl(epollfd, EPOLL_CTL_DEL, ep_events[i].data.fd, NULL);
					close( connData.sock );
					for( size_t j = 0; j < connections.size(); ++j )
					{
						if (connData.sock == connections[j].sock)
							connections[j].sock = -1;
					}
				}*/
			}
		}

		// Stops connections
		connections.erase(
			std::remove_if(
				connections.begin(), connections.end(), &is_invalid_connection
			),
			connections.end()
		);

		// Active connections
		//printf("Active connections: %ld\n", connections.size());
	}

	// The program will never reach this part, but for demonstration purposes,
	// we'll clean up the server resources here and then exit nicely.
	close(listenfd);
	close(epollfd);

	return 0;
}

//--    process_client_recv()   ///{{{1///////////////////////////////////////
static bool process_client_recv(ConnectionData &cd)
{
	assert(cd.state == eConnStateReceiving);

	// receive from socket
	ssize_t ret = recv(cd.sock, cd.buffer, kTransferBufferSize, 0);

	if (0 == ret)
	{
#if VERBOSE
		printf("  Socket %d - Orderly shutdown\n", cd.sock);
		fflush(stdout);
#endif

		return false;
	}

	if (-1 == ret)
	{
#if VERBOSE
		printf("  Socket %d - Receive: '%s'\n", cd.sock, strerror(errno));
		fflush(stdout);
#endif

		return false;
	}

	// update connection buffer
	cd.bufferSize += ret;

	// zero-terminate received data
	cd.buffer[cd.bufferSize] = '\0';

	// transition to sending state
	cd.bufferOffset = 0;
	cd.state = eConnStateSending;
	return true;
}

//--    process_client_send()   ///{{{1///////////////////////////////////////
static bool process_client_send(ConnectionData &cd)
{
	assert(cd.state == eConnStateSending);

	// send as much data as possible from buffer
	ssize_t ret = send(cd.sock,
					   cd.buffer + cd.bufferOffset,
					   cd.bufferSize - cd.bufferOffset,
					   MSG_NOSIGNAL // suppress SIGPIPE signals, generate EPIPE instead
	);

	if (-1 == ret)
	{
#if VERBOSE
		printf("  Socket %d - Error on send: '%s'\n", cd.sock,
			   strerror(errno));
		fflush(stdout);
#endif

		return false;
	}

	// update buffer data
	cd.bufferOffset += ret;

	// did we finish sending all data
	if (cd.bufferOffset == cd.bufferSize)
	{
		// if so, transition to receiving state again
		cd.bufferSize = 0;
		cd.bufferOffset = 0;
		cd.state = eConnStateReceiving;
	}

	return true;
}

//--    setup_server_socket()   ///{{{1///////////////////////////////////////
static int setup_server_socket(short port)
{
	// create new socket file descriptor
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == fd)
	{
		perror("socket() failed");
		return -1;
	}

	// bind socket to local address
	sockaddr_in servAddr;
	memset(&servAddr, 0, sizeof(servAddr));

	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(port);

	if (-1 == bind(fd, (const sockaddr *)&servAddr, sizeof(servAddr)))
	{
		perror("bind() failed");
		close(fd);
		return -1;
	}

	// get local address (i.e. the address we ended up being bound to)
	sockaddr_in actualAddr;
	socklen_t actualAddrLen = sizeof(actualAddr);
	memset(&actualAddr, 0, sizeof(actualAddr));

	if (-1 == getsockname(fd, (sockaddr *)&actualAddr, &actualAddrLen))
	{
		perror("getsockname() failed");
		close(fd);
		return -1;
	}

	char actualBuff[128];
	printf("Socket is bound to %s %d\n",
		   inet_ntop(AF_INET, &actualAddr.sin_addr, actualBuff, sizeof(actualBuff)),
		   ntohs(actualAddr.sin_port));

	// and start listening for incoming connections
	if (-1 == listen(fd, kServerBacklog))
	{
		perror("listen() failed");
		close(fd);
		return -1;
	}

	// allow immediate reuse of the address (ip+port)
	int one = 1;
	if (-1 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)))
	{
		perror("setsockopt() failed");
		close(fd);
		return -1;
	}

#if NONBLOCKING
	// enable non-blocking mode
	if (!set_socket_nonblocking(fd))
	{
		close(fd);
		return -1;
	}
#endif

	return fd;
}

//--    set_socket_nonblocking()   ///{{{1////////////////////////////////////
static bool set_socket_nonblocking(int fd)
{
	int oldFlags = fcntl(fd, F_GETFL, 0);
	if (-1 == oldFlags)
	{
		perror("fcntl(F_GETFL) failed");
		return false;
	}

	if (-1 == fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK))
	{
		perror("fcntl(F_SETFL) failed");
		return false;
	}

	return true;
}

// Invalid connection
static bool is_invalid_connection( const ConnectionData& cd )
{
	return cd.sock == -1;
}
