/*
 * Copyright (c) 2015-2016, 2020-2022 The Khronos Group Inc.
 * Copyright (c) 2015-2016, 2020-2022 Valve Corporation
 * Copyright (c) 2015-2016, 2020-2022 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Cody Northrop <cody@lunarg.com>
 * Author: John Zulauf <jzulauf@lunarg.com>
 */

#ifndef VKTESTBINDING_H
#define VKTESTBINDING_H

#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <vector>

#include "lvt_function_pointers.h"
#include "test_common.h"

namespace vk_testing {

template <class Dst, class Src>
std::vector<Dst> MakeVkHandles(const std::vector<Src> &v) {
    std::vector<Dst> handles;
    handles.reserve(v.size());
    std::transform(v.begin(), v.end(), std::back_inserter(handles), [](const Src &o) { return o.handle(); });
    return handles;
}

template <class Dst, class Src>
std::vector<Dst> MakeVkHandles(const std::vector<Src *> &v) {
    std::vector<Dst> handles;
    handles.reserve(v.size());
    std::transform(v.begin(), v.end(), std::back_inserter(handles),
                   [](const Src *o) { return (o) ? o->handle() : VK_NULL_HANDLE; });
    return handles;
}

typedef void (*ErrorCallback)(const char *expr, const char *file, unsigned int line, const char *function);
void set_error_callback(ErrorCallback callback);

class PhysicalDevice;
class Device;
class Queue;
class DeviceMemory;
class Fence;
class Semaphore;
class Event;
class QueryPool;
class Buffer;
class BufferView;
class Image;
class ImageView;
class DepthStencilView;
class Shader;
class Pipeline;
class PipelineDelta;
class Sampler;
class DescriptorSetLayout;
class PipelineLayout;
class DescriptorSetPool;
class DescriptorSet;
class CommandBuffer;
class CommandPool;

std::vector<VkLayerProperties> GetGlobalLayers();
std::vector<VkExtensionProperties> GetGlobalExtensions();
std::vector<VkExtensionProperties> GetGlobalExtensions(const char *pLayerName);

namespace internal {

template <typename T>
class Handle {
  public:
    const T &handle() const NOEXCEPT { return handle_; }
    bool initialized() const NOEXCEPT { return (handle_ != T{}); }

  protected:
    typedef T handle_type;

    explicit Handle() NOEXCEPT : handle_{} {}
    explicit Handle(T handle) NOEXCEPT : handle_(handle) {}

    // handles are non-copyable
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    // handles can be moved out
    Handle(Handle &&src) NOEXCEPT : handle_{src.handle_} { src.handle_ = {}; }
    Handle &operator=(Handle &&src) NOEXCEPT {
        handle_ = src.handle_;
        src.handle_ = {};
        return *this;
    }

    void init(T handle) NOEXCEPT {
        assert(!initialized());
        handle_ = handle;
    }

  private:
    T handle_;
};

template <typename T>
class NonDispHandle : public Handle<T> {
  protected:
    explicit NonDispHandle() NOEXCEPT : Handle<T>(), dev_handle_(VK_NULL_HANDLE) {}
    explicit NonDispHandle(VkDevice dev, T handle) NOEXCEPT : Handle<T>(handle), dev_handle_(dev) {}

    NonDispHandle(NonDispHandle &&src) NOEXCEPT : Handle<T>(std::move(src)) {
        dev_handle_ = src.dev_handle_;
        src.dev_handle_ = VK_NULL_HANDLE;
    }
    NonDispHandle &operator=(NonDispHandle &&src) NOEXCEPT {
        Handle<T>::operator=(std::move(src));
        dev_handle_ = src.dev_handle_;
        src.dev_handle_ = VK_NULL_HANDLE;
        return *this;
    }

    const VkDevice &device() const NOEXCEPT { return dev_handle_; }

    void init(VkDevice dev, T handle) NOEXCEPT {
        assert(!Handle<T>::initialized() && dev_handle_ == VK_NULL_HANDLE);
        Handle<T>::init(handle);
        dev_handle_ = dev;
    }

  private:
    VkDevice dev_handle_;
};

}  // namespace internal

class PhysicalDevice : public internal::Handle<VkPhysicalDevice> {
  public:
    explicit PhysicalDevice(VkPhysicalDevice phy) : Handle(phy) {
        memory_properties_ = memory_properties();
        device_properties_ = properties();
    }

    VkPhysicalDeviceProperties properties() const;
    VkPhysicalDeviceMemoryProperties memory_properties() const;
    std::vector<VkQueueFamilyProperties> queue_properties() const;
    VkPhysicalDeviceFeatures features() const;

    bool set_memory_type(const uint32_t type_bits, VkMemoryAllocateInfo *info, const VkMemoryPropertyFlags properties,
                         const VkMemoryPropertyFlags forbid = 0) const;

    // vkEnumerateDeviceExtensionProperties()
    std::vector<VkExtensionProperties> extensions(const char *pLayerName = nullptr) const;

    // vkEnumerateLayers()
    std::vector<VkLayerProperties> layers() const;

  private:
    void add_extension_dependencies(uint32_t dependency_count, VkExtensionProperties *depencency_props,
                                    std::vector<VkExtensionProperties> &ext_list);

    VkPhysicalDeviceMemoryProperties memory_properties_;

    VkPhysicalDeviceProperties device_properties_;
};

class QueueCreateInfoArray {
  private:
    std::vector<VkDeviceQueueCreateInfo> queue_info_;
    std::vector<std::vector<float>> queue_priorities_;

