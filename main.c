#include "irc.h"

int nfiles=100;
struct file {
  int    f_fd;				/* descriptor */
  int	 f_flags;			/* F_xxx below */
  pthread_t	 f_tid;			/* thread ID */
  char	 *pwd;
}file[MAXFILES];


/* 初始化pool */
void init_pool(int listenfd, pool *p){
	// initially, there are no connected descriptors
	int i;
	p->maxi = -1;
	for(i = 0; i < FD_SETSIZE; i++){
		p->client[i].clientfd = -1;
	}
	// initially, listenfd is only member of select read set
	p->maxfd = listenfd;
	bzero(p->added_connfd,FD_SETSIZE);
	FD_ZERO(&p->read_set);
	FD_SET(listenfd, &p->read_set);
}

/* 添加client */
void add_client(int connfd, pool *p){
	int i,j;
	p->nready--;/* 用户pool中可用数量-1 */
	/* 找一个可用的client_t结构体（clientfd类似端口） */
	for(i = 0; i < FD_SETSIZE; i++){
		if(p->client[i].clientfd < 0){	
			p->client[i].clientfd = connfd;
			/* add descriptor to read set */
			FD_SET(connfd, &p->read_set);
			if(connfd > p->maxfd)
				p->maxfd = connfd;
			if(i > p->maxi)
				p->maxi = i;
			break;
		}
	}
	/* Initialize a new client for the client list */
	client_t *client = Malloc(sizeof(client_t));
	strcpy(client->nickname , "*");
	strcpy(client->username , "*");
	client->clientfd = connfd;
	for(j=0;j<MAX_CHANNEL_PER_CLI;j++)
		client->channel_id[j] = -1;
	client->nick_is_set = 0;
	client->user_is_set = 0;
	client->mode='0';
	client_list[connfd] = client;
	
	// couldn't find an empty slot
	if(i == FD_SETSIZE)
		perror("add_client error: too many clients");
}

/* 通过你nick获取*client */
client_t *get_clientnick(char *nick){
	int i;
	for(i = 0; i < FD_SETSIZE; i++){//change
		if(client_list[i] != NULL){
			if(strcmp(client_list[i]->nickname, nick) == 0)
					return client_list[i];
		}
	}
}

/* 处理“NICK”命令 */
int execute_nick(int connfd, char *nick){
    char buf[MAX_MSG_LEN];
	client_t *curclient = get_client(connfd);
	char hostname[256];
	gethostname(hostname,256);
	printf("h:%s\n",hostname);
	int k;
    /*nick存在，分两种情况：1.该用户已经注册完成要登陆 2.该用户尚未注册完，尝试注册重复nick名*/
	if(client_exist(nick,&k)){
		if((curclient->nick_is_set == 1) && (curclient->user_is_set == 1)){/* user已经设置好了说明是已经注册完成了 ，发welcome*/
            snprintf(buf,MAX_MSG_LEN,":%s %s %s :%s %s!%s@%s\r\n",hostname,RPL_WELCOME,nick,
                                                                "Welcome to the Internet Relay Network",
                                                                nick,curclient->username,curclient->hostname);
			write(connfd, buf, strlen(buf));
		}
		else{
			char buf[MAX_MSG_LEN];
			snprintf(buf,MAX_MSG_LEN,":%s %s * %s :Nickname is already in use\r\n", hostname,ERR_NICKNAMEINUSE,nick);
			write(connfd, buf, strlen(buf));
            //print_client_list();
            return 1;
		}
	}
	/* nick不存在，说明是一个注册消息 */
	else{
		if(curclient != NULL){
			strcpy(curclient->nickname, nick);
			curclient->nick_is_set = 1;
			/* 如果发现该用户已经发送过user消息，认为在修改昵称，并且发welcome*/
			if(curclient->user_is_set == 1){
                strcpy(curclient->nickname,nick);
				snprintf(buf,MAX_MSG_LEN,":%s %s %s :%s %s!%s@%s\r\n",hostname,RPL_WELCOME,nick,
                                                                "Welcome to the Internet Relay Network",
                                                                nick,curclient->username,curclient->hostname);
			    write(connfd, buf, strlen(buf));
			}
			return 1;
		}
	}
	return 0;
}

/* 处理“USER”命令 */
int execute_user(int connfd, char *username, char *hostname, char *servname, char *realname)
{
	char buf[MAX_MSG_LEN];
    client_t *curclient = get_client(connfd);
	char myhostname[256];
	gethostname(myhostname,256);
	/* If the client with the nickname has registed, send the message of the day */
	if((curclient->nick_is_set == 1) && (curclient->user_is_set == 1)){
		snprintf(buf,MAX_MSG_LEN,":%s %s %s :%s %s!%s@%s\r\n",myhostname,RPL_WELCOME,curclient->nickname,
                                                                "Welcome to the Internet Relay Network",
                                                                curclient->nickname,curclient->username,curclient->hostname);
		write(connfd, buf, strlen(buf));
	}
	
	/* set the username and all other informations to the client */
	else if(curclient != NULL){
		strcpy(curclient->username, username);
		strcpy(curclient->hostname, hostname);
		strcpy(curclient->servname, servname);
		strcpy(curclient->realname, realname);
		curclient->user_is_set = 1;
		/* nick存在，说明可以登陆了 */
		if(curclient->nick_is_set == 1){
			snprintf(buf,MAX_MSG_LEN,":%s %s %s :%s %s!%s@%s\r\n",myhostname,RPL_WELCOME,curclient->nickname,
                                                                "Welcome to the Internet Relay Network",
                                                                curclient->nickname,curclient->username,curclient->hostname);
		    write(connfd, buf, strlen(buf));
		}
		return 1;
	}
	return 0;
}








