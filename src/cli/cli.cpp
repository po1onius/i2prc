#include <iostream>
#include <cligen/cligen.h>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <functional>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fstream>
#include <fcntl.h>
#include "cli.h"

#define C2CP_MSG_ANY 0
#define C2CP_MSG_SHOW_LEASESET 1
#define C2CP_MSG_SHOW_TUNNELS 2
#define C2CP_MSG_SHOW_STATUS 3
#define C2CP_MSG_SEND_MESSAGE 4
#define C2CP_MSG_UPLOAD_FILE 5
#define C2CP_MSG_DOWNLOAD_FILE 6
#define C2CP_MSG_EXEC_CMD 7
#define C2CP_MSG_FILE_CONTENT 8
#define C2CP_MSG_CREATE_SESSION 9

#define BUFMAX 65536
#define C2CP_MSG_HEADER_SIZE 5
#define C2CP_MSG_HEADER_SIZE_OFFSET 1

/* Callback look-up table */
static cb_lut_t cb_lut[] = {{"exit_cb", exit_cb}, {"common_cb", common_cb}};


/* Common Callback look-up table */
static cb_common_t cb_common[] = {
    {"show_status", show_status},
    {"send_message", send_message},
    {"exec_handler", exec_handler},
    {"upload_file", upload_file},
    {"create_session", create_session},
    {"download_file", download_file},
};

static int cfd = 0;
static int ffd = 0;
static size_t filesize = 0;

uint16_t buf16toh(const void *buf)
{
    uint16_t b16;
    memcpy(&b16, buf, sizeof(uint16_t));
    return b16;
}

uint32_t buf32toh(const void *buf)
{
    uint32_t b32;
    memcpy(&b32, buf, sizeof(uint32_t));
    return b32;
}

uint64_t buf64toh(const void *buf)
{
    uint64_t b64;
    memcpy(&b64, buf, sizeof(uint64_t));
    return b64;
}

uint16_t bufbe16toh(const void *buf)
{
    return be16toh(buf16toh(buf));
}

uint32_t bufbe32toh(const void *buf)
{
    return be32toh(buf32toh(buf));
}

uint64_t bufbe64toh(const void *buf)
{
    return be64toh(buf64toh(buf));
}

void htobuf16(void *buf, uint16_t b16)
{
    memcpy(buf, &b16, sizeof(uint16_t));
}

void htobuf32(void *buf, uint32_t b32)
{
    memcpy(buf, &b32, sizeof(uint32_t));
}

void htobuf64(void *buf, uint64_t b64)
{
    memcpy(buf, &b64, sizeof(uint64_t));
}

void htobe16buf(void *buf, uint16_t big16)
{
    htobuf16(buf, htobe16(big16));
}

void htobe32buf(void *buf, uint32_t big32)
{
    htobuf32(buf, htobe32(big32));
}

void htobe64buf(void *buf, uint64_t big64)
{
    htobuf64(buf, htobe64(big64));
}

void htole16buf(void *buf, uint16_t big16)
{
    htobuf16(buf, htole16(big16));
}

void htole32buf(void *buf, uint32_t big32)
{
    htobuf32(buf, htole32(big32));
}

void htole64buf(void *buf, uint64_t big64)
{
    htobuf64(buf, htole64(big64));
}

uint16_t bufle16toh(const void *buf)
{
    return le16toh(buf16toh(buf));
}

uint32_t bufle32toh(const void *buf)
{
    return le32toh(buf32toh(buf));
}

uint64_t bufle64toh(const void *buf)
{
    return le64toh(buf64toh(buf));
}

cgv_fnstype_t *str2fn(char *name, void *arg, char **error)
{
    for (auto & i : cb_lut) {
        if (0 == strcmp(name, i.name)) {
            return i.cb;
        }
    }
    return nullptr;
}

