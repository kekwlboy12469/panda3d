/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file vulkanTextureContext.h
 * @author rdb
 * @date 2016-02-19
 */

#ifndef VULKANTEXTURECONTEXT_H
#define VULKANTEXTURECONTEXT_H

#include "config_vulkandisplay.h"
#include "textureContext.h"
#include "small_vector.h"

/**
 * Manages a Vulkan image and device memory.
 */
class EXPCL_VULKANDISPLAY VulkanTextureContext : public TextureContext {
public:
  INLINE VulkanTextureContext(PreparedGraphicsObjects *pgo, Texture *texture = nullptr);
  ~VulkanTextureContext() {};

  ALLOC_DELETED_CHAIN(VulkanTextureContext);

  void release(VulkanFrameData &frame_data);
  void destroy_now(VkDevice device);

  INLINE VkImageView get_image_view(int view) const;
  INLINE VkBufferView get_buffer_view(int view) const;

  INLINE void mark_read(VkPipelineStageFlags stage_mask);
  INLINE void mark_written(VkPipelineStageFlags stage_mask,
                           VkAccessFlags access_mask);
  INLINE void discard();

  void clear_color_image(VkCommandBuffer cmd, const VkClearColorValue &value);
  void clear_depth_stencil_image(VkCommandBuffer cmd, const VkClearDepthStencilValue &value);
  void clear_buffer(VkCommandBuffer cmd, uint32_t fill);

  void transition(VkCommandBuffer cmd, uint32_t queue_family, VkImageLayout layout,
                  VkPipelineStageFlags dst_stage_mask, VkAccessFlags dst_access_mask);

public:
  VkFormat _format = VK_FORMAT_UNDEFINED;
  VkExtent3D _extent;
  int _mipmap_begin = 0, _mipmap_end = 1;
  uint32_t _mip_levels = 1;
  uint32_t _array_layers = 1;
  VkImageAspectFlags _aspect_mask = 0;
  bool _generate_mipmaps = false;
  bool _pack_bgr8 = false;
  bool _swap_bgra8 = false;

  // Depending on whether it's a buffer texture or image texture, either the
  // image and image view or buffer and buffer view will be set.
  VkImage _image = VK_NULL_HANDLE;
  small_vector<VkImageView> _image_views;
  VkBuffer _buffer = VK_NULL_HANDLE;
  small_vector<VkBufferView> _buffer_views;
  VulkanMemoryBlock _block;

  VkImageLayout _layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkAccessFlags _write_access_mask = 0;
  VkPipelineStageFlags _write_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags _read_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

public:
  static TypeHandle get_class_type() {
    return _type_handle;
  }
  static void init_type() {
    TextureContext::init_type();
    register_type(_type_handle, "VulkanTextureContext",
                  TextureContext::get_class_type());
  }
  virtual TypeHandle get_type() const {
    return get_class_type();
  }
  virtual TypeHandle force_init_type() {init_type(); return get_class_type();}

private:
  static TypeHandle _type_handle;
};

#include "vulkanTextureContext.I"

#endif  // VULKANTEXTURECONTEXT_H
