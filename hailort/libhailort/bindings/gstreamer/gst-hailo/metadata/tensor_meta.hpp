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
#ifndef __TENSOR_META_HPP__
#define __TENSOR_META_HPP__

#include "hailo/hailo_gst_tensor_metadata.hpp"
#include "include/hailo_gst.h"

#define TENSOR_META_API_NAME "GstHailoTensorMetaAPI"
#define TENSOR_META_IMPL_NAME "GstHailoTensorMeta"
#define TENSOR_META_TAG "tensor_meta"

G_BEGIN_DECLS

/**
 * @brief This function returns a pointer to the fixed array of tensor bytes
 * @param s GstStructure* to get tensor from. It's assumed that tensor data is stored in "data_buffer" field
 * @param[out] nbytes pointer to the location to store the number of bytes in returned array
 * @return void* to tensor data as bytes, NULL if s has no "data_buffer" field
 */
inline const void *get_tensor_data(GstStructure *s) {
    gsize nbytes=0;
    (void)gst_structure_get(s, "size", G_TYPE_LONG, &nbytes, NULL);
    const GValue *f = gst_structure_get_value(s, "data_buffer");
    if (!f)
        return NULL;
    GVariant *v = g_value_get_variant(f);
    return g_variant_get_fixed_array(v, &nbytes, 1);
}

/**
 * @brief This struct represents raw tensor metadata and contains instance of parent GstMeta and fields describing
 * inference result tensor. This metadata instances is attached to buffer by gvainference elements
 */
struct HAILORTAPI GstHailoTensorMeta {
    GstMeta meta;                 /**< parent meta object */
    hailo_tensor_metadata_t info; /**< struct that holds tensor metadata, e.g. shape, quant_info, layer_name etc... */
};

/**
 * @brief This function registers, if needed, and returns GstMetaInfo for _GstHailoTensorMeta
 * @return GstMetaInfo* for registered type
 */
HAILORTAPI const GstMetaInfo *gst_tensor_meta_get_info(void);

/**
 * @brief This function registers, if needed, and returns a GType for api "GstHailoTensorMetaAPI" and associate it with
 * TENSOR_META_TAG tag
 * @return GType type
 */
HAILORTAPI GType gst_tensor_meta_api_get_type(void);
#define GST_TENSOR_META_API_TYPE (gst_tensor_meta_api_get_type())

/**
 * @def GST_TENSOR_META_INFO
 * @brief This macro calls gst_tensor_meta_get_info
 * @return const GstMetaInfo* for registered type
 */
#define GST_TENSOR_META_INFO (gst_tensor_meta_get_info())

/**
 * @def GST_TENSOR_META_GET
 * @brief This macro retrieves ptr to _GstHailoTensorMeta instance for passed buf
 * @param buf GstBuffer* of which metadata is retrieved
 * @return _GstHailoTensorMeta* instance attached to buf
 */
#define GST_TENSOR_META_GET(buf) ((GstHailoTensorMeta *)gst_buffer_get_meta(buf, gst_tensor_meta_api_get_type()))

/**
 * @def GST_TENSOR_META_ITERATE
 * @brief This macro iterates through _GstHailoTensorMeta instances for passed buf, retrieving the next _GstHailoTensorMeta.
 * If state points to NULL, the first _GstHailoTensorMeta is returned
 * @param buf GstBuffer* of which metadata is iterated and retrieved
 * @param state gpointer* that updates with opaque pointer after macro call.
 * @return _GstHailoTensorMeta* instance attached to buf
 */
#define GST_TENSOR_META_ITERATE(buf, state)                                                                        \
    ((GstHailoTensorMeta *)gst_buffer_iterate_meta_filtered(buf, state, gst_tensor_meta_api_get_type()))

/**
 * @def GST_TENSOR_META_ADD
 * @brief This macro attaches new _GstHailoTensorMeta instance to passed buf
 * @param buf GstBuffer* to which metadata will be attached
 * @return _GstHailoTensorMeta* of the newly added instance attached to buf
 */
#define GST_TENSOR_META_ADD(buf)                                                                                   \
    ((GstHailoTensorMeta *)gst_buffer_add_meta(buf, gst_tensor_meta_get_info(), NULL))

/**
 * @def GST_TENSOR_META_COUNT
 * @brief This macro counts the number of _GstHailoTensorMeta instances attached to passed buf
 * @param buf GstBuffer* of which metadata instances are counted
 * @return guint number of _GstHailoTensorMeta instances attached to passed buf
 */
#define GST_TENSOR_META_COUNT(buf) (gst_buffer_get_n_meta(buf, gst_tensor_meta_api_get_type()))

/**
 * @brief This function searches for first _GstHailoTensorMeta instance that satisfies passed parameters
 * @param buffer GstBuffer* that is searched for metadata
 * @param model_name substring that should be in _GstHailoTensorMeta instance's model_name
 * @param output_layer substring that should be in _GstHailoTensorMeta instance's layer_name
 * @return GstHailoTensorMeta* for found instance or NULL if none are found
 */
//GstHailoTensorMeta *find_tensor_meta(GstBuffer *buffer, const char *model_name, const char *output_layer);

/**
 * @brief This function searches for first _GstHailoTensorMeta instance that satisfies passed parameters
 * @param buffer GstBuffer* that is searched for metadata
 * @param model_name substring that should be in _GstHailoTensorMeta instance's model_name
 * @param output_layer substring that should be in _GstHailoTensorMeta instance's layer_name
 * @param element_id element_id substring that should be in _GstHailoTensorMeta instance's element_id
 * @return GstHailoTensorMeta* for found instance or NULL if none are found
 */
// GstHailoTensorMeta *find_tensor_meta_ext(GstBuffer *buffer, const char *model_name, const char *output_layer,
//                                        const char *element_id);

G_END_DECLS

#endif /* __TENSOR_META_HPP__ */