bool recv_n(uint8_t* buf, uint32_t size)
{
    uint32_t ret;
    uint32_t offset = 0;
    while ((ret = read(cfd, buf + offset, size - offset)) != 0) {
        if (ret < 0 && errno == EINTR) {
            continue;
        }
        offset += ret;
        if (offset == size) {
            return true;
        }
    }
    return false;
}

void *client_loop(void *arg)
{
    while (1) {
        uint8_t buf[BUFMAX];
        if (!recv_n(buf, C2CP_MSG_HEADER_SIZE)) {
            continue;
        }
        uint8_t type = buf[0];
        uint32_t size = bufbe32toh(buf + C2CP_MSG_HEADER_SIZE_OFFSET);
        if (size > 0) {
            recv_n(buf + C2CP_MSG_HEADER_SIZE, size);
        }
        if (type != C2CP_MSG_FILE_CONTENT) {
            for (uint32_t i = C2CP_MSG_HEADER_SIZE; i < size + C2CP_MSG_HEADER_SIZE; i++) {
                std::cout << buf[i];
            }
        } else {
            if (filesize == 0) {
                filesize = bufbe32toh(buf + C2CP_MSG_HEADER_SIZE);
                std::cout << "filesize: " << filesize << std::endl;
                continue;
            }
            if (ffd > 0) {
                auto ret = write(ffd, buf + C2CP_MSG_HEADER_SIZE, size);
                filesize -= size;
                std::cout << "ready " << size << " bytes, real write " << ret << " bytes, last " << filesize << " bytes" << std::endl;
                if (!filesize) {
                    close(ffd);
                    ffd = 0;
                }
            }
        }
    }
}

void tcp_connect(char* ip, uint32_t port)
{
    cfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(port);
    remote_addr.sin_addr.s_addr = inet_addr(ip);
    auto ret = connect(cfd, (struct sockaddr*)(&remote_addr), sizeof(remote_addr));
    if (ret < 0) {
        perror("cli connect: ");
    }
}

void send_n(uint8_t* buf, uint32_t size)
{
    uint32_t ret;
    uint32_t offset = 0;
    while((ret = write(cfd, buf + offset, size - offset)) != 0) {
        if(ret < 0 && errno == EINTR) {
            continue;
        }
        offset += ret;
        if (offset == size) {
            break;
        }
    }
}


void wrap_send(uint8_t type, uint8_t* buf, uint32_t size)
{
    uint8_t b[BUFMAX];
    b[0] = type;
    if (type == C2CP_MSG_SHOW_STATUS || type == C2CP_MSG_SHOW_LEASESET || type == C2CP_MSG_SHOW_TUNNELS) {
        htobe32buf(b + 1, 0);
    } else {
        htobe32buf(b + 1, size);
        memcpy(b + 5, buf, size);
    }
    send_n(b, size + C2CP_MSG_HEADER_SIZE);
}


int main(int argc, char *argv[])
{
    tcp_connect(argv[1], atoi(argv[2]));

    cligen_handle h = cligen_init();
    cligen_prompt_set(h, "cli> ");

    char tree[] =
        "show status, common_cb(\"show_status\");\n"
        "show leaseset, common_cb(\"show_status\");\n"
        "show tunnels, common_cb(\"show_status\");\n"
        "lookup <dest:string>, common_cb(\"dest_lookup\");\n"
        "send <msg:string>, common_cb(\"send_message\");\n"
        "exec <command:string>, common_cb(\"exec_handler\");\n"
        "create session <dest:string>, common_cb(\"create_session\");\n"
        "upload <src_file:string>, common_cb(\"upload_file\");\n"
        "download <src_file:string>, common_cb(\"download_file\");\n"
        "exit, exit_cb();";
    cligen_parse_str(h, tree, "name", nullptr, nullptr);

    parse_tree *pt = nullptr;
    pt_head *ph = nullptr; /* cligen parse tree head */

    while ((ph = cligen_ph_each(h, ph)) != nullptr) {
        pt = cligen_ph_parsetree_get(ph);

        if (cligen_callbackv_str2fn(pt, str2fn, nullptr) < 0) {
            exit(1);
        }
    }

    auto t = std::thread([] { return client_loop(nullptr); });

    cligen_loop(h);


    if (h) {
        cligen_exit(h);
    }
    return 0;
}