  public:
    QueueCreateInfoArray(const std::vector<VkQueueFamilyProperties> &queue_props);
    size_t size() const { return queue_info_.size(); }
    const VkDeviceQueueCreateInfo *data() const { return queue_info_.data(); }
};

class Device : public internal::Handle<VkDevice> {
  public:
    explicit Device(VkPhysicalDevice phy) : phy_(phy) {}
    ~Device() NOEXCEPT;

    // vkCreateDevice()
    void init(const VkDeviceCreateInfo &info);
    void init(std::vector<const char *> &extensions, VkPhysicalDeviceFeatures *features = nullptr,
              void *create_device_pnext = nullptr);  // all queues, all extensions, etc
    void init() {
        std::vector<const char *> extensions;
        init(extensions);
    };

    const PhysicalDevice &phy() const { return phy_; }

    std::vector<const char *> GetEnabledExtensions() { return enabled_extensions_; }
    bool IsEnabledExtension(const char *extension);

    // vkGetDeviceProcAddr()
    PFN_vkVoidFunction get_proc(const char *name) const { return vk::GetDeviceProcAddr(handle(), name); }

    // vkGetDeviceQueue()
    const std::vector<Queue *> &graphics_queues() const { return queues_[GRAPHICS]; }
    const std::vector<Queue *> &compute_queues() { return queues_[COMPUTE]; }
    const std::vector<Queue *> &dma_queues() { return queues_[DMA]; }

    typedef std::vector<std::unique_ptr<Queue>> QueueFamilyQueues;
    typedef std::vector<QueueFamilyQueues> QueueFamilies;
    const QueueFamilyQueues &queue_family_queues(uint32_t queue_family) const;

    uint32_t graphics_queue_node_index_;

    struct Format {
        VkFormat format;
        VkImageTiling tiling;
        VkFlags features;
    };
    // vkGetFormatInfo()
    VkFormatProperties format_properties(VkFormat format);
    const std::vector<Format> &formats() const { return formats_; }

    // vkDeviceWaitIdle()
    void wait();

    // vkWaitForFences()
    VkResult wait(const std::vector<const Fence *> &fences, bool wait_all, uint64_t timeout);
    VkResult wait(const Fence &fence) { return wait(std::vector<const Fence *>(1, &fence), true, (uint64_t)-1); }

    // vkUpdateDescriptorSets()
    void update_descriptor_sets(const std::vector<VkWriteDescriptorSet> &writes, const std::vector<VkCopyDescriptorSet> &copies);
    void update_descriptor_sets(const std::vector<VkWriteDescriptorSet> &writes) {
        return update_descriptor_sets(writes, std::vector<VkCopyDescriptorSet>());
    }

    static VkWriteDescriptorSet write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                     VkDescriptorType type, uint32_t count,
                                                     const VkDescriptorImageInfo *image_info);
    static VkWriteDescriptorSet write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                     VkDescriptorType type, uint32_t count,
                                                     const VkDescriptorBufferInfo *buffer_info);
    static VkWriteDescriptorSet write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                     VkDescriptorType type, uint32_t count, const VkBufferView *buffer_views);
    static VkWriteDescriptorSet write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                     VkDescriptorType type, const std::vector<VkDescriptorImageInfo> &image_info);
    static VkWriteDescriptorSet write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                     VkDescriptorType type, const std::vector<VkDescriptorBufferInfo> &buffer_info);
    static VkWriteDescriptorSet write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                     VkDescriptorType type, const std::vector<VkBufferView> &buffer_views);

    static VkCopyDescriptorSet copy_descriptor_set(const DescriptorSet &src_set, uint32_t src_binding, uint32_t src_array_element,
                                                   const DescriptorSet &dst_set, uint32_t dst_binding, uint32_t dst_array_element,
                                                   uint32_t count);

  private:
    enum QueueIndex {
        GRAPHICS,
        COMPUTE,
        DMA,
        QUEUE_COUNT,
    };

    void init_queues(const VkDeviceCreateInfo &info);
    void init_formats();

    PhysicalDevice phy_;

    std::vector<const char *> enabled_extensions_;

    QueueFamilies queue_families_;
    std::vector<Queue *> queues_[QUEUE_COUNT];
    std::vector<Format> formats_;
};

class Queue : public internal::Handle<VkQueue> {
  public:
    explicit Queue(VkQueue queue, int index) : Handle(queue) { family_index_ = index; }

    // vkQueueSubmit()
    VkResult submit(const std::vector<const CommandBuffer *> &cmds, const Fence &fence, bool expect_success = true);
    VkResult submit(const CommandBuffer &cmd, const Fence &fence, bool expect_success = true);
    VkResult submit(const CommandBuffer &cmd, bool expect_success = true);

    // vkQueueWaitIdle()
    VkResult wait();

    int get_family_index() { return family_index_; }

  private:
    int family_index_;
};

class DeviceMemory : public internal::NonDispHandle<VkDeviceMemory> {
  public:
    ~DeviceMemory() NOEXCEPT;

    // vkAllocateMemory()
    void init(const Device &dev, const VkMemoryAllocateInfo &info);

    // vkMapMemory()
    const void *map(VkFlags flags) const;
    void *map(VkFlags flags);
    const void *map() const { return map(0); }
    void *map() { return map(0); }

    // vkUnmapMemory()
    void unmap() const;

    static VkMemoryAllocateInfo alloc_info(VkDeviceSize size, uint32_t memory_type_index);
    static VkMemoryAllocateInfo get_resource_alloc_info(const vk_testing::Device &dev, const VkMemoryRequirements &reqs,
                                                        VkMemoryPropertyFlags mem_props);
};