/* PRIVSMG */
int execute_privmsg(int connfd, char *target, char *msg)
{
	char buf[MAX_MSG_LEN];
	int fd_in_list;
	char myhostname[256];
	gethostname(myhostname,256);
	client_t *curcli=get_client(connfd);
	//target为空 ERR_NORECIPIENT
	if(target==NULL||strlen(target)==0){
		sprintf(buf,":%s %s %s :%s\r\n",myhostname,ERR_NORECIPIENT,
										client_list[connfd]->nickname,
										"No recipient given");
		write(connfd, buf, strlen(buf));
        return atoi(ERR_NORECIPIENT);
	}
	//msg为空 ERR_NOTEXTTOSEND
	if(msg==NULL||strlen(msg)==0){
		sprintf(buf,":%s %s %s :%s\r\n",myhostname,ERR_NOTEXTTOSEND,
										client_list[connfd]->nickname,
										"No text to send");
        write(connfd, buf, strlen(buf));                                
		return atoi(ERR_NOTEXTTOSEND);
	}
	//target是一个频道
	if(target[0]=='#'){
		del_prefix(target);//去掉头部的#
		channel_t *chnl;
		int id;
		if((chnl=get_channel(target))!=NULL){//频道是否存在
			int id;
			id=find_client_id(connfd,chnl->clientfd_list);
			if(id<0){//该用户不在这个频道
				send_err_cannotsendtochan(curcli,target);
			}
			else{
				if(chnl->channel_mode!='m'|| (chnl->channel_mode=='m'&&chnl->permission[id]>=1)){
					//有权限向频道的每个用户发消息
					int j,fd;
					for(j=0;j<MAX_CLI_PER_CHANNEL;j++){
						if((fd=chnl->clientfd_list[j])!=-1)
							send_privmsg(connfd,fd,msg);	
					}
				}
				else
					send_err_cannotsendtochan(curcli,target);
			}
		}
		else
			send_err_cannotsendtochan(curcli,target);		
	}
	//对个人发消息
	else {
		if(!client_exist(target,&fd_in_list)){//target这个用户不存在 ERR_NOSUCHNICK
			sprintf(buf,":%s %s %s :%s\r\n",myhostname,ERR_NORECIPIENT,
											client_list[connfd]->nickname,
											"No such nick/channel");
			write(connfd, buf, strlen(buf));
			return atoi(ERR_NOSUCHNICK);
		}
		//找到，构造消息发送
		int target_conn=client_list[fd_in_list]->clientfd;//找到target对应的客户的connfd
		client_t *target_cli=get_client(target_conn);
		char *tmsg=target_cli->awaymsg;
		if(target_cli->mode=='a')
			send_rpl_away(curcli,tmsg);
		else
			send_privmsg(connfd,target_conn,msg);	
	}
	return 0;
}

/* JOIN */
int execute_join(int connfd, char *channel_name){
    /* 加入已有channel OR 创建新的channel */
    channel_t *curchannel;
    if((curchannel=get_channel(channel_name)) == NULL){
        curchannel = create_channel(channel_name);
    }
    /* 获取该用户的结构体 */
    client_t *curclient = get_client(connfd);
    if(curclient == NULL || curchannel == NULL)
		return -1;
    /* 该用户已经存在于该聊天室内 */
    int i;
    for(i = 0; i < MAX_CLI_PER_CHANNEL; i++){
        if(curchannel->clientfd_list[i] == curclient->clientfd)
            return 2;
    }
    int r = find_blank_in_clientfd_list(curchannel->clientfd_list);//r=-1 聊天室已满
    int s = find_blank_in_channelid_array(curclient->channel_id);//s=-1 该用户加的聊天室数量达到上线
    if(r == -1 || s == -1)
        return -1;

    else{//join
        curchannel->clientfd_list[r] = curclient->clientfd;
        curclient->channel_id[s] = curchannel->channel_id;
        curchannel->client_count++; 
		/* 如果是首个进入的用户，权限为1:o */
		if(curchannel->client_count == 1){
			if(curchannel->permission[r] == 0)
				curchannel->permission[r] = 1;  
			else
				curchannel->permission[r] = 3; 
		}  
    }
	printf("fdasfrfgrqegqer\n");
    char buf[MAX_MSG_LEN],subbuf[MAX_MSG_LEN],myhostname[256];
	gethostname(myhostname,256);
    memset(buf, 0, sizeof(buf));
    sprintf(buf,"%s!%s@%s JOIN #%s\r\n",curclient->nickname, 
										curclient->nickname,
										curclient->hostname, 
										curchannel->name);
    int i2;
    //告诉所有聊天室成员connfd加入
    if(curchannel->client_count != 0){
        for(i2 = 0; i2 < MAX_CLI_PER_CHANNEL; i2++){
            if(curchannel->clientfd_list[i2] != -1)
				write(curchannel->clientfd_list[i2], buf, strlen(buf));
        }
    }
    //RPL_TOPIC(为空时忽略)
	if(strcmp(curchannel->topic, "") != 0){
		memset(buf, 0, sizeof(buf));
		sprintf(buf, ":%s 332 %s #%s :%s\r\n", myhostname, 
											curclient->nickname, 
											curchannel->name, 
											curchannel->topic);
		write(curclient->clientfd, buf, strlen(buf));
	}

    //RPL_NAMREPLY
    memset(buf, 0, sizeof(buf));
    int j;
    for(j = 0; j < curchannel->client_count; j++){
        memset(subbuf, 0, sizeof(subbuf));
        if(j == 0){
            client_t *cli = get_client(curchannel->clientfd_list[j]);
			if(curchannel->permission[j] == 1 || curchannel->permission[j] == 3)
            	sprintf(subbuf, "@%s", cli->nickname);
			else
				sprintf(subbuf, "%s", cli->nickname);
            strcpy(buf, subbuf);
        }
        else{
            client_t *cli = get_client(curchannel->clientfd_list[j]);
			if(curchannel->permission[j] == 1 || curchannel->permission[j] == 3)/* 如果用户具有频道操作员模式 */
				sprintf(subbuf, "%s @%s", buf, cli->nickname);
			else
            	sprintf(subbuf, "%s %s", buf, cli->nickname);
			memset(buf, 0, sizeof(buf));
			strcpy(buf, subbuf);
        }
    }
    memset(subbuf, 0, sizeof(subbuf));
	char mode = curchannel->channel_mode;
	if(curchannel->channel_mode == '0')	mode = '=';
    sprintf(subbuf, ":%s 353 %s %c #%s :%s\r\n", myhostname, 
												curclient->nickname, 
												mode,
												curchannel->name, 
												buf);
    write(curclient->clientfd, subbuf, strlen(subbuf));
	//RPL_ENDOFNAMES
    memset(buf, 0, sizeof(buf));
    sprintf(buf, ":%s 366 %s #%s :End of NAMES list\r\n", myhostname, 
														curclient->nickname, 
														curchannel->name);
    write(curclient->clientfd, buf, strlen(buf));
    return 1;
}



