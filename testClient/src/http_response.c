#include "http_.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void process_response(char *response, struct http_response *res)
{

    char response_line[1024] = {0};
    char *end_res_line = strchr(response, '\n');
    strncpy(response_line, response, end_res_line - response);
    process_status_line(response_line, res->res_line);
}
void process_status_line(char *response, struct status_line *status_line)
{
    char buf[1024] = {0};
    int res_code;

    char *end_http_ver = strchr(response, ' ');
    strncpy(buf, response, (end_http_ver - response));
    status_line->HTTP_VER = strdup(buf);

    char *end_status_code = strchr(end_http_ver + 1, ' ');
    strncpy(buf, end_http_ver + 1, end_status_code - end_http_ver);
    res_code = atoi(buf);
    status_line->res = (int)malloc(sizeof(res_code));
    status_line->res = res_code;

    char *end_reason = strchr(end_status_code + 1, '\r');
    strncpy(buf, end_reason + 1, end_reason - end_status_code);
    status_line->reason_phrase = strdup(buf);
}

int main()
{
    struct http_response *test =
        (struct http_request *)malloc(sizeof(struct http_request));
    struct status_line *line =
        (struct status_line *)malloc(sizeof(struct status_line));
    test->res_line = line;
    char *temp_response = "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: 6\r\n\r\nHello\n\r\n\r\n";

    process_response(temp_response, test);

    printf("%s\n", test->res_line->HTTP_VER);
    printf("%s\n", test->res_line->res);
    printf("%s\n", test->res_line->reason_phrase);
    // TODO: write destroy function
    free(test->res_line->HTTP_VER);
    free(test->res_line->reason_phrase);
    free(test->res_line);
    free(test);

    return 0;
}
