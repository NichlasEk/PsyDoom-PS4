/*
 * vk_ps4_queue.c — VkQueue implementation via sceGnmSubmitCommandBuffers.
 *
 * vkQueueSubmit submits recorded command buffers to the GPU.  After the
 * user's command buffers, if a fence or signal semaphores are present, an
 * EOP event write is appended to the device's epilogue command buffer and
 * submitted so the GPU writes a signal_value to each sync object's label
 * when all preceding work completes.  WaitForFences polls those labels.
 *
 * Wait semaphores emit a WAIT_REG_MEM packet into the epilogue command
 * buffer and submit it before the user's command buffers, so the GPU blocks
 * until the signaling submit's EOP write completes.  On host builds where
 * sceGnmSubmitCommandBuffers is a no-op, the CPU-side signaled flag is the
 * fallback (safe because QueueSubmit is serialized).
 */

#include "vk_ps4_internal.h"

#include <string.h>
#include "vk_ps4_pm4_chunks.h"

/* Physical-console experiment: finish submission before CPU completion waits.
 * Keep packets, allocation and cache handling identical to the prior candidate. */
#ifdef VK_USE_PLATFORM_PS4
static VkResult vk_ps4_submit_done(void) {
    vk_ps4_log_raw("SubmitDone experiment: calling sceGnmSubmitDone");
    int32_t result = sceGnmSubmitDone();
    vk_ps4_log("SubmitDone experiment: returned=%d", result);
    return result == 0 ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
}
#endif

/* Emit an EOP event write into the epilogue command buffer that writes
 * signal_value to the GPU label at gpuaddr.  Returns the number of bytes
 * written to the epilogue buffer. */
static uint32_t vk_ps4_queue_emit_eop(
    VkPs4Device *dev, uint64_t gpuaddr, uint32_t signal_value
) {
    GnmCommandBuffer cmd = sceGnmCmdInit(
        dev->gnm_epilogue_cmd,
        dev->gnm_epilogue_cmd_dwords * sizeof(uint32_t),
        NULL, NULL
    );
    /* CACHE_FLUSH_AND_INV_TS_EVENT flushes all caches and writes the
     * immediate value to gpuaddr when the GPU reaches this point. */
    sceGnmDrawCmdEventWriteEop(
        &cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, gpuaddr,
        GNM_DATA_SEL_SEND_DATA32, (uint64_t)signal_value
    );
    /* Labels are polled; request a memory write without a CP interrupt. */
    cmd.cmdptr[-3] &= ~(3u << 24);
    return (uint32_t)(cmd.cmdptr - cmd.beginptr) * sizeof(uint32_t);
}

/* Emit a WAIT_REG_MEM packet into the epilogue command buffer that blocks
 * the GPU until the label at gpuaddr equals refval.  Returns the number of
 * bytes written.  This is used for wait-semaphore GPU synchronization. */
static uint32_t vk_ps4_queue_emit_waitmem(
    VkPs4Device *dev, uint64_t gpuaddr, uint32_t refval
) {
    GnmCommandBuffer cmd = sceGnmCmdInit(
        dev->gnm_epilogue_cmd,
        dev->gnm_epilogue_cmd_dwords * sizeof(uint32_t),
        NULL, NULL
    );
    /* Wait until the GPU label equals refval.  Mask 0xffffffff compares
     * all 32 bits. */
    sceGnmDrawCmdWaitMem(
        &cmd, GNM_WAIT_REG_MEM_FUNC_EQUAL, gpuaddr, refval, 0xffffffff
    );
    return (uint32_t)(cmd.cmdptr - cmd.beginptr) * sizeof(uint32_t);
}

#include <stdio.h>

bool vk_ps4_operation_diagnostic(void) {
#ifdef VK_USE_PLATFORM_PS4
    static int enabled = -1;
    if (enabled < 0) {
        FILE *f = fopen("/app0/scummvm-operation-submit.txt", "r");
        enabled = f != NULL;
        if (f) fclose(f);
        vk_ps4_log("OP DIAG 1.23 enabled=%d", enabled);
    }
    return enabled != 0;
#else
    return false;
#endif
}

