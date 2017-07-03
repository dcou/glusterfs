/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "monitoring.h"
#include "xlator.h"
#include "syscall.h"

#include <stdlib.h>

static void
dump_mem_acct_details(xlator_t *xl, int fd)
{
        struct mem_acct_rec *mem_rec;
        int i = 0;

        if (!xl || !xl->mem_acct)
                return;
        dprintf (fd, "%s.%s.total.num_types %d\n", xl->type, xl->name,
                 xl->mem_acct->num_types);
        dprintf (fd, "type, in-use-size, in-use-units, max-size, "      \
                 "max-units, total-allocs\n");
        for (i = 0; i < xl->mem_acct->num_types; i++) {
                mem_rec = &xl->mem_acct->rec[i];
                if (mem_rec->num_allocs == 0)
                        continue;
                dprintf (fd, "%s, %"GF_PRI_SIZET", %u, %"GF_PRI_SIZET", %u, %u\n",
                         mem_rec->typestr, mem_rec->size, mem_rec->num_allocs,
                         mem_rec->max_size, mem_rec->max_num_allocs,
                         mem_rec->total_allocs);
        }
}

static void
dump_memory_accounting (xlator_t *xl, int fd)
{
#if MEMORY_ACCOUNTING_STATS
        int      i        = 0;
        uint64_t count    = 0;

        uint64_t tcalloc = GF_ATOMIC_GET (gf_memory_stat_counts.total_calloc);
        uint64_t tmalloc = GF_ATOMIC_GET (gf_memory_stat_counts.total_malloc);
        uint64_t tfree   = GF_ATOMIC_GET (gf_memory_stat_counts.total_free);

        dprintf (fd, "memory.total.calloc %lu\n", tcalloc);
        dprintf (fd, "memory.total.malloc %lu\n", tmalloc);
        dprintf (fd, "memory.total.realloc %lu\n",
                 GF_ATOMIC_GET (gf_memory_stat_counts.total_realloc));
        dprintf (fd, "memory.total.free %lu\n", tfree);
        dprintf (fd, "memory.total.in-use %lu\n", ((tcalloc + tmalloc) - tfree));

        for (i = 0; i < GF_BLK_MAX_VALUE; i++) {
                count = GF_ATOMIC_GET (gf_memory_stat_counts.blk_size[i]);
                dprintf (fd, "memory.total.blk_size[%d] %lu\n", i, count);
        }

        dprintf (fd, "----\n");
#endif

        /* This is not a metric to be watched in admin guide,
           but keeping it here till we resolve all leak-issues
           would be great */
        while (xl) {
                dump_mem_acct_details (xl, fd);
                xl = xl->next;
        }
}


static void
dump_latency_and_count (xlator_t *xl, int fd)
{
        int32_t  index = 0;
        uint64_t fop;
        uint64_t cbk;
        glusterfs_graph_t *graph = NULL;

        graph = xl->graph;

        for (index = 0; index < GF_FOP_MAXVALUE; index++) {
                fop = GF_ATOMIC_GET (xl->metrics[index].fop);
                cbk = GF_ATOMIC_GET (xl->metrics[index].cbk);
                if (fop) {
                        dprintf (fd, "%s.%d.%s.count %lu\n", xl->name,
                                 (graph) ? graph->id : 0, gf_fop_list[index], fop);
                }
                if (cbk) {
                        dprintf (fd, "%s.%d.%s.fail_count %lu\n", xl->name,
                                 (graph) ? graph->id : 0, gf_fop_list[index], cbk);
                }
                if (xl->latencies[index].mean != 0.0) {
                        dprintf (fd, "%s.%d.%s.latency %lf\n", xl->name,
                                 (graph) ? graph->id : 0, gf_fop_list[index],
                                 xl->latencies[index].mean);
                }
        }
}

static void
dump_call_stack_details (glusterfs_ctx_t *ctx, int fd)
{
        dprintf (fd, "total.stack_count %lu\n",
                 GF_ATOMIC_GET (ctx->pool->total_count));
        dprintf (fd, "in-flight.stack_count %lu\n",
                 ctx->pool->cnt);
}

static void
dump_metrics (glusterfs_ctx_t *ctx, int fd)
{
        xlator_t *xl = NULL;

        xl = ctx->active->top;

        /* Let every file have information on which process dumped info */
        dprintf (fd, "%s\n", ctx->cmdlinestr);

        /* Dump memory accounting */
        dump_memory_accounting (xl, fd);
        dprintf (fd, "-----\n");

        dump_call_stack_details (ctx, fd);
        dprintf (fd, "-----\n");

        while (xl) {
                dump_latency_and_count (xl, fd);
                xl = xl->next;
        }

        return;
}

void
gf_monitor_metrics (int sig, glusterfs_ctx_t *ctx)
{
        int fd = 0;
        char filepath[128] = {0,};

        strncat (filepath, "/tmp/glusterfs.XXXXXX",
                 strlen ("/tmp/glusterfs.XXXXXX"));

        fd = mkstemp (filepath);
        if (fd < 0) {
                gf_log ("signal", GF_LOG_ERROR,
                        "failed to open tmp file %s (%s)",
                        filepath, strerror (errno));
                return;
        }

        dump_metrics (ctx, fd);

        sys_fsync (fd);
        sys_close (fd);

        return;
}