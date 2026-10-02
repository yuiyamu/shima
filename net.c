#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "heliotrope.h"
#include "helpers.h"
#include "build.h"

#ifndef MAX_CHUNK
#define MAX_CHUNK 131072
#endif

static struct ServerResp *parse_html_response(FILE *socket_file) {
    struct ServerResp *response = safe_calloc(1, sizeof(struct ServerResp));
    char line[1024] = {0};

    //top should always be "HTTP/1.1 [stat number] [stat response]\r\n"
    fgets(line, sizeof(line), socket_file);
    char header[] = "HTTP/1.1 ";
    if (memcmp(header, line, sizeof(header) - 1) != 0) {
        free(response);
        return NULL;
    }

    char *code = strdup(line);
    code += strlen(header);
    char *past_code = strchr(code, ' ');
    if (past_code == NULL) {
        free(response);
        return NULL;
    }
    *past_code = '\0'; //null term da code~
    response->status = atoi(code);

    while (fgets(line, sizeof(line), socket_file) != NULL) {
        if (strncmp(line, "Content-Length", strlen("Content-Length")) == 0) {
            char *content_len = NULL;
            get_colon_parsed_string(line, &content_len);
            response->content_len = atoi(content_len);
        } else if (strncmp(line, "Accept-Ranges", strlen("Accept-Ranges")) == 0) { //theoretically should be where our data starts!!!!
            break;
        }
    }

    return response;
}

int download_file(char *fetch_url, FILE *output_file) {    
    printf("* fetch %s ", fetch_url);
    fflush(stdout);

    //ok. we know what to get from the fUcking INTERNET now.
    //since we're not actually using libcurl, we have to open our own socket and talk to the server ourselves and everything
    //not too hard on unix~!!
    struct addrinfo hints = {0};
    struct addrinfo *res;
    hints.ai_family = AF_INET; //only want to fetch ipv4, older systems may not even know wtf an ipv6 is
    hints.ai_socktype = SOCK_STREAM; //tcp

    //for getting the actual domain name, remove "://" and just get whatever is before first slash then :p
    char *fetch_httpless = helio_strdup(fetch_url);
    fetch_httpless = strstr(fetch_httpless, "://");
    if (fetch_httpless == NULL) goto malformed_url;
    fetch_httpless += 3;

    char *domain = helio_strdup(fetch_httpless);
    char *path = strchr(domain, '/');
    if (path == NULL) goto malformed_url;
    *path = '\0'; //cut this, so that our domain is null termed
    path++; //now the actual path is here~

    char ipstr[INET_ADDRSTRLEN];
    int dns_resolution_ret = getaddrinfo(domain, "80", &hints, &res);
    if (dns_resolution_ret != 0) {
        fprintf(stderr, "failed to resolve %s. from getaddrinfo: %s\n", fetch_url, gai_strerror(dns_resolution_ret));
        return 1;
    } else {
        getnameinfo(res->ai_addr, res->ai_addrlen, ipstr, sizeof(ipstr), NULL, 0, NI_NUMERICHOST);
        //^ while res is technically a linked list containing all the valid ips to get to the domain... generally first is fine
        //and we only log one since Yea

        printf("(%s)... ", ipstr);
        fflush(stdout);
    }

    int sock_id = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock_id < 0) { 
        fprintf(stderr, "error opening systen network socket.\n");
        return 1; 
    }

    if (connect(sock_id, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "error intiating tcp handshake with server.\n");
        return 1;
    }
    freeaddrinfo(res);

    //we ask the server pretty please, may i have file? :pleading:
    char req[1024] = {0};
    int req_len = snprintf(req, sizeof(req),
        "GET /%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, domain);
    send(sock_id, req, req_len, 0);

    /* A SERVER IS TALKING. listen and learn */
    char buf[MAX_CHUNK];
    FILE *socket_file = fdopen(sock_id, "r"); //needed for fgetsing (i sound like gollum)
    struct ServerResp *http_info = parse_html_response(socket_file); //this reads the first little bit, and gets us to the file data :3
    if (http_info == NULL) {
        printf("unable to parse server's http response.\n");
        return 1;
    }

    if (http_info->status != 200) {
        fprintf(stderr, "encountered http code [%i].\n", http_info->status);
        free(http_info);
        return 1;
    }

    int amt_printed = strlen("* fetch ") + strlen(fetch_url) + strlen(ipstr) + strlen("()... "); //kinda jank way to do this but whateves

    http_info->data = safe_calloc(1, http_info->content_len);
    ssize_t amount_read = 0;
    size_t total_read = 0;
    while ((amount_read = recv(sock_id, buf, sizeof(buf), 0)) > 0) {
        memcpy(http_info->data + total_read, buf, amount_read);

        //also update to show download progress!!
        printf("\r\033[%iC [%likb/%likb]", amt_printed, total_read / 1000, http_info->content_len / 1000);
        fflush(stdout);

        total_read += amount_read;
    }
    printf("\r\033[%iC [%likb/%likb]", amt_printed, total_read / 1000, http_info->content_len / 1000); //one last update after >.<
    fflush(stdout);

    //should have read now~ save to the file we want!!
    fwrite(http_info->data, http_info->content_len, 1, output_file);
    free(http_info->data);
    free(http_info);
    fclose(output_file);
    fclose(socket_file);

    printf(" ok.\n");
    return 0;

    malformed_url:
        fprintf(stderr, "provided url appears to be malformed.\n");
        return 1;
}