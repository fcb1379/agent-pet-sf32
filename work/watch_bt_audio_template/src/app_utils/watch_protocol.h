#ifndef WATCH_PROTOCOL_H
#define WATCH_PROTOCOL_H

#include <stddef.h>

/* Returns 1 when the payload is a v1 request and a response was generated. */
int watch_protocol_handle_request(const char *request, char *response, size_t response_size);

#endif /* WATCH_PROTOCOL_H */
