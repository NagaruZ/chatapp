/* chatclient.c */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include "chatclientinfo.h"
#include <poll.h>

#define ANSI_COLOR_RED     "\x1b[31m" 
#define ANSI_COLOR_BRIGHT_GREEN   "\x1b[92m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define BRIGHT_GREEN(string) ANSI_COLOR_BRIGHT_GREEN string ANSI_COLOR_RESET
#define RED(string) ANSI_COLOR_RED string ANSI_COLOR_RESET

char public_fifo_name[PUBLIC_FIFO_NUM][100] = {
	{"/home/zouyufan_2016200087/chatapp/data/zyf_register"}, // handling register requests
	{"/home/zouyufan_2016200087/chatapp/data/zyf_login"}, // handling login requests
	{"/home/zouyufan_2016200087/chatapp/data/zyf_sendmsg"}  // handling messages
};

char mypipename[FIFO_NAME_MAXLENGTH];
int my_fifo_fd = -1;
char login_username[USERNAME_MAXLENGTH] = "";
int session_id = -1;
char buffer[MESSAGE_MAXLENGTH];
struct pollfd pollfds[2];
int tmp_fifo_fd;
char tmp_pipename[FIFO_NAME_MAXLENGTH];

char operation[10];
void get_user_operation();
int logout();
void handler(int sig){	/* remove pipe if signaled */
    if(session_id != -1) logout();
    unlink(mypipename);
    close(tmp_fifo_fd);
    unlink(tmp_pipename);
	exit(1);
}

void ReplaceStrSpace(char* str){
	int len,i;
	len = strlen(str);
	for(i=0; i<len; i++){
		if(str[i] == ' ') str[i] = '_';
	}
}

