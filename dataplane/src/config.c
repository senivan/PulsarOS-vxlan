#include "config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jsmn.h"

#define CONFIG_MAX_BYTES (1024 * 1024)
#define CONFIG_MAX_TOKENS 2048

static void set_err(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;

    if (!err || err_len == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

void app_config_defaults(struct app_config *conf)
{
    memset(conf, 0, sizeof(*conf));
    conf->pmd = PMD_AFPKT;
    conf->lcore_mask = 0x3;
    snprintf(conf->lcores, sizeof(conf->lcores), "0-1");
    conf->socket_id = -1;
    conf->mbufs = DPDK_MBUF_COUNT;
    conf->mbuf_cache = DPDK_MBUF_CACHE;
    conf->rx_desc = DPDK_RX_DESC;
    conf->tx_desc = DPDK_TX_DESC;
    conf->vxlan_udp_port = VXLAN_UDP_PORT;
    conf->controlplane = CONTROLPLANE_STATIC;
}

static int token_streq(const char *json, const jsmntok_t *tok, const char *s)
{
    size_t len;

    if (tok->type != JSMN_STRING) return 0;
    len = strlen(s);
    return (size_t)(tok->end - tok->start) == len &&
           strncmp(json + tok->start, s, len) == 0;
}

static int token_copy(const char *json,
                      const jsmntok_t *tok,
                      char *dst,
                      size_t dst_len)
{
    size_t len;

    if (tok->type != JSMN_STRING) return -1;
    len = (size_t)(tok->end - tok->start);
    if (len >= dst_len) return -1;
    memcpy(dst, json + tok->start, len);
    dst[len] = '\0';
    return 0;
}

static int token_u32(const char *json, const jsmntok_t *tok, uint32_t *out)
{
    char buf[32];
    char *end = NULL;
    unsigned long value;
    size_t len;

    if (tok->type != JSMN_PRIMITIVE) return -1;
    len = (size_t)(tok->end - tok->start);
    if (len == 0 || len >= sizeof(buf)) return -1;
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    errno = 0;
    value = strtoul(buf, &end, 10);
    if (errno || *end != '\0' || value > UINT32_MAX) return -1;
    *out = (uint32_t)value;
    return 0;
}

static int token_i32(const char *json, const jsmntok_t *tok, int *out)
{
    char buf[32];
    char *end = NULL;
    long value;
    size_t len;

    if (tok->type != JSMN_PRIMITIVE) return -1;
    len = (size_t)(tok->end - tok->start);
    if (len == 0 || len >= sizeof(buf)) return -1;
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    errno = 0;
    value = strtol(buf, &end, 10);
    if (errno || *end != '\0' || value < INT32_MIN || value > INT32_MAX) return -1;
    *out = (int)value;
    return 0;
}

static int parse_ipv4(const char *s, uint32_t *ip_be)
{
    struct in_addr addr;

    if (inet_pton(AF_INET, s, &addr) != 1) return -1;
    *ip_be = addr.s_addr;
    return 0;
}

static int parse_ipv4_cidr(const char *s, uint32_t *ip_be, uint8_t *prefix_len)
{
    char buf[64];
    char *slash;
    char *end = NULL;
    unsigned long prefix;
    size_t len = strlen(s);

    if (len == 0 || len >= sizeof(buf)) return -1;
    memcpy(buf, s, len + 1);
    slash = strchr(buf, '/');
    if (!slash) return -1;
    *slash = '\0';
    slash++;
    errno = 0;
    prefix = strtoul(slash, &end, 10);
    if (errno || *end != '\0' || prefix > 32) return -1;
    if (parse_ipv4(buf, ip_be) < 0) return -1;
    *prefix_len = (uint8_t)prefix;
    return 0;
}

static int parse_lcores_mask(const char *s, uint64_t *mask)
{
    char buf[APP_LCORES_MAX_LEN];
    char *part;
    uint64_t out = 0;
    size_t len = strlen(s);

    if (len == 0 || len >= sizeof(buf)) return -1;
    memcpy(buf, s, len + 1);

    part = buf;
    while (part && *part) {
        char *next = strchr(part, ',');
        char *dash = strchr(part, '-');
        unsigned long first;
        unsigned long last;
        char *end = NULL;

        if (next) {
            *next = '\0';
            next++;
        }
        errno = 0;
        first = strtoul(part, &end, 10);
        if (errno || first >= 64) return -1;
        if (dash) {
            *dash = '\0';
            errno = 0;
            first = strtoul(part, &end, 10);
            if (errno || *end != '\0' || first >= 64) return -1;
            errno = 0;
            last = strtoul(dash + 1, &end, 10);
            if (errno || *end != '\0' || last >= 64 || last < first) return -1;
        } else {
            if (*end != '\0') return -1;
            last = first;
        }
        for (; first <= last; first++) out |= UINT64_C(1) << first;
        part = next;
    }

    if (out == 0) return -1;
    *mask = out;
    return 0;
}

static int skip_token(const jsmntok_t *tokens, int index)
{
    int i;
    int next = index + 1;

    if (tokens[index].type == JSMN_OBJECT) {
        for (i = 0; i < tokens[index].size; i++) {
            next++;
            next = skip_token(tokens, next);
        }
        return next;
    }
    if (tokens[index].type == JSMN_ARRAY) {
        for (i = 0; i < tokens[index].size; i++) {
            next = skip_token(tokens, next);
        }
        return next;
    }
    return next;
}

static int object_get(const char *json,
                      const jsmntok_t *tokens,
                      int object_index,
                      const char *key)
{
    int i;
    int cursor = object_index + 1;

    if (tokens[object_index].type != JSMN_OBJECT) return -1;
    for (i = 0; i < tokens[object_index].size; i++) {
        int value = cursor + 1;
        if (token_streq(json, &tokens[cursor], key)) return value;
        cursor = skip_token(tokens, value);
    }
    return -1;
}

static int parse_pmd(const char *s, enum app_pmd *out)
{
    if (strcmp(s, "tap") == 0) {
        *out = PMD_TAP;
        return 0;
    }
    if (strcmp(s, "af_packet") == 0) {
        *out = PMD_AFPKT;
        return 0;
    }
    if (strcmp(s, "phys") == 0) {
        *out = PMD_PHYS;
        return 0;
    }
    return -1;
}

static int parse_role(const char *s, enum app_port_role *out)
{
    if (strcmp(s, "underlay") == 0) {
        *out = PORT_UNDERLAY;
        return 0;
    }
    if (strcmp(s, "access") == 0) {
        *out = PORT_ACCESS;
        return 0;
    }
    return -1;
}

static int parse_controlplane(const char *s, enum app_controlplane_mode *out)
{
    if (strcmp(s, "static") == 0) {
        *out = CONTROLPLANE_STATIC;
        return 0;
    }
    if (strcmp(s, "runtime") == 0) {
        *out = CONTROLPLANE_RUNTIME;
        return 0;
    }
    if (strcmp(s, "static-and-runtime") == 0) {
        *out = CONTROLPLANE_STATIC_AND_RUNTIME;
        return 0;
    }
    return -1;
}

static int find_port(const struct app_config *conf, const char *name)
{
    uint16_t i;

    for (i = 0; i < conf->port_count; i++) {
        if (strcmp(conf->ports[i].name, name) == 0) return i;
    }
    return -1;
}

static int parse_dataplane(const char *json,
                           const jsmntok_t *tokens,
                           int object_index,
                           struct app_config *conf,
                           char *err,
                           size_t err_len)
{
    int value;
    uint32_t tmp;

    value = object_get(json, tokens, object_index, "lcores");
    if (value >= 0) {
        if (token_copy(json, &tokens[value], conf->lcores, sizeof(conf->lcores)) < 0 ||
            parse_lcores_mask(conf->lcores, &conf->lcore_mask) < 0) {
            set_err(err, err_len, "dataplane.lcores is invalid");
            return -1;
        }
    }

    value = object_get(json, tokens, object_index, "socket_id");
    if (value >= 0 && token_i32(json, &tokens[value], &conf->socket_id) < 0) {
        set_err(err, err_len, "dataplane.socket_id is invalid");
        return -1;
    }

    value = object_get(json, tokens, object_index, "mbufs");
    if (value >= 0) {
        if (token_u32(json, &tokens[value], &tmp) < 0 || tmp == 0) {
            set_err(err, err_len, "dataplane.mbufs is invalid");
            return -1;
        }
        conf->mbufs = tmp;
    }

    value = object_get(json, tokens, object_index, "mbuf_cache");
    if (value >= 0) {
        if (token_u32(json, &tokens[value], &tmp) < 0) {
            set_err(err, err_len, "dataplane.mbuf_cache is invalid");
            return -1;
        }
        conf->mbuf_cache = tmp;
    }

    value = object_get(json, tokens, object_index, "rx_desc");
    if (value >= 0) {
        if (token_u32(json, &tokens[value], &tmp) < 0 || tmp > UINT16_MAX) {
            set_err(err, err_len, "dataplane.rx_desc is invalid");
            return -1;
        }
        conf->rx_desc = (uint16_t)tmp;
    }

    value = object_get(json, tokens, object_index, "tx_desc");
    if (value >= 0) {
        if (token_u32(json, &tokens[value], &tmp) < 0 || tmp > UINT16_MAX) {
            set_err(err, err_len, "dataplane.tx_desc is invalid");
            return -1;
        }
        conf->tx_desc = (uint16_t)tmp;
    }

    return 0;
}

static int parse_port(const char *json,
                      const jsmntok_t *tokens,
                      int object_index,
                      struct app_config *conf,
                      char *err,
                      size_t err_len)
{
    struct app_port_config *port;
    char value_buf[APP_NAME_MAX_LEN];
    int value;

    if (conf->port_count >= APP_MAX_PORTS) {
        set_err(err, err_len, "too many ports");
        return -1;
    }
    if (tokens[object_index].type != JSMN_OBJECT) {
        set_err(err, err_len, "port entry must be an object");
        return -1;
    }

    port = &conf->ports[conf->port_count];
    memset(port, 0, sizeof(*port));

    value = object_get(json, tokens, object_index, "name");
    if (value < 0 || token_copy(json, &tokens[value], port->name, sizeof(port->name)) < 0) {
        set_err(err, err_len, "port.name is required or too long");
        return -1;
    }
    if (find_port(conf, port->name) >= 0) {
        set_err(err, err_len, "duplicate port name: %s", port->name);
        return -1;
    }

    value = object_get(json, tokens, object_index, "role");
    if (value < 0 || token_copy(json, &tokens[value], value_buf, sizeof(value_buf)) < 0 ||
        parse_role(value_buf, &port->role) < 0) {
        set_err(err, err_len, "port %s has invalid role", port->name);
        return -1;
    }

    value = object_get(json, tokens, object_index, "pmd");
    if (value < 0 || token_copy(json, &tokens[value], value_buf, sizeof(value_buf)) < 0 ||
        parse_pmd(value_buf, &port->pmd) < 0) {
        set_err(err, err_len, "port %s has invalid pmd", port->name);
        return -1;
    }

    value = object_get(json, tokens, object_index, "iface");
    if (value >= 0 && token_copy(json, &tokens[value], port->iface, sizeof(port->iface)) < 0) {
        set_err(err, err_len, "port %s iface is too long", port->name);
        return -1;
    }

    value = object_get(json, tokens, object_index, "pci");
    if (value >= 0 && token_copy(json, &tokens[value], port->pci_addr, sizeof(port->pci_addr)) < 0) {
        set_err(err, err_len, "port %s pci is too long", port->name);
        return -1;
    }

    if ((port->pmd == PMD_TAP || port->pmd == PMD_AFPKT) && port->iface[0] == '\0') {
        set_err(err, err_len, "port %s requires iface", port->name);
        return -1;
    }
    if (port->pmd == PMD_PHYS && port->pci_addr[0] == '\0') {
        set_err(err, err_len, "port %s requires pci", port->name);
        return -1;
    }

    value = object_get(json, tokens, object_index, "ip");
    if (value >= 0) {
        char cidr[64];
        if (token_copy(json, &tokens[value], cidr, sizeof(cidr)) < 0 ||
            parse_ipv4_cidr(cidr, &port->ip_be, &port->prefix_len) < 0) {
            set_err(err, err_len, "port %s has invalid ip CIDR", port->name);
            return -1;
        }
        port->has_ip = 1;
    }

    if (port->role == PORT_UNDERLAY && !port->has_ip) {
        set_err(err, err_len, "underlay port %s requires ip", port->name);
        return -1;
    }

    conf->port_count++;
    return 0;
}

static int parse_ports(const char *json,
                       const jsmntok_t *tokens,
                       int array_index,
                       struct app_config *conf,
                       char *err,
                       size_t err_len)
{
    int i;
    int cursor = array_index + 1;

    if (tokens[array_index].type != JSMN_ARRAY) {
        set_err(err, err_len, "ports must be an array");
        return -1;
    }

    for (i = 0; i < tokens[array_index].size; i++) {
        if (parse_port(json, tokens, cursor, conf, err, err_len) < 0) return -1;
        cursor = skip_token(tokens, cursor);
    }

    if (conf->port_count == 0) {
        set_err(err, err_len, "at least one port is required");
        return -1;
    }
    return 0;
}

static int find_segment(const struct app_config *conf, const char *name)
{
    uint16_t i;

    for (i = 0; i < conf->segment_count; i++) {
        if (strcmp(conf->segments[i].name, name) == 0) return i;
    }
    return -1;
}

static int parse_peer(const char *json,
                      const jsmntok_t *tokens,
                      int object_index,
                      struct app_vxlan_segment *segment,
                      char *err,
                      size_t err_len)
{
    int value;
    char ip[64];

    if (segment->peer_count >= APP_MAX_PEERS) {
        set_err(err, err_len, "too many peers in segment %s", segment->name);
        return -1;
    }
    value = object_get(json, tokens, object_index, "ip");
    if (value < 0 || token_copy(json, &tokens[value], ip, sizeof(ip)) < 0 ||
        parse_ipv4(ip, &segment->peers[segment->peer_count].ip_be) < 0) {
        set_err(err, err_len, "segment %s has invalid peer ip", segment->name);
        return -1;
    }
    segment->peer_count++;
    return 0;
}

static int parse_segment(const char *json,
                         const jsmntok_t *tokens,
                         int object_index,
                         struct app_config *conf,
                         char *err,
                         size_t err_len)
{
    struct app_vxlan_segment *segment;
    char name[APP_NAME_MAX_LEN];
    int value;
    int i;
    int cursor;
    uint32_t u32;

    if (conf->segment_count >= APP_MAX_SEGMENTS) {
        set_err(err, err_len, "too many VXLAN segments");
        return -1;
    }
    if (tokens[object_index].type != JSMN_OBJECT) {
        set_err(err, err_len, "VXLAN segment must be an object");
        return -1;
    }

    segment = &conf->segments[conf->segment_count];
    memset(segment, 0, sizeof(*segment));
    segment->underlay_port = UINT16_MAX;

    value = object_get(json, tokens, object_index, "name");
    if (value < 0 || token_copy(json, &tokens[value], segment->name, sizeof(segment->name)) < 0) {
        set_err(err, err_len, "segment.name is required or too long");
        return -1;
    }
    if (find_segment(conf, segment->name) >= 0) {
        set_err(err, err_len, "duplicate segment name: %s", segment->name);
        return -1;
    }

    value = object_get(json, tokens, object_index, "vni");
    if (value < 0 || token_u32(json, &tokens[value], &u32) < 0 || u32 == 0 || u32 > 16777215U) {
        set_err(err, err_len, "segment %s has invalid vni", segment->name);
        return -1;
    }
    segment->vni = u32;

    value = object_get(json, tokens, object_index, "underlay_port");
    if (value < 0 || token_copy(json, &tokens[value], name, sizeof(name)) < 0) {
        set_err(err, err_len, "segment %s requires underlay_port", segment->name);
        return -1;
    }
    i = find_port(conf, name);
    if (i < 0 || conf->ports[i].role != PORT_UNDERLAY) {
        set_err(err, err_len, "segment %s references invalid underlay_port %s", segment->name, name);
        return -1;
    }
    segment->underlay_port = (uint16_t)i;

    value = object_get(json, tokens, object_index, "access_ports");
    if (value < 0 || tokens[value].type != JSMN_ARRAY || tokens[value].size == 0) {
        set_err(err, err_len, "segment %s requires access_ports", segment->name);
        return -1;
    }
    cursor = value + 1;
    for (i = 0; i < tokens[value].size; i++) {
        int port_index;
        if (segment->access_port_count >= APP_MAX_ACCESS_PORTS) {
            set_err(err, err_len, "segment %s has too many access_ports", segment->name);
            return -1;
        }
        if (token_copy(json, &tokens[cursor], name, sizeof(name)) < 0) {
            set_err(err, err_len, "segment %s has invalid access_port", segment->name);
            return -1;
        }
        port_index = find_port(conf, name);
        if (port_index < 0 || conf->ports[port_index].role != PORT_ACCESS) {
            set_err(err, err_len, "segment %s references invalid access_port %s", segment->name, name);
            return -1;
        }
        segment->access_ports[segment->access_port_count++] = (uint16_t)port_index;
        cursor = skip_token(tokens, cursor);
    }

    value = object_get(json, tokens, object_index, "peers");
    if (value >= 0) {
        if (tokens[value].type != JSMN_ARRAY) {
            set_err(err, err_len, "segment %s peers must be an array", segment->name);
            return -1;
        }
        cursor = value + 1;
        for (i = 0; i < tokens[value].size; i++) {
            if (parse_peer(json, tokens, cursor, segment, err, err_len) < 0) return -1;
            cursor = skip_token(tokens, cursor);
        }
    }

    conf->segment_count++;
    return 0;
}

static int parse_vxlan(const char *json,
                       const jsmntok_t *tokens,
                       int object_index,
                       struct app_config *conf,
                       char *err,
                       size_t err_len)
{
    int value;
    int i;
    int cursor;
    uint32_t u32;
    char mode[APP_NAME_MAX_LEN];

    if (tokens[object_index].type != JSMN_OBJECT) {
        set_err(err, err_len, "vxlan must be an object");
        return -1;
    }

    value = object_get(json, tokens, object_index, "udp_port");
    if (value >= 0) {
        if (token_u32(json, &tokens[value], &u32) < 0 || u32 == 0 || u32 > UINT16_MAX) {
            set_err(err, err_len, "vxlan.udp_port is invalid");
            return -1;
        }
        conf->vxlan_udp_port = (uint16_t)u32;
    }

    value = object_get(json, tokens, object_index, "controlplane");
    if (value >= 0) {
        if (token_copy(json, &tokens[value], mode, sizeof(mode)) < 0 ||
            parse_controlplane(mode, &conf->controlplane) < 0) {
            set_err(err, err_len, "vxlan.controlplane is invalid");
            return -1;
        }
    }

    value = object_get(json, tokens, object_index, "segments");
    if (value < 0 || tokens[value].type != JSMN_ARRAY) {
        set_err(err, err_len, "vxlan.segments must be an array");
        return -1;
    }

    cursor = value + 1;
    for (i = 0; i < tokens[value].size; i++) {
        if (parse_segment(json, tokens, cursor, conf, err, err_len) < 0) return -1;
        cursor = skip_token(tokens, cursor);
    }

    return 0;
}

static int apply_legacy_underlay(struct app_config *conf, char *err, size_t err_len)
{
    uint16_t i;

    for (i = 0; i < conf->port_count; i++) {
        struct app_port_config *port = &conf->ports[i];
        if (port->role != PORT_UNDERLAY) continue;
        conf->pmd = port->pmd;
        snprintf(conf->underlay.name, sizeof(conf->underlay.name), "%s", port->iface);
        snprintf(conf->underlay.pcie_addr, sizeof(conf->underlay.pcie_addr), "%s", port->pci_addr);
        conf->underlay.ip_addr = port->ip_be;
        return 0;
    }

    set_err(err, err_len, "at least one underlay port is required");
    return -1;
}

static char *read_file(const char *path, size_t *len_out, char *err, size_t err_len)
{
    FILE *f;
    long len;
    char *buf;

    f = fopen(path, "rb");
    if (!f) {
        set_err(err, err_len, "failed to open %s: %s", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        set_err(err, err_len, "failed to seek %s", path);
        fclose(f);
        return NULL;
    }
    len = ftell(f);
    if (len < 0 || len > CONFIG_MAX_BYTES) {
        set_err(err, err_len, "config file %s is too large", path);
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = calloc(1, (size_t)len + 1);
    if (!buf) {
        set_err(err, err_len, "out of memory reading %s", path);
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        set_err(err, err_len, "failed to read %s", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len_out = (size_t)len;
    return buf;
}

int app_config_load_json(const char *path,
                         struct app_config *conf,
                         char *err,
                         size_t err_len)
{
    char *json;
    size_t json_len = 0;
    jsmn_parser parser;
    jsmntok_t *tokens;
    int token_count;
    int value;
    uint32_t schema_version;
    int rc = -1;

    if (!path || !conf) {
        set_err(err, err_len, "path and conf are required");
        return -1;
    }

    app_config_defaults(conf);
    json = read_file(path, &json_len, err, err_len);
    if (!json) return -1;

    tokens = calloc(CONFIG_MAX_TOKENS, sizeof(*tokens));
    if (!tokens) {
        set_err(err, err_len, "out of memory allocating JSON tokens");
        free(json);
        return -1;
    }

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, json_len, tokens, CONFIG_MAX_TOKENS);
    if (token_count < 0 || token_count == 0 || tokens[0].type != JSMN_OBJECT) {
        set_err(err, err_len, "invalid JSON config");
        goto out;
    }

    value = object_get(json, tokens, 0, "schema_version");
    if (value < 0 || token_u32(json, &tokens[value], &schema_version) < 0 || schema_version != 1) {
        set_err(err, err_len, "schema_version must be 1");
        goto out;
    }

    value = object_get(json, tokens, 0, "dataplane");
    if (value >= 0 && parse_dataplane(json, tokens, value, conf, err, err_len) < 0) goto out;

    value = object_get(json, tokens, 0, "ports");
    if (value < 0) {
        set_err(err, err_len, "ports is required");
        goto out;
    }
    if (parse_ports(json, tokens, value, conf, err, err_len) < 0) goto out;

    value = object_get(json, tokens, 0, "vxlan");
    if (value < 0) {
        set_err(err, err_len, "vxlan is required");
        goto out;
    }
    if (parse_vxlan(json, tokens, value, conf, err, err_len) < 0) goto out;

    if (apply_legacy_underlay(conf, err, err_len) < 0) goto out;

    rc = 0;

out:
    free(tokens);
    free(json);
    return rc;
}
