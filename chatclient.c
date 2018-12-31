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
	{"/home/zouyufan_2016200087/chatapp/data/zyf_register"},    // handling register requests
	{"/home/zouyufan_2016200087/chatapp/data/zyf_login"},       // handling login requests
	{"/home/zouyufan_2016200087/chatapp/data/zyf_sendmsg"}      // handling messages
};
int public_fifo_fd[PUBLIC_FIFO_NUM];

char user_pipename[FIFO_NAME_MAXLENGTH];
int user_fifo_fd = -1;
char login_username[USERNAME_MAXLENGTH];
int session_id = -1;

struct pollfd pollfds[POLLFDS_NUM];
int tmp_fifo_fd;
char tmp_pipename[FIFO_NAME_MAXLENGTH];

/* function definitions */
void handler(int sig);
void replace_str_space(char* str);
void read_username_password(char **username, char **password);
void open_public_fifo();
void create_tmp_fifo();
void open_tmp_fifo();
int register_user();
int login();
int logout();
int send_message();

void handler(int sig){	/* remove pipe if signaled */
    printf("\nGoodbye.\n");
    if(session_id != -1) logout();
    unlink(user_pipename);
    close(tmp_fifo_fd);
    unlink(tmp_pipename);
	exit(1);
}

void replace_str_space(char* str){
	int len,i;
	len = strlen(str);
	for(i=0; i<len; i++){
		if(str[i] == ' ') str[i] = '_';
	}
}

void read_username_password(char **username, char **password)
{
    char temp;
    printf("Type your username: ");
	scanf("%c", &temp);
	scanf("%[^\n]", *username);
	replace_str_space(*username);
	*password = getpass("Type your password: ");
}

void open_public_fifo()
{
	/* open public FIFO for reading */
    // notice: should be block write
	for(int i=0; i<PUBLIC_FIFO_NUM; i++){
		public_fifo_fd[i] = open(public_fifo_name[i], O_WRONLY | O_NONBLOCK);
		if(public_fifo_fd[i] == -1){
			printf("Could not open public FIFO %s for read only access\n", public_fifo_name[i]);
			perror(public_fifo_name[i]);
			exit(EXIT_FAILURE);
		}
	}
}

