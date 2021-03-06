#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<assert.h>

#define MIN_ALLOC_SIZE 4096 /*We Allocate blocks in page sized chunks*/
#define UNTAG(p) (((unsigned long)(p))&0xfffffffc)

typedef struct header
{
	size_t size;
	struct header *next;
}header_t;

static header_t *freep,*usedp;
static header_t base; /*Zero si1ed block to get us started*/


/* 
 * Scan the free list and look for a place to put the block.
 * Basically we're looking for any block the to be freed block might have been partitioned from
 */

static void add_to_free_list(header_t *bp)
		{
		header_t *p;
		for(p = freep; !(bp > p && bp < p->next); p = p->next)
			if(p>=p->next && (bp>p||bp<p->next))
				break;
		/*
		if(bp + bp->size == p->next){
				bp->size += p->next->size;
				bp->next = p->next->next;
		}
		else 
		bp->next = p->next;*/
		if( p + p->size == bp){
		p->size += bp->size;
		p->next = bp->next;
		} else 
			p->next = bp;
		freep = p;
		}

/*Request for more memory from the kernel*/

static header_t * morecore(size_t num_units)
	{	
		void *vp;
		header_t *up;
		if(num_units < MIN_ALLOC_SIZE)
			num_units = MIN_ALLOC_SIZE/sizeof(header_t);
		if((vp = sbrk(num_units * sizeof(header_t))) == ( void * )-1)
			return NULL;
		up = (header_t *)vp;
		up->size = num_units;
		add_to_free_list(up);
		return freep;
	}	
/*Find chunk from free list and put it in the used list*/
void * GC_malloc(size_t alloc_size)
{ 
size_t num_units;
header_t *p, *prevp;
num_units = (alloc_size + sizeof(header_t) - 1)/sizeof(header_t) + 1;
prevp = freep;
for(p = prevp->next;;prevp = p, p = p->next)
	if(p->size >=num_units){/*Big Enough.*/
		if(p->size == num_units)/*Exact size*/
			prevp->next = p->next;
		else {
			p->size -= num_units;
			p+=p->size;
			p->size = num_units;
		}
		freep = prevp;
		/*Add to p to the used list. */
		if(usedp == NULL)
			usedp = p->next = p;
		else{
			p->next = usedp->next;
			usedp->next = p;
		}
		return (void*)(p+1);
		}
	if( p ==freep ){/*Not enough memory*/
			p = morecore(num_units);
			if (p == NULL)/*Request for more memory failed*/
				return NULL;
		}
}

/*Mark and Sweep*/

/*
 *Scan a region of memory and mark any items in the used list appropriately 
 *Both arguments must be word aligned.
 */
static void mark_from_region(unsigned long *sp, unsigned long *end)
{
	header_t *bp;
	for( ; sp<end ; sp++){
		unsigned long *v = sp;
		bp = usedp;
		do{
			if((bp+1)<=v && bp+1+bp->size > v){
				bp->next = (( unsigned long ) bp->next) | 1;
				break;
			}
		}while((bp = UNTAG(bp->next))!=usedp);
	}
}
/*Scan the marked blocks for references to other unmarked blocks*/
static void mark_from_heap(void)
{
		unsigned long *vp;
		header_t *bp,*up;
		for(bp = UNTAG(usedp->next); bp!= usedp; bp = UNTAG(bp->next)){
			if(!((unsigned long)bp->next & 1))
				continue;
			for(vp = (unsigned long *)(bp + 1) ; vp < (bp + bp->size+1) ; vp++){
				unsigned long *v = *vp;
				up = UNTAG(bp->next);
				do{
				if(up!=bp && up+1 <= v && up+1+up->size > v){
				up->next = ((unsigned long ) up->next) | 1;
				break;
				}
			}while ((up = UNTAG(up->next))!=bp);
		}
	}
}

unsigned long * stack_bottom;
/*Find the absolute top bottom of the stack*/
void GC_init(void)
{
	static int initted;
	FILE *statfp;
	int i;
	char buf[30];
	if(initted)
		return;
	initted = 1;
	statfp = fopen("/proc/self/stat","r");
	assert(statfp !=NULL);
	for(i = 0; i<27 ;i++)
	{
		fscanf(statfp,"%s",buf);
	}
	fscanf(statfp,"%lu",stack_bottom);
	fclose(statfp);
	usedp = NULL;
	base.next = freep = &base;
	base.size = 0;
}
/*Mark blocks of memory in use and free the ones not in use*/
void GC_collect(void)
{
		header_t *p, *prevp, *tp;
		unsigned long *stack_top;
		extern char end, etext; /*Provided by the linker*/
		if (usedp == NULL)
			return;
		/*Scan the BSS and initialized data segments.*/
		mark_from_region(&etext, &end);
		
		/*Scan the stack. */
		asm volatile ("movl %%ebp, %0" : "=r" (stack_top));
		
		mark_from_region(stack_top, stack_bottom);
		/*Mark from the heap. */
		
		mark_from_heap();
		/*And now we collect!*/
		
		for(prevp = usedp, p = UNTAG(usedp->next);;prevp =p,p =UNTAG(p->next)){
		next_chunk:
			if(((unsigned long)p->next & 1)){
				/*The chunk hasnt been marked. Thus it must be set free*/
				tp = p;
				p = UNTAG(p->next);
				add_to_free_list(tp);
				if(usedp == tp){
					usedp = NULL;
					break;
				}
				prevp->next = (unsigned long)p | ((unsigned long)prevp->next & 1);
				goto next_chunk;
			}
			p->next = ((unsigned long)p->next)&~1;
			if(p ==usedp)
				break;
		}
}
void main()
{
	GC_init();
	GC_collect();
	printf("hello");
}
