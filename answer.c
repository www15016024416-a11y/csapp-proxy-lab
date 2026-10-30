#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "csapp.h"

/* Recommended max cache and object sizes (unused in the basic version) */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAX_REQ_SIZE (MAXLINE * 16)

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

static void doit(int fd);
static void clienterror(int fd, char *cause, char *errnum,
                        char *shortmsg, char *longmsg);
static void parse_uri(char *uri, char *host, char *port, char *path);
static void build_request(char *host, char *path, rio_t *client_rio,
                          char *out, size_t outsize);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* Writing to a closed socket must not kill the proxy */
    Signal(SIGPIPE, SIG_IGN);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        doit(connfd);              /* sequential: handle one at a time */
        Close(connfd);
    }
    return 0;
}

/*
 * Send a small error page back to the client.
 */
static void clienterror(int fd, char *cause, char *errnum,
                        char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body + strlen(body), "<body bgcolor=\"ffffff\">\r\n");
    sprintf(body + strlen(body), "%s: %s\r\n", errnum, shortmsg);
    sprintf(body + strlen(body), "<p>%s: %s\r\n", longmsg, cause);
    sprintf(body + strlen(body), "<hr><em>CSAPP Proxy</em>\r\n");

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-Type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-Length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

/*
 * Parse an absolute URI like "http://host:port/path" into its parts.
 * Defaults: port 80, path "/".
 */
static void parse_uri(char *uri, char *host, char *port, char *path)
{
    char *p = uri;

    if (!strncasecmp(p, "http://", 7))
        p += 7;

    /* The host part ends at '/' or ':' */
    char *host_end = p;
    while (*host_end && *host_end != '/' && *host_end != ':')
        host_end++;

    char *colon = (*host_end == ':') ? host_end : NULL;
    char *host_stop = colon ? colon : host_end;

    size_t host_len = (size_t)(host_stop - p);
    strncpy(host, p, host_len);
    host[host_len] = '\0';

    if (colon) {
        char *ps = colon + 1;
        char *pe = ps;
        while (*pe && *pe != '/')
            pe++;
        size_t port_len = (size_t)(pe - ps);
        if (port_len == 0) {
            strcpy(port, "80");
        } else {
            strncpy(port, ps, port_len);
            port[port_len] = '\0';
        }
        p = pe;
    } else {
        strcpy(port, "80");
        p = host_end;
    }

    if (*p == '/')
        strcpy(path, p);
    else
        strcpy(path, "/");
}

/*
 * Read the remaining client headers and build the request that will be
 * sent to the origin server.  Connection-specific headers are dropped
 * and replaced with our own, per the lab handout.
 */
static void build_request(char *host, char *path, rio_t *client_rio,
                          char *out, size_t outsize)
{
    char buf[MAXLINE];
    size_t len = 0;

    len += sprintf(out + len, "GET %s HTTP/1.0\r\n", path);
    len += sprintf(out + len, "Host: %s\r\n", host);

    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n"))
            break;                                  /* end of headers   */
        if (!strncasecmp(buf, "Connection:", 11))
            continue;                               /* drop these       */
        if (!strncasecmp(buf, "Proxy-Connection:", 17))
            continue;
        if (!strncasecmp(buf, "Host:", 5))
            continue;
        if (!strncasecmp(buf, "User-Agent:", 11))
            continue;
        if (len + strlen(buf) + 1 >= outsize)
            break;
        len += sprintf(out + len, "%s", buf);
    }

    if (len + strlen(user_agent_hdr) + 1 < outsize)
        len += sprintf(out + len, "%s", user_agent_hdr);
    if (len + 30 < outsize) {
        len += sprintf(out + len, "Connection: close\r\n");
        len += sprintf(out + len, "Proxy-Connection: close\r\n");
        len += sprintf(out + len, "\r\n");
    }
}

/*
 * Handle one client connection: parse the request, forward it to the
 * origin server, and relay the response back to the client.
 */
static void doit(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], path[MAXLINE];
    char *req;
    rio_t client_rio, server_rio;
    ssize_t n;
    int serverfd;

    Rio_readinitb(&client_rio, fd);
    if (Rio_readlineb(&client_rio, buf, MAXLINE) <= 0)
        return;

    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        clienterror(fd, buf, "400", "Bad Request",
                    "Proxy could not parse the request line");
        return;
    }

    /* The basic version only supports GET */
    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    parse_uri(uri, host, port, path);

    req = Malloc(MAX_REQ_SIZE);
    build_request(host, path, &client_rio, req, MAX_REQ_SIZE);

    serverfd = Open_clientfd(host, port);
    Rio_writen(serverfd, req, strlen(req));
    Free(req);

    /* Relay the origin server's response, binary safe */
    Rio_readinitb(&server_rio, serverfd);
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0)
        Rio_writen(fd, buf, n);

    Close(serverfd);
}


static void mydoit(int fd){
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], path[MAXLINE];
    char *req;
    int serverfd;
    rio_t client_rio, server_rio;
    ssize_t n;
    int len;
    Rio_readinitb(&client_rio, fd);
    len=Rio_readlineb(&client_rio,buf,MAXLINE);
    if(len<=0){
        return;
    }
    if(buf[len-1]!='\n'){
        clienterror(fd, buf, "400", "Bad Request",
                    "Proxy could not parse the request line");
        return;
    }
    if(sscanf(buf, "%s %s %s", method, uri, version)!=3){
        clienterror(fd, buf, "400", "Bad Request",
                    "Proxy could not parse the request line");
        return;
    }
    if(strcasecmp(method, "GET")!=0){
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    parse_uri(uri, host, port, path);

}