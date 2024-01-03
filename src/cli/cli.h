#ifndef _CLI_H
#define _CLI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <cligen/cligen.h>

typedef int (*cg_callback_t)(cligen_handle h, cvec *cvv, cvec *argv);
typedef struct cb_lut_entry {
        char* name;
        cg_callback_t cb;
} cb_lut_t;

typedef void (*common_cb_t)(cligen_handle h, cvec *cvv,  void *arg);
typedef struct cb_common_entry {
    char *name;
    common_cb_t cb;
} cb_common_t;


cgv_fnstype_t* str2fn(char* name, void* arg, char** error);
int exit_cb(cligen_handle h, cvec* cvv, cvec* argv);
int common_cb(cligen_handle h, cvec* cvv, cvec* argv);
void download_file(cligen_handle h, cvec *cvv, void *arg);
void show_status(cligen_handle h, cvec* cvv, void* arg);
void send_message(cligen_handle h, cvec* cvv, void* arg);
void exec_handler(cligen_handle h, cvec *cvv, void *arg);
void upload_file(cligen_handle h, cvec *cvv, void *arg);
void create_session(cligen_handle h, cvec *cvv, void *arg);

#ifdef __cplusplus
}
#endif

#endif
