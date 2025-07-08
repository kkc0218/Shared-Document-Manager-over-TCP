#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_DOCS 100
#define MAX_SECTIONS 10
#define MAX_TITLE 64
#define MAX_LINE 256
#define MAX_LINES 10
#define BUF_SIZE 1024

typedef struct {
    char title[MAX_TITLE];
    char section_titles[MAX_SECTIONS][MAX_TITLE];
    char section_contents[MAX_SECTIONS][MAX_LINES][MAX_LINE];
    int section_line_count[MAX_SECTIONS];
    int section_count;
} Document;

typedef struct WriteRequest {
    int client_sock;
    int estimated_lines;
    struct WriteRequest *next;
} WriteRequest;

Document docs[MAX_DOCS];
int doc_count = 0;

pthread_mutex_t docs_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t section_mutex[MAX_DOCS][MAX_SECTIONS];
pthread_cond_t section_cond[MAX_DOCS][MAX_SECTIONS];
int section_writing[MAX_DOCS][MAX_SECTIONS] = {{0}};

WriteRequest *section_queue[MAX_DOCS][MAX_SECTIONS] = {{{0}}};
pthread_mutex_t section_queue_mutex[MAX_DOCS][MAX_SECTIONS];
pthread_cond_t section_queue_cond[MAX_DOCS][MAX_SECTIONS];

void send_all(int sock, const char *msg) {
    send(sock, msg, strlen(msg), 0);
}

Document* find_doc(const char* title) {
    for (int i = 0; i < doc_count; ++i) {
        if (strcmp(docs[i].title, title) == 0)
            return &docs[i];
    }
    return NULL;
}

ssize_t read_line(int sock, char *buf, size_t max_len) {
    size_t i = 0;
    char ch;
    while (i < max_len - 1) {
        ssize_t n = recv(sock, &ch, 1, 0);
        if (n <= 0) break;
        if (ch == '\n') break;
        buf[i++] = ch;
    }
    buf[i] = '\0';
    return i;
}

void parse_command(const char* input, char* args[], int* argc) {
    *argc = 0;
    const char* p = input;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') {
            p++;
            const char* start = p;
            while (*p && *p != '"') p++;
            int len = p - start;
            args[*argc] = malloc(len + 1);
            strncpy(args[*argc], start, len);
            args[*argc][len] = '\0';
            (*argc)++;
            if (*p == '"') p++;
        } else {
            const char* start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            int len = p - start;
            args[*argc] = malloc(len + 1);
            strncpy(args[*argc], start, len);
            args[*argc][len] = '\0';
            (*argc)++;
        }
    }
}

void* client_handler(void* arg);

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <Port>\n", argv[0]);
        exit(1);
    }

    for (int i = 0; i < MAX_DOCS; ++i)
        for (int j = 0; j < MAX_SECTIONS; ++j) {
            pthread_mutex_init(&section_mutex[i][j], NULL);
            pthread_cond_init(&section_cond[i][j], NULL);
            pthread_mutex_init(&section_queue_mutex[i][j], NULL);
            pthread_cond_init(&section_queue_cond[i][j], NULL);
        }

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 10);

    printf("[Server] Listening on %s:%s\n", argv[1], argv[2]);

    while (1) {
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addrlen);

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, client_sock);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}

