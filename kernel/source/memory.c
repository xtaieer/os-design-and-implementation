/***************************************************
*		版权声明
*
*	本操作系统名为：MINE
*	该操作系统未经授权不得以盈利或非盈利为目的进行开发，
*	只允许个人学习以及公开交流使用
*
*	代码最终所有权及解释权归田宇所有；
*
*	本模块作者：	田宇
*	EMail:		345538255@qq.com
*
*
***************************************************/
#include "memory.h"
#include "lib.h"

unsigned long page_init(struct Page* page, unsigned long flags)
{
	if (!page->attribute)
	{
		*(memory_management_struct.bits_map + ((page->PHY_address >> PAGE_SHIFT) >> 6)) |= 1UL << (page->PHY_address >> PAGE_SHIFT) % 64;
		page->attribute = flags;
		page->reference_count++;
		page->zone_struct->page_using_count++;
		page->zone_struct->page_free_count--;
		page->zone_struct->total_pages_link++;
	}
	else if ((page->attribute & PG_Referenced) || (page->attribute & PG_K_Share_To_U) || (flags & PG_Referenced) || (flags & PG_K_Share_To_U))
	{
		page->attribute |= flags;
		page->reference_count++;
		page->zone_struct->total_pages_link++;
	}
	else
	{
		*(memory_management_struct.bits_map + ((page->PHY_address >> PAGE_SHIFT) >> 6)) |= 1UL << (page->PHY_address >> PAGE_SHIFT) % 64;
		page->attribute |= flags;
	}
	return 0;
}


