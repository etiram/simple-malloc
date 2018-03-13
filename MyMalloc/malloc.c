#include <stdlib.h>
#include <unistd.h>

/*  name: word_align()
 *  description: take a size_t integer and return the least multiple
 *    of sizeof (size_t) greater or equal to the input value
 */
static inline size_t word_align(size_t n) {
  return (n + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);
}

/* zerofill(ptr, len): write len 0 bytes at the address ptr */
static void zerofill(void *ptr, size_t len) {
  for(; len; len -= sizeof(size_t))
    *(size_t *)(ptr + len) = 0;
}

/* wordcpy(dst, src, len) copy len bytes from src to dst */
static void wordcpy(void *dst, void *src, size_t len) {
  for(; len; len -= sizeof(size_t))
    *(size_t *)(dst + len) = *(size_t *)(src + len);
}

struct chunk {
  struct chunk *next, *prev;
  size_t        size;
  long          free;
  void         *data;
};

/* Retrieve the base adress of the head & init the head [SENTINELLE] */
static struct chunk* get_base(void) {
  static struct chunk *base = NULL;
  if(!base) {
    void *oldbrk = sbrk(sizeof(struct chunk));
    if(oldbrk == (void *)(-1) || oldbrk == sbrk(0))
      return NULL;
    base = oldbrk;
    base->next = NULL;
    base->prev = NULL;
    base->size = 0;
    base->free = 0;
  }
  return base;
}

/*
 * extend_heap(last, size) extends the heap with a new
 * chunk containing a data block of size bytes.
 * Return 1 in case of success, and return 0 if sbrk(2) fails.
 */
static int extend_heap(struct chunk *last, size_t size) {
    struct chunk *new;
    void *oldbrk = sbrk(word_align(sizeof(struct chunk) + size));
    if(oldbrk == (void*)(-1) || oldbrk == sbrk(0))
        return 0;
    new = oldbrk;
    new->next = NULL;
    new->prev = last;
    new->size = size;
    new->free = 0;
    new->data = (char *)new + sizeof(struct chunk);
    last->next = new;
    return 1;
}

static void give_back_mem(struct chunk *ck) {
  if(ck->data == ((char *)ck + sizeof(struct chunk)))
    return;
  if(ck->prev->prev) {
    if(ck->data == ((char *)ck + sizeof(struct chunk)))
      return;
    ck->prev->next = NULL;
    brk(ck);
  }
}

static void merge(struct chunk *ck) {
  if(ck->data == ((char *)ck + sizeof(struct chunk)))
    return;
  if(!ck->next) {
    give_back_mem(ck);
    return;
  }
  if(ck->next && ck->next->free) {
    struct chunk *nxt = ck->next;
    if(nxt->data == ((char *)nxt + sizeof(struct chunk)))
      return;
    ck->next = nxt->next;
    if(nxt->next) {
      if(nxt->next->data == ((char *)nxt->next + sizeof(struct chunk)))
        return;
      nxt->next->prev = ck;
    }
    ck->size += nxt->size + sizeof(struct chunk);
  }
  if(ck->prev && ck->prev->free) {
    struct chunk *pre = ck->prev;
    if(pre->data == ((char *)pre + sizeof(struct chunk)))
      return;
    pre->next = ck->next;
    if(ck->next) {
      if(ck->next->data == ((char *)ck->next + sizeof(struct chunk)))
        return;
      ck->next->prev = pre;
    }
    pre->size += ck->size + sizeof(struct chunk);
  }
}

/* find a free chunk of at least the asked size   [CHECKED] */
static struct chunk* find_chunk(size_t size) {
  struct chunk *cur = get_base();
  for(; cur->next; cur = cur->next)
    if((cur->next->free) && (cur->next->size >= size))
      break;
  return cur;
}

/* retrieve the pointer to the chunk from the data */
static struct chunk* get_chunk(void *p) {
  if(!p || (size_t)p != word_align((size_t)p) || p > sbrk(0) ||
                    (struct chunk*)p < get_base())
    return NULL;
  struct chunk* ck = (struct chunk*)(p - sizeof(struct chunk));
  if(ck->data != p)
    return NULL;
  return ck;
}

void* malloc(size_t size) {
  if(!size)
    return NULL;
  size_t st = word_align(size);
  struct chunk *ck = find_chunk(st);
  if(!ck->next)
    if(extend_heap(ck, st) == 0)
      return NULL;
  ck = ck->next;
  ck->free = 0;
  return ck->data;
}

void* calloc(size_t nb, size_t size) {
  void* point = malloc(nb * size);
  if(point)
    zerofill(point, word_align(nb * size));
  return point;
}

void* realloc(void *old, size_t newsize) {
  if(!old)
    return malloc(newsize);
  if(!newsize) {
    free(old);
    return malloc(0);
  }
  struct chunk *old_ck = get_chunk(old);
  if(newsize < old_ck->size)
    return old;
  struct chunk *new = malloc(newsize);
  if(!new)
    return NULL;
  size_t min = newsize > old_ck->size ? old_ck->size : newsize;
  wordcpy(new, old, word_align(min));
  free(old);
  return new;
}

void free(void *p) {
  struct chunk *ck = get_chunk(p);
  if(!ck || (ck->data != p))
    return;
  ck->free = 1;
  merge(ck);
}
