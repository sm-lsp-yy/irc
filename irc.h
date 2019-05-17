#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "unpthread.h"
#include <unp.h>
#include "log.h"
#include "reply.h"
#define MAX_CHANNEL_PER_CLI 10
#define MAX_CLI_PER_CHANNEL 20
#define MAX_CHANNEL_POSSIBLE 100
#define MAX_NICK_LEN 16
#define MAX_LINE 512
#define MAX_MSG_TOKENS 10
#define MAX_MSG_LEN 512

#define	F_CONNECTING	1	/* connect() in progress */
#define	F_READING		2	/* connect() complete; now reading */
#define	F_DONE			4	/* all done */
#define	F_JOINED		8	/* main has pthread_join'ed */
typedef struct {
  pthread_t		thread_tid;		/* thread ID */
  long			thread_count;	/* # connections handled */
} Thread;
Thread	*tptr;		/* array of Thread structures; calloc'ed */
#define MAXFILES 100
int				listenfd, nthreads;
socklen_t		addrlen;
pthread_mutex_t	mlock;
typedef struct s_client{
	int clientfd;
	char nickname[16];
	char username[16];
	char hostname[16];
	char servname[16];
	char realname[16];
	int channel_id[MAX_CHANNEL_PER_CLI];
	int nick_is_set;
	int user_is_set;
    char mode;//a离开模式/操作员模式o
    char awaymsg[128];//TODO
} client_t;

typedef struct Threadpara{
    
}Threadpara;

typedef struct s_channel{
	char name[16];
	int channel_id;
	char type;//初始化为= "@" is used for secret channels, "*" for private channels, and "=" for others (public channels)
	int connected_clients[MAX_CLI_PER_CHANNEL];//TODO 不知道有用吗
	int clientfd_list[MAX_CLI_PER_CHANNEL];//todo 不管有客户进来离开，其他客户都保持原来的位置，顺序不能改变 初始化为-1
	int permission[MAX_CLI_PER_CHANNEL];//0:无 1:o 2:v 3:o+v
    char channel_mode;//m:审核 t:主题
	int isActive;
	int client_count;
	char topic[16];
} channel_t;

typedef struct s_pool {
	int maxfd; 		// largest descriptor in sets
	fd_set read_set; 	// all active read descriptors
	fd_set write_set; 	// all active write descriptors
	fd_set ready_set;	// descriptors ready for reading
	int nready;		// return of select()
	int maxi;		// hignwater index into client array
	int added_connfd[FD_SETSIZE];
	client_t client[FD_SETSIZE];
} pool;

client_t *client_list[FD_SETSIZE];/* The global client list *///但是在main中声明了静态变量pool里面已经有clientlist
unsigned long client_count;/* The global client count of the server */
channel_t *channel_list[MAX_CHANNEL_POSSIBLE];
unsigned long channel_count;


/*get listen fd*/
int open_listenfd(int port) {
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
 
    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int)) < 0) 
		return -1;

    /* Listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0) 
		return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)	
		return -1;
    return listenfd;
}

/*已经注册过 1 未注册 0 没有连接 -1*/
int have_register(int connfd){
	if(client_list[connfd]==NULL)
		return -1;
	if(client_list[connfd]->nick_is_set==1&&client_list[connfd]->user_is_set==1)
		return 1;
	else
		return 0;
}

/* 查找client是否存在 */
int client_exist(char *nick,int *fd){
	int i;
	for (i = 0; i < FD_SETSIZE; i++){
		if(client_list[i] != NULL){
			if(strcmp(client_list[i]->nickname, "*") != 0){
				if(strcmp(client_list[i]->nickname, nick)==0){
					*fd=i;
					return 1;
				}
			}
		}
	}
  	return 0;
}

/* 查看client_list中有无nick */
int get_nick(int fd,char *nick){
	int i;
	for(i=0;i<FD_SETSIZE;i++){
		if(client_list[i]==NULL)continue;
		if(client_list[i]->clientfd==fd){
			if(client_list[i]->nick_is_set==1){
				strncpy(nick,client_list[i]->nickname,MAX_NICK_LEN);
				return 0;
			}	
			else
				return -1;/*该client没有设置nick*/
		}
	}
	return -2;/*client_list中没有对应这个fd的client*/
}

/*去掉尾部的\r\n“*/
size_t get_msg(char *buf, char *msg){
    char *end;
    int  len;
    /* Find end of message */
    end = strstr(buf, "\r\n");
    if(end)
        len = end - buf + 2;
    else{
        /* Could not find \r\n, try searching only for \n */
        end = strstr(buf, "\n");
		if(end)
	    	len = end - buf + 1;
		else
	    	return -1;
    }
    /* found a complete message */
    memcpy(msg, buf, len);
    msg[end-buf] = '\0';
    return len;	
}

/* 获取client */
client_t *get_client(int connfd){
  	return client_list[connfd];
}

