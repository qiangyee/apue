/**
 * translate rot13 game
 */
#include "apue.h"
#include <ctype.h>
#include <fcntl.h>
#include <aio.h>
#include <errno.h>

#define BSZ 4096
#define NBUF 8
#define FILE_MODE 0644

unsigned char buf[BSZ];

enum rwop {
    UNUSED = 0,
    READ_PENDING = 1,
    WRITE_PENDING = 2
};

struct nbuf {
    enum rwop   op;
    int last;
    struct aiocb aiocb;
    unsigned char data[BSZ];
};

struct nbuf bufs[NBUF];

unsigned char
translateROT13(unsigned char c)
{
    if (isalpha(c)) {
        if (c >= 'n')
            c -= 13;
        else if (c >= 'a')
            c += 13;
        else if (c >= 'N')
            c -= 13;
        else
            c += 13;
            
    }
    return c;
}

void
sync_translate(int ifd, int ofd)
{
    int n, i, nw;
    while ( (n = read(ifd, buf, BSZ)) > 0) {
        for (i = 0; i < n; i++) {
            buf[i] = translateROT13(buf[i]);
        }
        if ((nw = write(ofd, buf, n)) != n) {
            if (nw < 0) {
                err_sys("write failed");
            } else {
                err_quit("short write (%d/%d)", nw, n);
            }
        }
    }
    fsync(ofd);
    close(ofd);
}

void
aio_translate(int ifd, int ofd)
{
    int                 i, j, n, err, numop;
    struct stat         sbuf;
    const struct aiocb  *aiolist[NBUF];
    off_t               off = 0;
    if (fstat(ifd, &sbuf) < 0)
        err_sys("fstat failed");
    
    for (i=0; i < NBUF; i++) {
        bufs[i].op = UNUSED;
        bufs[i].aiocb.aio_buf = bufs[i].data;
        bufs[i].aiocb.aio_sigevent.sigev_notify = SIGEV_NONE;
        aiolist[i] = NULL;
    }

    numop = 0;
    for (;;) {
        for (i = 0; i < NBUF; i++) {
            switch (bufs[i].op) {
                case UNUSED:
                    if (off < sbuf.st_size) {
                        bufs[i].op = READ_PENDING;
                        bufs[i].aiocb.aio_fildes = ifd;
                        bufs[i].aiocb.aio_offset = off;
                        off += BSZ;
                        if (off >= sbuf.st_size)
                            bufs[i].last = 1;
                        bufs[i].aiocb.aio_nbytes = BSZ;
                        if (aio_read(&bufs[i].aiocb) < 0)
                            err_sys("aio_read failed");
                        aiolist[i] = &bufs[i].aiocb;
                        numop ++;
                    }
                    break;
                case READ_PENDING:
                    if ((err = aio_error(&bufs[i].aiocb)) == EINPROGRESS)
                        continue;
                    if (err != 0) {
                        if (err == -1)
                            err_sys("aio_error failed");
                        else
                            err_exit(err, "read failed");
                    }

                    if ((n = aio_return(&bufs[i].aiocb)) < 0)
                        err_sys("aio_return failed");
                    if (n != BSZ && !bufs[i].last)
                        err_quit("short read (%d/%d)", n, BSZ);
                    for (j = 0; j < n; j++)
                        bufs[i].data[j] = translateROT13(bufs[i].data[j]);
                    bufs[i].op = WRITE_PENDING;
                    bufs[i].aiocb.aio_fildes = ofd;
                    bufs[i].aiocb.aio_nbytes = n;
                    if (aio_write(&bufs[i].aiocb) < 0)
                        err_sys("aio_write failed");
                    /* retain our spot in aiolist*/
                    break;
                case WRITE_PENDING:
                    if ((err = aio_error(&bufs[i].aiocb)) == EINPROGRESS)
                        continue;
                    if (err != 0) {
                        if (err == -1)
                            err_sys("aio_error failed");
                        else
                            err_exit(err, "write failed");
                    }

                    if ((n = aio_return(&bufs[i].aiocb)) < 0)
                        err_sys("aio_return failed");
                    if (n != bufs[i].aiocb.aio_nbytes)
                        err_quit("short wirte (%d/%d)", n, BSZ);
                    aiolist[i] = NULL;
                    bufs[i].op = UNUSED;
                    numop--;
                    break;
            }
        }
        if (numop == 0) {
            if (off >= sbuf.st_size)
                break;
        } else {
            if (aio_suspend(aiolist, NBUF, NULL) < 0)
                err_sys("aio_suspend failed");
        }
    }
    
    bufs[0].aiocb.aio_fildes = ofd;
    if (aio_fsync(O_SYNC, &bufs[0].aiocb) < 0)
        err_sys("aio_fsync failed");

}

int
main(int argc, char* argv[]){
    int ifd, ofd;
    
    if (argc != 3)
        err_quit("usage: rot13 infile outfile");
    if ((ifd = open(argv[1], O_RDONLY)) < 0)
        err_sys("can't open %s", argv[1]);
    if ((ofd = open(argv[2], O_RDWR|O_CREAT|O_TRUNC, FILE_MODE)) < 0)
        err_sys("can't create %s", argv[2]);
/*     sync_translate(ifd, ofd);*/
    aio_translate(ifd, ofd);

    return (0);
}
