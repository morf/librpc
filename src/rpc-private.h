#pragma once

#include <rpc.h>

#include <stddef.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <sys/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rpc_connection {
    struct rpc_server *server;

    rpc_connection_cb cb;
    void *cbarg;
    void *ctx;

    struct bufferevent *evbuff;

    TAILQ_ENTRY(rpc_connection) entries;
};

#ifdef __cplusplus
}
#endif