class Fence : public internal::NonDispHandle<VkFence> {
  public:
    ~Fence() NOEXCEPT;

    // vkCreateFence()
    void init(const Device &dev, const VkFenceCreateInfo &info);

    // vkGetFenceStatus()
    VkResult status() const { return vk::GetFenceStatus(device(), handle()); }
    VkResult wait(uint64_t timeout) const;

    static VkFenceCreateInfo create_info(VkFenceCreateFlags flags);
    static VkFenceCreateInfo create_info();
};

class Semaphore : public internal::NonDispHandle<VkSemaphore> {
  public:
    ~Semaphore() NOEXCEPT;

    // vkCreateSemaphore()
    void init(const Device &dev, const VkSemaphoreCreateInfo &info);

    static VkSemaphoreCreateInfo create_info(VkFlags flags);
};

class Event : public internal::NonDispHandle<VkEvent> {
  public:
    ~Event() NOEXCEPT;

    // vkCreateEvent()
    void init(const Device &dev, const VkEventCreateInfo &info);

    // vkGetEventStatus()
    // vkSetEvent()
    // vkResetEvent()
    VkResult status() const { return vk::GetEventStatus(device(), handle()); }
    void set();
    void cmd_set(const CommandBuffer &cmd, VkPipelineStageFlags stage_mask);
    void cmd_reset(const CommandBuffer &cmd, VkPipelineStageFlags stage_mask);
    void reset();

    static VkEventCreateInfo create_info(VkFlags flags);
};

class QueryPool : public internal::NonDispHandle<VkQueryPool> {
  public:
    ~QueryPool() NOEXCEPT;

    // vkCreateQueryPool()
    void init(const Device &dev, const VkQueryPoolCreateInfo &info);

    // vkGetQueryPoolResults()
    VkResult results(uint32_t first, uint32_t count, size_t size, void *data, size_t stride);

    static VkQueryPoolCreateInfo create_info(VkQueryType type, uint32_t slot_count);
};

class Buffer : public internal::NonDispHandle<VkBuffer> {
  public:
    explicit Buffer() : NonDispHandle() {}
    explicit Buffer(const Device &dev, const VkBufferCreateInfo &info) { init(dev, info); }
    explicit Buffer(const Device &dev, VkDeviceSize size) { init(dev, size); }

    ~Buffer() NOEXCEPT;

    // vkCreateBuffer()
    void init(const Device &dev, const VkBufferCreateInfo &info, VkMemoryPropertyFlags mem_props);
    void init(const Device &dev, const VkBufferCreateInfo &info) { init(dev, info, 0); }
    void init(const Device &dev, VkDeviceSize size, VkMemoryPropertyFlags mem_props,
              VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, const std::vector<uint32_t> &queue_families = {}) {
        init(dev, create_info(size, usage, &queue_families), mem_props);
    }
    void init(const Device &dev, VkDeviceSize size) { init(dev, size, 0); }
    void init_as_src(const Device &dev, VkDeviceSize size, VkMemoryPropertyFlags &reqs,
                     const std::vector<uint32_t> *queue_families = nullptr) {
        init(dev, create_info(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, queue_families), reqs);
    }
    void init_as_dst(const Device &dev, VkDeviceSize size, VkMemoryPropertyFlags &reqs,
                     const std::vector<uint32_t> *queue_families = nullptr) {
        init(dev, create_info(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, queue_families), reqs);
    }
    void init_as_src_and_dst(const Device &dev, VkDeviceSize size, VkMemoryPropertyFlags &reqs,
                             const std::vector<uint32_t> *queue_families = nullptr, bool memory = true) {
        if (memory)
            init(dev, create_info(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, queue_families), reqs);
        else
            init_no_mem(dev,
                        create_info(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, queue_families));
    }
    void init_as_storage(const Device &dev, VkDeviceSize size, VkMemoryPropertyFlags &reqs,
        const std::vector<uint32_t> *queue_families = nullptr) {
        init(dev, create_info(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, queue_families), reqs);
    }

    void init_no_mem(const Device &dev, const VkBufferCreateInfo &info);

    // get the internal memory
    const DeviceMemory &memory() const { return internal_mem_; }
    DeviceMemory &memory() { return internal_mem_; }

    // vkGetObjectMemoryRequirements()
    VkMemoryRequirements memory_requirements() const;

    // vkBindObjectMemory()
    void bind_memory(const DeviceMemory &mem, VkDeviceSize mem_offset);

    const VkBufferCreateInfo &create_info() const { return create_info_; }
    static VkBufferCreateInfo create_info(VkDeviceSize size, VkFlags usage, const std::vector<uint32_t> *queue_families = nullptr);

    VkBufferMemoryBarrier buffer_memory_barrier(VkFlags output_mask, VkFlags input_mask, VkDeviceSize offset,
                                                VkDeviceSize size) const {
        VkBufferMemoryBarrier barrier = LvlInitStruct<VkBufferMemoryBarrier>();
        barrier.buffer = handle();
        barrier.srcAccessMask = output_mask;
        barrier.dstAccessMask = input_mask;
        barrier.offset = offset;
        barrier.size = size;
        if (create_info_.sharingMode == VK_SHARING_MODE_CONCURRENT) {
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        }
        return barrier;
    }

