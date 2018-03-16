#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/*
 nginx upstream 负载均衡 随机负载均衡
*/

typedef struct {

    ngx_http_upstream_rr_peer_data_t  rrp;

    u_char                            tries;
} ngx_htto_upstream_random_peer_data_t;

static ngx_int_t ngx_http_upstream_init_random_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_upstream_get_random_peer(ngx_peer_connection_t *pc, 
    void *data);
static char *ngx_http_upsteram_random(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t ngx_http_upstream_random_commands[] = {

    { ngx_string("random_ups"),
      NGX_HTTP_UPS_CONF|NGX_CONF_NOARGS,
      ngx_http_upsteram_random,
      0,
      0,
      NULL  },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_upstream_random_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};

ngx_module_t  ngx_http_upstream_random_module = {
    NGX_MODULE_V1,
    &ngx_http_upstream_random_module_ctx, /* module context */
    ngx_http_upstream_random_commands,    /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_upstream_init_random(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                   "init random ups");

    if (ngx_http_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }

    us->peer.init = ngx_http_upstream_init_random_peer;

    return NGX_OK;
}

static ngx_int_t
ngx_http_upstream_init_random_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us)
{

    ngx_htto_upstream_random_peer_data_t   *rdp;

    rdp = ngx_palloc(r->pool, sizeof(ngx_htto_upstream_random_peer_data_t));
    if (rdp == NULL) {
        return NGX_ERROR;
    }

    r->upstream->peer.data = &rdp->rrp;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                    "init random peer");

    if (ngx_http_upstream_init_round_robin_peer(r, us) != NGX_OK) {
        return NGX_ERROR;
    }

    rdp->tries = 0;
    r->upstream->peer.get = ngx_http_upstream_get_random_peer;


    return NGX_OK;
}


static ngx_int_t 
ngx_http_upstream_get_random_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_htto_upstream_random_peer_data_t   *rdp = data;

    time_t                         now;
    uintptr_t                      m;
    ngx_uint_t                     n,p;
    ngx_int_t                      w;
    ngx_http_upstream_rr_peer_t   *peer;
    ngx_http_upstream_rr_peers_t  *peers;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "get random peer, try: %ui", pc->tries);

    if (rdp->tries > 20 || rdp->rrp.peers->single) {
        return ngx_http_upstream_get_round_robin_peer(pc, &rdp->rrp);
    }

    pc->cached = 0;
    pc->connection = NULL;

    now = ngx_time();

    peers = rdp->rrp.peers;

    ngx_http_upstream_rr_peers_wlock(peers);


    for ( ;; ) {

        w = ngx_random() % peers->total_weight;
        peer = peers->peer;
        p = 0;

        while (w >= peer->weight) {
            w -= peer->weight;
            peer = peer->next;
            p++;
        }

        n = p / (8 * sizeof(uintptr_t));
        m = (uintptr_t)1 << p % (8 * sizeof(uintptr_t));

        if (rdp->rrp.tried[n] & m) {
            goto next;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                       "get random peer, random: %ui %04XL", p, (uint64_t) m);

        if (peer->down) {
            goto next;
        }

        if (peer->max_fails
            && peer->fails >= peer->max_fails
            && now - peer->checked <= peer->fail_timeout)
        {
            goto next;   
        }

        break;

    next:

        if (++rdp->tries >= 20) {
            ngx_http_upstream_rr_peers_unlock(peers);
            return ngx_http_upstream_get_round_robin_peer(pc, &rdp->rrp);
        }

    }

    rdp->rrp.current = peer;

    pc->sockaddr = peer->sockaddr;
    pc->socklen = peer->socklen;
    pc->name = &peer->name;

    peer->conns++;

    if (now - peer->checked > peer->fail_timeout) {
        peer->checked = now;
    }

    ngx_http_upstream_rr_peers_unlock(rdp->rrp.peers);

    rdp->rrp.tried[n] |= m;

    return NGX_OK;
}


static char *
ngx_http_upsteram_random(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_srv_conf_t  *uscf;

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    if (uscf->peer.init_upstream) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "load balancing method redefined");
    }

    uscf->peer.init_upstream = ngx_http_upstream_init_random;

    uscf->flags = NGX_HTTP_UPSTREAM_CREATE
                  |NGX_HTTP_UPSTREAM_WEIGHT
                  |NGX_HTTP_UPSTREAM_MAX_FAILS
                  |NGX_HTTP_UPSTREAM_FAIL_TIMEOUT
                  |NGX_HTTP_UPSTREAM_DOWN
                  |NGX_HTTP_UPSTREAM_BACKUP;

    return NGX_CONF_OK;
}