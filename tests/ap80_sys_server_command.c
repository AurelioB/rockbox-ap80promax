/*
 * Minimal diagnostic client for the AP80 Pro Max vendor sys_server.
 *
 * Build for the player and pass one command such as BT:LIST or BT:ON.
 * This is intentionally kept outside the Rockbox application so recovery
 * and protocol validation do not depend on the UI being operational.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define SYS_SERVER_SOCKET "/var/run/sys_server"

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s COMMAND\n", argv[0]);
        return 2;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return 1;
    }

    struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
             SYS_SERVER_SOCKET);

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("connect");
        close(fd);
        return 1;
    }

    size_t command_size = strlen(argv[1]);
    if (send(fd, argv[1], command_size, 0) != (ssize_t)command_size)
    {
        perror("send");
        close(fd);
        return 1;
    }

    char reply[256];
    ssize_t received = recv(fd, reply, sizeof(reply) - 1, 0);
    if (received < 0)
    {
        fprintf(stderr, "recv: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    reply[received] = '\0';
    puts(reply);
    close(fd);
    return 0;
}
