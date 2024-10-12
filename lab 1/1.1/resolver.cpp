#include <stdio.h>
#include <stddef.h>

#include <assert.h>
#include <limits.h>
#include <unistd.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

void print_usage(const char* aProgramName);

#if !defined(HOST_NAME_MAX)
#	if defined(__APPLE__)
#		include <sys/param.h>
#		define HOST_NAME_MAX MAXHOSTNAMELEN
#	else  // !__APPLE__
#		error "HOST_NAME_MAX undefined!"
#	endif // ~ __APPLE__
#endif // ~ HOST_NAME_MAX

// ### MAIN
int main(int aArgc, char* aArgv[])
{
	if(aArgc != 2)
	{
		print_usage(aArgv[0]);
		return 1;
	}

    // Host (argument to resolve)
	const char* remoteHostName = aArgv[1];

    // Get local host name
	const size_t kHostNameMaxLength = HOST_NAME_MAX+1;
	char localHostName[kHostNameMaxLength];
	if(-1 == gethostname(localHostName, kHostNameMaxLength))
	{
		perror("gethostname(): ");
		return 1;
	}

    // Print initial program message
	printf("Resolving `%s' from `%s':\n", remoteHostName, localHostName);

    // Set up hints "suggestions" (will be sent to getaddrinfo())
    struct addrinfo hints = {0}, *addresses;

    // Limit the query to valid TCP connections
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Get info from getaddrinfo()
    int returnAddrInfo = getaddrinfo(remoteHostName, NULL, &hints, &addresses);

    // Check if errors while executing getaddrinfo()
    if (returnAddrInfo) {
        printf("getaddrinfo(): %s\n", gai_strerror(returnAddrInfo));
        return 1;
    }

    // We need to fetch infos from getaddrinfo() results (stored in addresses) and print them
    for (struct addrinfo* result = addresses; result != NULL; result = result->ai_next) {
        // Check if IP is IPv4 or IPv6
        sockaddr const *sockAddr = result->ai_addr;
        if (sockAddr->sa_family == AF_INET) {
            // Retrieve IP string
            const size_t bufferSize = 32;
            char nameBuffer[bufferSize];
            // Cast to sockaddr_in* (for IPv4 usage)
            sockaddr_in const *inAddr = (sockaddr_in const *)sockAddr;
            printf("IPv4: %s\n", inet_ntop(AF_INET, &inAddr->sin_addr, nameBuffer, bufferSize));
        }
        else if (sockAddr->sa_family == AF_INET6) {
            // Retrieve IP string
            const size_t bufferSize = 64;
            char nameBuffer[bufferSize];
            // Cast to sockaddr_in6 (for IPv6 usage)
            sockaddr_in6 const *inAddr = (sockaddr_in6 const *)sockAddr;
            printf("IPv6: %s\n", inet_ntop(AF_INET6, &inAddr->sin6_addr, nameBuffer, bufferSize));
        }
    }

	// Free resources allocated by getaddrinfo()!
	freeaddrinfo(addresses);

	// Ok, we're done. Return success.
	return 0;
}

void print_usage(const char* aProgramName)
{
	fprintf(stderr, "Usage: %s <hostname>\n", aProgramName);
}