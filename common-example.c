#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <asm/errno.h>

struct object
{
	/* These two protected by cache_lock. */
	/* This is protected by RCU */
	struct list_head list;
	int popularity;

	struct rcu_head rcu;

	atomic_t refcnt;

	/* Doesn't change once created. */
	int id;

	spinlock_t lock; /* Protects the name */
	char name[32];
};

/* Protects the cache, cache_num, and the objects within it */
static DEFINE_SPINLOCK(cache_lock);
static LIST_HEAD(cache);
static unsigned int cache_num = 0;
#define MAX_CACHE_SIZE 10

void object_put(struct object *obj)
{
	if (atomic_dec_and_test(&obj->refcnt))
		kfree(obj);
}

void object_get(struct object *obj)
{
	atomic_inc(&obj->refcnt);
}

/* Must be holding cache_lock */
static struct object *__cache_find(int id)
{
	struct object *i;

	list_for_each_entry(i, &cache, list) {
		if (i->id == id) {
			i->popularity++;
			return i;
		}
	}

	return NULL;
}

/* Final discard done once we know no readers are looking. */
static void cache_delete_rcu(void *arg)
{
	object_put(arg);
}

/* Must be holding cache_lock */
static void __cache_delete(struct object *obj)
{
	BUG_ON(!obj);
	list_del_rcu(&obj->list);
	cache_num--;
	call_rcu(&obj->rcu, cache_delete_rcu);
}

/* Must be holding cache_lock */
static void __cache_add(struct object *obj)
{
	list_add_rcu(&obj->list, &cache);
	if (++cache_num > MAX_CACHE_SIZE) {
		struct object *i, *outcast = NULL;
		list_for_each_entry(i, &cache, list) {
			if (!outcast || i->popularity < outcast->popularity)
				outcast = i;
		}
		__cache_delete(outcast);
	}
}

int cache_add(int id, const char *name)
{
	struct object *obj;
	unsigned long flags;

	if ((obj = kmalloc(sizeof(*obj), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	strlcpy(obj->name, name, sizeof(obj->name));
	obj->id = id;
	obj->popularity = 0;
	atomic_set(&obj->refcnt, 1); /* The cache holds a reference */
	spin_lock_init(&obj->lock);

	spin_lock_irqsave(&cache_lock, flags);
	__cache_add(obj);
	spin_unlock_irqrestore(&cache_lock, flags);

	return 0;
}

void cache_delete(int id)
{
	unsigned long flags;

	spin_lock_irqsave(&cache_lock, flags);
	__cache_delete(__cache_find(id));
	spin_unlock_irqrestore(&cache_lock, flags);
}

struct object *cache_find(int id)
{
	struct object *obj;

	rcu_read_lock();
	obj = __cache_find(id);
	if (obj)
		object_get(obj);
	rcu_read_unlock();

	return obj;
}