void init_memory()
{
	int i, j;
	// 通过所有可用的物理内存（RAM）的大小
	unsigned long TotalMem = 0;
	struct E820* p = NULL;

	color_printk(BLUE, BLACK, "Display Physics Address MAP,Type(1:RAM,2:ROM or Reserved,3:ACPI Reclaim Memory,4:ACPI NVS Memory,Others:Undefine)\n");
	p = (struct E820*)0xffff800000007e00;

	int max_phy_index = 0;
	for (i = 0; i < 32; i++)
	{
		color_printk(ORANGE, BLACK, "Address:%#018lx\tLength:%#018lx\tType:%#010x\n", p->address, p->length, p->type);
		unsigned long tmp = 0;
		if (p->type == 1) 
		{
			TotalMem += p->length;
			max_phy_index = i;
		}

		memory_management_struct.e820[i].address += p->address;

		memory_management_struct.e820[i].length += p->length;

		memory_management_struct.e820[i].type = p->type;

		// 要注意到这个e820_length是可以访问的最大索引，和普通的数组长度有点不同
		memory_management_struct.e820_length = i;

		p++;
		if (p->type > 4 || p->length == 0 || p->type < 1)
			break;
	}

	color_printk(ORANGE, BLACK, "OS Can Used Total RAM:%#018lx\n", TotalMem);

	TotalMem = 0;

	for (i = 0; i <= memory_management_struct.e820_length; i++)
	{
		unsigned long start, end;
		if (memory_management_struct.e820[i].type != 1)
			continue;
		start = PAGE_ALIGN(memory_management_struct.e820[i].address);
		end = ((memory_management_struct.e820[i].address + memory_management_struct.e820[i].length) >> PAGE_SHIFT) << PAGE_SHIFT;
		// 过滤掉小于一页大小的物理内存区域，不统计
		if (end <= start)
			continue;
		// 有多少个的物理页
		TotalMem += (end - start) >> PAGE_SHIFT;
	}

	color_printk(ORANGE, BLACK, "OS Can Used Total 4kb PAGEs:%#010x=%010d\n", TotalMem, TotalMem);

	// 最大的物理地址，应该是最大的可访问的物理地址+1（不可访问的地方）
	TotalMem = memory_management_struct.e820[max_phy_index].address + memory_management_struct.e820[max_phy_index].length;

	//bits map construction init
	color_printk(WHITE, BLACK, "total mem %#018lx \n", TotalMem);

	// 存储在内核结束后的第一个4kb页面（线性空间和物理空间同时分配的）上
	memory_management_struct.bits_map = (unsigned long*)((memory_management_struct.end_brk + PAGE_4K_SIZE - 1) & PAGE_4K_MASK);
	// 一个物理页对应一位，TotalMem / PAGE_SIZE
	memory_management_struct.bits_size = TotalMem >> PAGE_SHIFT;
	// 
	memory_management_struct.bits_length = (((unsigned long)(TotalMem >> PAGE_SHIFT) + sizeof(long) * 8 - 1) / 8) & (~(sizeof(long) - 1));
	color_printk(ORANGE, BLACK, "bits_map:%#018lx,bits_size:%#018lx,bits_length:%#018lx\n", memory_management_struct.bits_map, memory_management_struct.bits_size, memory_management_struct.bits_length);

	// 默认所有的物理页都不可用，然后后面再遍历可用的物理内存，重新复位
	memset(memory_management_struct.bits_map, 0xff, memory_management_struct.bits_length);		//init bits map memory

	//pages construction init
	// 存在在bit_map之后的另一个4kb页面（线性空间和物理空间同时分配的）上
	memory_management_struct.pages_struct = (struct Page*)(((unsigned long)memory_management_struct.bits_map + memory_management_struct.bits_length + PAGE_4K_SIZE - 1) & PAGE_4K_MASK);

	memory_management_struct.pages_size = TotalMem >> PAGE_SHIFT;

	memory_management_struct.pages_length = ((TotalMem >> PAGE_SHIFT) * sizeof(struct Page) + sizeof(long) - 1) & (~(sizeof(long) - 1));

	memset(memory_management_struct.pages_struct, 0x00, memory_management_struct.pages_length);	//init pages memory

	//zones construction init
	// 存在在pages_struct之后的另一个页面（线性空间和物理空间同时分配的）上
	memory_management_struct.zones_struct = (struct Zone*)(((unsigned long)memory_management_struct.pages_struct + memory_management_struct.pages_length + PAGE_4K_SIZE - 1) & PAGE_4K_MASK);

	memory_management_struct.zones_size = 0;

	memory_management_struct.zones_length = (5 * sizeof(struct Zone) + sizeof(long) - 1) & (~(sizeof(long) - 1));

	memset(memory_management_struct.zones_struct, 0x00, memory_management_struct.zones_length);	//init zones memory

	for (i = 0; i <= memory_management_struct.e820_length; i++)
	{
		unsigned long start, end;
		struct Zone* z;
		struct Page* p;
		unsigned long* b;

		// 只关心物理内存
		if (memory_management_struct.e820[i].type != 1)
			continue;

		// 只关心至少一个物理页面大小的内存
		start = PAGE_ALIGN(memory_management_struct.e820[i].address);
		end = ((memory_management_struct.e820[i].address + memory_management_struct.e820[i].length) >> PAGE_SHIFT) << PAGE_SHIFT;
		if (end <= start)
			continue;

		//zone init

		z = memory_management_struct.zones_struct + memory_management_struct.zones_size;
		memory_management_struct.zones_size++;

		z->zone_start_address = start;
		z->zone_end_address = end;
		z->zone_length = end - start;

		z->page_using_count = 0;
		z->page_free_count = (end - start) >> PAGE_SHIFT;

		z->total_pages_link = 0;

		z->attribute = 0;
		z->GMD_struct = &memory_management_struct;

		z->pages_length = (end - start) >> PAGE_SHIFT;
		z->pages_group = (struct Page*)(memory_management_struct.pages_struct + (start >> PAGE_SHIFT));

		//page init
		p = z->pages_group;
		for (j = 0; j < z->pages_length; j++, p++)
		{
			p->zone_struct = z;
			p->PHY_address = start + PAGE_SIZE * j;
			p->attribute = 0;

			p->reference_count = 0;

			p->age = 0;

			// 要看一下这个位图的计算对不对
			*(memory_management_struct.bits_map + ((p->PHY_address >> PAGE_SHIFT) >> 6)) ^= 1UL << (p->PHY_address >> PAGE_SHIFT) % 64;

		}


		/////////////init address 0 to page struct 0; because the memory_management_struct.e820[0].type != 1

		memory_management_struct.pages_struct->zone_struct = memory_management_struct.zones_struct;

		memory_management_struct.pages_struct->PHY_address = 0UL;
		memory_management_struct.pages_struct->attribute = 0;
		memory_management_struct.pages_struct->reference_count = 0;
		memory_management_struct.pages_struct->age = 0;

		/////////////

		memory_management_struct.zones_length = (memory_management_struct.zones_size * sizeof(struct Zone) + sizeof(long) - 1) & (~(sizeof(long) - 1));

		color_printk(ORANGE, BLACK, "bits_map:%#018lx,bits_size:%#018lx,bits_length:%#018lx\n", memory_management_struct.bits_map, memory_management_struct.bits_size, memory_management_struct.bits_length);

		color_printk(ORANGE, BLACK, "pages_struct:%#018lx,pages_size:%#018lx,pages_length:%#018lx\n", memory_management_struct.pages_struct, memory_management_struct.pages_size, memory_management_struct.pages_length);

		color_printk(ORANGE, BLACK, "zones_struct:%#018lx,zones_size:%#018lx,zones_length:%#018lx\n", memory_management_struct.zones_struct, memory_management_struct.zones_size, memory_management_struct.zones_length);

		ZONE_DMA_INDEX = 0;	//need rewrite in the future
		ZONE_NORMAL_INDEX = 0;	//need rewrite in the future

		for (i = 0; i < memory_management_struct.zones_size; i++)	//need rewrite in the future
		{
			struct Zone* z = memory_management_struct.zones_struct + i;
			color_printk(ORANGE, BLACK, "zone_start_address:%#018lx,zone_end_address:%#018lx,zone_length:%#018lx,pages_group:%#018lx,pages_length:%#018lx\n", z->zone_start_address, z->zone_end_address, z->zone_length, z->pages_group, z->pages_length);

			if (z->zone_start_address == 0x100000000)
				ZONE_UNMAPED_INDEX = i;
		}

		memory_management_struct.end_of_struct = (unsigned long)((unsigned long)memory_management_struct.zones_struct + memory_management_struct.zones_length + sizeof(long) * 32) & (~(sizeof(long) - 1));	////need a blank to separate memory_management_struct

		color_printk(ORANGE, BLACK, "start_code:%#018lx,end_code:%#018lx,end_data:%#018lx,end_brk:%#018lx,end_of_struct:%#018lx\n", memory_management_struct.start_code, memory_management_struct.end_code, memory_management_struct.end_data, memory_management_struct.end_brk, memory_management_struct.end_of_struct);


		// 初始化内核使用的所有页面信息
		i = Virt_To_Phy(memory_management_struct.end_of_struct) >> PAGE_SHIFT;
		for (j = 0; j <= i; j++)
		{
			page_init(memory_management_struct.pages_struct + j, PG_PTable_Maped | PG_Kernel_Init | PG_Active | PG_Kernel);
		}


		Global_CR3 = Get_gdt();
		// 就要注意修改页表时不能直接使用CR3中的地址，要使用线性地址
		// CR3（根页表的）的物理地址
		color_printk(INDIGO, BLACK, "Global_CR3\t:%#018lx\n", Global_CR3);
		// CR3（根页表的）的线性地址
		color_printk(INDIGO, BLACK, "*Global_CR3\t:%#018lx\n", *Phy_To_Virt(Global_CR3) & (~0xff));
		// 次级页表的线性地址
		color_printk(PURPLE, BLACK, "**Global_CR3\t:%#018lx\n", *Phy_To_Virt(*Phy_To_Virt(Global_CR3) & (~0xff)) & (~0xff));


		for (i = 0; i < 10; i++)
			*(Phy_To_Virt(Global_CR3) + i) = 0UL;

		flush_tlb();
	}
}

