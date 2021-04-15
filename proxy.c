#include <arpa/inet.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAXEVENTS 64

#define HTTP_REQUEST_MAX_SIZE 4096
#define HOSTNAME_MAX_SIZE 512
#define PORT_MAX_SIZE 6
#define URI_MAX_SIZE 4096
#define METHOD_SIZE 32

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int efd;

struct info
{
    int connfd;
    int bytesRead;
    int sfd;
    char buf[MAX_OBJECT_SIZE];
};

struct event
{
    struct info *data;
    void (*callback)(struct event *);
};


int parse_request(char *request, char *method, char *hostname, char *port, char *uri);
void initialize(struct event *e);
void read_request(struct event *e);
void send_request(struct event *e);
void read_response(struct event *e);
void send_response(struct event *e);


int main(int argc, char *argv[])
{
    int listenfd;
    struct epoll_event event;
    struct epoll_event *events;
    int i;

    struct event *e;
    struct event *listen_action;
    struct info *data;

    size_t n;


    if ((efd = epoll_create1(0)) < 0)
    {
        fprintf(stderr, "error creating epoll fd\n");
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0)
    {
        fprintf(stderr, "error setting socket option\n");
        exit(1);
    }

    data = malloc(sizeof(struct info));
    data->connfd = listenfd;
    memset(data->buf, 0, sizeof(data->buf));
    data->bytesRead = 0;
    data->sfd = 0;

    listen_action = malloc(sizeof(struct event));
    listen_action->callback = &initialize;
    listen_action->data = data;

    event.data.ptr = listen_action;
    event.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0)
    {
        fprintf(stderr, "error adding event\n");
        exit(1);
    }

    events = calloc(MAXEVENTS, sizeof(event));

    while (1)
    {
        n = epoll_wait(efd, events, MAXEVENTS, -1);


        for (i = 0; i < n; i++)
        {
            e = (struct event *)events[i].data.ptr;
            printf("0: %d\n", e->data->connfd);
            if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            {
                fprintf(stderr, "epoll error on fd %d\n", e->data->connfd);
                close(e->data->connfd);
            }

            e->callback(e);
        }
    }
    free(data);
    free(listen_action);
    free(events);
    return 0;
}

void initialize(struct event *e)
{
    socklen_t clientlen = sizeof(struct sockaddr_storage);
    int connfd = e->data->connfd;
    struct epoll_event event;
    struct sockaddr_storage clientaddr;
    struct event *action;
    struct info *data;

    while (1)
    {
        data = malloc(sizeof(struct info));
		data->connfd = accept(connfd, (SA *)&clientaddr, &clientlen);
		if(data->connfd < 0){
			break;
		}
		printf("accept connfd = %d\n", data->connfd);
		if(fcntl(data->connfd, F_SETFL, fcntl(data->connfd, F_GETFL, 0) | O_NONBLOCK) < 0){
			printf("Error\n");
			exit(1);
		}

        memset(data->buf, 0, sizeof(data->buf));
        data->bytesRead = 0;
        data->sfd = 0;

        action = malloc(sizeof(struct event));
        action->callback = &read_request;
        action->data = data;

        event.data.ptr = action;
        event.events = EPOLLIN | EPOLLET;
        if (epoll_ctl(efd, EPOLL_CTL_ADD, data->connfd, &event) < 0)
        {
			printf("Error\n");
			exit(1);
        }
    }
    // read_request(e);
    // send_request(e);
    // read_response(e);
    // send_response(e);

    // close(e->data->sfd);
    // close(e->data->connfd);
    if (errno != EWOULDBLOCK && errno != EAGAIN) close(e->data->connfd);
}