void create_tmp_fifo()
{
    /* create client-owned tmp FIFO to receive response from server before user login */
	sprintf(tmp_pipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%d_tmp_fifo", getpid());
	if(access(tmp_pipename, F_OK) == -1){
		int res = mkfifo(tmp_pipename, 0777);
		if(res != 0){
			printf(RED("Temp FIFO %s was not created\n"), tmp_pipename);
			perror(tmp_pipename);
			unlink(tmp_pipename);
            exit(res);
		}
	}
}

void open_tmp_fifo()
{
    /* open client's tmp FIFO */
	tmp_fifo_fd = open(tmp_pipename, O_RDONLY | O_NONBLOCK);
	if(tmp_fifo_fd == -1){
		printf(RED("Could not open %s for read only access\n"), tmp_pipename);
		perror(tmp_pipename);
        close(tmp_fifo_fd);
        unlink(tmp_pipename);
        exit(-1);
	}
}

int register_user(){
	int res;
	char* my_username = (char*)malloc(USERNAME_MAXLENGTH);
	char* password;
    char new_user_pipename[FIFO_NAME_MAXLENGTH];
    
    read_username_password(&my_username, &password);
	
	REGISTER_REQUEST req;
	
	/* construct register request */
    req.pid = getpid();
	strcpy(req.username, my_username);
	strcpy(req.password, password);
	strcpy(req.myfifo, new_user_pipename);
	
	/* write register request to public FIFO */
	if(write(public_fifo_fd[IDX_REGISTER], &req, sizeof(REGISTER_REQUEST)) == -1){
		printf(RED("Send register request to public FIFO %s failed\n"), public_fifo_name[IDX_REGISTER]);
		return -1;
	}
    
    /* read response from client's tmp FIFO */
    // notice: should be block read
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
		(positive) session_id on success
		(negative) error on fail
*/
int login(){
	int res;
	char* username = (char*)malloc(USERNAME_MAXLENGTH);
	char* password;
    read_username_password(&username, &password);
	
	/* construct login request */
    LOGIN_REQUEST req;
	strcpy(req.username, username);
	strcpy(req.password, password);
    req.type = TYPE_LOGIN;
	req.pid = getpid();
	
	/* write login request to server fifo */
	if(write(public_fifo_fd[IDX_LOGIN], &req, sizeof(LOGIN_REQUEST)) == -1){
		printf(RED("Send login request to public FIFO %s failed\n"), public_fifo_name[IDX_LOGIN]);
	}
	else{
		/* get result from server through client's tmp FIFO */
        // notice: should be block read
		LOGIN_RESPONSE response;
		while(1){
			res = read(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE));
			if(res> 0){
				if(response.status == 0){
					printf(BRIGHT_GREEN("Login successfully!\n"));
                    /* update login status in client */
					strcpy(login_username, username); 
					session_id = response.session_id;
                    /* create user FIFO */
					sprintf(user_pipename, "/home/zouyufan_2016200087/chatapp/data/chat_client_%s_fifo", username);
					if(access(user_pipename, F_OK) == -1){
						res = mkfifo(user_pipename, 0777);
						if(res != 0){
							printf(RED("User FIFO %s was not created\n"), user_pipename);
							perror(user_pipename);
							return -1;
						}
					}
					/* open user FIFO, preparing for receiving server-forwarded messages */
					user_fifo_fd = open(user_pipename, O_RDONLY | O_NONBLOCK);
					if(user_fifo_fd == -1){
						printf(RED("Unable to open %s\n"), user_pipename);
						perror(user_pipename);
					}
                    /* update pollfds */
					pollfds[1].fd = user_fifo_fd;        
					pollfds[1].events = POLLIN|POLLPRI;
                    //pollfds[2].fd = tmp_fifo_fd;        
					//pollfds[2].events = POLLIN|POLLPRI;
					return 0;
					break;
				}
                else if(response.status == -2){ /* reached max online user number limit */
                    printf(RED("Login failed: online user number has reached the limit. Please try again later.\n"));
					return -1;
                }
                else if(response.status == -3){
                    printf(RED("Login failed: unsupported request type.\n"));
					return -1;
                }
				else{
					printf(RED("Login failed: unknown problem. Please try again.\n"));
					return -1;
				}
			}
		}
	}
}

int logout()
{
    LOGIN_REQUEST req;
    LOGIN_RESPONSE response;
    
    /* construct logout request */
    req.type = TYPE_LOGOUT;
    strcpy(req.username, login_username);
    req.pid = getpid();
    
    /* write logout request to public FIFO */
    if(write(public_fifo_fd[IDX_LOGIN], &req, sizeof(LOGIN_REQUEST)) == -1){
		printf(RED("Send logout request to public FIFO %s failed\n"), public_fifo_name[IDX_LOGIN]);
	}
	else{
        /* read logout response from server through client's tmp FIFO */
        // notice: should be block read
        while(1){
			int res = read(tmp_fifo_fd, &response, sizeof(LOGIN_RESPONSE));
			if(res> 0){
				if(response.status == 0){
                    return 0;
                }
                else
                    break;
            }
        }
        printf("Logout failed.\n");
    }
}

int send_message(){
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
        replace_str_space(dst_username);
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

        /* construct send_message request */
        SEND_MESSAGE_REQUEST req;
        strcpy(req.src_username, login_username);
        strcpy(req.dst_username, dst_username);
        strcpy(req.message, message);
        req.pid = getpid();
        
        /* write send_message request to public FIFO */
        if(write(public_fifo_fd[IDX_MESSAGE], &req, sizeof(SEND_MESSAGE_REQUEST)) == -1){
            printf(RED("Send message to public FIFO %s failed\n"), public_fifo_name[IDX_MESSAGE]);
        }
        else{
            printf(BRIGHT_GREEN("Message has been sent!\n"));
        }
    }
    else if(type == 'm'){
        int user_num = 0;
        char dst_usernames[100][USERNAME_MAXLENGTH];
        
        printf("Type your message.\n");
        printf("> ");
        fflush(stdout);
        scanf("%c", &temp);
        scanf("%[^\n]", message);
        
        printf("How many users do you want to send a message to?\n");
        printf("> ");
        fflush(stdout);
        scanf("%d", &user_num);
        printf("Type users that you want to send a message to, with a name in a line.\n");
        for(int i=0; i<user_num; i++)
        {    
            scanf("%c", &temp);
            scanf("%[^\n]", dst_usernames[i]);
            replace_str_space(dst_usernames[i]);
        }
        
        for(int i=0; i<user_num; i++){
            /* construct send_message request */
            SEND_MESSAGE_REQUEST req;
            strcpy(req.src_username, login_username);
            strcpy(req.dst_username, dst_usernames[i]);
            strcpy(req.message, message);
            req.pid = getpid();
            
            /* write send_message request to public FIFO */
            if(write(public_fifo_fd[IDX_MESSAGE], &req, sizeof(SEND_MESSAGE_REQUEST)) == -1){
                printf(RED("Send message to public FIFO %s failed\n"), public_fifo_name[IDX_MESSAGE]);
            }
            else{
                printf(BRIGHT_GREEN("Message has been sent to %s.\n"), dst_usernames[i]);
            }
        }
    }
    else {
        printf("Sorry, I don't understand this. Please try again.\n");
        return -1;
    }
	return 0;
}

