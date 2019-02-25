/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <android_ab.h>

#include <android_bootloader_message.h>
#include <common.h>
#include <malloc.h>
#include <u-boot/crc.h>

/** android_boot_control_compute_crc - Compute the CRC-32 of the bootloader
 * control struct. Only the bytes up to the crc32_le field are considered for
 * the CRC-32 calculation.
 */
static uint32_t android_boot_control_compute_crc(
		struct android_bootloader_control *abc)
{
	return crc32(0, (void *)abc, offsetof(typeof(*abc), crc32_le));
}

/** android_boot_control_default - Initialize android_bootloader_control to the
 * default value which allows to boot all slots in order from the first one.
 * This value should be used when the bootloader message is corrupted, but not
 * when a valid message indicates that all slots are unbootable.
 */
void android_boot_control_default(struct android_bootloader_control *abc)
{
	int i;
	const struct android_slot_metadata metadata = {
		.priority = 15,
		.tries_remaining = 7,
		.successful_boot = 0,
		.verity_corrupted = 0,
		.reserved = 0
	};
	memcpy(abc->slot_suffix, "a\0\0\0", 4);
	abc->magic = ANDROID_BOOT_CTRL_MAGIC;
	abc->version = ANDROID_BOOT_CTRL_VERSION;
	abc->nb_slot = ARRAY_SIZE(abc->slot_info);
	memset(abc->reserved0, 0, sizeof(abc->reserved0));
	for (i = 0; i < abc->nb_slot; ++i) {
		abc->slot_info[i] = metadata;
	}
	memset(abc->reserved1, 0, sizeof(abc->reserved1));
	abc->crc32_le = android_boot_control_compute_crc(abc);
}

/** android_boot_control_create_from_disk
 * Load the boot_control struct from disk into newly allocated memory. This
 * function allocates and returns an integer number of disk blocks, based on the
 * block size of the passed device to help performing a read-modify-write
 * operation on the boot_control struct. The boot_control struct offset (2 KiB)
 * must be a multiple of the device block size, for simplicity.
 * @dev_desc: device where to read the boot_control struct from.
 * @part_info: partition in 'dev_desc' where to read from, normally the "misc"
 *             partition should be used.
 */
static void *android_boot_control_create_from_disk(
		struct blk_desc *dev_desc,
		const disk_partition_t *part_info)
{
	ulong abc_offset, abc_blocks;
	void *buf;

	abc_offset = offsetof(struct android_bootloader_message_ab,
			      slot_suffix);
	if (abc_offset % part_info->blksz) {
		printf("ANDROID: Boot control block not block aligned.\n");
		return NULL;
	}
	abc_offset /= part_info->blksz;

	abc_blocks = DIV_ROUND_UP(sizeof(struct android_bootloader_control),
				  part_info->blksz);
	if (abc_offset + abc_blocks > part_info->size) {
		printf("ANDROID: boot control partition too small. Need at"
		       " least %lu blocks but have %lu blocks.\n",
		       abc_offset + abc_blocks, part_info->size);
		return NULL;
	}
	buf = malloc(abc_blocks * part_info->blksz);
	if (!buf)
		return NULL;

	if (blk_dread(dev_desc, part_info->start + abc_offset, abc_blocks,
		      buf) != abc_blocks) {
		printf("ANDROID: Could not read from boot control partition\n");
		free(buf);
		return NULL;
	}
	debug("ANDROID: Loaded ABC, %lu blocks.\n", abc_blocks);
	return buf;
}

/** android_boot_control_store
 * Store the loaded boot_control block back to the same location it was read
 * from with android_boot_control_create_from_misc().
 *
 * @abc_data_block: pointer to the boot_control struct and the extra bytes after
 *                  it up to the nearest block boundary.
 * @dev_desc: device where we should write the boot_control struct.
 * @part_info: partition on the 'dev_desc' where to write.
 * @return 0 on success and -1 on error.
 */
static int android_boot_control_store(void *abc_data_block,
				      struct blk_desc *dev_desc,
				      const disk_partition_t *part_info)
{
	ulong abc_offset, abc_blocks;

	abc_offset = offsetof(struct android_bootloader_message_ab,
			      slot_suffix) / part_info->blksz;
	abc_blocks = DIV_ROUND_UP(sizeof(struct android_bootloader_control),
				  part_info->blksz);
	if (blk_dwrite(dev_desc, part_info->start + abc_offset, abc_blocks,
		       abc_data_block) != abc_blocks) {
		printf("ANDROID: Could not write back the misc partition\n");
		return -1;
	}
	return 0;
}

/** android_boot_compare_slots - compares two slots returning which slot is
 * should we boot from among the two.
 * @a: The first bootable slot metadata
 * @b: The second bootable slot metadata
 * @return negative if the slot "a" is better, positive of the slot "b" is
 * better or 0 if they are equally good.
 */
