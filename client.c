#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 2048

void read_config(const char* filename, char* ip, int* port) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "docs_server", 11) == 0) {
            char* p = strchr(line, '=');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                sscanf(p, "%s %d", ip, port);
                break;
            }
        }
    }
    fclose(fp);
}

int main(int argc, char* argv[]) {
    char server_ip[64];
    int server_port;

    read_config("config.txt", server_ip, &server_port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    printf("[Client] Connected to server.\n");

    char input[BUF_SIZE], buf[BUF_SIZE];
    while (1) {
        printf("> ");
        if (!fgets(input, sizeof(input), stdin)) break;

        send(sock, input, strlen(input), 0);
        if (strncmp(input, "bye", 3) == 0) {
            int len = recv(sock, buf, sizeof(buf) - 1, 0);
            if (len > 0) {
                buf[len] = '\0';
                printf("%s", buf);
            }
            break;
        }

        // write 명령 처리
        if (strncmp(input, "write", 5) == 0) {
            while (1) {
                memset(buf, 0, sizeof(buf));
                int len = recv(sock, buf, sizeof(buf) - 1, 0);
                if (len <= 0) break;
                buf[len] = '\0';
                printf("%s", buf);

                if (strstr(buf, "[Error]")) {
                    goto next_prompt;
                }
                if (strstr(buf, ">> ")) break;
            }

            while (1) {
                if (!fgets(input, sizeof(input), stdin)) break;
                send(sock, input, strlen(input), 0);
                if (strncmp(input, "<END>", 5) == 0) break;

                memset(buf, 0, sizeof(buf));
                int len = recv(sock, buf, sizeof(buf) - 1, 0);
                if (len <= 0) break;
                buf[len] = '\0';
                printf("%s", buf);
            }

            while (1) {
                memset(buf, 0, sizeof(buf));
                int len = recv(sock, buf, sizeof(buf) - 1, 0);
                if (len <= 0) break;
                buf[len] = '\0';
                printf("%s", buf);
                if (strstr(buf, "[Write_Completed]")) break;
            }
            continue;
        }

        // read 명령 처리
        if (strncmp(input, "read", 4) == 0) {
            while (1) {
                int len = recv(sock, buf, sizeof(buf), 0);
                if (len <= 0) break;
                fwrite(buf, 1, len, stdout);
                if (memmem(buf, len, "__END__", 7)) break;
            }
            continue;
        }

        // 일반 응답 처리
        memset(buf, 0, sizeof(buf));
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) break;
        buf[len] = '\0';
        printf("%s", buf);

    next_prompt:
        continue;
    }

    close(sock);
    return 0;
}