/* PART */
int execute_part(int connfd, char* channel_name, char* massage){
	char buf[MAX_MSG_LEN],servhostname[256];
	gethostname(servhostname, 256);
    /* 获取当前用户 */
    client_t *curclient = get_client(connfd);
    channel_t *curchannel = get_channel(channel_name);
    if(curclient == NULL)
        return -1;
    /* ERR_NOSUCHCHANNEL */
    if(curchannel == NULL){
        sprintf(buf, "%s :No such channel\r\n", channel_name);
        write(curclient->clientfd, buf, strlen(buf));
        return 1;
    }
    /* 成功part，相应处理 */
    int j;
    for(j = 0; j < MAX_CLI_PER_CHANNEL; j++){
        if(curchannel->clientfd_list[j] == curclient->clientfd){
            /* channel中找到了clientfd */
            memset(buf, 0, sizeof(buf));
            if(strcmp(massage, "") == 0)
                sprintf(buf, ":%s!%s@%s PART #%s\r\n", curclient->nickname, 
													curclient->nickname, 
													servhostname, 
													curchannel->name);
            else
                sprintf(buf, ":%s!%s@%s PART #%s :%s\r\n",curclient->nickname, 
														curclient->nickname, 
														servhostname, 
														curchannel->name, 
														massage);
            /* 更新数据 */
            if(leave_channel(connfd, channel_name) == -1)
                return -1;

            /* 发送全局消息 */
            int i;
            for(i = 0; i < MAX_CLI_PER_CHANNEL; i++){
                if(curchannel->clientfd_list[i] != -1)
                    write(curchannel->clientfd_list[i], buf, strlen(buf));
            }
            return 1;
        }
    }
    /* ERR_NOTONCHANNEL */
    sprintf(buf, "%s :You're not on that channel\r\n", channel_name);
    write(curclient->clientfd, buf, strlen(buf));
    //printf("%s", buf);
    return 1;
}



/* TOPIC */
int execute_topic(int connfd, char* channel_name, char* topic, int flag){
	char buf[MAX_MSG_LEN];
	memset(buf, 0, sizeof(buf));
	channel_t *curchannel = get_channel(channel_name);
    if(curchannel == NULL)	return -1;

	if(flag == 0){/* 没有:符号 */
        int i;
        /* 循环确认client在channel里面 */
        for(i = 0; i < MAX_CLI_PER_CHANNEL; i++){
            if(curchannel->clientfd_list[i] == connfd){/* topic存在 */
                /* RPL_TOPIC */
                if(strlen(curchannel->topic) != 0){
                    sprintf(buf, "%s :%s\r\n", curchannel->name, curchannel->topic);
                    write(connfd, buf, strlen(buf));
                    return 1; 
                }
                /* RPL_NOTOPIC */
                else{
                    sprintf(buf, "%s :No topic is set\r\n", channel_name);
                	write(connfd, buf, strlen(buf));
                    return 1;
                }
            }
        }
        //循环完毕没有找到clientfd，说明client不在channel中
        /* ERR_NOTONCHANNEL */
        sprintf(buf, "%s :You're not on that channel\r\n", channel_name);
        write(connfd, buf, strlen(buf));
    }
    /* 有指定修改的topic参数 */
    else{
        int i;
        for(i = 0; i < MAX_CLI_PER_CHANNEL; i++){
            if(curchannel->clientfd_list[i] == connfd){   
                /* 清空topic */
                if(strcmp(topic, "") == 0){
                    change_topic(curchannel, NULL);
                    return 1;
                }
                /* 修改topic */
                else{
					int cnt;
					for(cnt = 0; cnt < MAX_CLI_PER_CHANNEL; cnt++){
						if(curchannel->clientfd_list[cnt] == connfd)
							break;
					}
					/*如果channel是t模式，只能操作员修改*/
					if(curchannel->channel_mode == 't'){
						if((curchannel->permission[cnt]&01) == 1)
							change_topic(curchannel, topic);
					}	
					/* 非t模式军可以修改 */
					else
						change_topic(curchannel, topic);
                    return 1;
                }                
            }
        }
        /* ERR_NOTONCHANNEL */
        sprintf(buf, "%s :You're not on that channel\r\n", channel_name);
        write(connfd, buf, strlen(buf));
    }
    return 1;
}

/* AWAY */
int execute_away(int connfd, char* massage){
	char buf[MAX_MSG_LEN];
	memset(buf, 0, sizeof(buf));

	client_t *curclient = get_client(connfd);
	if(curclient == NULL)
		return -1;

	/* 清空away消息,退出a模式 */
	if(strcmp(massage, "") == 0){
		/* RPL_UNAWAY */
		memset(client_list[connfd]->awaymsg, 0, sizeof(client_list[connfd]->awaymsg));
		client_list[connfd]->mode = '0';//TODO
		sprintf(buf, ":You are no longer marked as being away\r\n");
		write(connfd, buf, sizeof(buf));
	}
	/* 设置away消息,进入a模式*/
	else{
		/* RPL_NOWAWAY */
		strcpy(client_list[connfd]->awaymsg, massage);
		client_list[connfd]->mode = 'a';
		sprintf(buf, ":You have been marked as being away\r\n");
		write(connfd, buf, sizeof(buf));
	}
}

/* MOTD */
int execute_motd(int connfd){
	char hostname[256];
	gethostname(hostname,256);
	
	client_t *cli=get_client(connfd);
	FILE *fp;
	if(fopen("motd.txt","r")==NULL)
	{
			char buf[MAX_MSG_LEN];
		snprintf(buf,MAX_MSG_LEN,":%s %s :MOTD File is missing\r\n",hostname,ERR_NOMOTD);
		write(connfd, buf, strlen(buf));
		return 1;
	}
	msg_of_the_day(cli,fp);
	fclose(fp);
	return 1;
}

/* PING */
int execute_ping(int connfd){
	char hostname[256];
	gethostname(hostname,256);
	char buf[MAX_MSG_LEN];
	snprintf(buf,MAX_MSG_LEN,"PONG %s\r\n",hostname);
	write(connfd,buf,strlen(buf));
}

