#include <string.h>
#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Request, Header, CachedItem struct declaration */
typedef struct
{
  char method[10];
  char uri[200];
  char hostname[200];
  char path[1000];
  char version[200];
} Request;

typedef struct RequestHeader
{
  char name[MAXLINE];
  char data[MAXLINE];
  struct RequestHeader* next;
} RequestHeader;

typedef struct CachedItem
{
  char hostname[200];
  char path[1000];
  size_t size;
  char* data;
  struct CachedItem* next;
  clock_t access_time;
} CachedItem;


/* Global and static variables */
CachedItem* root_cache;
int cache_volume = 0;
static const char *user_agent_hdr = "Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";

/* Helper functions */
static RequestHeader *root_header = NULL;
void *thread_handler(void*);
void client_handler(int, Request*);
void server_handler(int, Request*);

void parse_request(Request *, char*);
void parse_header(char*);

void insert_header(RequestHeader*);
RequestHeader* get_last_header();
RequestHeader* get_header_by_key(char*);
void init_header(Request*);
void free_req_and_header(Request*, RequestHeader*);

void send_request(int, Request*, RequestHeader* header_host);

char* safe_strncpy(char *, const char*, size_t);

void init_cache();
void delete_cache(CachedItem*);
void update_time(CachedItem*);

int main(int argc, char **argv) {
  char *port;
  int listenfd, *connfd;
  socklen_t clientlen;
  struct sockaddr_in clientaddr;
  pthread_t tid;

  if (argc != 2) {
    printf("Argument error, ex: ./proxy <port_number>\n");
  }
  root_cache = malloc(sizeof(CachedItem));
  init_cache();
  port  = argv[1];
  listenfd = Open_listenfd(port);

  while(1) {
    clientlen = sizeof(clientaddr);
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Pthread_create(&tid, NULL, thread_handler, connfd);
  }
  return 0;
}

/* initialize cache */
void init_cache() {
  strcpy(root_cache->hostname, "");
  strcpy(root_cache->path, "");
  root_cache->size= 0;
  root_cache->data= NULL;
  root_cache->next = NULL;
  root_cache->access_time= clock();
}

/* function for each thread */
void *thread_handler(void* vargp) {    
  Pthread_detach(pthread_self());
  int clientfd = *((int*)vargp);
  free(vargp);
  Request* req = Malloc(sizeof(Request));
  client_handler(clientfd, req);
  server_handler(clientfd, req);
  return NULL;
}

