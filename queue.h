
#ifndef __QUEUE_H__
#define __QUEUE_H__


struct queue {
	int head;
	int tail;
	int size;
	void *entries;
	void (*assign_to)(struct queue *, int head, void *y); // push
	void (*assign_from)(struct queue *, int tail, void *x); //pop
};

/* shall be atomic */
#define rd_queue_head(q)	\
		(q->head)

/* shall be atomic */
#define rd_queue_tail(q)	\
		(q->tail)

/* shall be atomic */
#define set_queue_head(q, val)	\
		do {					\
			q->head = val;			\
		} while (0)

/* shall be atomic */
#define set_queue_tail(q, val)	\
		do {					\
			q->tail = val;			\
		} while (0)

#define queue_arr(q)		\
		(q->arr)

/* size shall be pow of 2 */
static inline bool qfull(int head, int tail, int size)
{
	int const mask = size - 1;

	if (((head - tail) & mask) == mask) {
		return true;
	}
	return false;
}

static inline bool qempty(int head, int tail)
{
	return head == tail;
}

static int qpush(struct queue *qbase, void *val)
{
	int const tail = rd_queue_tail(qbase);
	int const size = qbase->size;
	int head = rd_queue_head(qbase);
	int head_;

	if (qfull(head, tail, size)) {
		return -1;
	}
#ifndef USER_APP
	printk("assign_to In \r\n");
#endif
	qbase->assign_to(qbase, head_ = head, val);
#ifndef USER_APP
	printk("assign_to Out \r\n");
#endif
	head = (head + 1) & (size - 1);
	set_queue_head(qbase, head);

	return 0;
}

static int qpop(struct queue *q, void *val)
{
	int const head = rd_queue_head(q);
	int const size = q->size;
	int tail = rd_queue_tail(q);
	int tail_;
	
	if (qempty(head, tail)) {
		return -1;
	}

	q->assign_from(q, tail_ = tail, val);
	tail = (tail + 1) & (size - 1);
	set_queue_tail(q, tail);

	return 0;
}

#endif
