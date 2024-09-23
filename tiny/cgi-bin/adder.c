/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
#include "csapp.h"
int main(void) {
  char *buf, *p; 
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1=0, n2=0;

  // 숙제문제 11.10
  // 인수 추출
  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&');
    *p = '\0';
    strcpy(arg1, buf);
    strcpy(arg2, p+1);
    n1 = atoi(arg1);
    n2 = atoi(arg2);
  }

  // response body 만들기
  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  // HTTP 응답 생성
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");

  if (getenv("REQUEST_METHOD") != NULL){
    fflush(stdout);
    exit(0);
  }

  printf("%s", content);
  fflush(stdout);
  exit(0);
}