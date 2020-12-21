#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <asm/errno.h>

struct object
{
	struct list_head list;
	atomic_t refcnt;
	int id;
	char name[32];
	int popularity;
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

/* Must be holding cache_lock */
static void __cache_delete(struct object *obj)
{
	BUG_ON(!obj);
	list_del(&obj->list);
	object_put(obj);
	cache_num--;
}

/* Must be holding cache_lock */
static void __cache_add(struct object *obj)
{
	list_add(&obj->list, &cache);
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
	unsigned long flags;

	spin_lock_irqsave(&cache_lock, flags);
	obj = __cache_find(id);
	if (obj)
		object_get(obj);
	spin_unlock_irqrestore(&cache_lock, flags);

	return obj;
}