    VkBufferMemoryBarrier2KHR buffer_memory_barrier(VkPipelineStageFlags2KHR src_stage, VkPipelineStageFlags2KHR dst_stage,
                                                    VkAccessFlags2KHR src_access, VkAccessFlags2KHR dst_access, VkDeviceSize offset,
                                                    VkDeviceSize size) const {
        VkBufferMemoryBarrier2KHR barrier = LvlInitStruct<VkBufferMemoryBarrier2KHR>();
        barrier.buffer = handle();
        barrier.srcStageMask = src_stage;
        barrier.dstStageMask = dst_stage;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = dst_access;
        barrier.offset = offset;
        barrier.size = size;
        if (create_info_.sharingMode == VK_SHARING_MODE_CONCURRENT) {
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        }
        return barrier;
    }

  private:
    VkBufferCreateInfo create_info_;

    DeviceMemory internal_mem_;
};

class BufferView : public internal::NonDispHandle<VkBufferView> {
  public:
    ~BufferView() NOEXCEPT;

    // vkCreateBufferView()
    void init(const Device &dev, const VkBufferViewCreateInfo &info);
    static VkBufferViewCreateInfo createInfo(VkBuffer buffer, VkFormat format, VkDeviceSize offset = 0,
                                             VkDeviceSize range = VK_WHOLE_SIZE);
};

inline VkBufferViewCreateInfo BufferView::createInfo(VkBuffer buffer, VkFormat format, VkDeviceSize offset, VkDeviceSize range) {
    VkBufferViewCreateInfo info = LvlInitStruct<VkBufferViewCreateInfo>();
    info.flags = VkFlags(0);
    info.buffer = buffer;
    info.format = format;
    info.offset = offset;
    info.range = range;
    return info;
}

class Image : public internal::NonDispHandle<VkImage> {
  public:
    explicit Image() : NonDispHandle(), format_features_(0) {}
    explicit Image(const Device &dev, const VkImageCreateInfo &info) : format_features_(0) { init(dev, info); }

    ~Image() NOEXCEPT;

    // vkCreateImage()
    void init(const Device &dev, const VkImageCreateInfo &info, VkMemoryPropertyFlags mem_props);
    void init(const Device &dev, const VkImageCreateInfo &info) { init(dev, info, 0); }
    void init_no_mem(const Device &dev, const VkImageCreateInfo &info);

    // get the internal memory
    const DeviceMemory &memory() const { return internal_mem_; }
    DeviceMemory &memory() { return internal_mem_; }

    // vkGetObjectMemoryRequirements()
    VkMemoryRequirements memory_requirements() const;

    // vkBindObjectMemory()
    void bind_memory(const DeviceMemory &mem, VkDeviceSize mem_offset);

    // vkGetImageSubresourceLayout()
    VkSubresourceLayout subresource_layout(const VkImageSubresource &subres) const;
    VkSubresourceLayout subresource_layout(const VkImageSubresourceLayers &subres) const;

    bool transparent() const;
    bool copyable() const { return (format_features_ & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT); }

    VkImageSubresourceRange subresource_range(VkImageAspectFlags aspect) const { return subresource_range(create_info_, aspect); }
    VkExtent3D extent() const { return create_info_.extent; }
    VkExtent3D extent(uint32_t mip_level) const { return extent(create_info_.extent, mip_level); }
    VkFormat format() const { return create_info_.format; }
    VkImageUsageFlags usage() const { return create_info_.usage; }
    VkSharingMode sharing_mode() const { return create_info_.sharingMode; }
    VkImageMemoryBarrier image_memory_barrier(VkFlags output_mask, VkFlags input_mask, VkImageLayout old_layout,
                                              VkImageLayout new_layout, const VkImageSubresourceRange &range,
                                              uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED) const {
        VkImageMemoryBarrier barrier = LvlInitStruct<VkImageMemoryBarrier>();
        barrier.srcAccessMask = output_mask;
        barrier.dstAccessMask = input_mask;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.image = handle();
        barrier.subresourceRange = range;
        barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
        barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
        return barrier;
    }

    VkImageMemoryBarrier2KHR image_memory_barrier(VkPipelineStageFlags2KHR src_stage, VkPipelineStageFlags2KHR dst_stage,
                                                  VkAccessFlags2KHR src_access, VkAccessFlags2KHR dst_access,
                                                  VkImageLayout old_layout, VkImageLayout new_layout,
                                                  const VkImageSubresourceRange &range,
                                                  uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                  uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED) const {
        VkImageMemoryBarrier2KHR barrier = LvlInitStruct<VkImageMemoryBarrier2KHR>();
        barrier.srcStageMask = src_stage;
        barrier.dstStageMask = dst_stage;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.image = handle();
        barrier.subresourceRange = range;
        barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
        barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
        return barrier;
    }

    static VkImageCreateInfo create_info();
    static VkImageSubresource subresource(VkImageAspectFlags aspect, uint32_t mip_level, uint32_t array_layer);
    static VkImageSubresource subresource(const VkImageSubresourceRange &range, uint32_t mip_level, uint32_t array_layer);
    static VkImageSubresourceLayers subresource(VkImageAspectFlags aspect, uint32_t mip_level, uint32_t array_layer,
                                                uint32_t array_size);
    static VkImageSubresourceLayers subresource(const VkImageSubresourceRange &range, uint32_t mip_level, uint32_t array_layer,
                                                uint32_t array_size);
    static VkImageSubresourceRange subresource_range(VkImageAspectFlags aspect_mask, uint32_t base_mip_level, uint32_t mip_levels,
                                                     uint32_t base_array_layer, uint32_t num_layers);
    static VkImageSubresourceRange subresource_range(const VkImageCreateInfo &info, VkImageAspectFlags aspect_mask);
    static VkImageSubresourceRange subresource_range(const VkImageSubresource &subres);

