#include "http_.h"

void process_request(char *request, struct http_request *req)
{
    req->req_line.HTTP_VER = "1.1";
}
