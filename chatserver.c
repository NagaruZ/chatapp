/* chatserver.c */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <syslog.h>
#include "chatclientinfo.h"

#define CONF_FILENAME "/home/zouyufan_2016200087/chatapp/server.conf"

#define DELIM "="
#define IS_DAEMON 1

struct __config
{
   char FIFO[3][FIFO_NAME_MAXLENGTH];    /* array of paths of FIFO for REGISTER, LOGIN, SENDMSG */
   int MAX_ONLINE_USERS;
} server_conf;

int public_fifo_fd[PUBLIC_FIFO_NUM];

struct __RegisterStatus {
	char username[USERNAME_MAXLENGTH];
	char password[PASSWORD_MAXLENGTH];
	char fifo[FIFO_NAME_MAXLENGTH];
    int is_logged_in;
} RegisteredUsers[100];

struct __LoginStatus {
	char username[USERNAME_MAXLENGTH];
	int session_id;
    pid_t pid;
    int is_valid;
} CurrentLoggedUsers[100];

struct __MessageBuffer {
    char message[MESSAGE_MAXLENGTH];
    char src_username[USERNAME_MAXLENGTH];
    char dst_username[USERNAME_MAXLENGTH];
    int is_valid;
} MessageBuffer[MAXBUF];

pthread_mutex_t mutex;
int last_user_id = 0;
int last_session_id = 0;
int online_user_num = 0;
pthread_t threadId_register, threadId_login, threadId_sendmsg;

/* function definitions */
void handler(int sig);
void init_message_buffer();
int init_daemon();
struct __config get_config(char *filename);
void create_public_fifo();
void open_public_fifo();
int open_client_tmp_fifo(pid_t pid);
STATE_UPDATE_NOTICE init_notice();
void send_notice_to_logged_in_users(STATE_UPDATE_NOTICE_PTR notice);
int get_dst_user_id(char* dst_username);
int get_dst_user_login_status(char* dst_username);
int set_dst_user_login_status(char* dst_username, int status);
int get_dst_user_fifo(char* dst_username, char* dst_user_fifo);
int open_dst_user_fifo(char* dst_user_fifo);
int get_dst_session_id(char* dst_username);
int get_dst_client_pid(char* dst_username);

void *process_login_request(void *arg);
void *process_register_request(void *arg);
void *process_sendmsg_request(void *arg);

void handler(int sig){
	int i;
    /* delete public FIFOs */
	for(i=0; i<PUBLIC_FIFO_NUM; i++){
		unlink(server_conf.FIFO[i]);
	}
	exit(1);
}

void init_message_buffer()
{
    for(int i=0; i<MAXBUF; i++)
        MessageBuffer[i].is_valid = 0;
}

int init_daemon()
{ 
	pid_t pid; 
	int i; 
	/*忽略终端I/O信号，STOP信号*/
	signal(SIGTTOU,SIG_IGN);
	signal(SIGTTIN,SIG_IGN);
	signal(SIGTSTP,SIG_IGN);
	signal(SIGHUP,SIG_IGN);
	
	pid = fork();
	if(pid > 0) { exit(0); /*结束父进程，使得子进程成为后台进程*/ }
	else if(pid < 0) {  return -1; }
	/*建立一个新的进程组,在这个新的进程组中,子进程成为这个进程组的首进程,以使该进程脱离所有终端*/
	setsid();
	/*再次新建一个子进程，退出父进程，保证该进程不是进程组长，同时让该进程无法再打开一个新的终端*/
	pid=fork();
	if( pid > 0) { exit(0); }
	else if( pid< 0) { return -1; }
	/*关闭所有从父进程继承的不再需要的文件描述符*/
	for(i=0;i< NOFILE;close(i++));
	/*改变工作目录，使得进程不与任何文件系统联系*/
	chdir("/");
	/*将文件当时创建屏蔽字设置为0*/
	umask(0);
	/*忽略SIGCHLD信号*/
	signal(SIGCHLD,SIG_IGN); 
	return 0;
}

