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

#include "dma_buf_allocator_wrapper.hpp"

Expected<std::shared_ptr<HailoDmaBuffAllocator>> HailoDmaBuffAllocator::create(gchar *name) {
    std::shared_ptr<HailoDmaBuffAllocator> hailo_dma_buff_allocator = std::make_shared<HailoDmaBuffAllocator>();
    hailo_dma_buff_allocator->impl = GST_HAILO_DMABUF_ALLOCATOR(g_object_new(GST_TYPE_HAILO_DMABUF_ALLOCATOR, "name", name, NULL));
    return hailo_dma_buff_allocator;
}

hailo_status HailoDmaBuffAllocator::close_dma_heap_fd() {
    if (GstHailoDmaHeapControl::dma_heap_fd_open) {
        close(GstHailoDmaHeapControl::dma_heap_fd);
        GstHailoDmaHeapControl::dma_heap_fd_open = false;
    }
    return HAILO_SUCCESS;
}

Expected<bool> HailoDmaBuffAllocator::is_dma_buf_memory(GstMapInfo &info) {
    return gst_is_dmabuf_memory(info.memory);
}

Expected<int> HailoDmaBuffAllocator::memory_get_fd(GstMapInfo &info) {
    return gst_fd_memory_get_fd(info.memory);
}