void* client_handler(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    char buf[BUF_SIZE];
    char* args[64];
    int argc;

    while (1) {
        memset(buf, 0, sizeof(buf));
        if (read_line(client_sock, buf, sizeof(buf)) <= 0) break;

        parse_command(buf, args, &argc);
        if (argc == 0) continue;

        if (strcmp(args[0], "create") == 0) {
            pthread_mutex_lock(&docs_mutex);
            if (argc < 3 || doc_count >= MAX_DOCS) {
                pthread_mutex_unlock(&docs_mutex);
                send_all(client_sock, "[Error] Invalid create command.\n");
                continue;
            }
            if (find_doc(args[1])) {
                pthread_mutex_unlock(&docs_mutex);
                send_all(client_sock, "[Error] Document already exists.\n");
                continue;
            }
            int section_count = atoi(args[2]);
            if (section_count <= 0 || section_count > MAX_SECTIONS || argc != 3 + section_count) {
                pthread_mutex_unlock(&docs_mutex);
                send_all(client_sock, "[Error] Invalid section count or titles.\n");
                continue;
            }
            strcpy(docs[doc_count].title, args[1]);
            docs[doc_count].section_count = section_count;
            for (int i = 0; i < section_count; ++i) {
                strcpy(docs[doc_count].section_titles[i], args[3 + i]);
                docs[doc_count].section_line_count[i] = 0;
            }
            ++doc_count;
            pthread_mutex_unlock(&docs_mutex);
            send_all(client_sock, "[OK] Document created.\n");
        }

        else if (strcmp(args[0], "write") == 0) {
            if (argc < 3) {
                send_all(client_sock, "[Error] Invalid write command.\n");
                continue;
            }
            pthread_mutex_lock(&docs_mutex);
            Document* doc = find_doc(args[1]);
            if (!doc) {
                pthread_mutex_unlock(&docs_mutex);
                send_all(client_sock, "[Error] Document not found.\n");
                continue;
            }
            int section_idx = -1;
            for (int i = 0; i < doc->section_count; ++i)
                if (strcmp(doc->section_titles[i], args[2]) == 0) {
                    section_idx = i;
                    break;
                }
            if (section_idx == -1) {
                pthread_mutex_unlock(&docs_mutex);
                send_all(client_sock, "[Error] Section not found.\n");
                continue;
            }
            int doc_idx = doc - docs;
            pthread_mutex_unlock(&docs_mutex);

            send_all(client_sock, "[OK] You can start writing. Send <END> to finish.\n>> ");

            int line_count = 0;
            char line[MAX_LINE];
            char temp_lines[MAX_LINES][MAX_LINE];
            while (1) {
                if (read_line(client_sock, line, sizeof(line)) <= 0) break;
                if (strcmp(line, "<END>") == 0) break;
                if (line_count < MAX_LINES)
                    strncpy(temp_lines[line_count++], line, MAX_LINE - 1);
                send_all(client_sock, ">> ");
            }

            WriteRequest *req = malloc(sizeof(WriteRequest));
            req->client_sock = client_sock;
            req->estimated_lines = line_count;
            req->next = NULL;

            pthread_mutex_lock(&section_queue_mutex[doc_idx][section_idx]);
            if (!section_queue[doc_idx][section_idx] || line_count < section_queue[doc_idx][section_idx]->estimated_lines) {
                req->next = section_queue[doc_idx][section_idx];
                section_queue[doc_idx][section_idx] = req;
            } else {
                WriteRequest *cur = section_queue[doc_idx][section_idx];
                while (cur->next && cur->next->estimated_lines <= line_count)
                    cur = cur->next;
                req->next = cur->next;
                cur->next = req;
            }
            pthread_cond_signal(&section_queue_cond[doc_idx][section_idx]);
            pthread_mutex_unlock(&section_queue_mutex[doc_idx][section_idx]);

            pthread_mutex_lock(&section_mutex[doc_idx][section_idx]);
            while (section_queue[doc_idx][section_idx]->client_sock != client_sock)
                pthread_cond_wait(&section_queue_cond[doc_idx][section_idx], &section_mutex[doc_idx][section_idx]);

            pthread_mutex_lock(&docs_mutex);
            doc->section_line_count[section_idx] = 0;
            for (int i = 0; i < line_count && i < MAX_LINES; ++i)
                strncpy(doc->section_contents[section_idx][i], temp_lines[i], MAX_LINE - 1);
            doc->section_line_count[section_idx] = line_count;
            pthread_mutex_unlock(&docs_mutex);

            section_queue[doc_idx][section_idx] = section_queue[doc_idx][section_idx]->next;
            pthread_cond_broadcast(&section_queue_cond[doc_idx][section_idx]);
            pthread_mutex_unlock(&section_mutex[doc_idx][section_idx]);

            send_all(client_sock, "[Write_Completed]\n");
        }

        else if (strcmp(args[0], "read") == 0) {
            pthread_mutex_lock(&docs_mutex);
            if (argc == 1) {
                for (int i = 0; i < doc_count; ++i) {
                    char line[BUF_SIZE];
                    snprintf(line, sizeof(line), "%s\n", docs[i].title);
                    send_all(client_sock, line);
                    for (int j = 0; j < docs[i].section_count; ++j) {
                        snprintf(line, sizeof(line), "    %d. %s\n", j + 1, docs[i].section_titles[j]);
                        send_all(client_sock, line);
                    }
                }
            } else if (argc >= 3) {
                Document* doc = find_doc(args[1]);
                if (!doc) {
                    pthread_mutex_unlock(&docs_mutex);
                    send_all(client_sock, "[Error] Document not found.\n");
                    continue;
                }
                int found = 0;
                for (int i = 0; i < doc->section_count; ++i)
                    if (strcmp(doc->section_titles[i], args[2]) == 0) {
                        found = 1;
                        char header[BUF_SIZE];
                        snprintf(header, sizeof(header), "%s\n    %d. %s\n", doc->title, i + 1, doc->section_titles[i]);
                        send_all(client_sock, header);
                        for (int j = 0; j < doc->section_line_count[i]; ++j) {
                            char line[BUF_SIZE];
                            snprintf(line, sizeof(line), "       %s\n", doc->section_contents[i][j]);
                            send_all(client_sock, line);
                        }
                        break;
                    }
                if (!found)
                    send_all(client_sock, "[Error] Section not found.\n");
            }
            send_all(client_sock, "__END__\n");
            pthread_mutex_unlock(&docs_mutex);
        }

        else if (strcmp(args[0], "bye") == 0) {
            send_all(client_sock, "[Disconnected]\n");
            break;
        } else {
            send_all(client_sock, "[Error] Unknown command.\n");
        }

        for (int i = 0; i < argc; ++i) free(args[i]);
    }
    close(client_sock);
    return NULL;
}