/* LIST */
int execute_list(int connfd,char *channel){
	char myhostname[256],buf[MAX_MSG_LEN];
	client_t *curclient;
	int i,j,channel_is_empt;
	
	gethostname(myhostname,256);
	curclient = get_client(connfd);	
	channel_is_empt=!strcmp(channel,"");//channel字符串为空的标志
	
	for(i=0,j=0; i<MAX_CHANNEL_POSSIBLE; i++){
		if(channel_list[i]==NULL)continue;
		if(channel_is_empt||(!channel_is_empt&&(!strcmp(channel_list[i]->name,channel)))){
			char cnt[100];
			itoa(channel_list[i]->client_count,cnt,10);
			snprintf(buf,MAX_MSG_LEN, ":%s %s %s %c%s %s :%s\r\n",myhostname, RPL_LIST,
													curclient->nickname, 
													channel_list[i]->type, channel_list[i]->name, 
													cnt,channel_list[i]->topic);											
			write(connfd, buf, strlen(buf));
			bzero(buf,MAX_MSG_LEN);
			if(!channel_is_empt)break;	
			j++;
			if(j>=channel_count)break;//减少不必要的遍历							
		}
	}
	snprintf(buf,MAX_MSG_LEN,":%s %s %s :End of LIST\r\n",myhostname,RPL_LISTEND,curclient->nickname);
	write(connfd, buf, strlen(buf));
	return 0;
}

/* NAMES */
/* channel==null 没有指定参数，给出所有
	channel!=null指定了参数 ,给出相应的channel内容*/
int execute_names(int connfd,char *channel){
	char myhostname[256];
	gethostname(myhostname,256);
	client_t *curclient = get_client(connfd);
	char buf[MAX_MSG_LEN];
	int channel_is_empt=!strcmp(channel,"");//channel字符串为空
	int i,j,t;
	for(i=0,t=0; i<MAX_CHANNEL_POSSIBLE; i++){//全局channel_list遍历
		if(channel_list[i] == NULL)continue;
		if(channel_is_empt||(!channel_is_empt&&!strcmp(channel_list[i]->name,channel))){
			channel_t *tmp_chnl;
			tmp_chnl = channel_list[i];
			for(j=0;j<MAX_CLI_PER_CHANNEL;j++){//channel中的client_fd遍历
				int cli_fd=tmp_chnl->clientfd_list[j];
				if(cli_fd>=0){//
					char nickn[MAX_NICK_LEN];
					if(get_nick(tmp_chnl->clientfd_list[j],nickn)==0){
						snprintf(buf,MAX_MSG_LEN,":%s %s %s %c%s :%s\r\n",
						myhostname,
						RPL_LIST,
						curclient->nickname, 
						tmp_chnl->type, tmp_chnl->name,
						nickn);														
						write(connfd, buf, strlen(buf));	
						bzero(buf,MAX_MSG_LEN);
					}		
				}						
			}
			if(!channel_is_empt)break;
			t++;
			if(t>=channel_count)break;										
		}
	}
	snprintf(buf,MAX_MSG_LEN,":%s %s %s :End of NAMES LIST\r\n",myhostname,RPL_LISTEND,curclient->nickname);
	write(connfd, buf, strlen(buf));
	return 0;
}

/* WHO */
int execute_who(int connfd, char *channel_name){	
	client_t *curcli=get_client(connfd);//获得当前连接者的client结构体
	if(curcli==NULL)perror("客户已中断连接");
	int channelnm_is_empt=!strcmp(channel_name,"");//channel字符串为空的标志
	char myhostname[256];
	gethostname(myhostname,256);

	//为服务器中与请求客户端没有公共频道的每个用户返回RPL_WHOREPLY
	if(channelnm_is_empt||strcmp(channel_name,"*")==0||strcmp(channel_name,"0")==0){
		strncpy(channel_name,"*",2);
		int i,j,k,id;
		channel_t *channel_iter;
		int groupcli[MAX_CHANNEL_POSSIBLE*MAX_CLI_PER_CHANNEL];//所有聊天室里的所有成员之和 的最大可能值 todo 安全性 k
		for(i=0;i<MAX_CHANNEL_PER_CLI;i++){	//获得当前客户的channel_id数组
			if((id=curcli->channel_id[i])!=-1){
				if((channel_iter=find_channel(id))==NULL)
					continue;		
				for(j=0,k=0;j<MAX_CLI_PER_CHANNEL&&channel_iter->client_count!=0;j++){//获得channel_iter里的clientfd
					if(channel_iter->clientfd_list[j]!=-1){
						groupcli[k++]=channel_iter->clientfd_list[j];
					}
				}
			}		
		}
		char HorG='H';
		if(curcli->mode=='a')
			HorG='G';
		char *is_oper=" ";//todo ???没有指定频道，默认这些都是0
		if(curcli->mode=='o')
			is_oper=" * ";
		char buf[MAX_MSG_LEN];
		for(i=0;i<FD_SETSIZE;i++){
			if(client_list[i]==NULL)continue;
			if(!match_in_array(client_list[i]->clientfd,groupcli)){//发送消息
				snprintf(buf,MAX_MSG_LEN,":%s %s %s %s %s %s %s %c%s:0 %s\r\n", myhostname, RPL_WHOREPLY,
														channel_name,curcli->username,curcli->hostname,
														curcli->servname,curcli->nickname, 								
														HorG,is_oper,
														curcli->realname);
				write(client_list[i]->clientfd, buf, strlen(buf));
			}
		}
	}
	else{
		//如果存在此频通道，则必须为该频道中的每个用户返回RPL_WHOREPLY,如果不存在就什么都不输出
		int i,t;
		for(t=0;t<MAX_CHANNEL_POSSIBLE;t++){
			if(channel_list[t]==NULL)continue;
			channel_t *chnl_iter=channel_list[t];

			if(!strcmp(channel_list[t]->name,channel_name))
			{
				char HorG='H';
				if(curcli->mode=='a')
					HorG='G';
				char *is_oper=" ";//todo ???没有指定频道，默认这些都是0
				if(curcli->mode=='o')
					is_oper=" * ";
				char buf[MAX_MSG_LEN];
				for(i=0;i<MAX_CLI_PER_CHANNEL;i++){
					if(chnl_iter->clientfd_list[i]==-1)continue;
					else{//发送消息
						snprintf(buf,MAX_MSG_LEN,":%s %s %s %s %s %s %s %c%s:0 %s\r\n", myhostname, RPL_WHOREPLY,
																channel_name,curcli->username,curcli->hostname,
																curcli->servname,curcli->nickname, 								
																HorG, is_oper,
																curcli->realname);
						write(chnl_iter->clientfd_list[i], buf, strlen(buf));
					}
				}
				break;
			}
		}
	} 
	return 0;
}

