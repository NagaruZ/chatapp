/* chatclientinfo.h */

#ifndef _CLIENTINFO_H
#define _CLIENTINFO_H

#define FIFO_NAME_MAXLENGTH 150
#define USERNAME_MAXLENGTH 50
#define PASSWORD_MAXLENGTH 100
#define MESSAGE_MAXLENGTH 128
#define MAXBUF 1024

#define IDX_REGISTER 0
#define IDX_LOGIN 1
#define IDX_MESSAGE 2

typedef struct {
	char username[USERNAME_MAXLENGTH];
	char password[PASSWORD_MAXLENGTH];
	char myfifo[FIFO_NAME_MAXLENGTH];
    pid_t pid;
} REGISTER_REQUEST, *REGISTER_REQUEST_PTR;

typedef struct {
    int status;
} REGISTER_RESPONSE, *LOGIN_REGISTER_PTR;

typedef struct {
	char username[USERNAME_MAXLENGTH];
	char password[PASSWORD_MAXLENGTH];
    pid_t pid;
} LOGIN_REQUEST, *LOGIN_REQUEST_PTR;

typedef struct {
    int status;
    int session_id;
    int current_online_user_num;
    char current_online_user_list[MAXBUF];
} LOGIN_RESPONSE, *LOGIN_RESPONSE_PTR;

typedef struct {
	char src_username[USERNAME_MAXLENGTH];
	char dst_username[USERNAME_MAXLENGTH];
	char message[MESSAGE_MAXLENGTH];
    pid_t pid;
} SEND_MESSAGE_REQUEST, *SEND_MESSAGE_REQUEST_PTR;
#endif