    static VkExtent2D extent(int32_t width, int32_t height);
    static VkExtent2D extent(const VkExtent2D &extent, uint32_t mip_level);
    static VkExtent2D extent(const VkExtent3D &extent);

    static VkExtent3D extent(int32_t width, int32_t height, int32_t depth);
    static VkExtent3D extent(const VkExtent3D &extent, uint32_t mip_level);

  private:
    void init_info(const Device &dev, const VkImageCreateInfo &info);

    VkImageCreateInfo create_info_;
    VkFlags format_features_;

    DeviceMemory internal_mem_;
};

class ImageView : public internal::NonDispHandle<VkImageView> {
  public:
    explicit ImageView() = default;
    explicit ImageView(const Device &dev, const VkImageViewCreateInfo &info) { init(dev, info); }
    ~ImageView() NOEXCEPT;

    // vkCreateImageView()
    void init(const Device &dev, const VkImageViewCreateInfo &info);
};

class AccelerationStructure : public internal::NonDispHandle<VkAccelerationStructureNV> {
  public:
    explicit AccelerationStructure(const Device &dev, const VkAccelerationStructureCreateInfoNV &info, bool init_memory = true) {
        init(dev, info, init_memory);
    }
    ~AccelerationStructure();

    // vkCreateAccelerationStructureNV
    void init(const Device &dev, const VkAccelerationStructureCreateInfoNV &info, bool init_memory = true);
    // vkGetAccelerationStructureMemoryRequirementsNV()
    VkMemoryRequirements2 memory_requirements() const;
    VkMemoryRequirements2 build_scratch_memory_requirements() const;

    uint64_t opaque_handle() const { return opaque_handle_; }

    const VkAccelerationStructureInfoNV &info() const { return info_; }

    const VkDevice &dev() const { return device(); }

    void create_scratch_buffer(const Device &dev, Buffer *buffer, VkBufferCreateInfo *pCreateInfo = NULL);

  private:
    VkAccelerationStructureInfoNV info_;
    DeviceMemory memory_;
    uint64_t opaque_handle_;
};

class AccelerationStructureKHR : public internal::NonDispHandle<VkAccelerationStructureKHR> {
  public:
    explicit AccelerationStructureKHR(const Device &dev, const VkAccelerationStructureCreateInfoKHR &info,
                                      bool init_memory = true) {
        init(dev, info, init_memory);
    }
    ~AccelerationStructureKHR();
    // vkCreateAccelerationStructureNV
    void init(const Device &dev, const VkAccelerationStructureCreateInfoKHR &info, bool init_memory = true);
    uint64_t opaque_handle() const { return opaque_handle_; }

    const VkAccelerationStructureCreateInfoKHR &info() const { return info_; }

    const VkDevice &dev() const { return device(); }

    void create_scratch_buffer(const Device &dev, Buffer *buffer, VkBufferCreateInfo *pCreateInfo = NULL);

  private:
    VkAccelerationStructureCreateInfoKHR info_;
    DeviceMemory memory_;
    uint64_t opaque_handle_;
};

class ShaderModule : public internal::NonDispHandle<VkShaderModule> {
  public:
    ~ShaderModule() NOEXCEPT;

    // vkCreateShaderModule()
    void init(const Device &dev, const VkShaderModuleCreateInfo &info);
    VkResult init_try(const Device &dev, const VkShaderModuleCreateInfo &info);

    static VkShaderModuleCreateInfo create_info(size_t code_size, const uint32_t *code, VkFlags flags);
};

class Pipeline : public internal::NonDispHandle<VkPipeline> {
  public:
    Pipeline() = default;
    Pipeline(const Device &dev, const VkGraphicsPipelineCreateInfo &info) { init(dev, info); }
    Pipeline(const Device &dev, const VkGraphicsPipelineCreateInfo &info, const VkPipeline basePipeline) {
        init(dev, info, basePipeline);
    }
    Pipeline(const Device &dev, const VkComputePipelineCreateInfo &info) { init(dev, info); }
    ~Pipeline() NOEXCEPT;

    // vkCreateGraphicsPipeline()
    void init(const Device &dev, const VkGraphicsPipelineCreateInfo &info);
    // vkCreateGraphicsPipelineDerivative()
    void init(const Device &dev, const VkGraphicsPipelineCreateInfo &info, const VkPipeline basePipeline);
    // vkCreateComputePipeline()
    void init(const Device &dev, const VkComputePipelineCreateInfo &info);
    // vkLoadPipeline()
    void init(const Device &dev, size_t size, const void *data);
    // vkLoadPipelineDerivative()
    void init(const Device &dev, size_t size, const void *data, VkPipeline basePipeline);

    // vkCreateGraphicsPipeline with error return
    VkResult init_try(const Device &dev, const VkGraphicsPipelineCreateInfo &info);

    // vkStorePipeline()
    size_t store(size_t size, void *data);
};

class PipelineLayout : public internal::NonDispHandle<VkPipelineLayout> {
  public:
    PipelineLayout() NOEXCEPT : NonDispHandle() {}
    PipelineLayout(const Device &dev, VkPipelineLayoutCreateInfo &info, const std::vector<const DescriptorSetLayout *> &layouts) {
        init(dev, info, layouts);
    }
    ~PipelineLayout() NOEXCEPT;

    // Move constructor for Visual Studio 2013
    PipelineLayout(PipelineLayout &&src) NOEXCEPT : NonDispHandle(std::move(src)){};