/* Opt-in physical diagnostic. Every native submit is followed by a private
 * EOP packet and a bounded CPU wait. Never reuse/free this packet on timeout.
 * Not a production scheduling policy; enabled only by the package marker. */
#ifdef VK_USE_PLATFORM_PS4
#include <orbis/libkernel.h>
#include <stdio.h>
static bool vk_ps4_serial_diagnostic(void) {
    static int enabled = -1;
    if (enabled < 0) {
        FILE *f = fopen("/app0/scummvm-serial-submit.txt", "r");
        enabled = f != NULL;
        if (f) fclose(f);
        vk_ps4_log("SERIAL DIAG 1.24 enabled=%d", enabled);
    }
    return enabled != 0 || vk_ps4_operation_diagnostic();
}
static int32_t vk_ps4_diagnostic_submit(uint32_t count, void **addresses,
                                      uint32_t *sizes, const char *stage) {
    static unsigned sequence;
    unsigned id = ++sequence;
    if (!vk_ps4_serial_diagnostic())
        return sceGnmSubmitCommandBuffers(count, addresses, sizes, NULL, NULL);
    GnmDirectMemory completion = {0};
    if (sceGnmDirectMemoryAllocate(&completion, 65536, 65536,
            GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK)
        return -1;
    volatile uint32_t *label = (volatile uint32_t *)((char *)completion.mapped + 65532);
    *label = 0;
    GnmCommandBuffer eop = sceGnmCmdInit(completion.mapped, 4096, NULL, NULL);
    sceGnmDrawCmdEventWriteEop(&eop, GNM_CACHE_FLUSH_AND_INV_TS_EVENT,
        (uint64_t)(uintptr_t)label, GNM_DATA_SEL_SEND_DATA32, 1);
    eop.cmdptr[-3] &= ~(3u << 24);
    for (uint32_t i=0; i<count; ++i)
        vk_ps4_log("SERIAL %u %s chunk=%u address=%p bytes=%u", id, stage, i, addresses[i], sizes[i]);
    __asm__ volatile("sfence" ::: "memory");
    int32_t result = sceGnmSubmitCommandBuffers(count, addresses, sizes, NULL, NULL);
    vk_ps4_log("SERIAL %u %s submit returned=%d", id, stage, result);
    if (result != 0) return result; /* Retain on any uncertain completion. */
    void *packet = completion.mapped;
    uint32_t bytes = (uint32_t)(eop.cmdptr-eop.beginptr)*4;
    result = sceGnmSubmitCommandBuffers(1, &packet, &bytes, NULL, NULL);
    if (result != 0) return result;
    result = sceGnmSubmitDone();
    vk_ps4_log("SERIAL %u %s EOP waiting label=%p SubmitDone=%d", id, stage, (void *)label, result);
    if (result != 0) return result;
    uint64_t start = sceKernelGetProcessTime();
    while (*label != 1) {
        if (sceKernelGetProcessTime()-start >= 5000000) {
            vk_ps4_log("SERIAL %u %s TIMEOUT observed=%u; allocation retained", id, stage, *label);
            return -1;
        }
        sceKernelUsleep(100);
    }
    vk_ps4_log("SERIAL %u %s GPU COMPLETE elapsed_us=%llu", id, stage,
        (unsigned long long)(sceKernelGetProcessTime()-start));
    sceGnmDirectMemoryRelease(&completion);
    return 0;
}
#endif

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence) {
    if (!queue) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Queue *q = (VkPs4Queue *)queue;
    VkPs4Device *dev = q->device;
    vk_ps4_log("QueueSubmit: count=%u family=%u", submitCount, q->family_index);

    for (uint32_t i = 0; i < submitCount; i++) {
        const VkSubmitInfo *submit = &pSubmits[i];

        /* Wait semaphores — emit GPU WaitMem packets into the epilogue
         * command buffer and submit them before the user's command buffers.
         * This makes the GPU block until each wait semaphore's label matches
         * its signal_value, providing real GPU-side synchronization.
         *
         * On host builds (no epilogue buffer or no-op submit), we fall back
         * to the CPU-side signaled flag.  Since QueueSubmit is serialized,
         * the signaling submit has already completed by the time we get
         * here, so the flag is authoritative on host. */
        for (uint32_t w = 0; w < submit->waitSemaphoreCount; w++) {
            VkPs4Semaphore *sem = (VkPs4Semaphore *)submit->pWaitSemaphores[w];
            if (!sem) continue;

            if (dev && dev->gnm_epilogue_cmd && sem->label) {
                /* GPU wait: emit WaitMem into epilogue and submit it. */
                uint64_t label_addr = (uint64_t)(uintptr_t)sem->label;
                uint32_t wait_bytes = vk_ps4_queue_emit_waitmem(
                    dev, label_addr, sem->signal_value
                );
                void *wait_addr = dev->gnm_epilogue_cmd;
#ifdef VK_USE_PLATFORM_PS4
                __asm__ volatile("sfence" ::: "memory");
#endif
                int32_t wait_result;
#ifdef VK_USE_PLATFORM_PS4
                wait_result = vk_ps4_diagnostic_submit(1, &wait_addr, &wait_bytes, "WAIT");
                if (wait_result != 0 && vk_ps4_serial_diagnostic()) return VK_ERROR_DEVICE_LOST;
#else
                wait_result = sceGnmSubmitCommandBuffers(1, &wait_addr, &wait_bytes, NULL, NULL);
#endif
                if (wait_result != 0) {
                    /* WaitMem submit failed — fall back to CPU check. */
                    if (!sem->signaled) {
                        /* On host, the serialized submit model means the
                         * signaling submit already ran.  If not signaled,
                         * proceed anyway to avoid deadlock. */
                    }
                }
            } else {
                /* Host fallback: check CPU-side signaled flag. */
                if (!sem->signaled) {
                    /* Serialized submit — proceed anyway. */
                }
            }
            /* Reset the semaphore after waiting (binary semaphore). */
            sem->signaled = false;
        }

        /* Submit each command buffer */
        for (uint32_t c = 0; c < submit->commandBufferCount; c++) {
            VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)submit->pCommandBuffers[c];
            if (!cmd || !cmd->pm4_buffer) continue;

            vk_ps4_log("QueueSubmit command=%p PM4=%p dwords=%u", (void *)cmd, (void *)cmd->pm4_buffer, cmd->pm4_used);
            uint32_t cmd_size_bytes = cmd->pm4_used * sizeof(uint32_t);
            if (cmd_size_bytes == 0) continue;

            /* Keep the recording allocation stable: packets can reference inline
             * data. Split only at complete PM4 packets, before submitting any. */
            uint32_t offsets[VK_PS4_PM4_MAX_CHUNKS], sizes[VK_PS4_PM4_MAX_CHUNKS], count;
            if (!vk_ps4_pm4_chunks(cmd->pm4_buffer, cmd->pm4_used,
                    VK_PS4_PM4_SUBMIT_DWORDS, offsets, sizes, &count))
                return VK_ERROR_INITIALIZATION_FAILED;
            void *addresses[VK_PS4_PM4_MAX_CHUNKS];
            for (uint32_t part=0; part<count; ++part) {
                addresses[part]=cmd->pm4_buffer+offsets[part];
                sizes[part]*=sizeof(uint32_t);
            }
            vk_ps4_log("QueueSubmit: sceGnmSubmitCommandBuffers dwords=%u chunks=%u",
                       cmd->pm4_used, count);
#ifdef VK_USE_PLATFORM_PS4
            __asm__ volatile("sfence" ::: "memory");
#endif
            int32_t result;
#ifdef VK_USE_PLATFORM_PS4
                if (vk_ps4_operation_diagnostic()) {
                    result = 0;
                    for (uint32_t op=0; op<cmd->diagnostic_count; ++op) {
                        uint32_t start=cmd->diagnostic_offsets[op];
                        uint32_t end=op+1<cmd->diagnostic_count ? cmd->diagnostic_offsets[op+1] : cmd->pm4_used;
                        if (end == start) continue;
                        uint32_t parts=0;
                        if (end < start || end > cmd->pm4_used ||
                            !vk_ps4_pm4_chunks(cmd->pm4_buffer, end,
                                VK_PS4_PM4_SUBMIT_DWORDS, offsets, sizes, &parts))
                            return VK_ERROR_INITIALIZATION_FAILED;
                        for (uint32_t k=0; k<parts; ++k) {
                            addresses[k]=cmd->pm4_buffer+offsets[k];
                            sizes[k]*=4;
                        }
                        vk_ps4_log("OP %u %s PREFIX BEGIN offset=%u end=%u", op,
                            cmd->diagnostic_names[op], start, end);
                        result=vk_ps4_diagnostic_submit(parts, addresses, sizes, cmd->diagnostic_names[op]);
                        if (result != 0) return VK_ERROR_DEVICE_LOST;
                        vk_ps4_log("OP %u %s GPU COMPLETE", op, cmd->diagnostic_names[op]);
                    }
                } else {
                    result = vk_ps4_diagnostic_submit(count, addresses, sizes, "COMMAND");
                }
                if (result != 0 && vk_ps4_serial_diagnostic()) return VK_ERROR_DEVICE_LOST;
#else
                result = sceGnmSubmitCommandBuffers(count, addresses, sizes, NULL, NULL);
#endif

            if (result != 0) {
                vk_ps4_log("QueueSubmit: sceGnmSubmitCommandBuffers FAILED: %d",
                           result);
                /* Submission failed — signal fence to avoid deadlock */
                if (fence) {
                    VkPs4Fence *f = (VkPs4Fence *)fence;
                    f->signaled = true;
                    f->signal_value++;
                    if (f->label) *f->label = f->signal_value;
                }
                return VK_ERROR_DEVICE_LOST;
            }
        }

        /* Signal semaphores — emit EOP writes to their GPU labels.
         * Clear the CPU-side signaled flag before emitting the EOP so
         * that WaitForFences/Semaphore doesn't short-circuit on a stale
         * value from a previous signal (e.g., AcquireNextImageKHR). */
        for (uint32_t s = 0; s < submit->signalSemaphoreCount; s++) {
            VkPs4Semaphore *sem = (VkPs4Semaphore *)submit->pSignalSemaphores[s];
            if (!sem) continue;
            sem->signal_value++;
            sem->signaled = false;
            if (dev && dev->gnm_epilogue_cmd && sem->label) {
                uint64_t label_addr = (uint64_t)(uintptr_t)sem->label;
                uint32_t eop_bytes = vk_ps4_queue_emit_eop(
                    dev, label_addr, sem->signal_value
                );
                void *eop_addr = dev->gnm_epilogue_cmd;
#ifdef VK_USE_PLATFORM_PS4
                __asm__ volatile("sfence" ::: "memory");
#endif
                int32_t result;
#ifdef VK_USE_PLATFORM_PS4
                result = vk_ps4_diagnostic_submit(1, &eop_addr, &eop_bytes, "SIGNAL");
                if (result != 0 && vk_ps4_serial_diagnostic()) return VK_ERROR_DEVICE_LOST;
#else
                result = sceGnmSubmitCommandBuffers(1, &eop_addr, &eop_bytes, NULL, NULL);
#endif
                if (result != 0) {
                    /* EOP submit failed — fall back to CPU signaling so
                     * the semaphore doesn't deadlock waiters. */
                    sem->signaled = true;
                    *sem->label = sem->signal_value;
                }
#ifndef VK_USE_PLATFORM_PS4
                /* On host, the GPU submit is a no-op so the label is never
                 * written by the GPU.  Set signaled=true for host builds. */
                sem->signaled = true;
#endif
            } else {
                /* No epilogue buffer (host) — CPU-side signaling. */
                sem->signaled = true;
                if (sem->label) *sem->label = sem->signal_value;
            }
        }
    }

    /* Signal fence — emit an EOP write to the fence's GPU label.
     * Clear the CPU-side signaled flag before emitting the EOP so
     * WaitForFences doesn't short-circuit on a stale value from a
     * previous signal (e.g., AcquireNextImageKHR sets signaled=true). */
    if (fence) {
        VkPs4Fence *f = (VkPs4Fence *)fence;
        f->signal_value++;
        f->signaled = false;
        if (dev && dev->gnm_epilogue_cmd && f->label) {
            uint64_t label_addr = (uint64_t)(uintptr_t)f->label;
            uint32_t eop_bytes = vk_ps4_queue_emit_eop(
                dev, label_addr, f->signal_value
            );
            void *eop_addr = dev->gnm_epilogue_cmd;
#ifdef VK_USE_PLATFORM_PS4
            __asm__ volatile("sfence" ::: "memory");
#endif
            int32_t result;
#ifdef VK_USE_PLATFORM_PS4
                result = vk_ps4_diagnostic_submit(1, &eop_addr, &eop_bytes, "SIGNAL");
                if (result != 0 && vk_ps4_serial_diagnostic()) return VK_ERROR_DEVICE_LOST;
#else
                result = sceGnmSubmitCommandBuffers(1, &eop_addr, &eop_bytes, NULL, NULL);
#endif
            if (result != 0) {
                /* EOP submit failed — fall back to CPU signaling. */
                f->signaled = true;
                *f->label = f->signal_value;
            }
            /* On success, the GPU will write the label asynchronously.
             * Set signaled=true as a fallback for host builds where the
             * GPU submit is a no-op and the label is never written. */
#ifndef VK_USE_PLATFORM_PS4
            f->signaled = true;
#endif
        } else {
            /* No epilogue buffer (host) — CPU-side signaling. */
            f->signaled = true;
            if (f->label) *f->label = f->signal_value;
        }
    }

