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
#include "chatclientinfo.h"

#define CONF_FILENAME "server.conf"

#define DELIM "="
#define PUBLIC_FIFO_NUM 3

/* 服务端配置信息 */
struct config
{
   char FIFO[3][MAXBUF];    /* array of paths of FIFO for REGISTER, LOGIN, SENDMSG */
   int MAX_ONLINE_USERS;
};

void create_public_fifo();
void handler(int sig);
void process_register_request(REGISTER_REQUEST_PTR req);
void process_login_request(LOGIN_REQUEST_PTR req);
int get_dst_user_fifo(char* dst_username, char* dst_user_fifo); // return value set to the second param
int get_dst_client_pid(char* dst_username);
int get_dst_user_id(char* dst_username);
int get_dst_session_id(char* dst_username);

int public_fifo_fd[PUBLIC_FIFO_NUM];
struct RegisterStatus {
	char username[USERNAME_MAXLENGTH];
	char password[PASSWORD_MAXLENGTH];
	char fifo[FIFO_NAME_MAXLENGTH];
    int is_logged_in;
} RegisteredUsers[100];

struct LoginStatus {
	char username[USERNAME_MAXLENGTH];
	int session_id;
    pid_t pid;
    int is_valid;
} CurrentLoggedUsers[100];

pthread_mutex_t mutex;
int last_user_id = 0;
int last_session_id = 0;
int online_user_num = 0;
pthread_t threadId_register, threadId_login, threadId_sendmsg;
struct config server_conf;

void handler(int sig){
	int i;
    /* delete public FIFOs */
	for(i=0; i<PUBLIC_FIFO_NUM; i++){
		unlink(server_conf.FIFO[i]);
	}
    
	exit(1);
}

int init_daemon(void) 
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

