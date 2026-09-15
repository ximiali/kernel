// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2017, 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/firmware/qcom/qcom_tzmem.h>
#include <linux/firmware/qcom/memory_dump.h>

#define MSM_DUMP_TABLE_VERSION		MSM_DUMP_MAKE_VERSION(2, 0)

struct msm_dump_table {
	u32 version;
	u32 num_entries;
	struct msm_dump_entry entries[MAX_NUM_ENTRIES];
};

struct msm_memory_dump {
	u64 table_phys;
	struct msm_dump_table *table;
	u64 shm_bridge_handle;
};

/*
 * Memory dump table layout
 * ------------------------
 *
 * IMEM (qcom,msm-imem-mem-dump-table)
 * +------------------------------------------+
 * |offset 0x000: memdump.table_phys (8 bytes) |
 * |offset 0x724: total_size         (8 bytes) |
 * +------------------------------------------+
 *
 * DMA-coherent buffer allocated in mem_dump_alloc() (dump_vaddr / phys_addr)
 * +--------------------------------------------------------------------------+
 * | struct msm_dump_table  "base table"                                      |
 * |   version     = MSM_DUMP_TABLE_VERSION                                   |
 * |   num_entries                                                            |
 * |   entries[0]  = { id    = MSM_DUMP_TABLE_APPS,                           |
 * |                   type  = MSM_DUMP_TYPE_TABLE,                           |
 * |                   addr  = phys addr of "apps table" below }              |
 * +--------------------------------------------------------------------------+
 * | struct msm_dump_table  "apps table"                                      |
 * |   version     = MSM_DUMP_TABLE_VERSION                                   |
 * |   num_entries                                                            |
 * |   entries[0]  = { id    = <qcom,dump-id of node 0>,                      |
 * |                   type  = MSM_DUMP_TYPE_DATA,                            |
 * |                   addr  = phys addr of msm_dump_data #0 below }          |
 * |   entries[1]  = { ... node 1 ... }                                       |
 * |   ...                                                                    |
 * +--------------------------------------------------------------------------+
 * |   struct msm_dump_data #0 (name/addr/len of dump buffer  #0)             |
 * |   raw dump buffer #0      (qcom,dump-size bytes from node 0)             |
 * +--------------------------------------------------------------------------+
 * |   struct msm_dump_data #1 (name/addr/len of dump buffer  #1)             |
 * |   raw dump buffer #1      (qcom,dump-size bytes from node 1)             |
 * +--------------------------------------------------------------------------+
 * |   ...                                                                    |
 * +--------------------------------------------------------------------------+
 *
 * memdump.table       - kernel virtual address of the "base table" above.
 * memdump.table_phys  - physical address of the "base table", also mirrored
 *                       into IMEM so that firmware can locate it after a crash.
 *
 * Each valid DT child node gets one MSM_DUMP_TYPE_DATA entry in the
 * "apps table", which references its struct msm_dump_data header;
 * that header in turn points at the actual dump buffer located immediately
 * after it in memory.
 */
static struct msm_memory_dump memdump;

static int msm_dump_table_register(struct msm_dump_entry *entry)
{
	struct msm_dump_entry *e;
	struct msm_dump_table *table = memdump.table;

	if (!table || table->num_entries >= MAX_NUM_ENTRIES)
		return -EINVAL;

	e = &table->entries[table->num_entries];
	e->id = entry->id;
	e->type = MSM_DUMP_TYPE_TABLE;
	e->addr = entry->addr;
	table->num_entries++;

	return 0;
}

static struct msm_dump_table *msm_dump_get_table(enum msm_dump_table_ids id)
{
	struct msm_dump_table *table = memdump.table;
	int i;
	unsigned long offset;

	if (!table) {
		pr_err("mem dump base table does not exist\n");
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; i < MAX_NUM_ENTRIES; i++) {
		if (table->entries[i].id == id)
			break;
	}
	if (i == MAX_NUM_ENTRIES || !table->entries[i].addr) {
		pr_err("mem dump base table entry %d invalid\n", id);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Table address stored in dump entries are physical addresses
	 * within the DMA-coherent allocation. Convert the stored physical
	 * address back into a kernel virtual address by computing its
	 * offset from the base table.
	 */
	offset = table->entries[i].addr - memdump.table_phys;
	table = (void *)memdump.table + offset;

	return table;
}

static int register_dump_table_entry(enum msm_dump_table_ids id,
				     struct msm_dump_entry *entry)
{
	struct msm_dump_entry *e;
	struct msm_dump_table *table;

	table = msm_dump_get_table(id);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (!table || table->num_entries >= MAX_NUM_ENTRIES)
		return -EINVAL;

	e = &table->entries[table->num_entries];
	e->id = entry->id;
	e->type = MSM_DUMP_TYPE_DATA;
	e->addr = entry->addr;
	table->num_entries++;

