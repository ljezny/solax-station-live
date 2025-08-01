#pragma once

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h> // struct addrinfo
#include <arpa/inet.h>
#include "esp_netif.h"
#include "esp_log.h"
#include <lwip/def.h>

class CustomNetworkClient
{
public:
    CustomNetworkClient() = default;
    virtual ~CustomNetworkClient() = default;

    bool connect(String hostname, uint16_t port)
    {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        int err = getaddrinfo(hostname.c_str(), String(port).c_str(), &hints, &res);
        if (err != 0 || res == nullptr)
        {
            log_e("getaddrinfo failed: %d", err);
            return false;
        }

        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock < 0)
        {
            log_e("Unable to create socket: errno %d", errno);
            freeaddrinfo(res);
            return false;
        }

        struct linger ling;
        ling.l_onoff = 1;
        ling.l_linger = 0;
        setsockopt(sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        err = ::connect(sock, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        if (err != 0)
        {
            log_e("Socket unable to connect: errno %d", errno);
            close(sock); // Fix: Close the socket on failure
            sock = -1;
            return false;
        }
        log_i("Successfully connected");
        return true;
    }

    bool connect(IPAddress ip, uint16_t port)
    {
        struct sockaddr_in dest_addr;
        inet_pton(AF_INET, ip.toString().c_str(), &dest_addr.sin_addr);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
        int addr_family = AF_INET;
        int ip_protocol = IPPROTO_IP;

        sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0)
        {
            log_e("Unable to create socket: errno %d", errno);
            return false;
        }
        log_i("Socket created %d, connecting to %s:%d", sock, ip.toString().c_str(), port);

        struct linger ling;
        ling.l_onoff = 1;
        ling.l_linger = 0;
        setsockopt(sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        int err = ::connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0)
        {
            log_e("Socket unable to connect: errno %d", errno);
            close(sock); // Fix: Close the socket on failure
            sock = -1;
            return false;
        }
        log_i("Successfully connected");
        return true;
    }

    bool connected()
    {
        return sock != -1;
    }

    void stop()
    {
        log_i("Stopping socket %d", sock);
        if (sock != -1)
        {
            shutdown(sock, SHUT_RDWR);
            close(sock);
            sock = -1;
            log_i("Socket closed");
        }
    }

    int write(const uint8_t *buf, size_t size)
    {
        int err = send(sock, buf, size, 0);
        if (err < 0)
        {
            log_e("Error occurred during sending: errno %d", errno);
            return err;
        }
        return err;
    }

    int read(uint8_t *buf, size_t size)
    {
        int bytesRead = recv(sock, buf, size, 0);
        if (bytesRead < 0)
        {
            log_e("Error occurred during receiving: errno %d", errno);
            return -1;
        }
        buf[bytesRead] = '\0'; // Null-terminate the buffer
        return bytesRead;
    }

private:
    int sock = -1;
};