    PipelineLayout &operator=(PipelineLayout &&src) NOEXCEPT {
        this->~PipelineLayout();
        this->NonDispHandle::operator=(std::move(src));
        return *this;
    };

    // vCreatePipelineLayout()
    void init(const Device &dev, VkPipelineLayoutCreateInfo &info, const std::vector<const DescriptorSetLayout *> &layouts);
};

class Sampler : public internal::NonDispHandle<VkSampler> {
  public:
    ~Sampler() NOEXCEPT;

    // vkCreateSampler()
    void init(const Device &dev, const VkSamplerCreateInfo &info);
};

class DescriptorSetLayout : public internal::NonDispHandle<VkDescriptorSetLayout> {
  public:
    DescriptorSetLayout() NOEXCEPT : NonDispHandle(){};
    DescriptorSetLayout(const Device &dev, const VkDescriptorSetLayoutCreateInfo &info) { init(dev, info); }
    ~DescriptorSetLayout() NOEXCEPT;

    // Move constructor for Visual Studio 2013
    DescriptorSetLayout(DescriptorSetLayout &&src) NOEXCEPT : NonDispHandle(std::move(src)){};

    DescriptorSetLayout &operator=(DescriptorSetLayout &&src) NOEXCEPT {
        this->~DescriptorSetLayout();
        this->NonDispHandle::operator=(std::move(src));
        return *this;
    }

    // vkCreateDescriptorSetLayout()
    void init(const Device &dev, const VkDescriptorSetLayoutCreateInfo &info);
};

class DescriptorPool : public internal::NonDispHandle<VkDescriptorPool> {
  public:
    ~DescriptorPool() NOEXCEPT;

    // Descriptor sets allocated from this pool will need access to the original
    // object
    VkDescriptorPool GetObj() { return pool_; }

    // vkCreateDescriptorPool()
    void init(const Device &dev, const VkDescriptorPoolCreateInfo &info);

    // vkResetDescriptorPool()
    void reset();

    // vkFreeDescriptorSet()
    void setDynamicUsage(bool isDynamic) { dynamic_usage_ = isDynamic; }
    bool getDynamicUsage() { return dynamic_usage_; }

    // vkAllocateDescriptorSets()
    std::vector<DescriptorSet *> alloc_sets(const Device &dev, const std::vector<const DescriptorSetLayout *> &layouts);
    std::vector<DescriptorSet *> alloc_sets(const Device &dev, const DescriptorSetLayout &layout, uint32_t count);
    DescriptorSet *alloc_sets(const Device &dev, const DescriptorSetLayout &layout);

    template <typename PoolSizes>
    static VkDescriptorPoolCreateInfo create_info(VkDescriptorPoolCreateFlags flags, uint32_t max_sets,
                                                  const PoolSizes &pool_sizes);

  private:
    VkDescriptorPool pool_;

    // Track whether this pool's usage is VK_DESCRIPTOR_POOL_USAGE_DYNAMIC
    bool dynamic_usage_;
};

template <typename PoolSizes>
inline VkDescriptorPoolCreateInfo DescriptorPool::create_info(VkDescriptorPoolCreateFlags flags, uint32_t max_sets,
                                                              const PoolSizes &pool_sizes) {
    VkDescriptorPoolCreateInfo info = LvlInitStruct<VkDescriptorPoolCreateInfo>();
    info.flags = flags;
    info.maxSets = max_sets;
    info.poolSizeCount = pool_sizes.size();
    info.pPoolSizes = (info.poolSizeCount) ? pool_sizes.data() : nullptr;
    return info;
}

class DescriptorSet : public internal::NonDispHandle<VkDescriptorSet> {
  public:
    ~DescriptorSet() NOEXCEPT;

    explicit DescriptorSet() : NonDispHandle() {}
    explicit DescriptorSet(const Device &dev, DescriptorPool *pool, VkDescriptorSet set) : NonDispHandle(dev.handle(), set) {
        containing_pool_ = pool;
    }

  private:
    DescriptorPool *containing_pool_;
};

class CommandPool : public internal::NonDispHandle<VkCommandPool> {
  public:
    ~CommandPool() NOEXCEPT;

    explicit CommandPool() : NonDispHandle() {}
    explicit CommandPool(const Device &dev, const VkCommandPoolCreateInfo &info) { init(dev, info); }

    void init(const Device &dev, const VkCommandPoolCreateInfo &info);

    static VkCommandPoolCreateInfo create_info(uint32_t queue_family_index, VkCommandPoolCreateFlags flags);
};

inline VkCommandPoolCreateInfo CommandPool::create_info(uint32_t queue_family_index, VkCommandPoolCreateFlags flags) {
    VkCommandPoolCreateInfo info = LvlInitStruct<VkCommandPoolCreateInfo>();
    info.queueFamilyIndex = queue_family_index;
    info.flags = flags;
    return info;
}

class CommandBuffer : public internal::Handle<VkCommandBuffer> {
  public:
    ~CommandBuffer() NOEXCEPT;

    explicit CommandBuffer() : Handle() {}
    explicit CommandBuffer(const Device &dev, const VkCommandBufferAllocateInfo &info) { init(dev, info); }

    // vkAllocateCommandBuffers()
    void init(const Device &dev, const VkCommandBufferAllocateInfo &info);

    // vkBeginCommandBuffer()
    void begin(const VkCommandBufferBeginInfo *info);
    void begin();

    // vkEndCommandBuffer()
    // vkResetCommandBuffer()
    void end();
    void reset(VkCommandBufferResetFlags flags);
    void reset() { reset(VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT); }

