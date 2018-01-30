/**
 *    Copyright(c) 2016-2018 rryqszq4
 *
 *
 */

#include "ngx_php_debug.h"
#include "ngx_http_php_module.h"
#include "ngx_http_php_socket.h"
#include "ngx_http_php_zend_uthread.h"

static void ngx_http_php_socket_handler(ngx_event_t *event);

static void ngx_http_php_socket_dummy_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u);

static void ngx_http_php_socket_resolve_handler(ngx_resolver_ctx_t *ctx);

static int ngx_http_php_socket_resolve_retval_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u);

static void ngx_http_php_socket_connected_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u);

static ngx_int_t ngx_http_php_socket_get_peer(ngx_peer_connection_t *pc, 
    void *data);

static void ngx_http_php_socket_finalize(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u);

static ngx_int_t ngx_http_php_socket_upstream_send(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u);

static void ngx_http_php_socket_send_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u);

static ngx_int_t ngx_http_php_socket_upstream_recv(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u);

static void ngx_http_php_socket_upstream_recv_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u);


static void
ngx_http_php_socket_handler(ngx_event_t *ev)
{
    ngx_connection_t                    *c;
    ngx_http_request_t                  *r;
    ngx_http_php_socket_upstream_t      *u;

    ngx_php_debug("php socket handler");

    c = ev->data;
    u = c->data;
    r = u->request;

    ngx_http_php_zend_uthread_resume(r);

}

static void 
ngx_http_php_socket_dummy_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, 
                    "ngx_php tcp socket dummy handler.");
}

static ngx_int_t 
ngx_http_php_socket_get_peer(ngx_peer_connection_t *pc, 
    void *data)
{
    return NGX_OK;
}

static void 
ngx_http_php_socket_resolve_handler(ngx_resolver_ctx_t *ctx)
{
    ngx_http_request_t              *r;
    ngx_connection_t                *c;
    ngx_http_upstream_resolved_t    *ur;
    ngx_http_php_socket_upstream_t  *u;
    u_char                          *p;
    size_t                          len;
    //ngx_http_php_ctx_t              *php_ctx;

    socklen_t                        socklen;
    struct sockaddr                 *sockaddr;

    ngx_uint_t                      i;

    u = ctx->data;
    r = u->request;
    c = r->connection;
    ur = u->resolved;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "php socket resolve handler.");

    ngx_php_debug("php socket resolve handler.");

    if (ctx->state) {

    }

    ur->naddrs = ctx->naddrs;
    ur->addrs = ctx->addrs;

#if (NGX_DEBUG)
    {
    u_char      text[NGX_SOCKADDR_STRLEN];
    ngx_str_t   addr;
    ngx_uint_t  i;

    addr.data = text;

    for (i = 0; i < ctx->naddrs; i++ ) {
        addr.len = ngx_sock_ntop(ur->addrs[i].sockaddr, ur->addrs[i].socklen,
                                 text, NGX_SOCKADDR_STRLEN, 0);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, 
                       "name was resolved to %V", &addr);
    }
    }
#endif

    if (ur->naddrs == 1) {
        i = 0;
    }else {
        i = ngx_random() % ur->naddrs;
    }

    socklen = ur->addrs[i].socklen;

    sockaddr = ngx_palloc(r->pool, socklen);
    if (sockaddr == NULL) {

    }

    ngx_memcpy(sockaddr, ur->addrs[i].sockaddr, socklen);

    switch (sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
    case AF_INET6:
        ((struct sockaddr_in6 *) sockaddr)->sin6_port = htons(ur->port);
        break;
#endif
    default: /* AF_INET */
        ((struct sockaddr_in *) sockaddr)->sin_port = htons(ur->port);
    }

    p = ngx_pnalloc(r->pool, NGX_SOCKADDR_STRLEN);
    if (p == NULL) {

    }

    len = ngx_sock_ntop(sockaddr, socklen, p, NGX_SOCKADDR_STRLEN, 1);
    ur->sockaddr = sockaddr;
    ur->socklen = socklen;

    ur->host.data = p;
    ur->host.len = len;
    ur->naddrs = 1;

    ngx_resolve_name_done(ctx);
    ur->ctx = NULL;

    ngx_http_php_socket_resolve_retval_handler(r, u);

}

