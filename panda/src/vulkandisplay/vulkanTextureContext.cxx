/**
 * PANDA 3D SOFTWARE
 * Copyright (c) Carnegie Mellon University.  All rights reserved.
 *
 * All use of this software is subject to the terms of the revised BSD
 * license.  You should have received a copy of this license along
 * with this source code in a file named "LICENSE."
 *
 * @file vulkanTextureContext.cxx
 * @author rdb
 * @date 2016-02-19
 */

#include "vulkanTextureContext.h"
#include "vulkanFrameData.h"

TypeHandle VulkanTextureContext::_type_handle;

/**
 * Schedules the deletion of the image resources for the end of the frame.
 */
void VulkanTextureContext::
release(VulkanFrameData &frame_data) {
  if (_image != VK_NULL_HANDLE) {
    frame_data._pending_destroy_images.push_back(_image);

    if (vulkandisplay_cat.is_debug()) {
      std::ostream &out = vulkandisplay_cat.debug()
        << "Scheduling image " << _image;

      if (!_image_views.empty()) {
        out << " with views";
        for (VkImageView image_view : _image_views) {
          out << " " << image_view;
        }
      }

      out << " for deletion\n";
    }

    _image = VK_NULL_HANDLE;
  }

  if (!_image_views.empty()) {
    frame_data._pending_destroy_image_views.insert(
      frame_data._pending_destroy_image_views.end(),
      _image_views.begin(), _image_views.end());

    _image_views.clear();
  }

  if (_buffer != VK_NULL_HANDLE) {
    frame_data._pending_destroy_buffers.push_back(_buffer);

    if (vulkandisplay_cat.is_debug()) {
      std::ostream &out = vulkandisplay_cat.debug()
        << "Scheduling buffer " << _buffer;

      if (!_buffer_views.empty()) {
        out << " with views";
        for (VkBufferView buffer_view : _buffer_views) {
          out << " " << buffer_view;
        }
      }

      out << " for deletion\n";
    }

    _buffer = VK_NULL_HANDLE;
  }

  if (!_buffer_views.empty()) {
    frame_data._pending_destroy_buffer_views.insert(
      frame_data._pending_destroy_buffer_views.end(),
      _buffer_views.begin(), _buffer_views.end());

    _buffer_views.clear();
  }

  // Make sure that the memory remains untouched until the frame is over.
  frame_data._pending_free.push_back(std::move(_block));

  // The memory isn't free yet, but it can be reclaimed by the memory allocator
  // if really necessary by waiting until the frame queue is empty.
  update_data_size_bytes(0);

  _format = VK_FORMAT_UNDEFINED;
  _layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

/**
 * Destroys the handles associated with this context immediately.
 */
void VulkanTextureContext::
destroy_now(VkDevice device) {
  for (VkImageView image_view : _image_views) {
    vkDestroyImageView(device, image_view, nullptr);
  }
  _image_views.clear();

  if (_image != VK_NULL_HANDLE) {
    vkDestroyImage(device, _image, nullptr);
    _image = VK_NULL_HANDLE;
  }

  for (VkBufferView buffer_view : _buffer_views) {
    vkDestroyBufferView(device, buffer_view, nullptr);
  }
  _buffer_views.clear();

  if (_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, _buffer, nullptr);
    _buffer = VK_NULL_HANDLE;
  }

  update_data_size_bytes(0);

  _format = VK_FORMAT_UNDEFINED;
  _layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

/**
 * Inserts commands to clear the image.
 */
void VulkanTextureContext::
clear_color_image(VkCommandBuffer cmd, const VkClearColorValue &value) {
  nassertv(_aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT);
  nassertv(_image != VK_NULL_HANDLE);

  // We're not interested in whatever was in here before.
  discard();

  transition(cmd, 0,//vkgsg->_graphics_queue_family_index,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

  VkImageSubresourceRange range;
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.baseMipLevel = 0;
  range.levelCount = _mip_levels;
  range.baseArrayLayer = 0;
  range.layerCount = _array_layers;
  vkCmdClearColorImage(cmd, _image, _layout, &value, 1, &range);
}

/**
 * Inserts commands to clear the image.
 */
void VulkanTextureContext::
clear_depth_stencil_image(VkCommandBuffer cmd, const VkClearDepthStencilValue &value) {
  nassertv(_aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT);
  nassertv(_image != VK_NULL_HANDLE);

  // We're not interested in whatever was in here before.
  discard();

  transition(cmd, 0,//vkgsg->_graphics_queue_family_index,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

  VkImageSubresourceRange range;
  range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  range.baseMipLevel = 0;
  range.levelCount = _mip_levels;
  range.baseArrayLayer = 0;
  range.layerCount = _array_layers;
  vkCmdClearDepthStencilImage(cmd, _image, _layout, &value, 1, &range);
}

/**
 * Inserts commands to clear the image.
 */
void VulkanTextureContext::
clear_buffer(VkCommandBuffer cmd, uint32_t fill) {
  nassertv(_buffer != VK_NULL_HANDLE);

  vkCmdFillBuffer(cmd, _buffer, 0, VK_WHOLE_SIZE, fill);
  mark_written(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
}

/**
 * Issues a command to transition the image to a new layout or queue family.
 * Also issues the appropriate memory barrier to prevent read-after-write and
 * write-after-write hazards.
 *
 * For a buffer texture, layout is ignored.
 *
 * Implicitly calls mark_read() or mark_written() depending on the access mask.
 * Does not (yet) do inter-queue synchronization.
 */
void VulkanTextureContext::
transition(VkCommandBuffer cmd, uint32_t queue_family, VkImageLayout layout,
           VkPipelineStageFlags dst_stage_mask, VkAccessFlags dst_access_mask) {

  if (_image == VK_NULL_HANDLE) {
    layout = _layout;
  }

  // Are we writing to the texture?
  VkAccessFlags write_mask = (dst_access_mask &
    (VK_ACCESS_SHADER_WRITE_BIT |
     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
     VK_ACCESS_TRANSFER_WRITE_BIT |
     VK_ACCESS_HOST_WRITE_BIT |
     VK_ACCESS_MEMORY_WRITE_BIT));

  // If we wrote to this recently (or performed a layout transition), we must
  // wait for that to be finished.
  VkPipelineStageFlags src_stage_mask = _write_stage_mask;

  if (_layout != layout || write_mask != 0) {
    // Before a layout transition or a write, all previous reads must have
    // finished.
    src_stage_mask |= _read_stage_mask;

    if (src_stage_mask == 0) {
      // Can't specify a source stage mask of zero.
      src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
  }
  else if (src_stage_mask == 0) {
    // This is a read-after-read, nothing to do here.
    return;
  }

  VkImageMemoryBarrier img_barrier;
  if (_image != VK_NULL_HANDLE) {
    img_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.pNext = nullptr;
    img_barrier.srcAccessMask = _write_access_mask;
    img_barrier.dstAccessMask = dst_access_mask;
    img_barrier.oldLayout = _layout;
    img_barrier.newLayout = layout;
    img_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    img_barrier.image = _image;
    img_barrier.subresourceRange.aspectMask = _aspect_mask;
    img_barrier.subresourceRange.baseMipLevel = 0;
    img_barrier.subresourceRange.levelCount = _mip_levels;
    img_barrier.subresourceRange.baseArrayLayer = 0;
    img_barrier.subresourceRange.layerCount = _array_layers;
  }
  VkBufferMemoryBarrier buf_barrier;
  if (_buffer != VK_NULL_HANDLE) {
    buf_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buf_barrier.pNext = nullptr;
    buf_barrier.srcAccessMask = _write_access_mask;
    buf_barrier.dstAccessMask = dst_access_mask;
    buf_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buf_barrier.buffer = _buffer;
    buf_barrier.offset = 0;
    buf_barrier.size = VK_WHOLE_SIZE;
  }
  vkCmdPipelineBarrier(cmd, src_stage_mask, dst_stage_mask, 0,
                       0, nullptr,
                       (_buffer != VK_NULL_HANDLE), &buf_barrier,
                       (_image != VK_NULL_HANDLE), &img_barrier);

  _layout = layout;

  if (write_mask != 0) {
    // We are writing to it, remember that for next time.
    _write_stage_mask = dst_stage_mask;
    _write_access_mask = write_mask;
  } else {
    // Note that layout transitions create an implicit execution dependency,
    // so if we're not writing, we don't need to set _write_stage_mask here.
    _write_stage_mask = 0;
    _write_access_mask = 0;
  }
  _read_stage_mask = dst_stage_mask & ~VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
}
