#ifndef TEMPLATE_HTTP__H
#define TEMPLATE_HTTP__H
#undef OK
typedef enum response_codes response_codes_t;
typedef enum request_method request_method_t;
/**
 * @brief HTTP request methods
 * 
 */
enum request_method
{
    GET,
    PUT,
    POST
};
/**
 * @brief HTTP response codes
 * 
 */
enum response_codes
{
    // Informational Responses
    CONTINUE = 100,
    SWITCH_PROTOCOL,
    PROCESSING,
    EARLY_HINTS,

    // Successful responses
    OK = 200,
    CREATED,
    ACCEPTED,
    NON_AUTHORITATIVE_INFO,
    NO_CONTENT,
    RESET_CONTENT,
    PARTIAL_CONTENT,

    // Redirection Messages
    MULTIPLE_CHOICE = 300,
    MOVED_PERMANENTLY,
    FOUND,
    SEE_OTHER,
    NOT_MODIFIED,
    USE_PROXY,
    TEMPORARY_REDIRECT = 307,
    PERMANENT_REDIRECT,

    // Client Error Responses
    BAD_REQUEST = 400,
    UNAUTHORIZED,
    PAYMENT_REQUIRED,
    FORBIDDEN,
    NOT_FOUND,
    METHOD_NOT_ALLOWED,
    NOT_ACCEPTABLE,
    PROXY_AUTHENTICATION_REQUIRED,
    REQUEST_TIMEOUT,
    CONFLICT,
    GONE,
    LENGTH_REQUIRED,
    PRECONDITION_FAILED,
    PAYLOAD_TOO_LARGE,
    URI_TOO_LONG,
    UNSUPPORTED_MEDIA_TYPE,
    RANGE_NOT_SATISFIABLE,
    EXPECTATION_FAILED,
    IM_A_TEAPOT,
    MISDIRECT_REQUEST,
    UNPROCESSABLE_ENTITY,
    LOCKED,
    FAILED_DEPENDENCY,
    TOO_EARLY,
    UPGRADE_REQUIRED,
    PRECONDITION_REQUIRED,
    TOO_MANY_REQUESTS,
    REQUEST_HEADER_FIELDS_TOO_LARGE,
    UNAVAILABLE_FOR_LEGAL_REASONS,

    // Server Error Responses
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED,
    BAD_GATEWAY,
    SERVICE_UNAVAILABLE,
    GATEWAY_TIMEOUT,
    HTTP_VERSION_NOT_SUPPORTED,
    VARIANT_ALSO_NEGOTIATES,
    INSUFFICIENT_STORAGE,
    LOOP_DETECTED,
    NOT_EXTENDED,
    NETWORK_AUTHENTICATION_REQUIRED
};

/**
 * @brief HTTP request line
 *
 */
struct request_line
{
    char *req_method;
    char *path;
    char *HTTP_VER;
};
/**
 * @brief HTTP status line
 *
 */
struct status_line
{
    char *HTTP_VER;
    response_codes_t res;
    char *reason_phrase;
};

/**
 * @brief HTTP request
 * 
 */
struct http_request
{
    struct request_line *req_line;
    char *headers;
    char *message_body;
};

/**
 * @brief HTTP response
 * 
 */
struct http_response
{
    struct status_line *stat_line;
    int content_length;
    char *headers;
    char *message_body;
};
/**
 * @brief Parses an HTTP request string into a struct
 *
 * @param request
 * @param req
 */
void process_request(char *request, struct http_request *req);
/**
 * @brief Parses request line out of http request string
 *
 * @param str_in
 * @param req
 */
void process_request_line(char *str_in, struct request_line *req);
/**
 * @brief Parses headers out of http request string
 *
 * @param header_line
 * @param req
 */
void process_header_line(char *header_line, struct http_request *req);
/**
 * @brief Parses an HTTP response string into a struct
 *
 * @param response
 * @param res
 */
void process_response(char *response, struct http_response *res);
/**
 * @brief Parses status line out of HTTP response string
 *
 * @param response
 * @param status_line
 */
void process_status_line(char *response, struct status_line *status_line);
/**
 * @brief Parses content length out of an HTTP response string
 *
 * @param response
 * @param res
 */
void process_content_length(char *response, struct http_response *res);
/**
 * @brief Parses body out of an HTTP request string
 *
 * @param request
 * @param res
 */
void process_body(char *request, struct http_response *res);
#endif  // TEMPLATE_HTTP__H