static int 
ngx_http_php_socket_resolve_retval_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u)
{
    ngx_int_t                       rc;
    ngx_http_php_ctx_t              *ctx;
    ngx_peer_connection_t           *peer;
    ngx_connection_t                *c;
    ngx_http_upstream_resolved_t    *ur;

    ctx = ngx_http_get_module_ctx(r, ngx_http_php_module);

    peer = &u->peer;

    ngx_php_debug("%p", peer);

    ur = u->resolved;

    if (ur->sockaddr) {
        peer->sockaddr = ur->sockaddr;
        peer->socklen = ur->socklen;
        peer->name = &ur->host;
    }

    peer->get = ngx_http_php_socket_get_peer;

    rc = ngx_event_connect_peer(peer);

    ngx_php_debug("rc: %d %p", (int)rc, ctx->generator_closure);

    if (rc == NGX_ERROR) {

    }

    if (rc == NGX_BUSY) {

    }

    if (rc == NGX_DECLINED) {

    }

    /* rc == NGX_OK || rc == NGX_AGAIN || rc == NGX_DONE */

    ctx->phase_status = NGX_AGAIN;

    c = peer->connection;
    c->data = u;

    c->write->handler = ngx_http_php_socket_handler;
    c->read->handler = ngx_http_php_socket_handler;

    u->write_event_handler = (ngx_http_php_socket_upstream_handler_pt )ngx_http_php_socket_connected_handler;
    u->read_event_handler = (ngx_http_php_socket_upstream_handler_pt )ngx_http_php_socket_connected_handler;

    c->sendfile &= r->connection->sendfile;

    if (c->pool == NULL) {

        /* we need separate pool here to be able to cache SSL connections */

        c->pool = ngx_create_pool(128, r->connection->log);
        if (c->pool == NULL) {

        }
    }

    c->log = r->connection->log;
    c->pool->log = c->log;
    c->read->log = c->log;
    c->write->log = c->log;

    /* init or reinit the ngx_output_chain() and ngx_chain_writer() contexts */

    if (rc == NGX_OK) {

    }

    if (rc == NGX_AGAIN) {
        ngx_add_timer(c->write, 5 * 1000);
    }
        
    return NGX_OK;
}

static void 
ngx_http_php_socket_finalize(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u)
{
    ngx_connection_t        *c;

    ngx_php_debug("request: %p, u: %p, u->cleanup: %p", r, u, u->cleanup);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "php finalize socket");

    if (u->cleanup) {
        *u->cleanup = NULL;
        u->cleanup = NULL;
    }

    if (u->resolved && u->resolved->ctx) {
        ngx_resolve_name_done(u->resolved->ctx);
        u->resolved->ctx = NULL;
    }

    if (u->peer.free && u->peer.sockaddr) {
        u->peer.free(&u->peer, u->peer.data, 0);
        u->peer.sockaddr = NULL;
    }

    c = u->peer.connection;
    if (c) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "close http upstream connection: %d",
                       u->peer.connection->fd);

        if (u->peer.connection->pool) {
            ngx_destroy_pool(u->peer.connection->pool);
        }

        ngx_close_connection(u->peer.connection);
    }
}

static void 
ngx_http_php_socket_connected_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u)
{
    ngx_php_debug("php socket connected handler.");
}