struct config get_config(char *filename) 
{
    struct config configstruct;
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
            if( i < PUBLIC_FIFO_NUM ){      /* reading lines of paths */
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

int get_dst_user_id(char* dst_username){
	int i;
	for(i=0; i<last_user_id; i++){
		if(strcmp(RegisteredUsers[i].username, dst_username) == 0){
			return i;
		}
	}
	return -1;
}


/*
    get user's login status
    @return is_logged_in or -1 when failed
*/
int get_dst_user_login_status(char* dst_username)
{
    int user_id = get_dst_user_id(dst_username);
    if(user_id == -1)   /* user does not exists */
        return -1;
    return RegisteredUsers[user_id].is_logged_in;
}

/*
    set user's login status
    @param  dst_username    the name of user whose login status will be set
    @param  status          the status to be set
    @return 0 when successful, -1 when failed
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

int get_dst_user_fifo(char* dst_username, char* dst_user_fifo){  // return value set to the second param
	int user_id = get_dst_user_id(dst_username);
	if(user_id != -1){
		strcpy(dst_user_fifo, RegisteredUsers[user_id].fifo);
		return 0;
	}
	else return -1;
}

int get_dst_session_id(char* dst_username){
	int i;
	for(i=0; i<last_session_id; i++){
		if(strcmp(CurrentLoggedUsers[i].username, dst_username) == 0){
			return i;
		}
	}
	return -1;
}

int get_dst_client_pid(char* dst_username){
	int session_id;
	session_id = get_dst_session_id(dst_username);
	if(session_id != -1){
		return CurrentLoggedUsers[session_id].pid;
	}
	else return -1;
}

/* retrieve message from src client, notice the dest client */
int retrieve_message_from_src_client(SEND_MESSAGE_REQUEST_PTR req){
	char dst_user_fifo[FIFO_NAME_MAXLENGTH];
	pid_t dst_client_pid;
	int fd,status;
	/* open dst client FIFO */
	if(get_dst_user_fifo(req->dst_username, dst_user_fifo) == -1){
		printf("Specified user does not exist!\n");
		return -1;
	}
	fd = open(dst_user_fifo, O_WRONLY);
	if(fd == -1){
		printf("Could not open client FIFO %s for write access\n", dst_user_fifo);
		perror(dst_user_fifo);
		return -1;
	}
	
	/* write message content into dst client FIFO */
	status = write(fd, req, sizeof(SEND_MESSAGE_REQUEST));
    if(status == -1){
        printf("Write into %s failed\n", dst_user_fifo);
        perror(dst_user_fifo);
    }
    else{
    	printf("Message has been written into FIFO %s\n", dst_user_fifo);
	}
    close(fd);
	return 0;
}

int process_sendmsg_request(SEND_MESSAGE_REQUEST_PTR req){
	char dst_user_fifo[FIFO_NAME_MAXLENGTH];
	pid_t dst_client_pid;
	int fd,status, tmp_fifo_fd;
    //tmp_fifo_fd = open_client_tmp_fifo(req->pid);
    
    int dst_user_login_status = get_dst_user_login_status(req->dst_username);
    if( dst_user_login_status == -1 ){  /* user does not exists */
        printf("Specified user does not exist!\n");
        return -1;
    }
    else if( dst_user_login_status == 0 ){ /* user has not logged in */
        // todo: store message in the buffer
    }
    else if( dst_user_login_status == 1 ){ /* user has logged in */
        /* open dst user FIFO */
        if(get_dst_user_fifo(req->dst_username, dst_user_fifo) == -1){
            printf("Specified user does not exist!\n");
            return -1;
        }
        fd = open(dst_user_fifo, O_WRONLY);
        if(fd == -1){
            printf("Could not open client FIFO %s for write access\n", dst_user_fifo);
            perror(dst_user_fifo);
            return -1;
        }
        
        /* write message content into dst user FIFO */
        status = write(fd, req, sizeof(SEND_MESSAGE_REQUEST));
        if(status == -1){
            printf("Write into %s failed\n", dst_user_fifo);
            perror(dst_user_fifo);
        }
        else{
            printf("Message has been written into FIFO %s\n", dst_user_fifo);
        }
        close(fd);
        return 0;
    }
    close(tmp_fifo_fd);
}

void process_login_request(LOGIN_REQUEST_PTR req){
	int i,flag = 0, status;
	LOGIN_RESPONSE response;
    
	/* open client's tmp FIFO */
    int tmp_fifo_fd;
	char tmp_pipename[FIFO_NAME_MAXLENGTH];
	sprintf(tmp_pipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%d_tmp_fifo", req->pid);
	tmp_fifo_fd = open(tmp_pipename, O_WRONLY);
	if(tmp_fifo_fd == -1){
		printf("Could not open %s for write access in process_login_request\n", tmp_pipename);
		perror(tmp_pipename);
		return ;
	}
    
    if(req->type == TYPE_LOGIN){
        // check if currently online user number exceeds max online user number limit
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
                // todo: return logged-in user info
                
                /* lock */
                if(pthread_mutex_lock(&mutex) != 0)
                    perror("lock failed");
                
                strcpy(CurrentLoggedUsers[last_session_id].username, req->username);
                CurrentLoggedUsers[last_session_id].session_id = last_session_id;
                CurrentLoggedUsers[last_session_id].pid = req->pid;
                CurrentLoggedUsers[last_session_id].is_valid = 1;
                RegisteredUsers[i].is_logged_in = 1; /* update user's log-in status in RegisterStatus */
                last_session_id++;
                online_user_num++;
                
                /* unlock */
                if(pthread_mutex_unlock(&mutex) != 0)
                    perror("unlock failed");
                printf("User %s has logged in!\n", req->username);
            }
            else{       /* not found */
                response.status = -1;
                response.session_id = -1;
            }
        }
        /* write response to the client's tmp fifo */
        if((status = write(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE))) == -1){
            printf("Could not write into %s in process_login_request\n", tmp_pipename);
            perror(tmp_pipename);
        }
        else{
            printf("Login response has been written into %s\n", tmp_pipename);
        }
        close(tmp_fifo_fd);
    }
    else if(req->type == TYPE_LOGOUT){
        set_dst_user_login_status(req->username, 0);
        response.status = 0;
        
        /* lock */
        if(pthread_mutex_lock(&mutex) != 0)
            perror("lock failed");
        
        online_user_num--;
        
        /* unlock */
        if(pthread_mutex_unlock(&mutex) != 0)
            perror("unlock failed");
                
        /* open dst user FIFO */
        char dst_user_fifo[FIFO_NAME_MAXLENGTH];
        get_dst_user_fifo(req->username, dst_user_fifo) == -1;
        int user_fifo_fd = open(dst_user_fifo, O_WRONLY);
        if(user_fifo_fd == -1){
            printf("Could not open client FIFO %s for write access\n", dst_user_fifo);
            perror(dst_user_fifo);
        }
        if((status = write(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE))) == -1){
            printf("Could not write into %s in process_login_request\n", dst_user_fifo);
            perror(dst_user_fifo);
        }
        else{
            printf("Logout response has been written into %s\n", dst_user_fifo);
        }
        close(user_fifo_fd);
    }
}