static int android_ab_compare_slots(const struct android_slot_metadata *a,
				    const struct android_slot_metadata *b)
{
	/* Higher priority is better */
	if (a->priority != b->priority)
		return b->priority - a->priority;

	/* Higher successful_boot value is better, in case of same priority. */
	if (a->successful_boot != b->successful_boot)
		return b->successful_boot - a->successful_boot;

	/* Higher tries_remaining is better to ensure round-robin. */
	if (a->tries_remaining != b->tries_remaining)
		return b->tries_remaining - a->tries_remaining;

	return 0;
}

int android_ab_select(struct blk_desc *dev_desc, disk_partition_t *part_info)
{
	struct android_bootloader_control *abc;
	u32 crc32_le;
	int slot, i;
	bool store_needed = false;
	char slot_suffix[4];

	abc = android_boot_control_create_from_disk(dev_desc, part_info);
	if (!abc) {
		/* This condition represents an actual problem with the code
		 * or the board setup, like an invalid partition information.
		 * Signal a repair mode and do not try to boot from either
		 * slot.
		 */
		return -1;
	}

	crc32_le = android_boot_control_compute_crc(abc);
	if (abc->crc32_le != crc32_le) {
		printf("ANDROID: Invalid CRC-32 (expected %.8x, found %.8x), "
		       "re-initializing A/B metadata.\n",
		       crc32_le, abc->crc32_le);
		android_boot_control_default(abc);
		store_needed = true;
	}

	if (abc->magic != ANDROID_BOOT_CTRL_MAGIC) {
		printf("ANDROID: Unknown A/B metadata: %.8x\n", abc->magic);
		free(abc);
		return -1;
	}

	if (abc->version > ANDROID_BOOT_CTRL_VERSION) {
		printf("ANDROID: Unsupported A/B metadata version: %.8x\n",
		       abc->version);
		free(abc);
		return -1;
	}

	/* At this point a valid boot control metadata is stored in abc,
	 * followed by other reserved data in the same block.
	 * We select a with the higher priority slot that
	 *  - is not marked as corrupted and
	 *  - either has tries_remaining > 0 or successful_boot is true.
	 * If the slot selected has a false successful_boot, we also decrement
	 * the tries_remaining until it eventually becomes unbootable because
	 * tries_remaining reaches 0. This mechanism produces a bootloader
	 * induced rollback, typically right after a failed update.
	 */

	/* Safety check: limit the number of slots. */
	if (abc->nb_slot > ARRAY_SIZE(abc->slot_info)) {
		abc->nb_slot = ARRAY_SIZE(abc->slot_info);
		store_needed = true;
	}

	slot = -1;
	for (i = 0; i < abc->nb_slot; ++i) {
		if (abc->slot_info[i].verity_corrupted ||
		    !abc->slot_info[i].tries_remaining) {
			debug("ANDROID: unbootable slot %d tries: %d, "
			      "corrupt: %d\n",
			      i,
			      abc->slot_info[i].tries_remaining,
			      abc->slot_info[i].verity_corrupted);
			continue;
		}
		debug("ANDROID: bootable slot %d pri: %d, tries: %d, "
		      "corrupt: %d, successful: %d\n",
		      i,
		      abc->slot_info[i].priority,
		      abc->slot_info[i].tries_remaining,
		      abc->slot_info[i].verity_corrupted,
		      abc->slot_info[i].successful_boot);

		if (slot < 0 ||
		    android_ab_compare_slots(&abc->slot_info[i],
					     &abc->slot_info[slot]) < 0) {
			slot = i;
		}
	}

	if (slot >= 0 && !abc->slot_info[slot].successful_boot) {
		printf("ANDROID: Attempting slot %c, tries remaining %d\n",
		       ANDROID_BOOT_SLOT_NAME(slot),
		       abc->slot_info[slot].tries_remaining);
		abc->slot_info[slot].tries_remaining--;
		store_needed = true;
	}

	if (slot >= 0) {
		/* Legacy user-space requires this field to be set in the BCB.
		 * Newer releases load this the slot suffix from the command
		 * line or the device tree.
		 */
		memset(slot_suffix, 0, sizeof(slot_suffix));
		slot_suffix[0] = ANDROID_BOOT_SLOT_NAME(slot);
		if (memcmp(abc->slot_suffix, slot_suffix,
			   sizeof(slot_suffix))) {
			memcpy(abc->slot_suffix, slot_suffix,
			       sizeof(slot_suffix));
			store_needed = true;
		}
	}

	if (store_needed) {
		abc->crc32_le = android_boot_control_compute_crc(abc);
		android_boot_control_store(abc, dev_desc, part_info);
	}
	free(abc);

	if (slot < 0)
		return -1;
	return slot;
}
