/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* 실행 시 인수로 포트번호가 들어오지 않았을 경우 exit */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) {
  signal(SIGPIPE, SIG_IGN);
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers   */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);             // line:netp:doit:readrequest
  // 여기서 요청을 읽고 HTTP 메소드, URI, HTTP 버전을 파싱
  sscanf(buf, "%s %s %s", method, uri, version); // line:netp:doit:parserequest
  printf(":: %s %s %s ::\n", method, uri, version);
  if (strcasecmp(method, "GET")) { // line:netp:doit:beginrequesterr
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  } // line:netp:doit:endrequesterr
  read_requesthdrs(&rio); // line:netp:doit:readrequesthdrs

  /* Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs); // line:netp:doit:staticcheck
  if (stat(filename, &sbuf) < 0) { // line:netp:doit:beginnotfound
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  } // line:netp:doit:endnotfound

  if (is_static) { /* Serve static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { // line:netp:doit:readable
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size); // line:netp:doit:servestatic
  } else { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { // line:netp:doit:executable
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // line:netp:doit:servedynamic
  }
}

/*
 * read_requesthdrs - read and parse HTTP request headers
 */
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) { // line:netp:readhdrs:checkterm
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  /* Static content */
  if (!strstr(uri, "cgi-bin")){      // line:netp:parseuri:isstatic
    strcpy(cgiargs, "");             // line:netp:parseuri:clearcgi
    strcpy(filename, ".");           // line:netp:parseuri:beginconvert1
    strcat(filename, uri);           // line:netp:parseuri:endconvert1
    if (uri[strlen(uri) - 1] == '/') { // line:netp:parseuri:slashcheck
      strcat(filename, "home.html"); // line:netp:parseuri:appenddefault
    }
    return 1;
  /* Dynamic content */
  } else {                 // line:netp:parseuri:isdynamic
    ptr = index(uri, '?'); // line:netp:parseuri:beginextract
    if (ptr) {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    } else {
      strcpy(cgiargs, ""); // line:netp:parseuri:endextract
    }
    strcpy(filename, "."); // line:netp:parseuri:beginconvert2
    strcat(filename, uri); // line:netp:parseuri:endconvert2
    return 0;
  }
}

/*
 * serve_static - copy a file back to the client
 */
void serve_static(int fd, char *filename, int filesize) {
  int srcfd, n;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* 파일 전송을 위한 청크 크기 설정 (예: 8192 바이트) */
  const int CHUNK_SIZE = MAXBUF;
  char chunk[CHUNK_SIZE];
  int remaining_size = filesize;

  /* Send response headers to client */
  get_filetype(filename, filetype);    // line:netp:servestatic:getfiletype
  // 서버 응답 버전 명시
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 여기 바꾸면 http 프로토콜 바뀜
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf)); // line:netp:servestatic:endserve

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);                        // line:netp:servestatic:open
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // line:netp:servestatic:mmap
  Close(srcfd);                                               // line:netp:servestatic:close
  Rio_writen(fd, srcp, filesize);                             // line:netp:servestatic:write
  Munmap(srcp, filesize);                                     // line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) {
  // 파일명 내에서 마지막 . 찾기 -> 파일명 중간에 확장자가 들어가는 케이스 방어
  char *ext = strrchr(filename, '.');

  if (ext != NULL) {
    if (strcmp(ext, ".html") == 0) {
      strcpy(filetype, "text/html");
    } else if (strcmp(ext, ".gif") == 0) {
      strcpy(filetype, "image/gif");
    } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
      strcpy(filetype, "image/jpeg");
    } else if (strcmp(ext, ".mp4") == 0) {
      strcpy(filetype, "video/mp4");
    } else {
      strcpy(filetype, "text/plain");
    }
  } else {
    // 확장자가 없을 경우 기본 값
    strcpy(filetype, "text/plain");
  }

  // if (strstr(filename, ".html")) {
  //   strcpy(filetype, "text/html");
  // } else if (strstr(filename, ".gif")) {
  //   strcpy(filetype, "image/gif");
  // } else if (strstr(filename, ".jpg")) {
  //   strcpy(filetype, "image/jpeg");
  // } else if (strstr(filename, ".mp4")) {
  //   strcpy(filetype, "video/mp4");
  // } else {
  //   strcpy(filetype, "text/plain");
  // }
}

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* Return first part of HTTP response */
  // 서버 응답 버전 명시
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { /* child */ // line:netp:servedynamic:fork
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1);                         // line:netp:servedynamic:setenv
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */    // line:netp:servedynamic:dup2
    Execve(filename, emptylist, environ); /* Run CGI program */ // line:netp:servedynamic:execve
  }
  Wait(NULL); /* Parent waits for and reaps child */ // line:netp:servedynamic:wait
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=" "ffffff" ">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}