int register_user(){
	int fifo_fd,res;
	char temp;
	char* my_username = (char*)malloc(USERNAME_MAXLENGTH);
	char* password;
    
	printf("Type your username:");
	scanf("%c", &temp);
	scanf("%[^\n]", my_username);
	ReplaceStrSpace(my_username);
	password = getpass("Type your password: ");
	
	/* create my own FIFO */
	sprintf(mypipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%s_fifo", my_username);
	if(access(mypipename, F_OK) == -1){
		res = mkfifo(mypipename, 0777);
		if(res != 0){
			printf(RED("Private FIFO %s was not created\n"), mypipename);
			perror(mypipename);
			return -1;
		}
	}
	else{
		//printf("This username has been used!\n");
		//return -1;
	}
	
	REGISTER_REQUEST req;
	
	/* open server fifo for writing */
	fifo_fd = open(public_fifo_name[IDX_REGISTER], O_WRONLY);
	if(fifo_fd == -1){
		printf(RED("Could not open public FIFO %s for write access\n"), public_fifo_name[IDX_REGISTER]);
		perror(public_fifo_name[IDX_REGISTER]);
		unlink(mypipename);
		return -1;
	}
	
	/* construct client info */
    req.pid = getpid();
	strcpy(req.username, my_username);
	strcpy(req.password, password);
	strcpy(req.myfifo, mypipename);
	
	/* write client info to server fifo */
	if(write(fifo_fd, &req, sizeof(REGISTER_REQUEST)) == -1){
		printf(RED("Send register request to public FIFO %s failed\n"), public_fifo_name[IDX_REGISTER]);
		close(fifo_fd);
		unlink(mypipename);
		return -1;
	}
	
	close(fifo_fd);
    
    REGISTER_RESPONSE response;
    while(1){
        res = read(tmp_fifo_fd, &response, sizeof(REGISTER_RESPONSE));
        if(res> 0){
            if(response.status == 0){
                printf(BRIGHT_GREEN("Register successfully!\n"));
                break;
            }
            else if(response.status == -1){
                printf(RED("Register failed: this username has been used. Please use another name and try again.\n"));
                break;
            }
        }
    }
	return 0;
}


/* 
	@return 
		session_id when positive
		error when negative
*/
int login(){
	int fifo_fd, res, errn = 0;
	char tmp_pipename[FIFO_NAME_MAXLENGTH];
	char temp;
	char username[USERNAME_MAXLENGTH];
	char* password;
	printf("Type your username:");
	fflush(stdout);
	scanf("%c", &temp);
	scanf("%[^\n]", username);
	ReplaceStrSpace(username);
	password = getpass("Type your password: ");
	
	/* open server fifo for writing */
	fifo_fd = open(public_fifo_name[IDX_LOGIN], O_WRONLY);
	if(fifo_fd == -1){
		printf(RED("Could not open %s for write access\n"), public_fifo_name[IDX_LOGIN]);
		perror(public_fifo_name[IDX_LOGIN]);
		errn = -1;
		goto LOGIN_ERR_HANDLER;
	}
	
	LOGIN_REQUEST req;
	/* construct client info */
	strcpy(req.username, username);
	strcpy(req.password, password);
	req.pid = getpid();
	
	/* create temp fifo to receive response from server */
	sprintf(tmp_pipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%d_tmp_fifo", req.pid);
	if(access(tmp_pipename, F_OK) == -1){
		res = mkfifo(tmp_pipename, 0777);
		if(res != 0){
			printf(RED("Temp FIFO %s was not created\n"), tmp_pipename);
			perror(tmp_pipename);
			errn = -2;
			goto LOGIN_ERR_HANDLER;
		}
	}
    
    /* open client's tmp FIFO */
	tmp_fifo_fd = open(tmp_pipename, O_RDONLY | O_NONBLOCK);
	if(tmp_fifo_fd == -1){
		printf(RED("Could not open %s for read only access\n"), tmp_pipename);
		perror(tmp_pipename);
		errn = -3;
		goto LOGIN_ERR_HANDLER;
	}
	
	
	/* write client info to server fifo */
	if(write(fifo_fd, &req, sizeof(LOGIN_REQUEST)) == -1){
		printf(RED("Send login request to public FIFO %s failed\n"), public_fifo_name[IDX_LOGIN]);
	}
	else{
		/* get result from server through client's tmp FIFO */
		LOGIN_RESPONSE response;
		while(1){
			res = read(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE));
			if(res> 0){
				if(response.status == 0){
					printf(BRIGHT_GREEN("Login successfully!\n"));
					strcpy(login_username, username); 
					session_id = response.session_id;
					sprintf(mypipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%s_fifo", username);
					if(access(mypipename, F_OK) == -1){
						res = mkfifo(mypipename, 0777);
						if(res != 0){
							printf(RED("Private FIFO %s was not created\n"), mypipename);
							perror(mypipename);
							return -1;
						}
					}
					/* open user fifo, preparing for receiving server-forwarded messages */
					my_fifo_fd = open(mypipename, O_RDONLY | O_NONBLOCK);
					if(my_fifo_fd == -1){
						printf(RED("Unable to open %s\n"), mypipename);
						perror(mypipename);
					}
					pollfds[1].fd = my_fifo_fd;        
					pollfds[1].events = POLLIN|POLLPRI;
					close(fifo_fd);
					return 0;
					break;
				}
                else if(response.status == -2){ /* reached max online user number limit */
                    printf(RED("Login failed: online user number has reached the limit. Please try again later.\n"));
					errn = -4;
					goto LOGIN_ERR_HANDLER;
                }
				else{
					printf(RED("Login failed. Please try again.\n"));
					errn = -4;
					goto LOGIN_ERR_HANDLER;
				}
			}
		}
	}
	
LOGIN_ERR_HANDLER:
	switch(errn){
		case -4:
		case -3:
			close(tmp_fifo_fd);
		case -2:
			//unlink(tmp_pipename);
		case -1:
			close(fifo_fd);
			return -1;
	}
}

int logout()
{
    LOGIN_REQUEST req;
    LOGIN_RESPONSE response;
    
    req.type = TYPE_LOGOUT;
    strcpy(req.username, login_username);
    req.pid = getpid();
    
    /* open server fifo for writing */
	int fifo_fd = open(public_fifo_name[IDX_LOGIN], O_WRONLY);
    if(write(fifo_fd, &req, sizeof(LOGIN_REQUEST)) == -1){
		printf(RED("Send logout request to public FIFO %s failed\n"), public_fifo_name[IDX_LOGIN]);
	}
	else{
        while(1){
			int res = read(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE));
			if(res> 0){
				if(response.status == 0){
                    return 0;
                }
            }
        }
        printf("Logout failed.\n");
    }
}

int send_message(){
	int fifo_fd;
    char type;
	char temp;
	char dst_username[USERNAME_MAXLENGTH];
	char message[MESSAGE_MAXLENGTH];
	if(session_id == -1){
		printf("Please login first!\n");
		return -1;
	}
    printf("Type 's' to send your message to a single user, or 'm' to mutiple users.\n");
    printf("> ");
    scanf("%c", &temp);
    scanf("%c", &type);
    if(type == 's'){
        printf("To whom do you want to send a message?\n");
        printf("> ");
        scanf("%c", &temp);
        scanf("%[^\n]", dst_username);
        ReplaceStrSpace(dst_username);
        printf("Type your message.\n");
        printf("> ");
        fflush(stdout);
        scanf("%c", &temp);
        scanf("%[^\n]", message);
        
        /* check if server fifo exists */
        if(access(public_fifo_name[IDX_MESSAGE], F_OK) == -1){
            printf(RED("Could not open FIFO %s\n"), public_fifo_name[IDX_MESSAGE]);
            exit(EXIT_FAILURE);
        }
        
        /* open server fifo for write */
        fifo_fd = open(public_fifo_name[IDX_MESSAGE], O_WRONLY);
        if(fifo_fd == -1){
            printf(RED("Could not open %s for write access\n"), public_fifo_name[IDX_MESSAGE]);
            exit(EXIT_FAILURE);
        }
        
        SEND_MESSAGE_REQUEST req;
        /* construct client info */
        strcpy(req.src_username, login_username);
        strcpy(req.dst_username, dst_username);
        strcpy(req.message, message);
        req.pid = getpid();
        
        /* write client info to server fifo */
        if(write(fifo_fd, &req, sizeof(SEND_MESSAGE_REQUEST)) == -1){
            printf(RED("Send message to public FIFO %s failed\n"), public_fifo_name[IDX_MESSAGE]);
        }
        else{
            printf(BRIGHT_GREEN("Message has been sent!\n"));
        }
        close(fifo_fd);
    }
    else if(type == 'm'){
        int user_num = 0;
        char dst_usernames[100][USERNAME_MAXLENGTH];
        
        printf("Type your message.\n");
        printf("> ");
        fflush(stdout);
        scanf("%c", &temp);
        scanf("%[^\n]", message);
        //fgets(dst_usernames[user_num], MESSAGE_MAXLENGTH, stdin);
        
        printf("How many users do you want to send a message to?\n");
        printf("> ");
        fflush(stdout);
        scanf("%d", &user_num);
        printf("Type users that you want to send a message to, with a name in a line.\n");
        for(int i=0; i<user_num; i++)
        {    
            scanf("%c", &temp);
            scanf("%[^\n]", dst_usernames[i]);
            ReplaceStrSpace(dst_usernames[i]);
        }
        
        /* check if server fifo exists */
        if(access(public_fifo_name[IDX_MESSAGE], F_OK) == -1){
            printf(RED("Could not open FIFO %s\n"), public_fifo_name[IDX_MESSAGE]);
            exit(EXIT_FAILURE);
        }
        
        /* open server fifo for write */
        fifo_fd = open(public_fifo_name[IDX_MESSAGE], O_WRONLY);
        if(fifo_fd == -1){
            printf(RED("Could not open %s for write access\n"), public_fifo_name[IDX_MESSAGE]);
            exit(EXIT_FAILURE);
        }
        
        for(int i=0; i<user_num; i++){
            SEND_MESSAGE_REQUEST req;
            /* construct client info */
            strcpy(req.src_username, login_username);
            strcpy(req.dst_username, dst_usernames[i]);
            strcpy(req.message, message);
            req.pid = getpid();
            
            /* write client info to server fifo */
            if(write(fifo_fd, &req, sizeof(SEND_MESSAGE_REQUEST)) == -1){
                printf(RED("Send message to public FIFO %s failed\n"), public_fifo_name[IDX_MESSAGE]);
            }
            else{
                printf(BRIGHT_GREEN("Message has been sent to %s.\n"), dst_usernames[i]);
            }
        }
        close(fifo_fd);
    }
    else {
        printf("Sorry, I don't understand this. Please try again.\n");
    }
	return 0;
}

int main(int argc, char *argv[]){
	int res; int status;
	SEND_MESSAGE_REQUEST req;
    
	/* handle some signals */
	signal(SIGKILL, handler);
	signal(SIGINT, handler);
	signal(SIGTERM, handler);
	
    /* create client-owned temp fifo to receive response from server */
	sprintf(tmp_pipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%d_tmp_fifo", getpid());
    
	if(access(tmp_pipename, F_OK) == -1){
		res = mkfifo(tmp_pipename, 0777);
		if(res != 0){
			printf(RED("Temp FIFO %s was not created\n"), tmp_pipename);
			perror(tmp_pipename);
			unlink(tmp_pipename);
		}
	}
	tmp_fifo_fd = open(tmp_pipename, O_RDONLY | O_NONBLOCK);
	if(tmp_fifo_fd == -1){
		printf(RED("Could not open %s for read only access\n"), tmp_pipename);
		perror(tmp_pipename);
        close(tmp_fifo_fd);
        unlink(tmp_pipename);
	}
    
	pollfds[0].fd = fileno(stdin);        
	pollfds[0].events = POLLIN|POLLPRI;
	pollfds[1].fd = -1;
	printf( "Welcome!\n");
	printf("I am process %d.\n", getpid());
	printf("Type 'r' to register, 'l' to login, 'm' to send messages, or 'q' to quit.\n");
	printf("> ");
	fflush(stdout);
	while(1){
		switch(poll(pollfds, 2, -1)){
			case -1:
				perror("poll()");
				break;
			case 0:
				break;
			default:
				if(pollfds[0].revents & POLLIN == POLLIN){	// stdin is readable
					scanf("%s", operation);
					if((strcmp(operation, "r") == 0)){
						if(session_id == -1){
							if(register_user() == -1){
								printf(RED("Register failed.")"\n");
							}
						}
						else{
							printf("You have logged in!\n");
						}
					}
					else if((strcmp(operation, "l") == 0)){
						if(session_id == -1){
							printf("Welcome to login!\n");
							login();
						}
						else{
							printf("You have logged in!\n");
						}
					}
					else if((strcmp(operation, "m") == 0)){
						send_message();
					}
					else if((strcmp(operation, "q") == 0)){
                        printf("Goodbye.\n");
                        // todo: log out from the server
                        if(session_id != -1){
                            logout();
                            close(tmp_fifo_fd);
                            unlink(tmp_pipename);
                        }
						close(my_fifo_fd);
						(void)unlink(mypipename);
						exit(0);
					}
					else{
						printf("Sorry, I don't understand this. Please try again.\n");
					}
					if(session_id == -1){
						printf("Type 'r' to register, 'l' to login, 'm' to send messages, or 'q' to quit.\n");
					}
					else{
						printf("Type 'm' to send messages, or 'q' to quit.\n");
					}
					printf("> ");
					fflush(stdout);
				}
				if(pollfds[1].revents & POLLIN == POLLIN){	// user FIFO is readable
					status = read(my_fifo_fd, &req, sizeof(SEND_MESSAGE_REQUEST));
					if(status> 0){
						printf( BRIGHT_GREEN("\n\nYou receive a message from %s: \n\t%s\n\n"), req.src_username, req.message);
						printf("Type 'm' to send messages, or 'q' to quit.\n");
						printf("> ");
						fflush(stdout);
						break;
					}
				}
		}
	}
	exit(0);
}
	