int exit_cb(cligen_handle h, cvec *cvv, cvec *argv)
{
    return cligen_exiting_set(h, 1);
}

int common_cb(cligen_handle h, cvec *cvv, cvec *argv)
{
    std::string name = cv_string_get(cvec_i(argv, 0));
    for (auto i : cb_common) {
        if (std::string(i.name) != name)
            continue;
        i.cb(h, cvv, nullptr);
        break;
    }
    return 0;
}




void show_status(cligen_handle h, cvec *cvv, void *arg)
{
    uint8_t type;
    if (cvec_find(cvv, "leaseset")) {
        type = C2CP_MSG_SHOW_LEASESET;
    } else if (cvec_find(cvv, "tunnels")) {
        type = C2CP_MSG_SHOW_TUNNELS;
    } else {
        type = C2CP_MSG_SHOW_STATUS;
    }
    wrap_send(type, nullptr, 0);
}


void download_file(cligen_handle h, cvec *cvv, void *arg)
{
    std::string file = cv_string_get(cvec_find(cvv, "src_file"));
    ffd = open(file.c_str(), O_WRONLY | O_CREAT);
    wrap_send(C2CP_MSG_DOWNLOAD_FILE, (uint8_t*)file.c_str(), file.size());
}

void send_message(cligen_handle h, cvec *cvv, void *arg)
{
    std::string msg = cv_string_get(cvec_find(cvv, "msg"));
    wrap_send(C2CP_MSG_SEND_MESSAGE, (uint8_t*)msg.c_str(), msg.size());
}

void exec_handler(cligen_handle h, cvec *cvv, void *arg)
{
    std::string cmd = cv_string_get(cvec_find(cvv, "command"));
    wrap_send(C2CP_MSG_EXEC_CMD, (uint8_t*)cmd.c_str(), cmd.size());
}

void sendf(const char* filename)
{
    struct stat statbuf;
    stat(filename, &statbuf);
    int size = statbuf.st_size;
    int ffd = open(filename, O_RDONLY);
    char filereadbuf[BUFMAX - C2CP_MSG_HEADER_SIZE];

    char buf[BUFMAX];
    buf[0] = C2CP_MSG_FILE_CONTENT;
    while (size) {
        memset(buf + C2CP_MSG_HEADER_SIZE_OFFSET, 0, BUFMAX - C2CP_MSG_HEADER_SIZE_OFFSET);
        int ret = read(ffd, filereadbuf, sizeof(filereadbuf));
        htobe32buf(buf + C2CP_MSG_HEADER_SIZE_OFFSET, ret);
        memcpy(buf + C2CP_MSG_HEADER_SIZE, filereadbuf, ret);
        write(cfd, buf, ret + C2CP_MSG_HEADER_SIZE);
        size -= ret;
        sleep(0.5);
        printf("socket write %d bytes file\n", ret);
    }
}


void upload_file(cligen_handle h, cvec *cvv, void *arg)
{
    std::string file = cv_string_get(cvec_find(cvv, "src_file"));
    struct stat statbuf;
    stat(file.c_str(), &statbuf);
    //4 is upload instruction type, msg header
    std::string msg = file + "^" + std::to_string(statbuf.st_size);
    wrap_send(C2CP_MSG_UPLOAD_FILE, (uint8_t*)msg.c_str(), msg.size());

    sleep(1);
    sendf(file.c_str());
}

void create_session(cligen_handle h, cvec *cvv, void *arg)
{
    std::string ip = cv_string_get(cvec_find(cvv, "dest"));
    wrap_send(C2CP_MSG_CREATE_SESSION, (uint8_t*)ip.c_str(), ip.size());
}