/* NOTICE */
int execute_notice(int connfd, char *target, char *msg){
	char buf[MAX_MSG_LEN], myhostname[256];
	int fd_in_list;
	gethostname(myhostname,256);
	//target为空 ERR_NORECIPIENT
	if(target==NULL||strlen(target)==0){
        return atoi(ERR_NORECIPIENT);
	}
	//msg为空 ERR_NOTEXTTOSEND
	if(msg==NULL||strlen(msg)==0){                               
		return atoi(ERR_NOTEXTTOSEND);
	}
	//target这个用户不存在 ERR_NOSUCHNICK
	if(!client_exist(target,&fd_in_list)){
		return atoi(ERR_NOSUCHNICK);
	}
	//找到，构造消息发送
	//处理超出MAX——MSG——LEN的情况
	int curlen,i,j=0;
    while(j<strlen(msg)){
        bzero(buf,MAX_MSG_LEN);
        sprintf(buf,":%s PRIVMSG %s :",client_list[connfd]->hostname,target);
        curlen=strlen(buf);//此处不考虑前缀过长的情况(基本不会发生），or事先规定hostname一个较小数值？//if(curlen>=MAX_MSG_LEN-2)
        for(i=curlen;i<MAX_MSG_LEN-2&&j<strlen(msg);i++,j++){
            buf[i]=msg[j];
        }
        buf[i]='\r';
        buf[i+1]='\n';

		int target_conn=client_list[fd_in_list]->clientfd;//找到target对应的客户的connfd
		write(target_conn, buf, strlen(buf));
    }
	return 0;
}

/* QUIT */
int execute_quit(int connfd, char* massage){
    char buf[MAX_MSG_LEN],servhostname[256];
	gethostname(servhostname, 256);
    client_t *curclient = get_client(connfd);
    if(curclient == NULL)   
        return -1;
    int q;
    /* 开始向各个聊天室发送消息 */
    for(q = 0; q < MAX_CHANNEL_PER_CLI; q++){
        /* 找到每个channel_id */
        if(curclient->channel_id[q] != -1){
			/* 更新client_list 和 channel_list */
			channel_t *chl = find_channel(curclient->channel_id[q]);
			int id = chl->channel_id;
			leave_channel(curclient->clientfd, chl->name);

			/* 获取更新后的channel_t结构体 */
			chl = find_channel(id);
            if(chl == NULL) return -1;

            /* 构建buf */
			memset(buf, 0, sizeof(buf));
            if(strcmp(massage, "") == 0){
                sprintf(buf, ":%s!%s@%s PART #%s\n",curclient->nickname, 
													curclient->nickname, 
													servhostname, 
													chl->name);
            }
            else{
                sprintf(buf, ":%s!%s@%s PART #%s :%s\n", curclient->nickname, 
													curclient->nickname, 
													servhostname, 
													chl->name, 
													massage);
            }
			/* 向聊天室的所有成员发送part消息 */
            int j;
            for(j = 0; j < MAX_CLI_PER_CHANNEL; j++){
                if(chl->clientfd_list[j] != -1)
                    write(chl->clientfd_list[j], buf, strlen(buf));
            }
        }
    }
	return 1;
}

/* OPER */
int execute_oper(int connfd, char *pwd, char *passwd){/* passwd是操作员的正确密码 */
	char buf[MAX_MSG_LEN];
	memset(buf, 0, sizeof(buf));
	client_t *curclient = get_client(connfd);
	/* 认证通过 */
	if(strcmp(pwd, passwd) == 0){
		/* 给操作员模式 */
		curclient->mode = 'o';
		/* RPL_YOUREOPER */
		sprintf(buf, ":You are now an IRC operator\r\n");
		write(connfd, buf, sizeof(buf));
	}
	else{
		/* ERR_PASSWDMISMATCH */
		printf("dsafsa\n");
		sprintf(buf, ":Password incorrect\r\n");
		write(connfd, buf, sizeof(buf));
	}
	return 1;
}

/* MODE */
int execute_mode(int connfd,char *nick,char *flag){
	client_t *curcli=get_client(connfd);
	if(strcmp(nick,curcli->nickname)){	//nick不匹配
		send_err_usersdontmatch(curcli);
		return 0;
	}
	else{
		//flag是否匹配
		if(strlen(flag)<2)
			send_err_umodeunknownflag(curcli);
		else{
			if(!(flag[0]=='+'||flag[0]=='-') || !(flag[1]=='o'))
				send_err_umodeunknownflag(curcli);
			else
			{
				if(flag[0]=='-'&&flag[1]=='o')
					curcli->mode='0';//todo
			}			
		}
	}
	return 0;	
}