    static VkCommandBufferAllocateInfo create_info(VkCommandPool const &pool);

  private:
    VkDevice dev_handle_;
    VkCommandPool cmd_pool_;
};

class RenderPass : public internal::NonDispHandle<VkRenderPass> {
  public:
    RenderPass() = default;
    RenderPass(const Device &dev, const VkRenderPassCreateInfo &info) { init(dev, info); }
    RenderPass(const Device &dev, const VkRenderPassCreateInfo2 &info) { init(dev, info); }
    ~RenderPass() NOEXCEPT;

    // vkCreateRenderPass()
    void init(const Device &dev, const VkRenderPassCreateInfo &info);
    // vkCreateRenderPass2()
    void init(const Device &dev, const VkRenderPassCreateInfo2 &info);
};


class Framebuffer : public internal::NonDispHandle<VkFramebuffer> {
  public:
    Framebuffer() = default;
    Framebuffer(const Device &dev, const VkFramebufferCreateInfo &info) { init(dev, info); }
    ~Framebuffer() NOEXCEPT;

    // vkCreateFramebuffer()
    void init(const Device &dev, const VkFramebufferCreateInfo &info);
};

inline VkMemoryAllocateInfo DeviceMemory::alloc_info(VkDeviceSize size, uint32_t memory_type_index) {
    VkMemoryAllocateInfo info = LvlInitStruct<VkMemoryAllocateInfo>();
    info.allocationSize = size;
    info.memoryTypeIndex = memory_type_index;
    return info;
}

inline VkBufferCreateInfo Buffer::create_info(VkDeviceSize size, VkFlags usage, const std::vector<uint32_t> *queue_families) {
    VkBufferCreateInfo info = LvlInitStruct<VkBufferCreateInfo>();
    info.size = size;
    info.usage = usage;

    if (queue_families && queue_families->size() > 1) {
        info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = static_cast<uint32_t>(queue_families->size());
        info.pQueueFamilyIndices = queue_families->data();
    }

    return info;
}

inline VkFenceCreateInfo Fence::create_info(VkFenceCreateFlags flags) {
    VkFenceCreateInfo info = LvlInitStruct<VkFenceCreateInfo>();
    info.flags = flags;
    return info;
}

inline VkFenceCreateInfo Fence::create_info() {
    VkFenceCreateInfo info = LvlInitStruct<VkFenceCreateInfo>();
    return info;
}

inline VkSemaphoreCreateInfo Semaphore::create_info(VkFlags flags) {
    VkSemaphoreCreateInfo info = LvlInitStruct<VkSemaphoreCreateInfo>();
    info.flags = flags;
    return info;
}

inline VkEventCreateInfo Event::create_info(VkFlags flags) {
    VkEventCreateInfo info = LvlInitStruct<VkEventCreateInfo>();
    info.flags = flags;
    return info;
}

inline VkQueryPoolCreateInfo QueryPool::create_info(VkQueryType type, uint32_t slot_count) {
    VkQueryPoolCreateInfo info = LvlInitStruct<VkQueryPoolCreateInfo>();
    info.queryType = type;
    info.queryCount = slot_count;
    return info;
}

inline VkImageCreateInfo Image::create_info() {
    VkImageCreateInfo info = LvlInitStruct<VkImageCreateInfo>();
    info.extent.width = 1;
    info.extent.height = 1;
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    return info;
}

inline VkImageSubresource Image::subresource(VkImageAspectFlags aspect, uint32_t mip_level, uint32_t array_layer) {
    VkImageSubresource subres = {};
    if (aspect == 0) {
        assert(false && "Invalid VkImageAspectFlags");
    }
    subres.aspectMask = aspect;
    subres.mipLevel = mip_level;
    subres.arrayLayer = array_layer;
    return subres;
}

inline VkImageSubresource Image::subresource(const VkImageSubresourceRange &range, uint32_t mip_level, uint32_t array_layer) {
    return subresource(range.aspectMask, range.baseMipLevel + mip_level, range.baseArrayLayer + array_layer);
}

inline VkImageSubresourceLayers Image::subresource(VkImageAspectFlags aspect, uint32_t mip_level, uint32_t array_layer,
                                                   uint32_t array_size) {
    VkImageSubresourceLayers subres = {};
    switch (aspect) {
        case VK_IMAGE_ASPECT_COLOR_BIT:
        case VK_IMAGE_ASPECT_DEPTH_BIT:
        case VK_IMAGE_ASPECT_STENCIL_BIT:
        case VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT:
            /* valid */
            break;
        default:
            assert(false && "Invalid VkImageAspectFlags");
    }
    subres.aspectMask = aspect;
    subres.mipLevel = mip_level;
    subres.baseArrayLayer = array_layer;
    subres.layerCount = array_size;
    return subres;
}

inline VkImageSubresourceLayers Image::subresource(const VkImageSubresourceRange &range, uint32_t mip_level, uint32_t array_layer,
                                                   uint32_t array_size) {
    return subresource(range.aspectMask, range.baseMipLevel + mip_level, range.baseArrayLayer + array_layer, array_size);
}

inline VkImageSubresourceRange Image::subresource_range(VkImageAspectFlags aspect_mask, uint32_t base_mip_level,
                                                        uint32_t mip_levels, uint32_t base_array_layer, uint32_t num_layers) {
    VkImageSubresourceRange range = {};
    if (aspect_mask == 0) {
        assert(false && "Invalid VkImageAspectFlags");
    }
    range.aspectMask = aspect_mask;
    range.baseMipLevel = base_mip_level;
    range.levelCount = mip_levels;
    range.baseArrayLayer = base_array_layer;
    range.layerCount = num_layers;
    return range;
}

