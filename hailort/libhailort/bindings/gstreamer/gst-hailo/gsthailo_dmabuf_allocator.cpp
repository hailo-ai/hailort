/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL 2.1 license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gsthailo_dmabuf_allocator.hpp"

#include <sys/ioctl.h>
#include <fcntl.h>

// TODO: HRT-13107
#define DEVPATH "/dev/dma_heap/linux,cma"
constexpr auto DEVPATH_ALT = "/dev/dma_heap/system";

G_DEFINE_TYPE (GstHailoDmabufAllocator, gst_hailo_dmabuf_allocator, GST_TYPE_DMABUF_ALLOCATOR);


bool GstHailoDmaHeapControl::dma_heap_fd_open = false;
int GstHailoDmaHeapControl::dma_heap_fd = -1;


static GstMemory *gst_hailo_dmabuf_allocator_alloc(GstAllocator* allocator, gsize size, GstAllocationParams* /*params*/) {
    GstHailoDmabufAllocator *hailo_allocator = GST_HAILO_DMABUF_ALLOCATOR(allocator);

    if (!GstHailoDmaHeapControl::dma_heap_fd_open) {
        GstHailoDmaHeapControl::dma_heap_fd = open(DEVPATH, O_RDWR | O_CLOEXEC);
        if (GstHailoDmaHeapControl::dma_heap_fd < 0) {
            GstHailoDmaHeapControl::dma_heap_fd = open(DEVPATH_ALT, O_RDWR | O_CLOEXEC);
            if (GstHailoDmaHeapControl::dma_heap_fd < 0) {
                HAILONET_ERROR("open fd failed!\n");
                return nullptr;
            }
        }
        GstHailoDmaHeapControl::dma_heap_fd_open = true;
    }

    dma_heap_allocation_data heap_data;
    heap_data = {
        .len = size,
        .fd = 0,
        .fd_flags = O_RDWR | O_CLOEXEC,
        .heap_flags = 0,
    };

    int ret = ioctl(GstHailoDmaHeapControl::dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data);
    if (ret < 0) {
        HAILONET_ERROR("ioctl DMA_HEAP_IOCTL_ALLOC failed! ret = %d\n", ret);
        return nullptr;
    }

    if (GST_IS_DMABUF_ALLOCATOR(hailo_allocator) == false) {
        HAILONET_ERROR("hailo_allocator is not dmabuf!\n");
        return nullptr;
    }

    GstMemory *memory = gst_dmabuf_allocator_alloc(allocator, heap_data.fd, size);
    if (nullptr == memory) {
        HAILONET_ERROR("Creating new GstMemory for allocator has failed!\n");
        return nullptr;
    }

    assert(nullptr != hailo_allocator->dma_buffers);
    (*hailo_allocator->dma_buffers)[memory] = heap_data;
    return memory;
}

static void gst_hailo_dmabuf_allocator_free(GstAllocator* allocator, GstMemory *memory) {
    GstHailoDmabufAllocator *hailo_allocator = GST_HAILO_DMABUF_ALLOCATOR(allocator);
    assert(nullptr != hailo_allocator->dma_buffers);
    hailo_allocator->dma_buffers->erase(memory);
}

static void gst_hailo_dmabuf_allocator_dispose(GObject *object) {
    GstHailoDmabufAllocator *allocator = GST_HAILO_DMABUF_ALLOCATOR(object);
    if (nullptr != allocator->dma_buffers) {
        delete allocator->dma_buffers;
        allocator->dma_buffers = nullptr;
    }
    G_OBJECT_CLASS(gst_hailo_dmabuf_allocator_parent_class)->dispose(object);
}


static void gst_hailo_dmabuf_allocator_class_init(GstHailoDmabufAllocatorClass* klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->dispose = gst_hailo_dmabuf_allocator_dispose;

    GstAllocatorClass* allocator_class = GST_ALLOCATOR_CLASS(klass);
    allocator_class->alloc = gst_hailo_dmabuf_allocator_alloc;
    allocator_class->free = gst_hailo_dmabuf_allocator_free;
}

static void gst_hailo_dmabuf_allocator_init(GstHailoDmabufAllocator* allocator) {
    allocator->dma_buffers = new std::unordered_map<GstMemory*, dma_heap_allocation_data>();
}