struct __config get_config(char *filename) 
{
    struct __config configstruct;
    FILE *file = fopen(filename, "r");

    if (file != NULL)
    { 
        char line[MAXBUF];
        int i = 0;

        while(fgets(line, sizeof(line), file) != NULL)
        {
            char *cfline;
            cfline = strstr((char *)line,DELIM);
            cfline = cfline + strlen(DELIM);
            if( i < PUBLIC_FIFO_NUM ){                      /* reading lines of paths */
                int len = strlen(cfline);
                memcpy(configstruct.FIFO[i],cfline,len);
                if(configstruct.FIFO[i][len-1] == '\n')     /* remove \n */
                    configstruct.FIFO[i][len-1] = '\0';
            }
            else {
                char tmp[MAXBUF]; int x;
                memcpy(tmp,cfline,strlen(cfline));
                x = atoi(tmp);
                configstruct.MAX_ONLINE_USERS = x;
            }
            i++;
        } // End while
        fclose(file);
    } // End if file

    return configstruct;
}

void create_public_fifo()
{
	int i,res;
	for(i=0; i<PUBLIC_FIFO_NUM; i++){
		/* create FIFO, if necessary */
		if(access(server_conf.FIFO[i], F_OK) == -1){
			res = mkfifo(server_conf.FIFO[i], 0777);
			if(res != 0){
				printf("Public FIFO %s was not created\n", server_conf.FIFO[i]);
				perror(server_conf.FIFO[i]);
				exit(EXIT_FAILURE);
			}
		}
	}
	printf("Create all public FIFO successfully!\n");
}

void open_public_fifo()
{
	int i;
	/* open FIFO for reading */
	for(i=0; i<PUBLIC_FIFO_NUM; i++){
		public_fifo_fd[i] = open(server_conf.FIFO[i], O_RDONLY | O_NONBLOCK);
        //public_fifo_fd[i] = open(server_conf.FIFO[i], O_RDONLY );
		if(public_fifo_fd[i] == -1){
			printf("Could not open public FIFO %s for read only access\n", server_conf.FIFO[i]);
			perror(server_conf.FIFO[i]);
			exit(EXIT_FAILURE);
		}
	}
	printf("Open all public FIFO successfully!\n");
}