/* 连接字符串 */
char* join(char *s1, char *s2){
    char *result = malloc(strlen(s1)+strlen(s2)+1);//+1 for the zero-terminator
    if (result == NULL) exit (1);
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

/* 获取channel */
channel_t *get_channel(char *channel_name){
	int i;
	for(i = 0; i < MAX_CHANNEL_POSSIBLE; i++){//change
		if(channel_list[i] != NULL){
			if(strcmp(channel_list[i]->name, channel_name) == 0)
				return channel_list[i];
		}
	}
	return NULL;
}

/* 切分msg函数 */
int tokenize( char const *in_buf, char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1], int *flag_topic ){
    int i = 0;
    const char *current = in_buf;
    int  done = 0;

    /* Possible Bug: handling of too many args */
    while (!done && (i<MAX_MSG_TOKENS)) {
        char *next = strchr(current, ' ');
		if (next) {
			memcpy(tokens[i], current, next-current);
			tokens[i][next-current] = '\0';
			current = next + 1;   /* 跳过空格*/
			++i;

			/* trailing token */
			if (*current == ':') {
				++current;
				strcpy(tokens[i], current);
				++i;
				done = 1;
				*flag_topic = 1;
			}
		} else {
			strcpy(tokens[i], current);
			++i;
			done = 1;
		}
    }
    return i;
}

/* 发送ERR_CANNOTSENDTOCHANNEL */
int send_err_cannotsendtochan(client_t *cli,char *channel_name)
{
	char buf[MAX_MSG_LEN];
	char myhostname[256];
	gethostname(myhostname,256);
	sprintf(buf,":%s %s %s %s :%s\r\n",myhostname,ERR_CANNOTSENDTOCHAN,
										cli->nickname,channel_name,
										"Cannot send to channel");
	write(cli->clientfd, buf, strlen(buf));
	return 0;
}

/* 发送RPL_AWAY */
int send_rpl_away(client_t *cli,char *awaymsg){
	char buf[MAX_MSG_LEN];
	char myhostname[256];
	gethostname(myhostname,256);
	snprintf(buf,MAX_MSG_LEN,":%s %s %s :%s\r\n",myhostname,RPL_AWAY,
										cli->nickname,
										awaymsg);
	write(cli->clientfd, buf, strlen(buf));
	return 0;
}

/* 发送PRIVMSG */
int send_privmsg(int connfd,int targetfd,char *msg){
	char buf[MAX_MSG_LEN];
	char targetnick[MAX_NICK_LEN];
	get_nick(targetfd,targetnick);
	int curlen,i,j=0;
    while(j<strlen(msg)){
        bzero(buf,MAX_MSG_LEN);
        sprintf(buf,":%s PRIVMSG %s :",client_list[connfd]->hostname,targetnick);
        curlen=strlen(buf);//此处不考虑前缀过长的情况(基本不会发生），or事先规定hostname一个较小数值？//if(curlen>=MAX_MSG_LEN-2)
        for(i=curlen;i<MAX_MSG_LEN-2&&j<strlen(msg);i++,j++){
            buf[i]=msg[j];
        }
        buf[i]='\r';
        buf[i+1]='\n';
		write(targetfd, buf, strlen(buf));
    }
	return 0;
}

int del_prefix(char *target){
	int i,len;
	len=strlen(target);
	if(len<1)return -1;
	for(i=0;i<len-1;i++){
		target[i]=target[i+1];
	}
	target[i]='\0';
	return 0;
}

int find_client_id(int fd,int *array){
	int i=0;
	for(i=0;i<sizeof(array);i++){
		if(fd==array[i])
			return i;
	}
	return -1;
}

/* 找到channel list中的空位 */
int find_blank_in_clientfd_list(int clients[MAX_CLI_PER_CHANNEL]){
    int i = 0;
    for(i; i < MAX_CLI_PER_CHANNEL; i++){
        if(clients[i] == -1)
            return i;
    }
    return -1;
}

/* 找到用户的channel list空位 */
int find_blank_in_channelid_array(int channel_id[MAX_CHANNEL_PER_CLI]){
    int i = 0;
    for(i; i < MAX_CHANNEL_POSSIBLE; i++){
        if(channel_id[i] == -1)
            return i;
    }
    return -1;
}

/* create a channel */
channel_t *create_channel(char *channel_name){
    int i;
    for(i = 0; i < MAX_CHANNEL_POSSIBLE; i++){
        if(channel_list[i]==NULL){
            /* init channel */
			channel_list[i]=Malloc(sizeof(channel_t));
            channel_list[i]->channel_id = i+1;
            memset(channel_list[i]->clientfd_list, -1, sizeof(channel_list[i]->clientfd_list));
            channel_list[i]->isActive = 0;
			memset(channel_list[i]->permission, 0, sizeof(channel_list[i]->permission));//lspchange
            strcpy(channel_list[i]->name, channel_name);
            channel_list[i]->client_count = 0;
			channel_list[i]->channel_mode = '0';
			channel_list[i]->type='=';
            memset(channel_list[i]->topic, 0, sizeof(channel_list[i]->topic));
			channel_count++;
            return channel_list[i];
        }    
    }
    return NULL;
}