#ifdef VK_USE_PLATFORM_PS4
    if (vk_ps4_submit_done() != VK_SUCCESS) return VK_ERROR_DEVICE_LOST;
#endif
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_QueueWaitIdle(VkQueue queue) {
    if (!queue) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Queue *q = (VkPs4Queue *)queue;
    /* With the EOP fence mechanism, QueueWaitIdle should wait for the last
     * submitted fence.  Since QueueSubmit is serialized and the GPU processes
     * command buffers in order, the last EOP write completing means all
     * prior work is done.  For now, we rely on the synchronous submit model
     * — sceGnmSubmitCommandBuffers queues work but the EOP label is polled
     * by WaitForFences.  QueueWaitIdle has no fence to poll, so we submit a
     * no-op EOP write and poll it. */
    VkPs4Device *dev = q->device;
    if (dev && dev->gnm_epilogue_cmd) {
        /* Use the last dword of the epilogue buffer as the idle label.
         * The EOP packet (~6 dwords) is written at the start of the buffer,
         * so it won't overlap the label at the end. */
        volatile uint32_t *idle_label =
            (volatile uint32_t *)((uintptr_t)dev->gnm_epilogue_cmd +
                (dev->gnm_epilogue_cmd_dwords - 1) * sizeof(uint32_t));
        *idle_label = 0;
        uint64_t label_addr = (uint64_t)(uintptr_t)idle_label;
        uint32_t eop_bytes = vk_ps4_queue_emit_eop(dev, label_addr, 1);
        void *eop_addr = dev->gnm_epilogue_cmd;
#ifdef VK_USE_PLATFORM_PS4
        __asm__ volatile("sfence" ::: "memory");
#endif
        int32_t result = sceGnmSubmitCommandBuffers(
            1, &eop_addr, &eop_bytes, NULL, NULL
        );
#ifdef VK_USE_PLATFORM_PS4
        if (result != 0)
            return VK_ERROR_DEVICE_LOST;
        if (vk_ps4_submit_done() != VK_SUCCESS) return VK_ERROR_DEVICE_LOST;
        if (result == 0) {
            /* Poll until the GPU writes the label. */
            while (*idle_label != 1) {
                /* spin */
            }
        }
#else
        /* Host build: sceGnmSubmitCommandBuffers is a no-op, so the GPU
         * never writes the label.  Set it ourselves to avoid an infinite
         * loop — on host there is no real GPU work to wait for. */
        (void)result;
        *idle_label = 1;
#endif
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_DeviceWaitIdle(VkDevice device) {
    if (!device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    /* DeviceWaitIdle waits for all queues to be idle.  Since we have a
     * single queue family, this is equivalent to QueueWaitIdle on the
     * first queue.  Use the same EOP-poll approach. */
    if (dev->queue_count > 0 && dev->queues[0]) {
        return vk_ps4_QueueWaitIdle((VkQueue)dev->queues[0]);
    }
    return VK_SUCCESS;
}