/* handle interaction with client */
void client_handler(int clientfd, Request* req) {
    rio_t rio;
    char buf[MAXLINE];

    Rio_readinitb(&rio, clientfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    parse_request(req, buf);
    memset(&buf[0], 0, sizeof(buf)); 

    Rio_readlineb(&rio, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        parse_header(buf);
        memset(&buf[0], 0, sizeof(buf)); 
        Rio_readlineb(&rio, buf, MAXLINE);
    }
    memset(&buf[0], 0, sizeof(buf));
    init_header(req);
}

/* search for cached request */
CachedItem* search_cache(char path[], char hostname[]) {
  CachedItem* temp = root_cache;
  while(temp!= NULL) {
    if((strcmp(path, temp-> path) == 0) && (strcmp(hostname, temp-> hostname) == 0)) return temp;
    temp = temp->next;
  }
  return NULL;
}

/* handle interaction with server */
void server_handler(int clientfd, Request* req) {
    RequestHeader* header_host;
    header_host= get_header_by_key("Host");
    CachedItem* target = search_cache(req->path, header_host->data);
    if (target) {
      Rio_writen(clientfd, target->data, target->size);
      update_time(target);
      Close(clientfd);
      return;
    }
    send_request(clientfd, req, header_host);
}

/* initialize new CachedItem and insert to list */
CachedItem* create_cache() {
  CachedItem* temp = root_cache;
  CachedItem* target;
  while(temp -> next) {
    temp = temp->next;
  }
  target = malloc(sizeof(CachedItem));
  temp->next = target;
  target->next = NULL;
  return target;
}

/* updated cache hit time */
void update_time(CachedItem* target) {
  target->access_time = clock();
  return;
}

/* select CachedItem to be evicted by LRU policy */
CachedItem* LRU() {
  CachedItem* temp = root_cache;
  temp = temp->next;
  CachedItem* eviction = temp;
  clock_t least = temp -> access_time;
  while (temp) {
    if (least > temp->access_time){
      least = temp->access_time;
      eviction = temp;
    }
    temp = temp->next;
  }
  return eviction;
}

/* delete eviction cache */
void delete_cache(CachedItem* eviction) {
  CachedItem* temp = root_cache;
  if (!eviction) return;
  if (!temp) return;
  while ((temp->next != eviction) && temp){
    temp = temp->next;
  }
  if (!temp) return;
  temp->next = eviction ->next;
  cache_volume -= (eviction->size);
  free(eviction->data);
  free(eviction);
  return;
}

/* send request to serverfd and get response to clientfd, update cache */
void send_request(int clientfd, Request* req, RequestHeader* header_host) {
  char* default_port="80";
  char* pport = NULL;

  char Request_port[200];
  char Request_domain[200];
  int serverfd;
  char Request_buf[10000];
  RequestHeader* header;

  if (strlen(req->hostname)) {
    strcpy(Request_domain, req->hostname);
  }
  else if ((header = get_header_by_key("Host")) != NULL) {
    strcpy(Request_domain, header->data);
  }
  else {
    printf("error occur: host not found\n");
    return;
  }
  pport = strstr(Request_domain, ":");
  if (pport) {
    *pport = '\0';
    pport = (pport+1);
    strcpy(Request_port, pport);
  } else {
    strcpy(Request_port, default_port);
  }

  strcat(Request_buf, req->method);
  strcat(Request_buf, " ");
  strcat(Request_buf, req->path);
  strcat(Request_buf, " ");
  strcat(Request_buf, "HTTP/1.0\r\n");
  header = root_header;
  while (header) {
    strcat(Request_buf, header->name);
    strcat(Request_buf, ": ");
    strcat(Request_buf, header->data);
    strcat(Request_buf, "\r\n");
    header = header->next;
  }
  strcat(Request_buf, "\r\n");

  serverfd = Open_clientfd(Request_domain, Request_port);
  
  Rio_writen(serverfd, Request_buf, strlen(Request_buf));
  rio_t rio;
  char read_buf[MAXLINE];
  ssize_t n = 0;
  char cache_buf[MAX_OBJECT_SIZE];
  char *cache_ptr = cache_buf;
  int cachable = 1;
  Rio_readinitb(&rio, serverfd);
  while ((n = Rio_readnb(&rio, read_buf, MAXLINE)) > 0) {
    Rio_writen(clientfd, read_buf, (size_t)n);
    if (cachable) {
      if ((n+(cache_ptr - cache_buf)) <= MAX_OBJECT_SIZE){
        safe_strncpy(cache_ptr, read_buf, n); 
        cache_ptr += n;
      } else {
        cachable = 0;
      }
    }
  }
  if (cachable) {
    int cache_size = cache_ptr - cache_buf;
    if ((cache_volume + cache_size) > MAX_CACHE_SIZE){
      while ((cache_volume + cache_size > MAX_CACHE_SIZE)){
        CachedItem* eviction = LRU();
        delete_cache(eviction);
      }
    }
    CachedItem* new_cache = create_cache();
    new_cache->size = cache_size;
    strcpy(new_cache->hostname, header_host->data);
    strcpy(new_cache->path, req->path);
    new_cache -> data = malloc(cache_size);
    safe_strncpy(new_cache ->data, cache_buf, cache_size);
    cache_volume += cache_size;
    update_time(new_cache);
  }
  Close(serverfd);
  Close(clientfd);
}

/* initialize header struct */
void init_header(Request* req) {
  RequestHeader* find = NULL;
  find = get_header_by_key("Host");
  if (!find){
    RequestHeader* new_header = Malloc(sizeof(RequestHeader));
    strcpy(new_header->name, "Host");
    strcpy(new_header->data, req->hostname);
    insert_header(new_header);
  }
  find = NULL;

  find = get_header_by_key("User-Agent");
  if (!find){
    RequestHeader* new_header = Malloc(sizeof(RequestHeader));
    strcpy(new_header->name, "User-Agent");
    strcpy(new_header->data, user_agent_hdr);
    insert_header(new_header);
  }
  find = NULL;

  find = get_header_by_key("Connection");
  if (!find){
    RequestHeader* new_header = Malloc(sizeof(RequestHeader));
    strcpy(new_header->name, "Connection");
    strcpy(new_header->data, "close");
    insert_header(new_header);
  }
  find = NULL;

  find = get_header_by_key("Proxy-Connection");
  if (!find){
    RequestHeader* new_header = Malloc(sizeof(RequestHeader));
    strcpy(new_header->name, "Proxy-Connection");
    strcpy(new_header->data, "close");
    insert_header(new_header);
  }
  find = NULL;
}

/* get header struct with key */
RequestHeader* get_header_by_key(char* type) {
  RequestHeader* temp; 
  temp = root_header;
  while (temp) {
    if (strcmp(temp->name, type) == 0) {
      return temp;
    } else {
      temp = temp->next;
    }
  }
  return NULL;
}

/* insert header to list */
void insert_header(RequestHeader *header) {
  RequestHeader* last=NULL;
  if (!root_header) {
    root_header = header;
    header->next = NULL;
  } else {
    last = get_last_header();
    last->next = header;
    header->next = NULL;
  }
}

/* parse request and update Request structure */
void parse_request(Request* req, char* buf) {
  char method[10];
  char uri[300];
  char version[100];
  char *host_start;
  char *path_start;
  sscanf(buf, "%s %s %s\n", method, uri, version);
  strcpy(req->method, method);
  strcpy(req->uri, uri);
  strcpy(req->version, version);

  if (strcmp("GET", method) == 0) {
    // URL to hostname && path
    host_start = strstr(uri, "http://");
    if (!host_start) {
      return;
    }
    host_start += 7;
    path_start = strstr(host_start, "/");
    if (!path_start) {
      //no path -> add default path '/'
      strcpy(req->hostname, host_start);
      strcpy(req->path, "/");
    } else {
      safe_strncpy(req->hostname, host_start, path_start - host_start);
      strcpy(req->path, path_start);
    }
  }
}

/* parse and insert header struct to send to serverfd */
void parse_header(char *buf) {
  RequestHeader* header = Malloc(sizeof(RequestHeader));
  char* pname = strstr(buf, ": ");
  char* pdata = strstr(buf, "\r\n");
  if ((!pname)||(!pdata)) {
    printf("bad header format error\n");
    return;
  }
  safe_strncpy(header->name, buf, pname-buf);
  safe_strncpy(header->data, pname+2, pdata-pname-2);
  insert_header(header);
}

/* get last header struct from the list */
RequestHeader* get_last_header() {
  RequestHeader* temp;
  temp = root_header;
  while (temp && temp->next){
    temp = temp ->next;
  }
  return temp;
}

/* free Request structure and header list */
void free_req_and_header(Request* req, RequestHeader* root_header) {
  RequestHeader* current = root_header;
  RequestHeader* temp = root_header;
  if (current) {
    while (current->next) {
      temp = current->next;
      free(current);
      current = temp;
    }
    free(current);
  }
  free(req);
}

/* copy string safely */
char* safe_strncpy(char *dst, const char *src, size_t n) {
  dst[0] = '\0';
  return strncat(dst, src, n );
}