#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

size_t readn(long fd, int* ptr, size_t n)
{
    size_t nleft = n;
    size_t nread;
    
    int *bufptr = (int*)ptr;
    
    while (nleft > 0) {
        if ((nread = read(fd, bufptr, nleft)) < 0){
            if (nleft == n)
                return(-1);
            else
                break;
        } 
        else if (nread == 0){  
            break;
        }
        nleft -= nread;
        bufptr += nread;
    }
    return(n - nleft);
}

size_t writen(long fd, int* ptr, size_t n)
{
    size_t nleft = n;
    size_t nwritten;
    
    int *bufptr = (int*)ptr;
    
    while (nleft > 0) {
        if ((nwritten = write(fd, bufptr, nleft)) < 0) {
            if (nleft == n)
                return(-1);
            else
                break; 
        } 
        
        else if (nwritten == 0) {
            break; 
        }
        
        nleft -= nwritten;
        bufptr += nwritten;
    }
    return(n - nleft);
}