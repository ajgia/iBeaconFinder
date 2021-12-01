#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// to test: change the include to "../include/http_.h"
#include "http_.h"
#include "common.h"
void process_request_line(char *req_line_str, struct request_line *req_line)
{
    const static int bufSize = 1024;
    char buf[1024] = {0};

    // can probably turn this into a function, will get back to it later
    char *end_method = strchr(req_line_str, ' ');
    strncpy(buf, req_line_str, (end_method - req_line_str));

    // note, these have to be freed
    req_line->req_method = strdup(buf);
    memset(buf, '\0', bufSize);

    char *end_path = strchr(end_method + 1, ' ');
    strncpy(buf, end_method + 1, end_path - end_method - 1);
    req_line->path = strdup(buf);
    memset(buf, '\0', bufSize);

    char *end_ver = strchr(end_path + 1, '\0');
    strncpy(buf, end_path + 1, end_ver - end_path);
    req_line->HTTP_VER = strdup(buf);
}

void process_request(char *request, struct http_request *req)
{
    const char* endOfHeaderDelimiter = "\r\n\r\n";
    // TODO: remove magic numbers
    char request_line[101] = {0};
    char *endOfFirstLine = strchr(request, '\r');
    strncpy(request_line, request, (endOfFirstLine - request));
    process_request_line(request_line, req->req_line);
    // headers

    // body
    char *startOfBody = strstr(request, endOfHeaderDelimiter);
    req->message_body = (char*)calloc(MAX_REQUEST_SIZE, sizeof(char));
    strcpy( req->message_body, (startOfBody + strlen(endOfHeaderDelimiter)) );
}

// int main()
// {
//     struct http_request *test =
//         (struct http_request *)malloc(sizeof(struct http_request));
//     struct request_line *line =
//         (struct request_line *)malloc(sizeof(struct request_line));
//     test->req_line = line;

//     char req[100] = "GET /software/htp/cics/index.html
//     HTTP/1.1\r\nheaders";

//     process_request(req, test);

//     printf("%s\n", test->req_line->req_method);
//     printf("%s\n", test->req_line->path);
//     printf("%s\n", test->req_line->HTTP_VER);
//     // TODO: write destroy function
//     free(test->req_line->req_method);
//     free(test->req_line->path);
//     free(test->req_line->HTTP_VER);
//     free(test->req_line);
//     free(test);

//     return 0;
// }
