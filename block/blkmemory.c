/*
 * MemoryRegion backed block driver
 *
 * Copyright (c) 2013 espes
 *
 * Based on "Add an in-memory block device" patch
 * Copyright IBM, Corp. 2007
 * Authors:
 * Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "block/block-common.h"
#include "block/block-io.h"
#include "system/memory.h"
#include "qemu/memalign.h"
#include "block/block_int.h"
#include "qemu/iov.h"

#include "block/blkmemory.h"

//#define DEBUG_BLKMEMORY

typedef struct BDRVMemoryState {
    uint64_t size;
    AddressSpace *as;
} BDRVMemoryState;

static int memory_open(BlockDriverState *bs, QDict *options, int flags,
                       Error **errp)
{
    /* We're kinda unique in that we're inited with a MemoryRegion instead
     * of a file. A MemoryRegion pointer can't be put in QDict, so we have
     * to be inited by hand. If something tries to init us normally, better
     * to fail than crash.
     */
    return -EINVAL;
}

static void memory_close(BlockDriverState *bs)
{
    /* nothing to do..? */
}

static int64_t coroutine_fn memory_co_getlength(BlockDriverState *bs)
{
    BDRVMemoryState *s = bs->opaque;
    
    /* Modern block layer expects the length in bytes, not sectors. */
    return s->size;
}

static int coroutine_fn memory_co_preadv(BlockDriverState *bs, int64_t offset,
                                         int64_t bytes, QEMUIOVector *qiov,
                                         BdrvRequestFlags flags)
{
    BDRVMemoryState *s = bs->opaque;
    int i;

#ifdef DEBUG_BLKMEMORY
    printf("blkmemory read 0x%lx : %ld\n", offset, bytes);
#endif

    if (offset >= s->size) {
        return 0; /* EOF */
    }
    bytes = MIN(bytes, s->size - offset);

    /* Process the scatter-gather list segment by segment (1:1 with guest) */
    for (i = 0; i < qiov->niov; i++) {
        size_t len = qiov->iov[i].iov_len;
        if (len > bytes) {
            len = bytes;
        }
        
        address_space_read(s->as, offset, MEMTXATTRS_UNSPECIFIED,
                           qiov->iov[i].iov_base, len);
        
        offset += len;
        bytes -= len;
        if (bytes == 0) {
            break;
        }
    }

    return 0;
}

static int coroutine_fn memory_co_pwritev(BlockDriverState *bs, int64_t offset,
                                          int64_t bytes, QEMUIOVector *qiov,
                                          BdrvRequestFlags flags)
{
    BDRVMemoryState *s = bs->opaque;
    int i;

#ifdef DEBUG_BLKMEMORY
    printf("blkmemory write 0x%lx : %ld\n", offset, bytes);
#endif

    if (offset >= s->size) {
        return -ENOSPC; /* Past end of device */
    }
    bytes = MIN(bytes, s->size - offset);

    /* Process the scatter-gather list segment by segment (1:1 with guest) */
    for (i = 0; i < qiov->niov; i++) {
        size_t len = qiov->iov[i].iov_len;
        if (len > bytes) {
            len = bytes;
        }
        
        address_space_write(s->as, offset, MEMTXATTRS_UNSPECIFIED,
                            qiov->iov[i].iov_base, len);
        
        offset += len;
        bytes -= len;
        if (bytes == 0) {
            break;
        }
    }

    return 0;
}

static BlockDriver bdrv_memory = {
    .format_name = "memory",
    .instance_size = sizeof(BDRVMemoryState),
    .bdrv_open = memory_open,
    .bdrv_close = memory_close,
    .bdrv_co_getlength = memory_co_getlength,

    .bdrv_co_preadv = memory_co_preadv,
    .bdrv_co_pwritev = memory_co_pwritev,
};

static void bdrv_memory_init(void)
{
    bdrv_register(&bdrv_memory);
}

block_init(bdrv_memory_init);

int bdrv_memory_open(BlockDriverState *bs, AddressSpace *as, uint64_t size)
{
    pstrcpy(bs->filename, sizeof(bs->filename), "<mem>");

    bs->drv = &bdrv_memory;
    bs->opaque = g_malloc0(bdrv_memory.instance_size);
    if (!bs->opaque) {
        return -1;
    }

    BDRVMemoryState *s = bs->opaque;
    s->as = as;
    s->size = size;

    return 0;
}