static ngx_int_t 
ngx_http_php_socket_upstream_send(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u)
{
    ngx_int_t           n;
    ngx_connection_t    *c;
    ngx_http_php_ctx_t  *ctx;
    ngx_buf_t           *b;

    c = u->peer.connection;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, 
                   "php socket send data");
    ngx_php_debug("php socket send data");

    ctx = ngx_http_get_module_ctx(r, ngx_http_php_module);
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    b = u->request_bufs->buf;

    for (;;) {
        ngx_php_debug("%s, %d", b->pos, (int)(b->last - b->pos));
        n = c->send(c, b->pos, b->last - b->pos);
    
        if (n >= 0) {
            b->pos += n;

            if (b->pos == b->last) {
                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, 
                               "php socket send all the data");
                ngx_php_debug("php socket send all the data");

                if (c->write->timer_set) {
                    ngx_del_timer(c->write);
                }

                ngx_chain_update_chains(r->pool, &u->free_bufs, &u->busy_bufs, &u->request_bufs,
                    (ngx_buf_tag_t) &ngx_http_php_module);

                u->write_event_handler = (ngx_http_php_socket_upstream_handler_pt) ngx_http_php_socket_dummy_handler;

                if (ngx_handle_write_event(c->write, 0) != NGX_OK) {

                    return NGX_ERROR;
                }

                //ngx_http_php_socket_handler(c->write);
                return NGX_OK;
            }

            /* keep sending more data */
            continue;
        }

        /* NGX_ERROR || NGX_AGAIN */
        break;
    }

    if (n == NGX_ERROR) {

        return NGX_ERROR;
    }

    ctx->phase_status = NGX_AGAIN;

    u->write_event_handler = (ngx_http_php_socket_upstream_handler_pt) ngx_http_php_socket_send_handler;

    ngx_add_timer(c->write, 5 * 1000);

    if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_AGAIN;


}

static void 
ngx_http_php_socket_send_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u)
{
    ngx_connection_t                    *c;
    ngx_http_php_loc_conf_t             *plcf;

    c = u->peer.connection;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, 
                   "php socket send handler.");
    ngx_php_debug("php socket send handler.");

    if (c->write->timedout) {
        plcf = ngx_http_get_module_loc_conf(r, ngx_http_php_module);

        if (plcf->log_socket_errors) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                          "php socket write timed out.");
        }

        return ;
    }

    if (u->request_bufs) {
        (void) ngx_http_php_socket_upstream_send(r, u);
    }

}

static ngx_int_t 
ngx_http_php_socket_upstream_recv(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u)
{
    //ngx_int_t           rc;
    ngx_connection_t    *c;
    ngx_event_t         *rev;
    ngx_buf_t           *b;
    size_t              size;
    ssize_t             n;


    c = u->peer.connection;
    rev = c->read;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, 
                   "php socket receive data");
    ngx_php_debug("php socket receive data");

    b = &u->buffer;

    if (b->start == NULL) {
        b->start = ngx_palloc(r->pool, 32*1024*1024);

        b->pos = b->start;
        b->last = b->start;
        b->end = b->start + 32*1024*1024;
        b->temporary = 1;
    }

    for (;;) {
        size = b->end - b->last;

        if (!rev->ready) {
            ngx_php_debug("recv ready: %d", rev->ready);
            if (ngx_handle_read_event(rev, 0) != NGX_OK) {
                n = -1;
                break;
            }

            //ngx_add_timer(rev, 1000);
        }

        n = c->recv(c, b->last, size);

        //ngx_php_debug("recv: %s, %d, %d", b->pos, (int)n, (int) size);
        
        if (n == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                          "php socket recv error");
            n = -1;
            return NGX_ERROR;
        }

        if (n == NGX_OK) {

            break;
        }

        if (n > 0) {
            b->last += n;
        }

        //b->last += n;
    }

    //ngx_php_debug("recv: %*s, %p, %p, %p, %p",(int)(b->last - b->pos),b->pos, b->pos, b->end, b->start, b->last);

    return NGX_OK;

}

static void 
ngx_http_php_socket_upstream_recv_handler(ngx_http_request_t *r, 
    ngx_http_php_socket_upstream_t *u)
{

}