/* WHOIS */
int execute_whois(int connfd,char *nick){
	char *hostname = malloc(sizeof(char) *1024);
	gethostname(hostname,sizeof(char)*1024);
	char buf[MAX_MSG_LEN];
    memset(buf,0,sizeof(buf));
    int flag;
	int i;
	for(i = 0; i < FD_SETSIZE; i++){//change
		if(client_list[i] != NULL){
			if(strcmp(client_list[i]->nickname, nick) == 0){
				sprintf(buf,"%s %s %s %s %s\r\n",client_list[i]->nickname,
												client_list[i]->username,
												client_list[i]->hostname,
												" * :",
												client_list[i]->realname);
            	flag=0;
            	break;
			}
		flag=1;
		}
	}
	if(flag==1){
        char *error=" :No such nick/channel";
        char *NOSUCHNICK,ERR_NOSUCHNICK0,ERR_NOSUCHNICK1;
        char *num1=" 401 ";
		char buf1[MAX_MSG_LEN];memset(buf1,0,sizeof(buf1));
		sprintf(buf1,"%s %s %s %s\r\n",hostname,num1,nick,error);
		write(connfd,buf1,sizeof(buf1));
		return 1;
    }else{
		write(connfd,buf,sizeof(buf));
		char *WHOISSERVER=" : "; 
    	int f=0;
    	char buffer[80]="";                         /* 定义字符串并初始化为'\0' */
     	char *buff="一分钟平均负载,五分钟平均负载,一刻钟平均负载,采样时刻的间隔,最大线程的数目分别为 ";
     	char *file="/proc/loadavg";
     	f = open(file, O_RDONLY);
     	if (f == 0)
         	printf("error to open: %s\r\n", file);
     	read(f, (void *)buffer, 80);
      	char hostname[60]={0};
        int l=gethostname(hostname,sizeof(hostname));
     	char *info;
     	info=join(buff,buffer);
		char buf2[MAX_MSG_LEN];
		memset(buf2,0,sizeof(buf2));
        char *num2=" 312 ";
     	
		sprintf(buf2,"%s %s %s %s %s\r\n",hostname,num2,nick,WHOISSERVER,info);
		write(connfd,buf2,sizeof(buf2));
		
		char buf4[MAX_MSG_LEN];
		memset(buf4,0,sizeof(buf4));
      	char *fou=" :is an IRC operator";
	  	char *num4=" 313 ";
      
	  	sprintf(buf4,"%s %s %s %s %s\r\n",hostname,num4,nick,nick,fou);
	  	write(connfd,buf4,sizeof(buf4));

		char mainbuf[MAX_MSG_LEN];
		memset(mainbuf, 0, sizeof(mainbuf));
		sprintf(mainbuf, "%s :", nick);
		client_t *curclient=get_clientnick(nick);
	  	for(i=0;i<MAX_CHANNEL_PER_CLI;i++){
			if( curclient->channel_id[i]!=-1){
				channel_t *curchannel = find_channel(curclient->channel_id[i]);
				int j;
				for(j = 0; j < MAX_CLI_PER_CHANNEL; j++){
					if(curchannel->clientfd_list[j] ==curclient->clientfd){
						char main[512];
        				memset(main, 0, sizeof(main));
        				strcpy(main, mainbuf);
						char sig[20];//0:无 1:o 2:v 3:o+v
						if(curchannel->permission[j]==0)
							strcpy(sig,"null");
						else if(curchannel->permission[j]==1)
							strcpy(sig,"o");
						else if(curchannel->permission[j]==2)
							strcpy(sig,"v");
						else
							strcpy(sig,"o+v");
						
        				sprintf(mainbuf, "%s %s %s ", main, sig, curchannel->name);
					}
				}
			}
	  	}
	  	char *mainbuf1;
	  	mainbuf1=join(mainbuf,"\r\n");
      	write(connfd,mainbuf1,strlen(mainbuf1));
      	char buf5[MAX_MSG_LEN];
	  	memset(buf5,0,sizeof(buf5));
      	for(i = 0; i < FD_SETSIZE; i++){//change
			if(client_list[i] != NULL){
				if(strcmp(client_list[i]->nickname, nick) == 0){	
					sprintf(buf5,"%s %s %s\r\n",nick," : ",client_list[i]->awaymsg);
            		break;  
				}
			}
		}
		write(connfd,buf5,strlen(buf5));
		char buf3[MAX_MSG_LEN];
		memset(buf3,0,sizeof(buf3));
		char *ENDOFWHOIS=" :End of WHOIS list";
		char *num3=" 318 ";
		
		sprintf(buf3,"%s %s %s %s %s\r\n",hostname,num3,nick,nick,ENDOFWHOIS);
		write(connfd,buf3,sizeof(buf3));
		return 1;	
	}
}

/* LUSERS */
int execute_lusers(int connfd){
	char nick[MAX_MSG_LEN];
    get_nick(connfd,nick);
    char *hostname = malloc(sizeof(char) *1024);
	gethostname(hostname,sizeof(char)*1024);
	int i,j;
    char *space=" ";
    int exist=0;//总共有几个客户
    for ( i = 0; i < FD_SETSIZE; i++){
		if(client_list[i] != NULL){
			exist++;
		}
    }
	int unknow=0;
    for (i = 0; i < FD_SETSIZE; i++){
		if(client_list[i] != NULL){
			if(client_list[i]->nick_is_set==0||client_list[i]->user_is_set==0){
				unknow++;
			}
		}
	}
    int users;
    users=exist-unknow;

	char us[20],ex[20],un[20];
	itoa(users,us,10);
	itoa(exist,ex,10);
	itoa(unknow,un,10);
	char *RPL_LUSERCLIENT0=" :There are ";
    char *RPL_LUSERCLIENT1=" users and  3 services on 1 servers";

	char *num0=" 251 ";
	char buf[MAX_MSG_LEN];
    memset(buf,0,sizeof(buf));
	sprintf(buf,"%s %s %s %s %s  %s\r\n",hostname,num0,nick,RPL_LUSERCLIENT0,us,
											RPL_LUSERCLIENT1);
    write(connfd, buf, strlen(buf));
	char buf1[MAX_MSG_LEN];
   	memset(buf1,0,sizeof(buf1));
    char  *num=" 252 ";
    char *LUSEROP=" :operator(s) online";
	sprintf(buf1,"%s %s %s %s %s  %s\r\n",hostname,num,nick,space,ex,
										LUSEROP);
    write(connfd, buf, strlen(buf));
	char buf2[MAX_MSG_LEN];
	memset(buf2,0,sizeof(buf2));
    char *num1=" 253 ";
   
    char *LUSERUNKNOWN=" :unknown connection(s)";
    sprintf(un, " %d", unknow);
  
	sprintf(buf2,"%s %s %s %s %s  %s\r\n",hostname,num1,nick,space,un,
										LUSERUNKNOWN);
    write(connfd, buf2, strlen(buf2));
	char buf3[MAX_MSG_LEN];
	memset(buf3,0,sizeof(buf3));
	int chan=0;
    for ( i = 0; i <MAX_CHANNEL_POSSIBLE ; i++){
		if(channel_list[i] != NULL){
			chan++;
		}
	}
    char ch[20];
    itoa(chan,ch,10); 
    
    char *num2=" 254 ";
    char  *LUSERCHANNELS=" :channels formed";
  
	sprintf(buf3,"%s %s %s %s %s  %s\r\n",hostname,num2,nick,space,ch,
										LUSERCHANNELS);
    write(connfd, buf3, strlen(buf3));
	char buf4[MAX_MSG_LEN];
	memset(buf4,0,sizeof(buf4));
    char *num3=" 255 ";
    char *RPL_LUSERME0=" :I have " ;
    char *RPL_LUSERME1=" clients and 1 servers";
   
	sprintf(buf4,"%s %s %s %s %s %s %s\r\n",hostname,num3,nick,space,RPL_LUSERME0,ex,RPL_LUSERME1);
    write(connfd, buf4, strlen(buf4));
	return 1;
}