void process_register_request(REGISTER_REQUEST_PTR req){
    int i,flag = 0, status;
    REGISTER_RESPONSE response;
    
    /* open client's tmp FIFO */
    int tmp_fifo_fd;
    char tmp_pipename[FIFO_NAME_MAXLENGTH];
    sprintf(tmp_pipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%d_tmp_fifo", req->pid);
    tmp_fifo_fd = open(tmp_pipename, O_WRONLY);
    if(tmp_fifo_fd == -1){
        printf("Could not open %s for write access in process_register_request\n", tmp_pipename);
        perror(tmp_pipename);
        return ;
    }
    
    /* check if there exists a user with the same username */
    for(int i=0; i<last_user_id; i++){
        if(strcmp(RegisteredUsers[i].username, req->username) == 0){
            flag = 1;
            break;
        }
    }
    if(flag){       /* user exists, register failed */
        /* set response */
        response.status = -1;
    }
    else {          /* user not exists, register a new one  */
        /* lock */
        if(pthread_mutex_lock(&mutex) != 0)
            perror("lock failed");
        
        strcpy(RegisteredUsers[last_user_id].username, req->username);
        strcpy(RegisteredUsers[last_user_id].password, req->password);
        strcpy(RegisteredUsers[last_user_id].fifo, req->myfifo);
        printf("%s %s %s\n", RegisteredUsers[last_user_id].username, RegisteredUsers[last_user_id].password, RegisteredUsers[last_user_id].fifo);
        last_user_id++;
        
        /* unlock */
        if(pthread_mutex_unlock(&mutex) != 0)
            perror("unlock failed");
        
        /* set response */
        response.status = 0;
    }
    /* write response to the client */
    if((status = write(tmp_fifo_fd, &response, sizeof(REGISTER_RESPONSE))) == -1){
        printf("Could not write into %s in process_register_request\n", tmp_pipename);
        perror(tmp_pipename);
    }
    else{
        printf("Register response has been written into %s\n", tmp_pipename);
    }
    close(tmp_fifo_fd);
}

void *check_login_request(void *arg)
{
    process_login_request((LOGIN_REQUEST_PTR)arg);	
    return NULL;
}

void *check_register_request(void *arg)
{
    process_register_request((REGISTER_REQUEST_PTR)arg);
    return NULL;
}

void *check_sendmsg_request(void *arg)
{
    process_sendmsg_request((SEND_MESSAGE_REQUEST_PTR)arg);	
    return NULL;
}

int main(){
	int res_register, res_login, res_message; 
	int i;
	int fifo_fd, fd1;
    int status;
    
	char buffer[100];
	
	//init_daemon(); /* 设置自身为守护进程 */ 
	server_conf = get_config(CONF_FILENAME);
    signal(SIGKILL, handler);
	signal(SIGINT, handler);
	signal(SIGTERM, handler);
    
    if(pthread_mutex_init(&mutex, NULL) != 0)
    {
        perror("Mutex init failed");
        exit(1);
    }
    
	create_public_fifo();
	open_public_fifo();
    
	printf("Server is now running!\n");
    
    struct pollfd *pollfds = (struct pollfd *)malloc(PUBLIC_FIFO_NUM * sizeof(struct pollfd));
	for(i=0; i<PUBLIC_FIFO_NUM; i++){
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
				for(i=0; i<PUBLIC_FIFO_NUM; i++){
					if(pollfds[i].revents & POLLIN == POLLIN){
						switch(i){
							case IDX_REGISTER:
								res_register = read(public_fifo_fd[IDX_REGISTER], &register_info, sizeof(REGISTER_REQUEST));
                                pthread_create(&threadId_register, NULL, &check_register_request, &register_info);
                                pthread_join(threadId_register, NULL);
								break;
							case IDX_LOGIN:
								res_login = read(public_fifo_fd[IDX_LOGIN], &login_info, sizeof(LOGIN_REQUEST));
								pthread_create(&threadId_login, NULL, &check_login_request, &login_info);
                                pthread_join(threadId_login, NULL);
								break;
							case IDX_MESSAGE:
								res_message = read(public_fifo_fd[IDX_MESSAGE], &message_info, sizeof(SEND_MESSAGE_REQUEST));
                                pthread_create(&threadId_sendmsg, NULL, &check_sendmsg_request, &message_info);
                                pthread_join(threadId_sendmsg, NULL);
                                break;
						}
					}
				}
		}
	}
	
	exit(0);
}
