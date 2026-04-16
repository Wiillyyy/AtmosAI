/* syscalls.c - newlib _write stub redirected to UART via __io_putchar */
/* Note: _sbrk est fourni par sysmem.c (généré par CubeIDE)           */
#include <sys/stat.h>
#include <errno.h>

extern int __io_putchar(int ch);

int _write(int file, char *ptr, int len)
{
    (void)file;
    for (int i = 0; i < len; i++)
        __io_putchar((unsigned char)ptr[i]);
    return len;
}

int _read(int file, char *ptr, int len)   { (void)file; (void)ptr; (void)len; return -1; }
int _close(int file)                       { (void)file; return -1; }
int _fstat(int file, struct stat *st)      { (void)file; st->st_mode = S_IFCHR; return 0; }
int _isatty(int file)                      { (void)file; return 1; }
int _lseek(int file, int ptr, int dir)     { (void)file; (void)ptr; (void)dir; return 0; }
