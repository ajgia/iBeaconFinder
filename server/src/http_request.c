#include "http_.h"
struct http_request req;
struct request_line req_line;
void process_request(char *request, struct http_request *req)
{
    req->req_line.HTTP_VER = "1.1";
};