void read_request(struct event *e)
{
    
    int nread = 0;

    while ((nread = read(e->data->connfd, e->data->buf + e->data->bytesRead, MAX_OBJECT_SIZE - e->data->bytesRead)) > 0)
    {
        e->data->bytesRead += nread;

        e->data->buf[e->data->bytesRead] = '\0';

        if (!strcmp(e->data->buf + e->data->bytesRead - 4, "\r\n\r\n"))
        {
            ++e->data->bytesRead;
            break;
        }
    }

    if (nread == 0)
    {
        close(e->data->connfd);
        return;
    }
    else if (nread < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))return;
    else if (nread < 0 && (errno != EWOULDBLOCK && errno != EAGAIN))
    {
        close(e->data->connfd);
        return;
    }

	char hostname[MAXLINE], port[MAXLINE], method[METHOD_SIZE], uri[URI_MAX_SIZE];

    parse_request(e->data->buf, method, hostname, port, uri);

    int sfd, s;
    struct addrinfo hints;
    struct addrinfo *result, *rp;


    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    s = getaddrinfo(hostname, port, &hints, &result);
    if (s != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }
    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1){
            printf("Success connecting to server\n");
            break;
        }
        close(sfd);
    }
    if (rp == NULL)
    {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
    }
	printf("HERE - buf: %s\n", e->data->buf);
    if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0)
    {
        printf("Error\n");
        exit(1);
    }

    e->data->sfd = e->data->connfd;
    e->data->connfd = sfd;
    char tempMessage[1024];
	strcpy(tempMessage, method);
	strcat(tempMessage, " /");
	strcat(tempMessage, uri);
	strcat(tempMessage, "\r\n");
	strcat(tempMessage, user_agent_hdr);
	strcat(tempMessage, "Connection: close\r\n");
	strcat(tempMessage, "Proxy-Connection: close\r\n");
	strcat(tempMessage, "\r\n\r\n");
	memcpy(e->data->buf, tempMessage, strlen(tempMessage)+1);
    e->data->bytesRead = strlen(e->data->buf);

    e->callback = &send_request;

    struct epoll_event event;
    event.data.ptr = e;
    event.events = EPOLLOUT | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) < 0)
    {
        printf("Error\n");
        exit(1);
    }
}

void send_request(struct event *e)
{
    printf("In SEND_REQUEST\n");
    int sentBytes = 0;
    do
    {
        sentBytes = write(e->data->connfd, e->data->buf, e->data->bytesRead);
        e->data->bytesRead -= sentBytes;
    }while (sentBytes < e->data->bytesRead);

    if (sentBytes == 0 || (!(errno == EWOULDBLOCK || errno == EAGAIN))) close(e->data->connfd);
    if(sentBytes <= 0) return;

    memset(e->data->buf, 0, sizeof(e->data->buf));
    e->data->bytesRead = 0;

    e->callback = &read_response;
    struct epoll_event event;
    event.data.ptr = e;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_MOD, e->data->connfd, &event) < 0)
    {
        printf("Error\n");
        exit(1);
    }
}

void read_response(struct event *e)
{
    printf("Read Response");
    int nread = 0;

    while ((nread = read(e->data->connfd, e->data->buf + e->data->bytesRead, MAX_OBJECT_SIZE - e->data->bytesRead)) > 0)
    {
        e->data->bytesRead += nread;
    }

    if (nread == 0)
    {
        e->callback = &send_response;
        struct epoll_event event;
        event.data.ptr = e;
        event.events = EPOLLOUT | EPOLLET;
        if (epoll_ctl(efd, EPOLL_CTL_MOD, e->data->sfd, &event) < 0)
        {
            printf("Error\n");
            exit(1);
        }
        close(e->data->connfd);
    }
    else if (errno != EWOULDBLOCK && errno != EAGAIN)
    {
        close(e->data->connfd);
    }
}

void send_response(struct event *e)
{
    printf("Send Response");
    int sentBytes = 0;
    e->data->connfd = e->data->sfd;
    while(1){
        sentBytes = write(e->data->connfd, e->data->buf, e->data->bytesRead);
        if(sentBytes <= 0){
            break;
        }
        e->data->bytesRead -= sentBytes;
    }
    if (sentBytes == 0)
    {
        close(e->data->connfd);
        free(e->data);
        free(e);
    }
}

int is_complete_request(const char *request) {
	int len = strlen(request);
	if(len < 4) return 0;
	char c1[2];
	char c2[2];
	char c3[2];
	char c4[2];
	sprintf(c1, "%c", request[len-1]);
	sprintf(c2, "%c", request[len-2]);
	sprintf(c3, "%c", request[len-3]);
	sprintf(c4, "%c", request[len-4]);
	// printf("chars: \n%s\n", c1);
	// printf("%s\n", c2);
	// printf("%s\n", c3);
	// printf("%s\n", c4);
	// return 1;
	if(strcmp(c1, "\n")) return 0;
	if(strcmp(c2, "\r")) return 0;
	if(strcmp(c3, "\n")) return 0;
	if(strcmp(c4, "\r")) return 0;
	return 1;
}

int parse_request(char *request, char *method,
	char *hostname, char *port, char *uri) {
    // printf("%s", request);
	//method
	strcpy(method, request);
	strtok(method, " ");

    char ip[100];
    int p = 80;
	char pString[100];
    char page[100];
	if(sscanf(request, "GET http://%99[^:]:%99d/%99[^\n]", ip, &p, page)==3);
	else if(sscanf(request, "GET http://%99[^/]/%99[^\n]", ip, page)==2);

	strcpy(hostname, ip);
	sprintf(pString, "%d", p);
	strcpy(port, pString);
	strcpy(uri, page);

	return is_complete_request(request);
}