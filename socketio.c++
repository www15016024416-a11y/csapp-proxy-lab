
#include <sys/types.h>   // ssize_t
#include <cstddef>       // size_t
#include<sys/unistd.h>
#include<errno.h>
#include<memory.h>

using namespace std; 
#define RIO_BUFSIZE 8192
#define min(a,b) ((a)<(b)?(a):(b))
typedef struct {
    int rio_fd;                /* 描述符 */
    size_t rio_cnt;           /* 缓冲区中未读字节数 */
    char *rio_bufptr;          /* 下一个未读字节 */
    char rio_buf[RIO_BUFSIZE]; /* 缓冲区 */
} rio_t;

//有缓冲读
ssize_t rio_readb(rio_t *rt,char* usrbuff,size_t n){
    //read 读出内核缓冲区的数据,如果被einter中断就接着读(while). 
    while(rt->rio_cnt==0){
        int len=read(rt->rio_fd,rt->rio_buf,sizeof(rt->rio_buf));
        if(len<0){
            if(errno!=EINTR){
                return -1;
            }else{
                continue;
            }

        }else if (len==0)
        {
            return 0;
        }
        rt->rio_cnt=len;
        rt->rio_bufptr=rt->rio_buf;
    }
    //把min(n,rt->rio_cnt) 个字节给usrbuff
    size_t comsume=min(n,rt->rio_cnt);
    memcpy(usrbuff,rt->rio_bufptr,comsume);
    rt->rio_bufptr+=comsume;
    rt->rio_cnt-=comsume;

    return comsume;
}
//有缓冲循环读

ssize_t rio_readnb(rio_t *rt,char* usrbuff,size_t n){
    size_t alread=0;

    while(alread<n){
        int len=rio_readb(rt,usrbuff+alread,n-alread);
        if(len<0){
            return -1;
            //出错了,einter被rio_readb给处理了.返回-1一定是错误
        }
        if(len==0) break;
        alread+=len;
    }
    return alread;
}


ssize_t rio_readlineb(rio_t *rt,char* usrbuff,size_t maxlen){\

    //我们就认为maxlen里一定有'\n',第maxlen个字符必须要补'\0',也就是下标maxlen-1这里.
    //那我们就读maxlen-1个字符.
    //有一个问题,当我们读完maxlen-1个字符都没有'\n'的时候,还有没读到'\n',就接到oef了,我们该怎么做
    int n=0,rc;
    char c;
    for(int i=0;i<maxlen-1;i++){
        rc=rio_readb(rt,&c,1);
        if(rc==0) {
            if(n==0) return 0;
            else break;
        }
        if(rc<0) return -1;
        usrbuff[n++]=c;
        if(c=='\n') break;
    }
    
    usrbuff[n]=0;
    return n;
}