	return 0;
}

/* Offset in IMEM used by firmware to read the total dump buffer size */
#define MSM_DUMP_TOTAL_SIZE_OFFSET	0x724
static int init_memdump_imem_area(size_t size)
{
	struct device_node *np;
	void __iomem *imem_base;

	np = of_find_compatible_node(NULL, NULL,
				     "qcom,msm-imem-mem-dump-table");
	if (!np) {
		pr_err("mem dump base table DT node does not exist\n");
		return -ENODEV;
	}

	imem_base = of_iomap(np, 0);
	of_node_put(np);
	if (!imem_base) {
		pr_err("mem dump base table imem offset mapping failed\n");
		return -ENOMEM;
	}

	memcpy_toio(imem_base, &memdump.table_phys,
		    sizeof(memdump.table_phys));
	memcpy_toio(imem_base + MSM_DUMP_TOTAL_SIZE_OFFSET,
		    &size, sizeof(size_t));

	/* Ensure write to imem_base is complete before unmapping */
	mb();
	pr_info("MSM Memory Dump base table set up in IMEM\n");

	iounmap(imem_base);
	return 0;
}

static void deinit_memdump_imem_area(void)
{
	struct device_node *np;
	void __iomem *imem_base;

	np = of_find_compatible_node(NULL, NULL,
				     "qcom,msm-imem-mem-dump-table");
	if (!np) {
		pr_err("mem dump base table DT node does not exist\n");
		return;
	}

	imem_base = of_iomap(np, 0);
	of_node_put(np);
	if (!imem_base) {
		pr_err("mem dump base table imem offset mapping failed\n");
		return;
	}

	memset_io(imem_base, 0, sizeof(memdump.table_phys));
	memset_io(imem_base + MSM_DUMP_TOTAL_SIZE_OFFSET, 0, sizeof(size_t));

	/* Ensure write to imem_base is complete before unmapping */
	mb();
	iounmap(imem_base);
}

/**
 * init_memory_dump() - Initialize dump tables in the allocated buffer.
 * @dump_vaddr: Base virtual address of the dump region.
 * @phys_addr: Physical address corresponding to @dump_vaddr.
 *
 * Create the base table and apps table used by firmware
 * to locate registered dump regions after a crash.
 *
 * Return: 0 on success or a negative error code.
 */
static int init_memory_dump(void *dump_vaddr, phys_addr_t phys_addr)
{
	struct msm_dump_table *table;
	struct msm_dump_entry entry;
	int ret;

	memdump.table = dump_vaddr;
	memdump.table->version = MSM_DUMP_TABLE_VERSION;
	memdump.table_phys = phys_addr;
	dump_vaddr +=  sizeof(*table);
	phys_addr += sizeof(*table);
	table = dump_vaddr;
	table->version = MSM_DUMP_TABLE_VERSION;
	entry.id = MSM_DUMP_TABLE_APPS;
	entry.addr = phys_addr;
	ret = msm_dump_table_register(&entry);
	if (ret) {
		pr_err("mem dump apps data table register failed\n");
		return ret;
	}
	pr_info("MSM Memory Dump apps data table set up\n");

	return 0;
}

static int mem_dump_reserve_mem(struct device *dev)
{
	struct device_node *mem_node;
	int ret;

	mem_node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (mem_node) {
		ret = of_reserved_mem_device_init_by_idx(dev, dev->of_node, 0);
		of_node_put(mem_node);
		if (ret) {
			dev_err(dev,
				"Failed to initialize reserved mem, ret %d\n",
				ret);
			return ret;
		}
	}
	return 0;
}

#define MSM_DUMP_DATA_SIZE sizeof(struct msm_dump_data)

/**
 * mem_dump_alloc() - Allocate and initialize dump memory.
 * @pdev: Platform device.
 *
 * Calculate the required memory size from DT child nodes,
 * allocate a DMA-coherent buffer, create dump tables and
 * expose the base table address through IMEM.
 *
 * Return: 0 on success or a negative error code.
 */
static int mem_dump_alloc(struct platform_device *pdev)
{
	struct device_node *child_node;
	const struct device_node *node = pdev->dev.of_node;
	struct msm_dump_data *dump_data;
	struct msm_dump_entry dump_entry;
	size_t total_size;
	u32 size, id;
	int ret, no_of_nodes = 0;
	dma_addr_t dma_handle;
	phys_addr_t phys_addr;
	struct sg_table mem_dump_sgt;
	void *dump_vaddr;
	u64 shm_bridge_handle;

	if (mem_dump_reserve_mem(&pdev->dev) != 0)
		return -ENOMEM;
	/* For dump table registration with IMEM */
	total_size = sizeof(struct msm_dump_table) * 2;
	for_each_available_child_of_node(node, child_node) {
		ret = of_property_read_u32(child_node, "qcom,dump-size", &size);
		if (ret) {
			dev_err(&pdev->dev, "Unable to find size for %s\n",
				child_node->name);
			continue;
		}

		ret = of_property_read_u32(child_node, "qcom,dump-id", &id);
		if (ret) {
			dev_err(&pdev->dev, "Unable to find id for %s\n",
				child_node->name);
			continue;
		}

		if (no_of_nodes >= MAX_NUM_ENTRIES) {
			dev_err(&pdev->dev, "Dump entries exceed limit %d\n",
				MAX_NUM_ENTRIES);
			of_node_put(child_node);
			break;
		}

		total_size += size;
		no_of_nodes++;
	}

	total_size += (MSM_DUMP_DATA_SIZE * no_of_nodes);
	total_size = ALIGN(total_size, SZ_4K);
	dump_vaddr = dmam_alloc_coherent(&pdev->dev, total_size,
					 &dma_handle, GFP_KERNEL);
	if (!dump_vaddr) {
		ret = -ENOMEM;
		goto release_reserved_mem;
	}

	ret = dma_get_sgtable(&pdev->dev, &mem_dump_sgt, dump_vaddr,
			      dma_handle, total_size);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get sgtable, ret=%d\n", ret);
		goto release_reserved_mem;
	}
	phys_addr = page_to_phys(sg_page(mem_dump_sgt.sgl));
	sg_free_table(&mem_dump_sgt);