/* 
    Open client's tmp FIFO 
    @param  pid_t   pid of a client
    @return {int}   file descriptor of client's tmp FIFO
*/
int open_client_tmp_fifo(pid_t pid)
{
    int tmp_fifo_fd;
	char tmp_pipename[FIFO_NAME_MAXLENGTH];
	sprintf(tmp_pipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%d_tmp_fifo", pid);
	tmp_fifo_fd = open(tmp_pipename, O_WRONLY);
	if(tmp_fifo_fd == -1){
		printf("Could not open %s for write access in open_client_tmp_fifo\n", tmp_pipename);
		perror(tmp_pipename);
	}
    return tmp_fifo_fd;
}

STATE_UPDATE_NOTICE init_notice()
{
    STATE_UPDATE_NOTICE notice;
    notice.type = TYPE_ONLINE_USER_INFO;
    notice.online_user_num = online_user_num;
    for(int idx2 = 0, idx = 0; idx < last_session_id; idx++) {
        if(idx2 >= online_user_num) break;
        if(CurrentLoggedUsers[idx].is_valid){
            strcpy(notice.username_list[idx2++], CurrentLoggedUsers[idx].username);
        }
    }
    return notice;
}

void send_notice_to_logged_in_users(STATE_UPDATE_NOTICE_PTR notice)
{
    int tmp_fifo_fd, status;
    for(int idx=0; idx < last_session_id; idx++) {
        if(CurrentLoggedUsers[idx].is_valid) {
            tmp_fifo_fd = open_client_tmp_fifo(CurrentLoggedUsers[idx].pid);
            if((status = write(tmp_fifo_fd, notice, sizeof(STATE_UPDATE_NOTICE))) == -1){
                printf("Could not write online user notice into tmp FIFO of client %d in %s()\n", CurrentLoggedUsers[idx].pid, __func__);
                perror("write");
            }
            else{
                printf("Online user notice has been written into tmp FIFO of client %d\n", CurrentLoggedUsers[idx].pid);
            }
            close(tmp_fifo_fd);
        }
    }
}

/*
    get user id of a specified user
    @param  dst_username    name of the specified user
    @return                 user id on success, -1 on fail
*/
int get_dst_user_id(char* dst_username){
	int i = -1;
	for(i=0; i<last_user_id; i++){
		if(strcmp(RegisteredUsers[i].username, dst_username) == 0)
            break;
	}
	return i;
}

/*
    get login status of a specified user
    @param  dst_username    name of the specified user
    @return                 login status on success, -1 on fail
*/
int get_dst_user_login_status(char* dst_username)
{
    int user_id = get_dst_user_id(dst_username);
    if(user_id == -1)   /* user does not exists */
        return -1;
    return RegisteredUsers[user_id].is_logged_in;
}

/*
    set login status of a specified user
    @param  dst_username    name of the specified user
    @param  status          the status to be set
    @return                 0 when successful, -1 when failed
*/
int set_dst_user_login_status(char* dst_username, int status)
{
    int user_id = get_dst_user_id(dst_username);
    int session_id = get_dst_session_id(dst_username);
    if(user_id == -1 || session_id == -1)   /* user does not exists */
        return -1;
        
    /* lock */
    if(pthread_mutex_lock(&mutex) != 0)
        perror("lock failed");
    
    RegisteredUsers[user_id].is_logged_in = status;
    CurrentLoggedUsers[session_id].is_valid = status;
    
    /* unlock */
    if(pthread_mutex_unlock(&mutex) != 0)
        perror("unlock failed");
    
    return 0;
}

/* 
    get user's FIFO path
    @param  dst_username    the name of user
    @param  dst_user_fifo   the path of user's FIFO
    @return                 0 on success, -1 on fail
*/
int get_dst_user_fifo(char* dst_username, char* dst_user_fifo){
	int user_id = get_dst_user_id(dst_username);
	if(user_id != -1){
		strcpy(dst_user_fifo, RegisteredUsers[user_id].fifo);
		return 0;
	}
	else return -1;
}

/* 
    open user's FIFO
    @param  dst_user_fifo   the path of user's FIFO
    @return                 file descriptor of dst_user_fifo
*/
int open_dst_user_fifo(char* dst_user_fifo)
{
    int fd = open(dst_user_fifo, O_WRONLY);
    if(fd == -1){
        printf("Could not open client FIFO %s for write access in %s()\n", dst_user_fifo, __func__);
        perror("open");
    }
    return fd;
}

/* 
    get session id of a specified user
    @param  dst_username    name of the specified user
    @return                 session id on success, -1 on fail
*/
int get_dst_session_id(char* dst_username){
	int i = -1;
	for(i=0; i<last_session_id; i++){
		if(strcmp(CurrentLoggedUsers[i].username, dst_username) == 0 && CurrentLoggedUsers[i].is_valid)
            break;
	}
	return i;
}

/* 
    get pid of client where a specified user logged in
    @param  dst_username    name of the specified user
    @return                 pid on success, -1 on fail
*/
int get_dst_client_pid(char* dst_username){
	int session_id;
	session_id = get_dst_session_id(dst_username);
    return (session_id != -1)? CurrentLoggedUsers[session_id].pid: -1;
}

void *process_sendmsg_request(void *arg){
    SEND_MESSAGE_REQUEST_PTR req = (SEND_MESSAGE_REQUEST_PTR)arg;
    
    int dst_user_login_status = get_dst_user_login_status(req->dst_username);
    if( dst_user_login_status == -1 ){  /* user does not exists */
        printf("Specified user does not exist!\n");
        return NULL;
    }
    else if( dst_user_login_status == 0 ){ /* user has not logged in */
        /* store message in the buffer */
        int pos = 0;
        while(pos < MAXBUF && MessageBuffer[pos].is_valid != 0) pos++; /* find a position which is valid to store message */
        if(pos == MAXBUF) { /* position invalid */
            printf("Message has not been sent: user %s has not logged in, and message buffer is full.\n", req->dst_username);
            return NULL;
        }
        else {  /* position valid */
        
            /* lock */
            
            
            if(pthread_mutex_lock(&mutex) != 0)
                perror("lock failed in process_sendmsg_request()");
            
            MessageBuffer[pos].is_valid = 1;
            strcpy(MessageBuffer[pos].message, req->message);
            strcpy(MessageBuffer[pos].src_username, req->src_username);
            strcpy(MessageBuffer[pos].dst_username, req->dst_username);
            
            /* unlock */
            if(pthread_mutex_unlock(&mutex) != 0)
                perror("unlock failed in process_sendmsg_request()");
            
            printf("Message to %s has been stored.\n", req->dst_username);
            return NULL;
        }
    }
    else if( dst_user_login_status == 1 ){ /* user has logged in */
        /* open dst user FIFO */
        char dst_user_fifo[FIFO_NAME_MAXLENGTH];
        if(get_dst_user_fifo(req->dst_username, dst_user_fifo) == -1){
            printf("%s failed\n", __func__);
            perror(__func__);
            return NULL;
        }
        int fd = open_dst_user_fifo(dst_user_fifo);
        printf("%s\n", dst_user_fifo);
        /* write message content into dst user FIFO */
        int status = write(fd, req, sizeof(SEND_MESSAGE_REQUEST));
        if(status == -1){
            printf("Write into %s failed\n", dst_user_fifo);
            perror(dst_user_fifo);
        }
        else{
            printf("Message has been written into FIFO %s\n", dst_user_fifo);
        }
        close(fd);
        return NULL;
    }
    else {
        return NULL;
    }
}


void *process_login_request(void *arg){
    LOGIN_REQUEST_PTR req = (LOGIN_REQUEST_PTR)arg;
	int i,flag = 0, status;
	LOGIN_RESPONSE response;
    STATE_UPDATE_NOTICE notice;
    
	/* open client's tmp FIFO */
    int tmp_fifo_fd = open_client_tmp_fifo(req->pid);
    
    if(req->type == TYPE_LOGIN){
        /* check if currently online user number exceeds max online user number limit */
        if(online_user_num >= server_conf.MAX_ONLINE_USERS){
            response.status = -2;
            response.session_id = -1;
        }
        else{
            /* find the specified user */
            for(i=0; i<last_user_id; i++){
                if(strcmp(RegisteredUsers[i].username, req->username) == 0 && strcmp(RegisteredUsers[i].password, req->password) == 0){
                    flag = 1;
                    break;
                }
            }
            if(flag){   /* found */
                response.status = 0;
                response.session_id = last_session_id;
                /* send logged-in user info to all online clients */
                
                /* lock */
                if(pthread_mutex_lock(&mutex) != 0)
                    perror("lock failed in process_login_request()");
                
                strcpy(CurrentLoggedUsers[last_session_id].username, req->username);
                CurrentLoggedUsers[last_session_id].session_id = last_session_id;
                CurrentLoggedUsers[last_session_id].pid = req->pid;
                CurrentLoggedUsers[last_session_id].is_valid = 1;
                RegisteredUsers[i].is_logged_in = 1; /* update user's log-in status in RegisterStatus */
                last_session_id++;
                online_user_num++;
                
                /* unlock */
                if(pthread_mutex_unlock(&mutex) != 0)
                    perror("unlock failed in process_login_request()");
                
                /* init notice */
                notice = init_notice();
                
            }
            else{       /* not found */
                response.status = -1;
                response.session_id = -1;
            }
        }
        /* write login response to the client's tmp FIFO */
        if((status = write(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE))) == -1){
            printf("Could not write login response into tmp FIFO of client %d in %s()\n", req->pid, __func__);
            perror("write");
        }
        else{
            printf("Login response has been written into tmp FIFO of client %d\n", req->pid);
        }
        close(tmp_fifo_fd);
        
        
        /* if login successfully, write online user notice to all client's tmp FIFO */
        if(flag) send_notice_to_logged_in_users(&notice);
        
        /* after a 'success' response, client has created user FIFO */
        /* check if there are some stored messages to be sent */
        for(int pos = 0; pos < MAXBUF; pos++){
            if(MessageBuffer[pos].is_valid == 1 && strcmp(MessageBuffer[pos].dst_username, req->username) == 0){
                /* if there are some */
                /* construct message response */
                SEND_MESSAGE_REQUEST msg_req;
                strcpy(msg_req.src_username, MessageBuffer[pos].src_username);
                strcpy(msg_req.dst_username, req->username);
                strcpy(msg_req.message, MessageBuffer[pos].message);
                msg_req.pid = 1; // not used here
                
                /* open dst user FIFO */
                char dst_user_fifo[FIFO_NAME_MAXLENGTH];
                get_dst_user_fifo(req->username, dst_user_fifo);
                int fd = open_dst_user_fifo(dst_user_fifo);
                
                /* write message content into dst user FIFO */
                status = write(fd, &msg_req, sizeof(SEND_MESSAGE_REQUEST));
                if(status == -1){
                    printf("Write into %s failed\n", dst_user_fifo);
                    perror("write");
                }
                else{
                    printf("Message has been written into FIFO %s\n", dst_user_fifo);
                }
                close(fd);
                /* update message buffer record state */
                
                /* lock */
                if(pthread_mutex_lock(&mutex) != 0)
                    perror("lock failed");
                
                MessageBuffer[pos].is_valid = 0;
                
                /* unlock */
                if(pthread_mutex_unlock(&mutex) != 0)
                    perror("unlock failed");
            }
        }
        
    }
    else if(req->type == TYPE_LOGOUT){
        
        response.status = set_dst_user_login_status(req->username, 0); // set_dst_user_login_status() is thread safe
        
        /* lock */
        if(pthread_mutex_lock(&mutex) != 0)
            perror("lock failed");
        
        online_user_num--;
        
        /* unlock */
        if(pthread_mutex_unlock(&mutex) != 0)
            perror("unlock failed");
                
        /* init notice */
        notice = init_notice();
        
        /* update tmp_fifo_fd  */
        tmp_fifo_fd = open_client_tmp_fifo(req->pid);
        if((status = write(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE))) == -1){
            printf("Could not write logout response into tmp FIFO of client %d in %s()\n", req->pid, __func__);
            perror("logout response");
        }
        else{
            printf("Logout response has been written into tmp FIFO of client %d\n", req->pid);
        }
        
        /* write online user notice to all client's tmp FIFO */
        send_notice_to_logged_in_users(&notice);
    }
    else {
        response.status = -3;
        /* write login response to the client's tmp FIFO */
        if((status = write(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE))) == -1){
            printf("Could not write login response into tmp FIFO of client %d in %s()\n", req->pid, __func__);
            perror("login response");
        }
        else{
            printf("Login response has been written into tmp FIFO of client %d\n", req->pid);
        }
        close(tmp_fifo_fd);
    }
    return NULL;
}