/* The entry of all the commands. dispatch the command to different executing methods. */
int execute_command(int connfd, char *buf, char *passwd){
	int flag_topic = 0;/*区分topic有：清空和无：检查的情况*/
	char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1];
	memset(tokens, 0, sizeof(tokens));
	if(tokenize(buf, tokens, &flag_topic) == 0){
		perror("read_from_clients error: tokenize error");
	}
	// printf("command:%d\n", flag_topic);
	char *temp = tokens[0];/* Get the command */
	/* NICK */
    if(strncasecmp(temp, "nick", sizeof("nick")) == 0){
		if(strcmp(tokens[1],"")){
			return execute_nick(connfd, tokens[1]);
		}
		else {
			char buf[128];
			sprintf(buf,"nick :Not enough parameters\r\n");
			write(connfd, buf, strlen(buf));
		}
	}
	/* USER */
	else if(strncasecmp(temp, "user", sizeof("user")) == 0){
		if(strcmp(tokens[1],"") && strcmp(tokens[2],"") && strcmp(tokens[3],"") && strcmp(tokens[4],"")){
			return execute_user(connfd, tokens[1], tokens[2], tokens[3], tokens[4]);
		}
		else {
			char buf[128];
			sprintf(buf,"user :Not enough parameters\r\n");
			write(connfd, buf, strlen(buf));
		}
	}
	/* PING */
    if(strncasecmp(temp, "ping", sizeof("ping")) == 0){
		return execute_ping(connfd);
	}
	/*未注册的客户*/
	else if(have_register(connfd)!=1){
		char *tmp="you haven't registered!\r\n";
		write(connfd ,tmp,strlen(tmp));
	}
	/* AWAY */
	else if(strncasecmp(temp, "away", sizeof("away")) == 0){
		return execute_away(connfd, tokens[1]);
	}
	/* OPER */
	else if(strncasecmp(temp, "oper", sizeof("oper")) == 0){
		if(strcmp(tokens[1], "") != 0 && strcmp(tokens[2], "") != 0){
			return execute_oper(connfd, tokens[2], passwd);
		}
		else {
			char buf[128];
			sprintf(buf,"oper :Not enough parameters\r\n");
			write(connfd, buf, strlen(buf));
		}
	}
	/* MODE */
	else if(strncasecmp(temp, "mode", sizeof("mode")) == 0){
		if(strcmp(tokens[1], "") != 0){
			return execute_mode(connfd, tokens[1], tokens[2]);
		}
		else{
			char buf[128];
			sprintf(buf,"mode :Not enough parameters\r\n");
			write(connfd, buf, strlen(buf));
		}
	}
	/* MOTD */
	else if(strncasecmp(temp, "motd", sizeof("motd")) == 0){
		return execute_motd(connfd);
	}
	/* NOTICE */
	else if(strncasecmp(temp, "notice", sizeof("notice")) == 0)
			return execute_privmsg(connfd, tokens[1], tokens[2]);
	/* JOIN */
	else if(strncasecmp(temp, "join", sizeof("join")) == 0){
		if(strcmp(tokens[1], "") != 0){
			return execute_join(connfd, tokens[1]);
		}
		else {
			char buf[128];
			sprintf(buf,"join :Not enough parameters\r\n");
			write(connfd, buf, strlen(buf));
		}
	}
	/* PART */
	else if(strncasecmp(temp, "part", sizeof("part")) == 0){
		if(strcmp(tokens[1], "")){
			return execute_part(connfd, tokens[1], tokens[2]);
		}
		else{
			char buf[128];
			sprintf(buf, "part :Not enough parameters\r\n");
			write(connfd, buf, strlen(buf));
		}
	}
	/* TOPIC */
	else if(strncasecmp(temp, "topic", sizeof("topic")) == 0){
		if(strcmp(tokens[1], "")){
			return execute_topic(connfd, tokens[1], tokens[2], flag_topic);
		}
		else{
			char buf[128];
			sprintf(buf, "topic :Not enough parameters\r\n");
			write(connfd, buf, sizeof(buf));
		}
	}
	/* PRIVMSG */
	else if(strncasecmp(temp, "privmsg", sizeof("privmsg")) == 0)
			return execute_privmsg(connfd, tokens[1], tokens[2]);
	/* LIST */
	else if(strncasecmp(temp, "list", sizeof("list")) == 0){
			return execute_list(connfd,tokens[1]);
	}
	/* NAMES*/
	else if(strncasecmp(temp, "names", sizeof("list")) == 0){
			return execute_names(connfd,tokens[1]);
	}
	/* WHO */
	else if(strncasecmp(temp, "who", sizeof("who")) == 0){
			return execute_who(connfd, tokens[1]);
	}
	/* WHOIS */
	else if(strncasecmp(temp, "whois", sizeof("whois")) == 0){
		if(strcmp(tokens[1], "")){
			return execute_whois(connfd, tokens[1]);
		}
		else{
			char buf[128];
			sprintf(buf, "whois :Not enough parameters\r\n");
			write(connfd, buf, sizeof(buf));
		}
	}
	/* LUSERS */
	else if(strncasecmp(temp, "lusers", sizeof("lusers")) == 0){
			return execute_lusers(connfd);
	}
	/* QUIT */
	else if(strncasecmp(temp, "quit", sizeof("quit")) == 0){
		return execute_quit(connfd, tokens[1]);
	}
    else 
        return -1;
	return 0;
}

