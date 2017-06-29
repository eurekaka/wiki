#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct HeapData
{
	int heap_size;
	int max_heap_size;
	size_t elem_size;
	void **elems;
	int (*compare) (void *, void *);
} HeapData;

typedef HeapData *Heap;

typedef int (*CompareFn) (void *, void *);

#ifndef NULL
#define NULL (void *)0
#endif

int
heap_destroy(Heap heap)
{
	int i;
	for (i = 0; i < heap->heap_size; ++i)
	{
		if (*(heap->elems + i) != NULL)
			free(*(heap->elems + i));
	}

	free(heap->elems);
	free(heap);

	return 0;
}

int heap_insert(Heap heap, void *elem)
{
	if (heap->heap_size == heap->max_heap_size)
		return 1;

	/* i is the index to insert into */
	int i = heap->heap_size++;

	void *heap_elem = malloc(heap->elem_size);
	if (heap_elem == NULL)
		return 1;

	memcpy(heap_elem, elem, heap->elem_size);

	while (i > 0)
	{
		int j = (i - 1) >> 1;
		/* minHeap */
		if (heap->compare(heap_elem, *(heap->elems + j)) >= 0)
			break;

		*(heap->elems + i) = *(heap->elems + j);

		i = j;
	}

	*(heap->elems + i) = heap_elem;

	return 0;
}

Heap
heap_init(void **elems, int nelems, int max_heap_size, size_t elem_size, CompareFn compare)
{
	Heap heap = (Heap) malloc(sizeof(HeapData));
	if (heap == NULL)
		return NULL;

	heap->heap_size = 0;
	heap->elem_size = elem_size;
	heap->compare = compare;
	heap->max_heap_size = max_heap_size;
	heap->elems = (void **) malloc(sizeof(void *) * max_heap_size);

	if (heap->elems == NULL)
	{
		free(heap);
		return NULL;
	}

	memset(heap->elems, 0, sizeof(void *) * max_heap_size);

	int i;
	for (i = 0; i < nelems; ++i)
	{
		if (heap_insert(heap, *(elems + i)) > 0)
			break;
	}

	if (heap->heap_size != nelems)
	{
		heap_destroy(heap);
		return NULL;
	}

	return heap;
}

void *heap_root(Heap heap)
{
	if (heap->heap_size == 0)
		return NULL;

	return heap->elems[0];
}

void
heap_siftup(Heap heap)
{
	if (heap->heap_size == 1)
	{
		heap->heap_size--;
		return;
	}

	void *last_elem = *(heap->elems + heap->heap_size - 1);
	heap->heap_size--;

	/* i is where the hole is */
	int i = 0;
	for (;;)
	{
		int j = 2 * i + 1;
		if (j >= heap->heap_size)
			break;
		if (j + 1 < heap->heap_size &&
			heap->compare(*(heap->elems + j), *(heap->elems + (j + 1))) > 0)
			j++;
		if (heap->compare(last_elem, *(heap->elems + j)) <= 0)
			break;
		*(heap->elems + i) = *(heap->elems + j);
		i = j;
	}
	*(heap->elems + heap->heap_size) = NULL;
	*(heap->elems + i) = last_elem;
}

/* caller should be responsible for freeing this element */
void *
heap_pop(Heap heap)
{
	void *result = heap_root(heap);

	heap->elems[0] = NULL;

	if (result != NULL)
		heap_siftup(heap);

	return result;
}

/*************** test code ****************/
int int_compare(void *l, void *r)
{
	if ((*(int *)l) > (*(int *)r))
		return 1;
	if ((*(int *)l) == (*(int *)r))
		return 0;
	return -1;
}

#define INPUT_SIZE 10

int
main(int argc, char **argv)
{
	int **input = (int **) malloc(sizeof(int *) * INPUT_SIZE);
	if (input == NULL)
		return 1;

	int i;
	Heap heap;

	for (i = 0; i < INPUT_SIZE; ++i)
	{
		input[i] = (int *) malloc(sizeof(int));
		if (input[i] == NULL)
		{
			free(input);
			return 1;
		}
		*input[i] = (int) random() % 100;
	}
	fprintf(stderr, "input int array:\n");

	heap = heap_init((void **)input, INPUT_SIZE, INPUT_SIZE, sizeof(int), int_compare);

	for (i = 0; i < INPUT_SIZE; ++i)
	{
		fprintf(stderr, "%d ", *input[i]);
		free(input[i]);
	}
	free(input);

	if (heap == NULL)
		return 1;

	fprintf(stderr, "\nafter sorted:\n");
	for (i = 0; i < INPUT_SIZE; ++i)
	{
		int *elem = (int *) heap_pop(heap);
		if (elem == NULL)
		{
			heap_destroy(heap);
			return 2;
		}
		fprintf(stderr, "%d ", *elem);
		free(elem);
	}
	fprintf(stderr, "\n");
	return 0;
}
