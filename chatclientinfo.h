/* chatclientinfo.h */

#ifndef _CLIENTINFO_H
#define _CLIENTINFO_H

#define FIFO_NAME_MAXLENGTH 150
#define USERNAME_MAXLENGTH 50
#define PASSWORD_MAXLENGTH 100
#define MESSAGE_MAXLENGTH 128
#define MAXBUF 1024

#define PUBLIC_FIFO_NUM 3

#define IDX_REGISTER 0
#define IDX_LOGIN 1
#define IDX_MESSAGE 2

#define TYPE_LOGIN 0
#define TYPE_LOGOUT 1
#define TYPE_ONLINE_USER_INFO 2

#define POLLFDS_NUM 3

typedef struct {
	char username[USERNAME_MAXLENGTH];
	char password[PASSWORD_MAXLENGTH];
	char myfifo[FIFO_NAME_MAXLENGTH];
    pid_t pid;
} REGISTER_REQUEST, *REGISTER_REQUEST_PTR;

typedef struct {
    int status;
} REGISTER_RESPONSE, *REGISTER_RESPONSE_PTR;

typedef struct {
	char username[USERNAME_MAXLENGTH];
	char password[PASSWORD_MAXLENGTH];
    pid_t pid;
    int type;
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

typedef struct {
    int type;               /* preserved for future feature */
    int online_user_num;
	//char **username_list;
    char username_list[50][USERNAME_MAXLENGTH];
} STATE_UPDATE_NOTICE, *STATE_UPDATE_NOTICE_PTR;
#endif
