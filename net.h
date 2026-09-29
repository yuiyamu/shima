#ifndef NET_H
#define NET_H

#include <stdio.h>
#include <stdint.h>

struct ServerResp {
    uint16_t status;
    long content_len;
    unsigned char *data;
};

int download_file(char *fetch_url, FILE *output_file);

#endif