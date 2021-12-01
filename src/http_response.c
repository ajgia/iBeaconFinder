#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_.h"
void process_response(char *response, struct http_response *res)
{
    char response_line[1024] = {0};
    char *end_res_line = strchr(response, '\n');
    strncpy(response_line, response, end_res_line - response);
    process_status_line(response_line, res->stat_line);
    process_body(response, res);
}
void process_body(char *request, struct http_response *res)
{
    size_t totallength;
    size_t body_length;
    char *start_body;

    totallength = strlen(request);
    body_length = totallength - (size_t)res->content_length;
    start_body = strstr(request, "\r\n\r\n") + 4;

    res->message_body = strndup(start_body, body_length);
}
void process_status_line(char *response, struct status_line *status_line)
{
    char buf[1024] = {0};
    char *end_res_buf;
    int res_code;
    char *end_http_ver;
    char *end_status_code;
    char *start_reason;
    size_t size;

    // takes the ptr to the end of the httpver "HTTP/1.0" & status
    // codes "200/400"
    end_http_ver = strchr(response, ' ');
    end_status_code = strchr(end_http_ver + 1, ' ');
    // ptr to start of the reason phrase "OK"
    start_reason = end_status_code + 1;

    strncpy(buf, response, (size_t)(end_http_ver - response));
    status_line->HTTP_VER = strdup(buf);

    // need this weird pointer to the end of the buffer to properly copy the
    // response code as atoi gives warnings
    end_res_buf = buf + 2;
    strncpy(buf, end_http_ver + 1, (size_t)(end_status_code - end_http_ver));
    res_code = (int)strtol(buf, &end_res_buf, 10);
    status_line->res = (response_codes_t)malloc(sizeof(res_code));
    status_line->res = (response_codes_t)res_code;

    size = strlen(start_reason);
    status_line->reason_phrase = strndup(start_reason, size - 1);
}
void process_content_length(char *response, struct http_response *res)
{
    char *inputDup = strdup(response);
    char *lengthStr;
    char *seekToPos;
    const char *seekTo = "Content-Length: ";
    int length;

    if (!strstr(inputDup, seekTo))
    {
        length = 0;
    }
    else
    {
        seekToPos = strstr(inputDup, seekTo);
        lengthStr = strtok(seekToPos + strlen(seekTo), " ");
        length = atoi(lengthStr);
    }
    // res->content_length = (int)malloc(sizeof(int));
    res->content_length = length;
    free(inputDup);
}

// testing purposes
// int main()
// {
//     printf("Here");
//     struct http_response *test =
//         (struct http_response *)malloc(sizeof(struct http_response));
//     struct status_line *line =
//         (struct status_line *)malloc(sizeof(struct status_line));
//     test->res_line = line;
//     char *temp_response =
//         "HTTP/1.0 200 OK\nContent-Type: text/plain\nContent-Length: "
//         "6\r\n\r\nHello\n\r\n\r\n";

//     process_response(temp_response, test);

//     printf("%s\n", test->res_line->HTTP_VER);
//     printf("%s\n", test->res_line->res);
//     printf("%s\n", test->res_line->reason_phrase);
//     // TODO: write destroy function
//     free(test->res_line->HTTP_VER);
//     free(test->res_line->reason_phrase);
//     free(test->res_line);
//     free(test);

//     return 0;
// }