/* The method to check whether the buf is a command, if not, return 0 */
int is_command(char *buf){
	char tokens[MAX_MSG_TOKENS][MAX_MSG_LEN+1];
	memset(tokens, 0, sizeof(tokens));
	int flag_topic = 0;
	if(tokenize(buf, tokens, &flag_topic) == 0){
		perror("read_from_clients error: tokenize error");
	}
	char *temp = tokens[0];
	if(strncasecmp(temp, "ping", sizeof("ping")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "nick", sizeof("nick")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "user", sizeof("user")) == 0){
		return 1;
	}
	if(strncasecmp(temp, "motd", sizeof("motd")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "notice", sizeof("notice")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "quit", sizeof("quit")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "join", sizeof("join")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "part", sizeof("part")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "topic", sizeof("topic")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "lusers", sizeof("lusers")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "whois", sizeof("whois")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "names", sizeof("names")) == 0){
		return 1;
	} 
	else if(strncasecmp(temp, "list", sizeof("list")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "privmsg", sizeof("privmsg")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "who", sizeof("who")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "away", sizeof("away")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "oper", sizeof("oper")) == 0){
		return 1;
	}
	else if(strncasecmp(temp, "mode", sizeof("mode")) == 0){
		return 1;
	}
	return 0;
}

void *
do_get_read(void *vptr)
{
	int					connfd;
	char				line[MAXLINE];
	struct file			*fptr;
	int n,r;
	char *passwd=NULL;
	char buf[MAX_LINE];
	
	fptr = (struct file *) vptr;

	connfd = fptr->f_fd;
	printf("%s\n",fptr->pwd);
	passwd=fptr->pwd;

	while(1){
		if((n = readline(connfd, buf, MAX_LINE)) != 0){			
				//byte_cnt += n;				
			char msg[MAX_MSG_LEN];
			get_msg(buf, msg);
			
			if(!is_command(msg)){
				/* 非正确格式命令，返回错误信息 */
				char buf1[MAX_MSG_LEN];
				bzero(buf1,MAX_MSG_LEN);
				//todo:msg过长的情况
				snprintf(buf1,MAX_MSG_LEN, "421: %s Unkown command\n", msg);
				write(connfd, buf1, strlen(buf1));
			}
			else{
				/* 是正确格式的命令，转到excute_command处理函数 */
				if((r = execute_command(connfd, msg, passwd)) == -1){
					snprintf(msg, MAX_MSG_LEN, "%s command failed\n", msg);
					write(connfd, msg, MAX_MSG_LEN);
				}
			}
		}
		// 读到EOF 关掉描述符和结构体
		else{
			Close(connfd);		
			free(client_list[connfd]);
			fptr->f_flags = F_DONE;		/* clears F_READING */
			return(fptr);
		}
	}

}

void read_from_clients(pool *p, char *passwd){
	int i, j,k,connfd,n;
	struct file	*fptr;
	pthread_t th_cli;

	//p里面有那些client可读了
	for(i = 0; (i <= p->maxi) && (p->nready > 0); i++){
		connfd = p->client[i].clientfd;
		//rio = p->clientrio[i];
		// 为每个准备好读的fd创建一个线程进行IO
		if((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))){
			if(p->added_connfd[connfd]==1)break;
			printf("connfd:%d\n",connfd);
			printf("i:%d\n",i);
			p->nready--;
			for (j = 0 ; j < nfiles; j++)
				if (file[j].f_flags == 0)
					break;
			if (j == nfiles)
				err_quit("nlefttoconn = nothing found");

			file[j].f_flags = F_CONNECTING;
			file[j].f_fd=connfd;
			file[j].pwd=passwd;
			printf("%s\n",file[j].pwd);//
			pthread_create(&th_cli,NULL,do_get_read,&file[j]);
			p->added_connfd[connfd]=1;
			file[j].f_tid = th_cli;	
		}
	}

	for (k = 0; k < nfiles; k++) {
		if (file[k].f_flags & F_DONE) {
			printf("k=%d\n",k);
			Pthread_join(file[k].f_tid, (void **) &fptr);

			if (&file[k] != fptr)
				err_quit("file[i] != fptr");
			fptr->f_flags = F_JOINED;	/* clears F_DONE */
			
			//线程退出时的处理
			FD_CLR(file[k].f_fd , &p->read_set);
			p->client[file[k].f_fd].clientfd = -1;
			p->added_connfd[file[k].f_fd]=0;
			//printf("thread %d done\n", (int)fptr->f_tid);
		}
	}
}


int main(int argc, char *argv[])
{
    int opt;
    extern char *optarg;
    char *port = "2323", *passwd = NULL;
    int verbosity = 0;

    while ((opt = getopt(argc, argv, "p:o:vqh")) != -1)
        switch (opt)
        {
        case 'p':
            port = strdup(optarg);
            break;
        case 'o':
            passwd = strdup(optarg);
            break;
        case 'v':
            verbosity++;
            break;
        case 'q':
            verbosity = -1;
            break;
        case 'h':
            fprintf(stderr, "Usage: chirc -o PASSWD [-p PORT] [(-q|-v|-vv)]\n");
            exit(0);
            break;
        default:
            fprintf(stderr, "ERROR: Unknown option -%c\n", opt);
            exit(-1);
        }

    if (!passwd)
    {
        fprintf(stderr, "ERROR: You must specify an operator password\n");
        exit(-1);
    }

    /* Set logging level based on verbosity */
    switch(verbosity)
    {
    case -1:
        chirc_setloglevel(QUIET);
        break;
    case 0:
        chirc_setloglevel(INFO);
        break;
    case 1:
        chirc_setloglevel(DEBUG);
        break;
    case 2:
        chirc_setloglevel(TRACE);
        break;
    default:
        chirc_setloglevel(INFO);
        break;
    }

    /* Your code goes here */
    struct sockaddr_in client_addr;
    int listenfd, connfd, client_length = sizeof(struct sockaddr_in);
    static pool pool;  
    channel_count = 0;
    // Open listenfd with the particular port
    listenfd = open_listenfd(atoi(port));

    // Init a pool to listen clients' requests
    init_pool(listenfd, &pool);
    int t;
	for (t = 0; t < nfiles; t++) {
		file[t].f_flags = 0;
	}
    while(1){
        // Call the select function
    	pool.ready_set = pool.read_set;
    	pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);
    	
    	if(FD_ISSET(listenfd, &pool.ready_set)){//listenfd可读
    		// Wait for client
			connfd = Accept(listenfd, (SA *)&client_addr, &client_length);
			add_client(connfd, &pool);
    	}
    	// Read client
    	read_from_clients(&pool, passwd);
    }
    return 0;
}