void *process_register_request(void *arg){
    REGISTER_REQUEST_PTR req = (REGISTER_REQUEST_PTR)arg;
    int flag = 0, status;
    REGISTER_RESPONSE response;
    
    /* open client's tmp FIFO */
    int tmp_fifo_fd = open_client_tmp_fifo(req->pid);
    
    /* check if there exists a user with the same username */
    for(int i=0; i<last_user_id; i++){
        if(strcmp(RegisteredUsers[i].username, req->username) == 0){
            flag = 1;
            break;
        }
    }
    if(flag){       /* user exists, register failed */
        /* set failed response status */
        response.status = -1;
    }
    else {          /* user not exists, register a new one  */
        /* lock */
        if(pthread_mutex_lock(&mutex) != 0)
            perror("lock failed in process_register_request()");
        
        strcpy(RegisteredUsers[last_user_id].username, req->username);
        strcpy(RegisteredUsers[last_user_id].password, req->password);
        strcpy(RegisteredUsers[last_user_id].fifo, req->myfifo);
        printf("%s %s %s\n", RegisteredUsers[last_user_id].username, RegisteredUsers[last_user_id].password, RegisteredUsers[last_user_id].fifo);
        last_user_id++;
        
        /* unlock */
        if(pthread_mutex_unlock(&mutex) != 0)
            perror("unlock failed in process_register_request()");
        
        /* set successful response status */
        response.status = 0;
    }
    /* write response to the client */
    if((status = write(tmp_fifo_fd, &response, sizeof(REGISTER_RESPONSE))) == -1){
        printf("Could not write register response into tmp FIFO of client %d in process_register_request()\n", req->pid);
        perror("register response");
    }
    else{
        printf("Register response has been written into tmp FIFO of client %d\n", req->pid);
    }
    close(tmp_fifo_fd);
    return NULL;
}