/* part channel后对channel和client list做的修改 */
int leave_channel(int connfd, char *channel_name){
    channel_t *curchannel = get_channel(channel_name);
    client_t *curclient = get_client(connfd);
    if(curchannel == NULL || curclient == NULL)
        return -1;

    /* 更新channel_list */
    int i,j;
    if(strcmp(curchannel->name, channel_name) == 0){
        for(j = 0; j < MAX_CLI_PER_CHANNEL; j++){
            if(curchannel->clientfd_list[j] == curclient->clientfd){
                curchannel->clientfd_list[j] = -1;
				curchannel->permission[j] = 0;
                curchannel->client_count--;
                /* 如果channel没有人了,销毁 */
                if(curchannel->client_count == 0)
					free(curchannel);
                break;
            }
        }
    }
	/* client进行修改 */
    for(j = 0; j < MAX_CHANNEL_POSSIBLE; j++){
        if(curclient->channel_id[j] == curchannel->channel_id){
            curclient->channel_id[j] = -1; 
            break;
        }
    }
    return 1;
}

/* 修改channel中的topic */
int change_topic(channel_t *channel, char* topic){
    int i;
    for(i = 0; i < MAX_CHANNEL_POSSIBLE; i++){
		if(channel_list[i] == NULL)
			continue;
        if(channel_list[i]->channel_id == channel->channel_id){
            memset(channel_list[i]->topic, 0, sizeof(channel_list[i]->topic));
            if(topic != NULL)
                strcpy(channel_list[i]->topic, topic);
        }
    }
	return -1;
}

int msg_of_the_day(client_t *client,FILE *fp){
	int max_text_line=80;
	char hostname [256];
	char buf[MAX_MSG_LEN];
	char buf1[MAX_MSG_LEN];
	char buf2[MAX_MSG_LEN];
	char text[max_text_line];
	
	gethostname(hostname,256);
	
	snprintf(buf1,MAX_MSG_LEN,":%s 375 %s :- %s Message of the day -\r\n", hostname, client->nickname,hostname);
	write(client->clientfd, buf1, strlen(buf1));

	void* ret;
    while(1) {
		/* 逐行发送motd.txt内容 */
       	bzero(text,max_text_line);
		bzero(buf,MAX_MSG_LEN);
        ret = fgets(text,max_text_line,fp);
        if(ret==NULL) break;
		text[strlen(text)-1]='\0';//去掉多余的\n
		
		snprintf(buf,MAX_MSG_LEN,":%s 372 %s :- %s\r\n",hostname, client->nickname,text);
		write(client->clientfd, buf, strlen(buf));
    }

	snprintf(buf2,MAX_MSG_LEN,":%s 376 %s :End of MOTD command.\r\n", hostname, client->nickname);
	write(client->clientfd, buf2, strlen(buf2));
	return 0;
}

//根据channel_id找channel结构体
channel_t *find_channel(int id){
	int i;
	for(i=0;i<MAX_CHANNEL_POSSIBLE;i++){
		if(channel_list[i]==NULL)continue;
		if(channel_list[i]->channel_id==id)
			return channel_list[i];
	}
	return NULL;
}

int match_in_array(int item,int *array){
	int i;
	for(i=0;i<sizeof(array);i++){
		if(array[i]==item)
			return 1;
	}
	return 0;
}

int send_err_umodeunknownflag(client_t *cli){
	char buf[MAX_MSG_LEN];
	char myhostname[256];
	gethostname(myhostname,256);
	snprintf(buf,MAX_MSG_LEN,":%s %s %s :%s\r\n",myhostname,ERR_UMODEUNKNOWNFLAG,
										cli->nickname,
										"Unknown Mode Flag");
	write(cli->clientfd, buf, strlen(buf));
	return 0;
}

int send_err_usersdontmatch(client_t *cli){
	char buf[MAX_MSG_LEN];
	char myhostname[256];
	gethostname(myhostname,256);
	snprintf(buf,MAX_MSG_LEN,":%s %s %s :%s\r\n",myhostname,ERR_USERSDONTMATCH,
										cli->nickname,
										"Cannot change mode for other users");
	write(cli->clientfd, buf, strlen(buf));
	return 0;
}

/* 整形转字符串 */
char* itoa(int num,char* str,int radix){/*索引表*/
    char index[]="0123456789ABCDEF";
    unsigned unum;/*中间变量*/
    int i=0,j,k;
    /*确定unum的值*/
    if(radix==10&&num<0){/*十进制负数*/
        unum=(unsigned)-num;
        str[i++]='-';
    }
    else unum=(unsigned)num;/*其他情况*/
    /*转换*/
    do{
        str[i++]=index[unum%(unsigned)radix];
        unum/=radix;
       }while(unum);
    str[i]='\0';
    /*逆序*/
    if(str[0]=='-')
        k=1;/*十进制负数*/
    else
        k=0;
    char temp;
    for(j=k;j<=(i-1)/2;j++){
        temp=str[j];
        str[j]=str[i-1+k-j];
        str[i-1+k-j]=temp;
    }
    return str;
}

