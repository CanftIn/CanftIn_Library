#pragma once
#ifndef _ALLOCATOR_H_
#define _ALLOCATOR_H_

#include <new>
#include <cstdlib>

namespace Can
{
	enum { _ALIGN = 8 };
	enum { _MAX_BYTES = 128 };
	enum { _NFREE_LISTS = _MAX_BYTES / _ALIGN };

	//second allocator
	class Alloc
	{
	private:
		union obj
		{
			union obj *free_list_link;
			char client_data[1];
		};

	private:
		static obj* volatile free_list[_NFREE_LISTS];

		static size_t FREELIST_INDEX(size_t bytes)
		{
			return (((bytes)+_ALIGN - 1) / _ALIGN - 1);
		}

		static size_t ROUND_UP(size_t bytes)
		{
			return (((bytes)+_ALIGN - 1) & ~(_ALIGN - 1));
		}

		static void* refill(size_t n);

		static char* chunk_alloc(size_t size, size_t& nobjs);

		static char* start_free;

		static char* end_free;

		static size_t heap_size;

	public:
		static void* allocate(size_t n);
		static void deallocate(void *p, size_t n);
		static void* reallocate(void *p, size_t old_sz, size_t new_sz);
	};

	char *Alloc::start_free = 0;
	char *Alloc::end_free = 0;
	size_t Alloc::heap_size = 0;

	Alloc::obj* volatile Alloc::free_list[_NFREE_LISTS] =
	{
		nullptr,nullptr,nullptr,nullptr,
		nullptr,nullptr,nullptr,nullptr,
		nullptr,nullptr,nullptr,nullptr,
		nullptr,nullptr,nullptr,nullptr
	};

	void* Alloc::allocate(size_t bytes)
	{
		if (bytes > _MAX_BYTES)
		{
			return Malloc_Allocator::allocate(bytes);
		}
		size_t index = FREELIST_INDEX(bytes);
		obj *list = free_list[index];
		if (list)
		{
			free_list[index] = list->free_list_link;
			return list;
		}
		else
		{
			return refill(ROUND_UP(bytes));
		}
	}
	
	void Alloc::deallocate(void *ptr, size_t bytes)
	{
		if (bytes > _MAX_BYTES)
		{
			Malloc_Allocator::deallocate(ptr, bytes);
			return;
		}
		size_t index = FREELIST_INDEX(bytes);
		obj *node = static_cast<obj *>(ptr);
		node->free_list_link = free_list[index];
		free_list[index] = node;
	}

	void* Alloc::reallocate(void *ptr, size_t old_sz, size_t new_sz)
	{
		deallocate(ptr, old_sz);
		ptr = allocate(new_sz);
		return ptr;
	}

	void* Alloc::refill(size_t bytes)
	{
		size_t nobjs = 20;
		char *chunk = chunk_alloc(bytes, nobjs);
		obj * volatile *my_free_list = 0;
		obj *result = 0;
		obj *current_obj = 0;
		obj *next_obj = 0;

		if (nobjs == 1)
		{
			return chunk;
		}
		else
		{
			my_free_list = free_list + FREELIST_INDEX(bytes);
			result = (obj *)(chunk);
			*my_free_list = next_obj = (obj *)(chunk + bytes);
			for (int i = 1; ; ++i)
			{
				current_obj = next_obj;
				next_obj = (obj *)((char *)next_obj + bytes);
				if (nobjs - 1 == i)
				{
					current_obj->free_list_link = 0;
					break;
				}
				else
				{
					current_obj->free_list_link = next_obj;
				}
			}
			return (result);
		}
	}

	char* Alloc::chunk_alloc(size_t bytes, size_t& nobjs)
	{
		char *result = 0;
		size_t total_bytes = bytes * nobjs;
		size_t bytes_left = end_free - start_free;

		if (bytes_left >= total_bytes)
		{
			result = start_free;
			start_free = start_free + total_bytes;
			return result;
		}
		else if (bytes_left >= bytes)
		{
			nobjs = bytes_left / bytes;
			total_bytes = nobjs * bytes;
			result = start_free;
			start_free += total_bytes;
			return result;
		}
		else
		{
			size_t bytes_to_get = 2 * total_bytes + ROUND_UP(heap_size >> 4);
			if (bytes_left > 0) {
				obj * volatile *my_free_list = free_list + FREELIST_INDEX(bytes_left);
				((obj *)start_free)->free_list_link = *my_free_list;
				*my_free_list = (obj *)start_free;
			}
			start_free = (char *)malloc(bytes_to_get);
			if (!start_free) {
				obj * volatile *my_free_list = 0, *p = 0;
				for (int i = 0; i <= _MAX_BYTES; i += _ALIGN) {
					my_free_list = free_list + FREELIST_INDEX(i);
					p = *my_free_list;
					if (!p) {
						*my_free_list = p->free_list_link;
						start_free = (char *)p;
						end_free = start_free + i;
						return chunk_alloc(bytes, nobjs);
					}
				}
				end_free = 0;
			}
			heap_size += bytes_to_get;
			end_free = start_free + bytes_to_get;
			return chunk_alloc(bytes, nobjs);
		}
	}

	//first allocator
	class Malloc_Allocator
	{
	public:
		static void* allocate(size_t n)
		{
			void *result = malloc(n);
			if (result == nullptr)
				result == oom_malloc(n);
			return (result);
		}

		static void deallocate(void *p, size_t)
		{
			free(p);
		}

		static void* reallocate(void *p, size_t, size_t new_size)
		{
			void *result = realloc(p, new_size);
			if (result == nullptr)
				result = oom_realloc(p, new_size);
			return (result);
		}

		static void(*set_malloc_handler(void(*f)()))()
		{
			void(*old)() = malloc_allo_oom_handler;
			malloc_allo_oom_handler = f;
			return (old);
		}

	private:
		static void* oom_malloc(size_t);
		static void* oom_realloc(void*, size_t);
		static void(*malloc_allo_oom_handler)();
	};

	void(*Malloc_Allocator::malloc_allo_oom_handler)() = nullptr;

	void* Malloc_Allocator::oom_malloc(size_t n)
	{
		void(*my_malloc_handler)();
		void *result;
		for (;;)
		{
			my_malloc_handler = malloc_allo_oom_handler;
			if (my_malloc_handler == nullptr)
				throw std::bad_alloc();
			(*my_malloc_handler)();
			result = malloc(n);
			if (result)
				return (result);
		}
	}

	void* Malloc_Allocator::oom_realloc(void *p, size_t n)
	{
		void(*my_malloc_handler)();
		void *result;
		for (;;)
		{
			my_malloc_handler = malloc_allo_oom_handler;
			if (my_malloc_handler == nullptr)
				throw std::bad_alloc();
			(*my_malloc_handler)();
			result = realloc(p, n);
			if (result)
				return (result);
		}
	}
}


#endif // !_ALLOCATOR_H_