int main(int argc, char *argv[]){
    
    /* open all public FIFO for future use */
    open_public_fifo();
    
	/* handle some signals */
	signal(SIGKILL, handler);
	signal(SIGINT, handler);
	signal(SIGTERM, handler);
	
    /* 
        create client-owned tmp FIFO and open it
        to receive response from server before user login 
    */
	create_tmp_fifo();
	open_tmp_fifo();
    
	pollfds[0].fd = fileno(stdin);        
	pollfds[0].events = POLLIN|POLLPRI;
	pollfds[1].fd = -1;         /* preserved for user FIFO fd after login */
    pollfds[2].fd = tmp_fifo_fd;         /* preserved for client's tmp FIFO fd after login */
    pollfds[2].events = POLLIN|POLLPRI;
	printf("Welcome!\n");
	printf("I am process %d.\n", getpid());
	printf("Type 'r' to register, 'l' to login, or 'q' to quit.\n");
	printf("> ");
	fflush(stdout);
    
    char operation[10];
	while(1){
		switch(poll(pollfds, POLLFDS_NUM, -1)){
			case -1:
				perror("poll()");
				break;
			case 0:
				break;
			default:
				if(pollfds[0].revents & POLLIN){	// stdin is readable
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
                        // log out from the server
                        if(session_id != -1){
                            logout();
                            close(user_fifo_fd);
                            unlink(user_pipename);
                        }
						close(tmp_fifo_fd);
                        unlink(tmp_pipename);
						exit(0);
					}
					else{
						printf("Sorry, I don't understand this. Please try again.\n");
					}
					if(session_id == -1){
						printf("Type 'r' to register, 'l' to login,  or 'q' to quit.\n");
					}
					else{
						printf("Type 'm' to send messages, or 'q' to quit.\n");
					}
					printf("> ");
					fflush(stdout);
				}
				if(pollfds[1].revents & POLLIN){	// user FIFO is readable
                    SEND_MESSAGE_REQUEST req;
					int status = read(user_fifo_fd, &req, sizeof(SEND_MESSAGE_REQUEST));
					if(status > 0){
						printf( BRIGHT_GREEN("\n\nYou receive a message from %s: \n\t%s\n\n"), req.src_username, req.message);
						printf("Type 'm' to send messages, or 'q' to quit.\n");
						printf("> ");
						fflush(stdout);
						break;
					}
				}
                if(pollfds[2].revents & POLLIN){	// client's tmp FIFO is readable
                    STATE_UPDATE_NOTICE notice;
					int status = read(tmp_fifo_fd, &notice, sizeof(STATE_UPDATE_NOTICE));
					if(status > 0){
                        printf("\n\nCurrently online user number: %d\n", notice.online_user_num);
                        printf("They are:\n\t");
						for(int i=0; i<notice.online_user_num; i++) {
                            printf("%s ", notice.username_list[i]);
                        }
                        printf("\n\n");
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
	