	memset(dump_vaddr, 0x0, total_size);
	ret = qcom_tzmem_shm_bridge_create(phys_addr, total_size, &shm_bridge_handle);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create shm bridge.ret=%d\n", ret);
		goto release_reserved_mem;
	}
	memdump.shm_bridge_handle = shm_bridge_handle;

	ret = init_memory_dump(dump_vaddr, phys_addr);
	if (ret) {
		dev_err(&pdev->dev, "Memory Dump table set up is failed\n");
		goto delete_shm_bridge;
	}

	ret = init_memdump_imem_area(total_size);
	if (ret)
		goto delete_shm_bridge;

	dump_vaddr += (sizeof(struct msm_dump_table) * 2);
	phys_addr += (sizeof(struct msm_dump_table) * 2);
	no_of_nodes = 0;
	for_each_available_child_of_node(node, child_node) {
		if (of_property_read_u32(child_node, "qcom,dump-size", &size))
			continue;

		if (of_property_read_u32(child_node, "qcom,dump-id", &id))
			continue;

		if (no_of_nodes >= MAX_NUM_ENTRIES) {
			of_node_put(child_node);
			break;
		}
		no_of_nodes++;

		dump_data = dump_vaddr;
		/* The buffer immediately follows its msm_dump_data header */
		dump_data->addr = phys_addr + MSM_DUMP_DATA_SIZE;
		dump_data->len = size;
		dump_entry.id = id;
		strscpy(dump_data->name, child_node->name,
			sizeof(dump_data->name));
		dump_entry.addr = phys_addr;
		ret = register_dump_table_entry(MSM_DUMP_TABLE_APPS, &dump_entry);
		if (ret)
			dev_err(&pdev->dev, "Data dump setup failed, id = %d\n",
				id);

		dump_vaddr += (size + MSM_DUMP_DATA_SIZE);
		phys_addr += (size  + MSM_DUMP_DATA_SIZE);
	}

	return 0;

delete_shm_bridge:
	qcom_tzmem_shm_bridge_delete(memdump.shm_bridge_handle);
	memdump.shm_bridge_handle = 0;

release_reserved_mem:
	of_reserved_mem_device_release(&pdev->dev);

	return ret;
}

static int mem_dump_probe(struct platform_device *pdev)
{
	int ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret < 0)
		return ret;

	ret = mem_dump_alloc(pdev);
	return ret;
}

static void mem_dump_remove(struct platform_device *pdev)
{
	deinit_memdump_imem_area();
	if (memdump.shm_bridge_handle)
		qcom_tzmem_shm_bridge_delete(memdump.shm_bridge_handle);
	of_reserved_mem_device_release(&pdev->dev);
	memdump.shm_bridge_handle = 0;
}

static const struct of_device_id mem_dump_match_table[] = {
	{.compatible = "qcom,mem-dump",},
	{}
};

static struct platform_driver mem_dump_driver = {
	.probe = mem_dump_probe,
	.remove = mem_dump_remove,
	.driver = {
		.name = "msm_mem_dump",
		.of_match_table = mem_dump_match_table,
	},
};

module_platform_driver(mem_dump_driver);

MODULE_DESCRIPTION("Memory Dump V2 Driver");
MODULE_LICENSE("GPL");