inline VkImageSubresourceRange Image::subresource_range(const VkImageCreateInfo &info, VkImageAspectFlags aspect_mask) {
    return subresource_range(aspect_mask, 0, info.mipLevels, 0, info.arrayLayers);
}

inline VkImageSubresourceRange Image::subresource_range(const VkImageSubresource &subres) {
    return subresource_range(subres.aspectMask, subres.mipLevel, 1, subres.arrayLayer, 1);
}

inline VkExtent2D Image::extent(int32_t width, int32_t height) {
    VkExtent2D extent = {};
    extent.width = width;
    extent.height = height;
    return extent;
}

inline VkExtent2D Image::extent(const VkExtent2D &extent, uint32_t mip_level) {
    const int32_t width = (extent.width >> mip_level) ? extent.width >> mip_level : 1;
    const int32_t height = (extent.height >> mip_level) ? extent.height >> mip_level : 1;
    return Image::extent(width, height);
}

inline VkExtent2D Image::extent(const VkExtent3D &extent) { return Image::extent(extent.width, extent.height); }

inline VkExtent3D Image::extent(int32_t width, int32_t height, int32_t depth) {
    VkExtent3D extent = {};
    extent.width = width;
    extent.height = height;
    extent.depth = depth;
    return extent;
}

inline VkExtent3D Image::extent(const VkExtent3D &extent, uint32_t mip_level) {
    const int32_t width = (extent.width >> mip_level) ? extent.width >> mip_level : 1;
    const int32_t height = (extent.height >> mip_level) ? extent.height >> mip_level : 1;
    const int32_t depth = (extent.depth >> mip_level) ? extent.depth >> mip_level : 1;
    return Image::extent(width, height, depth);
}

inline VkShaderModuleCreateInfo ShaderModule::create_info(size_t code_size, const uint32_t *code, VkFlags flags) {
    VkShaderModuleCreateInfo info = LvlInitStruct<VkShaderModuleCreateInfo>();
    info.codeSize = code_size;
    info.pCode = code;
    info.flags = flags;
    return info;
}

inline VkWriteDescriptorSet Device::write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                         VkDescriptorType type, uint32_t count,
                                                         const VkDescriptorImageInfo *image_info) {
    VkWriteDescriptorSet write = LvlInitStruct<VkWriteDescriptorSet>();
    write.dstSet = set.handle();
    write.dstBinding = binding;
    write.dstArrayElement = array_element;
    write.descriptorCount = count;
    write.descriptorType = type;
    write.pImageInfo = image_info;
    return write;
}

inline VkWriteDescriptorSet Device::write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                         VkDescriptorType type, uint32_t count,
                                                         const VkDescriptorBufferInfo *buffer_info) {
    VkWriteDescriptorSet write = LvlInitStruct<VkWriteDescriptorSet>();
    write.dstSet = set.handle();
    write.dstBinding = binding;
    write.dstArrayElement = array_element;
    write.descriptorCount = count;
    write.descriptorType = type;
    write.pBufferInfo = buffer_info;
    return write;
}

inline VkWriteDescriptorSet Device::write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                         VkDescriptorType type, uint32_t count, const VkBufferView *buffer_views) {
    VkWriteDescriptorSet write = LvlInitStruct<VkWriteDescriptorSet>();
    write.dstSet = set.handle();
    write.dstBinding = binding;
    write.dstArrayElement = array_element;
    write.descriptorCount = count;
    write.descriptorType = type;
    write.pTexelBufferView = buffer_views;
    return write;
}

inline VkWriteDescriptorSet Device::write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                         VkDescriptorType type,
                                                         const std::vector<VkDescriptorImageInfo> &image_info) {
    return write_descriptor_set(set, binding, array_element, type, image_info.size(), &image_info[0]);
}

inline VkWriteDescriptorSet Device::write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                         VkDescriptorType type,
                                                         const std::vector<VkDescriptorBufferInfo> &buffer_info) {
    return write_descriptor_set(set, binding, array_element, type, buffer_info.size(), &buffer_info[0]);
}

inline VkWriteDescriptorSet Device::write_descriptor_set(const DescriptorSet &set, uint32_t binding, uint32_t array_element,
                                                         VkDescriptorType type, const std::vector<VkBufferView> &buffer_views) {
    return write_descriptor_set(set, binding, array_element, type, buffer_views.size(), &buffer_views[0]);
}

inline VkCopyDescriptorSet Device::copy_descriptor_set(const DescriptorSet &src_set, uint32_t src_binding,
                                                       uint32_t src_array_element, const DescriptorSet &dst_set,
                                                       uint32_t dst_binding, uint32_t dst_array_element, uint32_t count) {
    VkCopyDescriptorSet copy = LvlInitStruct<VkCopyDescriptorSet>();
    copy.srcSet = src_set.handle();
    copy.srcBinding = src_binding;
    copy.srcArrayElement = src_array_element;
    copy.dstSet = dst_set.handle();
    copy.dstBinding = dst_binding;
    copy.dstArrayElement = dst_array_element;
    copy.descriptorCount = count;

    return copy;
}

inline VkCommandBufferAllocateInfo CommandBuffer::create_info(VkCommandPool const &pool) {
    VkCommandBufferAllocateInfo info = LvlInitStruct<VkCommandBufferAllocateInfo>();
    info.commandPool = pool;
    info.commandBufferCount = 1;
    return info;
}

}  // namespace vk_testing

#endif  // VKTESTBINDING_H