void
ngx_http_php_socket_connect(ngx_http_request_t *r)
{
    ngx_http_php_ctx_t                  *ctx;
    //ngx_http_php_loc_conf_t             *plcf;
    //ngx_str_t                           host;
    //int                                 port;
    ngx_resolver_ctx_t                  *rctx, temp;
    ngx_http_core_loc_conf_t            *clcf;

    ngx_url_t                           url;

    ngx_int_t                           rc;
    ngx_peer_connection_t               *peer;

    ngx_http_php_socket_upstream_t      *u;

    ctx = ngx_http_get_module_ctx(r, ngx_http_php_module);

    if (ctx->upstream == NULL){
        ctx->upstream = ngx_pcalloc(r->pool, sizeof(ngx_http_php_socket_upstream_t));
    }

    u = ctx->upstream;

    u->request = r;

    peer = &u->peer;

    peer->log = r->connection->log;
    peer->log_error = NGX_ERROR_ERR;

    ngx_php_debug("php peer connection log: %p %p", peer->log, peer);

    ngx_memzero(&url, sizeof(ngx_url_t));

    url.url.len = ctx->host.len;
    url.url.data = ctx->host.data;
    url.default_port = (in_port_t) ctx->port;
    url.no_resolve = 1;

    if (ngx_parse_url(r->pool, &url) != NGX_OK) {
        if (url.err) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "%s in upstream \"%V\"", url.err, &url.url);
        }else {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                          "failed to parse host name \"%s\"", ctx->host.data);
        }
        //return ;
    }

    u->resolved = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_resolved_t));
    if (u->resolved == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_pcalloc resolved error. %s.", strerror(errno));
        //return ;
    }

    if (url.addrs && url.addrs[0].sockaddr) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, 
                       "php socket network address given directly");

        u->resolved->sockaddr = url.addrs[0].sockaddr;
        u->resolved->socklen = url.addrs[0].socklen;
        u->resolved->naddrs = 1;
        u->resolved->host = url.addrs[0].name;
    } else {
        u->resolved->host = ctx->host;
        u->resolved->port = (in_port_t) ctx->port;
    }

    if (u->resolved->sockaddr) {
        rc = ngx_http_php_socket_resolve_retval_handler(r, u);
        if (rc == NGX_AGAIN) {
            return ;
        }

        return ;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    temp.name = ctx->host;
    rctx = ngx_resolve_start(clcf->resolver, &temp);
    if (rctx == NULL) {
        ngx_php_debug("failed to start the resolver.");
        //return ;
    }

    if (rctx == NGX_NO_RESOLVER) {
        ngx_php_debug("no resolver defined to resolve \"%s\"", ctx->host.data);
        //return ;
    }

    rctx->name = ctx->host;
    rctx->handler = ngx_http_php_socket_resolve_handler;
    rctx->data = u;
    rctx->timeout = clcf->resolver_timeout;

    u->resolved->ctx = rctx;

    if (ngx_resolve_name(rctx) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "php tcp socket fail to run resolver immediately");
        //return ;
    }

    
}

void 
ngx_http_php_socket_close(ngx_http_request_t *r)
{
    ngx_http_php_socket_upstream_t      *u;
    ngx_http_php_ctx_t                  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_php_module);

    u = ctx->upstream;

    if (u == NULL || 
        u->peer.connection == NULL )
    {
        return ;
    }

    if (u->request != r) {

    }

    ngx_http_php_socket_finalize(r, u);

    return ;
}

void
ngx_http_php_socket_send(ngx_http_request_t *r)
{
    ngx_int_t                           rc;
    ngx_connection_t                    *c;
    ngx_http_php_ctx_t                  *ctx;
    ngx_http_php_socket_upstream_t      *u;

    ctx = ngx_http_get_module_ctx(r, ngx_http_php_module);

    u = ctx->upstream;

    if (u == NULL || u->peer.connection == NULL) {

    }

    c = u->peer.connection;

    if (c->tcp_nodelay) {

    }

    if (u->request != r) {

    }

    rc = ngx_http_php_socket_upstream_send(r, u);

    ngx_php_debug("socket send returned %d", (int)rc);

    if (rc == NGX_ERROR) {

    }

    if (rc == NGX_OK) {

    }

    /* rc == NGX_AGAIN */




}

void 
ngx_http_php_socket_recv(ngx_http_request_t *r)
{
    ngx_int_t                           rc;
    ngx_http_php_ctx_t                  *ctx;
    ngx_http_php_socket_upstream_t      *u;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, 
                   "php tcp receive");
    ngx_php_debug("php socket receive");

    ctx = ngx_http_get_module_ctx(r, ngx_http_php_module);

    u = ctx->upstream;

    if (u == NULL || u->peer.connection == NULL) {

    }

    if (u->request != r) {

    }

    rc = ngx_http_php_socket_upstream_recv(r, u);

    if (rc == NGX_ERROR) {

    }

    if (rc == NGX_OK) {

    }

    /* rc == NGX_AGAIN */

    u->read_event_handler = (ngx_http_php_socket_upstream_handler_pt) ngx_http_php_socket_upstream_recv_handler;
}