int main(int argc, char *argv[]){ 
    if(IS_DAEMON) init_daemon(); /* 设置自身为守护进程 */ 
	server_conf = get_config(CONF_FILENAME);
    signal(SIGKILL, handler);
	signal(SIGINT, handler);
	signal(SIGTERM, handler);
    
    if(pthread_mutex_init(&mutex, NULL) != 0)
    {
        perror("Mutex init failed");
        exit(1);
    }
    
    init_message_buffer();
	create_public_fifo();
	open_public_fifo();
    
    printf("I am process %d.\n", getpid());
	printf("Server is now running!\n");
    
    struct pollfd *pollfds = (struct pollfd *)malloc(PUBLIC_FIFO_NUM * sizeof(struct pollfd));
	for(int i=0; i<PUBLIC_FIFO_NUM; i++){
		pollfds[i].fd = public_fifo_fd[i];
		pollfds[i].events = POLLIN|POLLPRI;
	}
    while(1){
        //sleep(1);
		REGISTER_REQUEST register_info;
		LOGIN_REQUEST login_info;
		SEND_MESSAGE_REQUEST message_info;
		switch(poll(pollfds, PUBLIC_FIFO_NUM, -1)){
			case -1:
				perror("poll()");
				break;
			case 0:
				printf("time out\n");
				break;
			default:
				for(int i=0; i<PUBLIC_FIFO_NUM; i++){
					if(pollfds[i].revents & POLLIN){
						switch(i){
							case IDX_REGISTER:
								read(public_fifo_fd[IDX_REGISTER], &register_info, sizeof(REGISTER_REQUEST));
                                pthread_create(&threadId_register, NULL, &process_register_request, &register_info);
                                pthread_join(threadId_register, NULL);
								break;
							case IDX_LOGIN:
								read(public_fifo_fd[IDX_LOGIN], &login_info, sizeof(LOGIN_REQUEST));
								pthread_create(&threadId_login, NULL, &process_login_request, &login_info);
                                pthread_join(threadId_login, NULL);
								break;
							case IDX_MESSAGE:
								read(public_fifo_fd[IDX_MESSAGE], &message_info, sizeof(SEND_MESSAGE_REQUEST));
                                pthread_create(&threadId_sendmsg, NULL, &process_sendmsg_request, &message_info);
                                pthread_join(threadId_sendmsg, NULL);
                                break;
						}
					}
				}
		}
	}
	
	exit(0);
}
