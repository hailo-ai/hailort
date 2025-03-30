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
#include "gsthailo_allocator.hpp"


G_DEFINE_TYPE (GstHailoAllocator, gst_hailo_allocator, GST_TYPE_ALLOCATOR);


static GstMemory *gst_hailo_allocator_alloc(GstAllocator* allocator, gsize size, GstAllocationParams* /*params*/) {
    GstHailoAllocator *hailo_allocator = GST_HAILO_ALLOCATOR(allocator);
    auto buffer = Buffer::create(size, BufferStorageParams::create_dma());
    if (!buffer) {
        HAILONET_ERROR("Creating buffer for allocator has failed, status = %d\n", buffer.status());
        return nullptr;
    }

    GstMemory *memory = gst_memory_new_wrapped(static_cast<GstMemoryFlags>(0), buffer->data(),
        buffer->size(), 0, buffer->size(), nullptr, nullptr);
    if (nullptr == memory) {
        HAILONET_ERROR("Creating new GstMemory for allocator has failed!\n");
        return nullptr;
    }

    assert(nullptr != hailo_allocator->buffers);
    (*hailo_allocator->buffers)[memory] = std::move(buffer.release());
    return memory;
}

static void gst_hailo_allocator_free(GstAllocator* allocator, GstMemory *mem) {
    GstHailoAllocator *hailo_allocator = GST_HAILO_ALLOCATOR(allocator);
    hailo_allocator->buffers->erase(mem);
}

static void gst_hailo_allocator_dispose(GObject *object) {
    GstHailoAllocator *allocator = GST_HAILO_ALLOCATOR(object);

    if (allocator->buffers != nullptr) {
        delete allocator->buffers;
        allocator->buffers = nullptr;
    }

    G_OBJECT_CLASS(gst_hailo_allocator_parent_class)->dispose(object);
}

static void gst_hailo_allocator_class_init(GstHailoAllocatorClass* klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->dispose = gst_hailo_allocator_dispose;

    GstAllocatorClass* allocator_class = GST_ALLOCATOR_CLASS(klass);
    allocator_class->alloc = gst_hailo_allocator_alloc;
    allocator_class->free = gst_hailo_allocator_free;
}

static void gst_hailo_allocator_init(GstHailoAllocator* allocator) {
    allocator->buffers = new std::unordered_map<GstMemory*, Buffer>();
}