/*

	number: number < 64

	zone_select: zone select from dma , mapped in  pagetable , unmapped in pagetable

	page_flags: struct Page flages

*/

struct Page* alloc_pages(int zone_select, int number, unsigned long page_flags)
{
	int i;
	unsigned long page = 0;

	int zone_start = 0;
	int zone_end = 0;

	switch (zone_select)
	{
	case ZONE_DMA:
		zone_start = 0;
		zone_end = ZONE_DMA_INDEX;

		break;

	case ZONE_NORMAL:
		zone_start = ZONE_DMA_INDEX;
		zone_end = ZONE_NORMAL_INDEX;

		break;

	case ZONE_UNMAPED:
		zone_start = ZONE_UNMAPED_INDEX;
		zone_end = memory_management_struct.zones_size - 1;

		break;

	default:
		color_printk(RED, BLACK, "alloc_pages error zone_select index\n");
		return NULL;
		break;
	}

	for (i = zone_start; i <= zone_end; i++)
	{
		struct Zone* z;
		unsigned long j;
		unsigned long start, end, length;
		unsigned long tmp;

		if ((memory_management_struct.zones_struct + i)->page_free_count < number)
			continue;

		z = memory_management_struct.zones_struct + i;
		// 把地址等数据映射成page的索引
		start = z->zone_start_address >> PAGE_SHIFT;
		end = z->zone_end_address >> PAGE_SHIFT;
		length = z->zone_length >> PAGE_SHIFT;

		tmp = 64 - start % 64;

		for (j = start; j <= end; j += j % 64 ? tmp : 64)
		{
			unsigned long* p = memory_management_struct.bits_map + (j >> 6);
			unsigned long shift = j % 64;
			unsigned long k;
			for (k = shift; k < 64 - shift; k++)
			{
				if (!(((*p >> k) | (*(p + 1) << (64 - k))) & (number == 64 ? 0xffffffffffffffffUL : ((1UL << number) - 1))))
				{
					unsigned long	l;
					page = j + k - 1;
					for (l = 0; l < number; l++)
					{
						struct Page* x = memory_management_struct.pages_struct + page + l;
						page_init(x, page_flags);
					}
					goto find_free_pages;
				}
			}

		}
	}

	return NULL;

find_free_pages:

	return (struct Page*)(memory_management_struct.pages_struct + page);
}
