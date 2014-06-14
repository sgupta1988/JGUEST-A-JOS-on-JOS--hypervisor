#ifndef JOS_INC_MALLOC_H
#define JOS_INC_MALLOC_H 1

void *malloc(size_t size);
void free(void *addr);

enum
{
    MAXMALLOC = 8 * 1024*1024	/* max size of one allocated chunk */
};

#endif
