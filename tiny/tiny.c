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
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /**
   * proxy lab pdf - Hints
   * As discussed in the Aside on page 964 of the CS:APP3e text, your proxy must ignore SIGPIPE signals
   * and should deal gracefully with write operations that return EPIPE errors.
   */
  // 위의 내용에 의거하여 아래 처리를 진행
  signal(SIGPIPE, SIG_IGN);

  // 실행 시 인수로 포트번호가 들어오지 않았을 경우 exit
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 서버 소켓 열기
  listenfd = Open_listenfd(argv[1]);

  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    // 클라이언트 진입 시 대기 끝내고 여기부터 실행
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    Close(connfd);
  }
}

/*
 * doit - 한 개의 HTTP transaction 을 처리
 */
void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /**
   * proxy lab pdf - Hints
   * As discussed in the Aside on page 964 of the CS:APP3e text, your proxy must ignore SIGPIPE signals
   * and should deal gracefully with write operations that return EPIPE errors.
   */
  // 위의 내용에 의거하여 아래 처리를 진행
  signal(SIGPIPE, SIG_IGN);

  // 여기서 요청을 읽고 HTTP 메소드, URI, HTTP 버전을 파싱
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);
  printf(":: %s %s %s ::\n", method, uri, version);
  
  // tiny 는 GET 메소드 이외에는 오류로 떨어트림
  // 연습문제 11.11 - HEAD 메소드 추가
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  // URI 파싱
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  // 정적컨텐츠 처리
  if (is_static) {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);

  // 동적컨텐츠 처리
  } else {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

/*
 * read_requesthdrs - HTTP request headers 를 읽고 파싱
 */
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf); // 헤더 로그에 찍기
  }

  return;
}

/*
 * parse_uri - URIparse URI into filename and CGI args
 * @return
 *  0: dynamic content
 *  1: static content
 */
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  /* Static content */
  if (!strstr(uri, "cgi-bin")){
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;
  /* Dynamic content */
  } else {
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    } else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/*
 * serve_static - copy a file back to the client
 */
void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd, n;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* 파일 전송을 위한 청크 크기 설정 (예: 8192 바이트) */
  const int CHUNK_SIZE = MAXBUF;
  char chunk[CHUNK_SIZE];
  int remaining_size = filesize;

  /* Send response headers to client */
  get_filetype(filename, filetype);
  // 서버 응답 버전 명시
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 여기 바꾸면 http 프로토콜 바뀜
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));

  // HEAD 메소드로 진입한 경우 여기서 return
  if (!strcasecmp(method, "HEAD")) {
    return;
  }

  // response body 를 클라이언트로 전송
  // srcfd = Open(filename, O_RDONLY, 0);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // Close(srcfd);
  // Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);

  // 연습문제 11.9
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = (char*)malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  free(srcp);
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
}

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* Return first part of HTTP response */
  // 서버 응답 버전 명시
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // HEAD 메소드로 진입한 경우 여기서 return
  if (!strcasecmp(method, "HEAD")) {
    return;
  }

  if (Fork() == 0) { /* child */
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1);
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child */
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
