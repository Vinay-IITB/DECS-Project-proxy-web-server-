/*
 * proxy_parse.h -- a HTTP Request Parsing Library.
 *
 * Written by: Matvey Arye
 * For: COS 518
 *
 * Modified and annotated for compatibility with proxy_server_with_LRU_cache.c
 */

#ifndef PROXY_PARSE
#define PROXY_PARSE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

#define DEBUG 1

/* 
   ParsedRequest objects are created from parsing a buffer containing a HTTP
   request. The request buffer consists of a request line followed by a number
   of headers. Request line fields such as method, protocol etc. are stored
   explicitly. Headers such as 'Content-Length' and their values are maintained
   in a linked list. Each node in this list is a ParsedHeader and contains a
   key-value pair.

   The buf and buflen fields are used internally to maintain the parsed request
   line.
*/
struct ParsedRequest {
    char *method;           // e.g. "GET"
    char *protocol;         // e.g. "http"
    char *host;             // e.g. "www.google.com"
    char *port;             // e.g. "80"
    char *path;             // e.g. "/index.html"
    char *version;          // e.g. "HTTP/1.1"
    char *buf;              // internal buffer
    size_t buflen;          // buffer length
    struct ParsedHeader *headers;  // headers array
    size_t headersused;     // number of headers used
    size_t headerslen;      // allocated header capacity
};

/* 
   ParsedHeader: represents one key-value header pair in HTTP request
   Example: "Host: www.google.com"
*/
struct ParsedHeader {
    char *key;              // e.g. "Host"
    size_t keylen;
    char *value;            // e.g. "www.google.com"
    size_t valuelen;
};

/* ================================================================
   Function declarations
   ================================================================ */

/* Create an empty parsing object to be used once for parsing a single request buffer */
struct ParsedRequest* ParsedRequest_create();

/* Parse the request buffer in buf given that buf is of length buflen */
int ParsedRequest_parse(struct ParsedRequest *parse, const char *buf, int buflen);

/* Destroy the parsing object and free allocated memory */
void ParsedRequest_destroy(struct ParsedRequest *pr);

/* 
   Retrieve the entire buffer from a parsed request object.
   buf must have enough space to store the request line, headers, and trailing \r\n.
*/
int ParsedRequest_unparse(struct ParsedRequest *pr, char *buf, size_t buflen);

/* 
   Retrieve only the headers (without the request line) from a parsed request object.
   buf must have enough space for headers and trailing \r\n.
*/
int ParsedRequest_unparse_headers(struct ParsedRequest *pr, char *buf, size_t buflen);

/* Total length including request line, headers, and trailing \r\n */
size_t ParsedRequest_totalLen(struct ParsedRequest *pr);

/* Length including headers (if any) and trailing \r\n but excluding request line */
size_t ParsedHeader_headersLen(struct ParsedRequest *pr);

/* Set, get, and remove null-terminated header keys and values */
int ParsedHeader_set(struct ParsedRequest *pr, const char *key, const char *value);
struct ParsedHeader* ParsedHeader_get(struct ParsedRequest *pr, const char *key);
int ParsedHeader_remove(struct ParsedRequest *pr, const char *key);

/* Debug printer (prints only if DEBUG == 1) */
void debug(const char *format, ...);

/* ================================================================
   Example usage (for reference only)
   ================================================================

   const char *c =
       "GET http://www.google.com:80/index.html HTTP/1.0\r\n"
       "Content-Length: 80\r\n"
       "If-Modified-Since: Sat, 29 Oct 1994 19:43:31 GMT\r\n\r\n";

   int len = strlen(c);
   ParsedRequest *req = ParsedRequest_create();
   if (ParsedRequest_parse(req, c, len) < 0) {
       printf("parse failed\n");
       return -1;
   }

   printf("Method: %s\n", req->method);
   printf("Host: %s\n", req->host);

   // Clean up
   ParsedRequest_destroy(req);
*/

/* ================================================================
   Added typedefs for shorthand names (Fix for compiler errors)
   ================================================================ */
typedef struct ParsedRequest ParsedRequest;
typedef struct ParsedHeader ParsedHeader;

#endif  // PROXY_PARSE

