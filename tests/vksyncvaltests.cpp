/*
 * Copyright (c) 2015-2021 The Khronos Group Inc.
 * Copyright (c) 2015-2021 Valve Corporation
 * Copyright (c) 2015-2021 LunarG, Inc.
 * Copyright (c) 2015-2021 Google, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Author: Chia-I Wu <olvaffe@gmail.com>
 * Author: Chris Forbes <chrisf@ijw.co.nz>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Mark Lobodzinski <mark@lunarg.com>
 * Author: Mike Stroyan <mike@LunarG.com>
 * Author: Tobin Ehlis <tobine@google.com>
 * Author: Tony Barbour <tony@LunarG.com>
 * Author: Cody Northrop <cnorthrop@google.com>
 * Author: Dave Houlton <daveh@lunarg.com>
 * Author: Jeremy Kniager <jeremyk@lunarg.com>
 * Author: Shannon McPherson <shannon@lunarg.com>
 * Author: John Zulauf <jzulauf@lunarg.com>
 */
#include <type_traits>

#include "cast_utils.h"
#include "layer_validation_tests.h"

TEST_F(VkSyncValTest, SyncBufferCopyHazards) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    if (DeviceExtensionSupported(gpu(), nullptr, VK_AMD_BUFFER_MARKER_EXTENSION_NAME)) {
        m_device_extension_names.push_back(VK_AMD_BUFFER_MARKER_EXTENSION_NAME);
    }
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
    bool has_amd_buffer_maker = DeviceExtensionEnabled(VK_AMD_BUFFER_MARKER_EXTENSION_NAME);

    VkBufferObj buffer_a;
    VkBufferObj buffer_b;
    VkBufferObj buffer_c;
    VkMemoryPropertyFlags mem_prop = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_a.init_as_src_and_dst(*m_device, 256, mem_prop);
    buffer_b.init_as_src_and_dst(*m_device, 256, mem_prop);
    buffer_c.init_as_src_and_dst(*m_device, 256, mem_prop);

    VkBufferCopy region = {0, 0, 256};
    VkBufferCopy front2front = {0, 0, 128};
    VkBufferCopy front2back = {0, 128, 128};
    VkBufferCopy back2back = {128, 128, 128};

    auto cb = m_commandBuffer->handle();
    m_commandBuffer->begin();

    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &region);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &region);
    m_errorMonitor->VerifyFound();

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto buffer_barrier = LvlInitStruct<VkBufferMemoryBarrier>();
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.buffer = buffer_a.handle();
    buffer_barrier.offset = 0;
    buffer_barrier.size = 256;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &buffer_barrier, 0,
                           nullptr);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &front2front);
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &back2back);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &front2back);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_b.handle(), 1, &region);
    m_errorMonitor->VerifyFound();

    // NOTE: Since the previous command skips in validation, the state update is never done, and the validation layer thus doesn't
    //       record the write operation to b.  So we'll need to repeat it successfully to set up for the *next* test.

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto mem_barrier = LvlInitStruct<VkMemoryBarrier>();
    mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mem_barrier, 0, nullptr, 0,
                           nullptr);
    m_errorMonitor->ExpectSuccess();

    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_c.handle(), buffer_b.handle(), 1, &region);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;  // Protect C but not B
    mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mem_barrier, 0, nullptr, 0,
                           nullptr);
    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_b.handle(), buffer_c.handle(), 1, &region);
    m_errorMonitor->VerifyFound();

    m_commandBuffer->end();

    // CmdFillBuffer
    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->reset();
    m_commandBuffer->begin();
    vk::CmdFillBuffer(m_commandBuffer->handle(), buffer_a.handle(), 0, 256, 1);
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    vk::CmdCopyBuffer(cb, buffer_b.handle(), buffer_a.handle(), 1, &region);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdFillBuffer(m_commandBuffer->handle(), buffer_a.handle(), 0, 256, 1);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();

    // CmdUpdateBuffer
    int i = 10;
    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->reset();
    m_commandBuffer->begin();
    vk::CmdUpdateBuffer(m_commandBuffer->handle(), buffer_a.handle(), 0, sizeof(i), &i);
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    vk::CmdCopyBuffer(cb, buffer_b.handle(), buffer_a.handle(), 1, &region);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdUpdateBuffer(m_commandBuffer->handle(), buffer_a.handle(), 0, sizeof(i), &i);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();

    // CmdWriteBufferMarkerAMD
    if (has_amd_buffer_maker) {
        auto fpCmdWriteBufferMarkerAMD =
            (PFN_vkCmdWriteBufferMarkerAMD)vk::GetDeviceProcAddr(m_device->device(), "vkCmdWriteBufferMarkerAMD");
        if (!fpCmdWriteBufferMarkerAMD) {
            printf("%s Test requires unsupported vkCmdWriteBufferMarkerAMD feature. Skipped.\n", kSkipPrefix);
        } else {
            m_errorMonitor->ExpectSuccess();
            m_commandBuffer->reset();
            m_commandBuffer->begin();
            fpCmdWriteBufferMarkerAMD(m_commandBuffer->handle(), VK_PIPELINE_STAGE_TRANSFER_BIT, buffer_a.handle(), 0, 1);
            m_commandBuffer->end();
            m_errorMonitor->VerifyNotFound();

            m_commandBuffer->reset();
            m_commandBuffer->begin();
            vk::CmdCopyBuffer(cb, buffer_b.handle(), buffer_a.handle(), 1, &region);
            m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
            fpCmdWriteBufferMarkerAMD(m_commandBuffer->handle(), VK_PIPELINE_STAGE_TRANSFER_BIT, buffer_a.handle(), 0, 1);
            m_errorMonitor->VerifyFound();
            m_commandBuffer->end();
        }
    } else {
        printf("%s Test requires unsupported vkCmdWriteBufferMarkerAMD feature. Skipped.\n", kSkipPrefix);
    }
}

TEST_F(VkSyncValTest, Sync2BufferCopyHazards) {
    SetTargetApiVersion(VK_API_VERSION_1_2);
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    if (DeviceExtensionSupported(gpu(), nullptr, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        m_device_extension_names.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    } else {
        printf("%s Synchronization2 not supported, skipping test\n", kSkipPrefix);
        return;
    }

    if (!CheckSynchronization2SupportAndInitState(this)) {
        printf("%s Synchronization2 not supported, skipping test\n", kSkipPrefix);
        return;
    }
    auto fpCmdPipelineBarrier2KHR = (PFN_vkCmdPipelineBarrier2KHR)vk::GetDeviceProcAddr(m_device->device(), "vkCmdPipelineBarrier2KHR");

    VkBufferObj buffer_a;
    VkBufferObj buffer_b;
    VkBufferObj buffer_c;
    VkMemoryPropertyFlags mem_prop = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_a.init_as_src_and_dst(*m_device, 256, mem_prop);
    buffer_b.init_as_src_and_dst(*m_device, 256, mem_prop);
    buffer_c.init_as_src_and_dst(*m_device, 256, mem_prop);

    VkBufferCopy region = {0, 0, 256};
    VkBufferCopy front2front = {0, 0, 128};
    VkBufferCopy front2back = {0, 128, 128};
    VkBufferCopy back2back = {128, 128, 128};

    auto cb = m_commandBuffer->handle();
    m_commandBuffer->begin();

    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &region);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &region);
    m_errorMonitor->VerifyFound();

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    {
        auto buffer_barrier = lvl_init_struct<VkBufferMemoryBarrier2KHR>();
        buffer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
        buffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
        buffer_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT_KHR;
        buffer_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
        buffer_barrier.buffer = buffer_a.handle();
        buffer_barrier.offset = 0;
        buffer_barrier.size = 256;
        auto dep_info = lvl_init_struct<VkDependencyInfoKHR>();
        dep_info.bufferMemoryBarrierCount = 1;
        dep_info.pBufferMemoryBarriers = &buffer_barrier;
        fpCmdPipelineBarrier2KHR(cb, &dep_info);
    }

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &front2front);
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &back2back);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &front2back);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_b.handle(), 1, &region);
    m_errorMonitor->VerifyFound();

    // NOTE: Since the previous command skips in validation, the state update is never done, and the validation layer thus doesn't
    //       record the write operation to b.  So we'll need to repeat it successfully to set up for the *next* test.

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    {
        auto mem_barrier = lvl_init_struct<VkMemoryBarrier2KHR>();
        mem_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
        mem_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
        mem_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
        mem_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
        auto dep_info = lvl_init_struct<VkDependencyInfoKHR>();
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &mem_barrier;
        fpCmdPipelineBarrier2KHR(cb, &dep_info);
        m_errorMonitor->ExpectSuccess();

        vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_c.handle(), buffer_b.handle(), 1, &region);
        m_errorMonitor->VerifyNotFound();

        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
        mem_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT_KHR;  // Protect C but not B
        mem_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
        fpCmdPipelineBarrier2KHR(cb, &dep_info);
        vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_b.handle(), buffer_c.handle(), 1, &region);
        m_errorMonitor->VerifyFound();

        m_commandBuffer->end();
    }
}

TEST_F(VkSyncValTest, SyncCopyOptimalImageHazards) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_a(m_device);
    auto image_ci = VkImageObj::ImageCreateInfo2D(128, 128, 1, 2, format, usage, VK_IMAGE_TILING_OPTIMAL);
    image_a.Init(image_ci);
    ASSERT_TRUE(image_a.initialized());

    VkImageObj image_b(m_device);
    image_b.Init(image_ci);
    ASSERT_TRUE(image_b.initialized());

    VkImageObj image_c(m_device);
    image_c.Init(image_ci);
    ASSERT_TRUE(image_c.initialized());

    VkImageSubresourceLayers layers_all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
    VkImageSubresourceLayers layers_0{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageSubresourceLayers layers_1{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1};
    VkImageSubresourceRange full_subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
    VkOffset3D zero_offset{0, 0, 0};
    VkOffset3D half_offset{64, 64, 0};
    VkExtent3D full_extent{128, 128, 1};  // <-- image type is 2D
    VkExtent3D half_extent{64, 64, 1};    // <-- image type is 2D

    VkImageCopy full_region = {layers_all, zero_offset, layers_all, zero_offset, full_extent};
    VkImageCopy region_0_to_0 = {layers_0, zero_offset, layers_0, zero_offset, full_extent};
    VkImageCopy region_0_to_1 = {layers_0, zero_offset, layers_1, zero_offset, full_extent};
    VkImageCopy region_1_to_1 = {layers_1, zero_offset, layers_1, zero_offset, full_extent};
    VkImageCopy region_0_front = {layers_0, zero_offset, layers_0, zero_offset, half_extent};
    VkImageCopy region_0_back = {layers_0, half_offset, layers_0, half_offset, half_extent};

    m_commandBuffer->begin();

    image_c.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    auto cb = m_commandBuffer->handle();

    vk::CmdCopyImage(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyFound();

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto image_barrier = LvlInitStruct<VkImageMemoryBarrier>();
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.image = image_a.handle();
    image_barrier.subresourceRange = full_subresource_range;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &image_barrier);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_to_0);
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_1_to_1);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_to_1);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyFound();

    // NOTE: Since the previous command skips in validation, the state update is never done, and the validation layer thus doesn't
    //       record the write operation to b.  So we'll need to repeat it successfully to set up for the *next* test.

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto mem_barrier = LvlInitStruct<VkMemoryBarrier>();
    mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mem_barrier, 0, nullptr, 0,
                           nullptr);
    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyNotFound();

    // Use barrier to protect last reader, but not last writer...
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;  // Protects C but not B
    mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mem_barrier, 0, nullptr, 0,
                           nullptr);
    vk::CmdCopyImage(cb, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyFound();

    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_front);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_back);
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->end();

    // CmdResolveImage
    VkImageFormatProperties formProps = {{0, 0, 0}, 0, 0, 0, 0};
    vk::GetPhysicalDeviceImageFormatProperties(m_device->phy().handle(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
                                               VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &formProps);

    if (!(formProps.sampleCounts & VK_SAMPLE_COUNT_2_BIT)) {
        printf("%s CmdResolveImage Test requires unsupported VK_SAMPLE_COUNT_2_BIT feature. Skipped.\n", kSkipPrefix);
    } else {
        m_errorMonitor->ExpectSuccess();
        VkImageObj image_s2_a(m_device), image_s2_b(m_device);
        image_ci.samples = VK_SAMPLE_COUNT_2_BIT;
        image_s2_a.Init(image_ci);
        ASSERT_TRUE(image_s2_a.initialized());

        image_s2_b.Init(image_ci);
        ASSERT_TRUE(image_s2_b.initialized());

        VkImageResolve r_full_region = {layers_all, zero_offset, layers_all, zero_offset, full_extent};

        m_commandBuffer->reset();
        m_commandBuffer->begin();
        image_s2_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
        image_s2_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
        vk::CmdResolveImage(cb, image_s2_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                            &r_full_region);
        m_commandBuffer->end();
        m_errorMonitor->VerifyNotFound();

        m_commandBuffer->reset();
        m_commandBuffer->begin();
        vk::CmdCopyImage(cb, image_s2_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_s2_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                         &full_region);
        vk::CmdCopyImage(cb, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);

        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
        vk::CmdResolveImage(cb, image_s2_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                            &r_full_region);
        m_errorMonitor->VerifyFound();

        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
        vk::CmdResolveImage(cb, image_s2_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                            &r_full_region);
        m_errorMonitor->VerifyFound();
        m_commandBuffer->end();
    }
}

TEST_F(VkSyncValTest, Sync2CopyOptimalImageHazards) {
    SetTargetApiVersion(VK_API_VERSION_1_2);
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    if (DeviceExtensionSupported(gpu(), nullptr, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        m_device_extension_names.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    } else {
        printf("%s Synchronization2 not supported, skipping test\n", kSkipPrefix);
        return;
    }

    if (!CheckSynchronization2SupportAndInitState(this)) {
        printf("%s Synchronization2 not supported, skipping test\n", kSkipPrefix);
        return;
    }
    auto fpCmdPipelineBarrier2KHR = (PFN_vkCmdPipelineBarrier2KHR)vk::GetDeviceProcAddr(m_device->device(), "vkCmdPipelineBarrier2KHR");

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_a(m_device);
    auto image_ci = VkImageObj::ImageCreateInfo2D(128, 128, 1, 2, format, usage, VK_IMAGE_TILING_OPTIMAL);
    image_a.Init(image_ci);
    ASSERT_TRUE(image_a.initialized());

    VkImageObj image_b(m_device);
    image_b.Init(image_ci);
    ASSERT_TRUE(image_b.initialized());

    VkImageObj image_c(m_device);
    image_c.Init(image_ci);
    ASSERT_TRUE(image_c.initialized());

    VkImageSubresourceLayers layers_all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
    VkImageSubresourceLayers layers_0{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageSubresourceLayers layers_1{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1};
    VkImageSubresourceRange full_subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
    VkOffset3D zero_offset{0, 0, 0};
    VkOffset3D half_offset{64, 64, 0};
    VkExtent3D full_extent{128, 128, 1};  // <-- image type is 2D
    VkExtent3D half_extent{64, 64, 1};    // <-- image type is 2D

    VkImageCopy full_region = {layers_all, zero_offset, layers_all, zero_offset, full_extent};
    VkImageCopy region_0_to_0 = {layers_0, zero_offset, layers_0, zero_offset, full_extent};
    VkImageCopy region_0_to_1 = {layers_0, zero_offset, layers_1, zero_offset, full_extent};
    VkImageCopy region_1_to_1 = {layers_1, zero_offset, layers_1, zero_offset, full_extent};
    VkImageCopy region_0_front = {layers_0, zero_offset, layers_0, zero_offset, half_extent};
    VkImageCopy region_0_back = {layers_0, half_offset, layers_0, half_offset, half_extent};

    m_commandBuffer->begin();

    image_c.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    auto cb = m_commandBuffer->handle();

    vk::CmdCopyImage(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyFound();

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    {
        auto image_barrier = lvl_init_struct<VkImageMemoryBarrier2KHR>();
        image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
        image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
        image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT_KHR;
        image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
        image_barrier.image = image_a.handle();
        image_barrier.subresourceRange = full_subresource_range;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        auto dep_info = lvl_init_struct<VkDependencyInfoKHR>();
        dep_info.imageMemoryBarrierCount = 1;
        dep_info.pImageMemoryBarriers = &image_barrier;
        fpCmdPipelineBarrier2KHR(cb, &dep_info);
    }

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_to_0);
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_1_to_1);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_to_1);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyFound();

    // NOTE: Since the previous command skips in validation, the state update is never done, and the validation layer thus doesn't
    //       record the write operation to b.  So we'll need to repeat it successfully to set up for the *next* test.

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    {
        auto mem_barrier = lvl_init_struct<VkMemoryBarrier2KHR>();
        mem_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
        mem_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT_KHR;
        mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        auto dep_info = lvl_init_struct<VkDependencyInfoKHR>();
        dep_info.memoryBarrierCount = 1;
        dep_info.pMemoryBarriers = &mem_barrier;
        fpCmdPipelineBarrier2KHR(cb, &dep_info);
        m_errorMonitor->ExpectSuccess();
        vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
        m_errorMonitor->VerifyNotFound();

        // Use barrier to protect last reader, but not last writer...
        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
        mem_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT_KHR;  // Protects C but not B
        mem_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR;
        fpCmdPipelineBarrier2KHR(cb, &dep_info);
        vk::CmdCopyImage(cb, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
        m_errorMonitor->VerifyFound();
    }

    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_front);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_0_back);
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->end();
}

TEST_F(VkSyncValTest, SyncCopyOptimalMultiPlanarHazards) {
    // TODO: Add code to enable sync validation
    // Enable KHR multiplane req'd extensions
    bool mp_extensions = InstanceExtensionSupported(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                                                    VK_KHR_GET_MEMORY_REQUIREMENTS_2_SPEC_VERSION);
    if (mp_extensions) {
        m_instance_extension_names.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    mp_extensions = mp_extensions && DeviceExtensionSupported(gpu(), nullptr, VK_KHR_MAINTENANCE1_EXTENSION_NAME);
    mp_extensions = mp_extensions && DeviceExtensionSupported(gpu(), nullptr, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
    mp_extensions = mp_extensions && DeviceExtensionSupported(gpu(), nullptr, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
    mp_extensions = mp_extensions && DeviceExtensionSupported(gpu(), nullptr, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    if (mp_extensions) {
        m_device_extension_names.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
        m_device_extension_names.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
        m_device_extension_names.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
        m_device_extension_names.push_back(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    } else {
        printf("%s test requires KHR multiplane extensions, not available.  Skipping.\n", kSkipPrefix);
        return;
    }

    ASSERT_NO_FATAL_FAILURE(InitState());

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
    VkImageObj image_a(m_device);
    const auto image_ci = VkImageObj::ImageCreateInfo2D(128, 128, 1, 2, format, usage, VK_IMAGE_TILING_OPTIMAL);
    // Verify format
    bool supported = ImageFormatAndFeaturesSupported(instance(), gpu(), image_ci,
                                                     VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
    if (!supported) {
        printf("%s Multiplane image format not supported.  Skipping test.\n", kSkipPrefix);
        return;  // Assume there's low ROI on searching for different mp formats
    }

    image_a.Init(image_ci);
    VkImageObj image_b(m_device);
    image_b.Init(image_ci);
    VkImageObj image_c(m_device);
    image_c.Init(image_ci);

    VkImageSubresourceLayers layer_all_plane0{VK_IMAGE_ASPECT_PLANE_0_BIT_KHR, 0, 0, 2};
    VkImageSubresourceLayers layer0_plane0{VK_IMAGE_ASPECT_PLANE_0_BIT_KHR, 0, 0, 1};
    VkImageSubresourceLayers layer0_plane1{VK_IMAGE_ASPECT_PLANE_1_BIT_KHR, 0, 0, 1};
    VkImageSubresourceLayers layer1_plane1{VK_IMAGE_ASPECT_PLANE_1_BIT_KHR, 0, 1, 1};
    VkImageSubresourceRange full_subresource_range{
        VK_IMAGE_ASPECT_PLANE_0_BIT_KHR | VK_IMAGE_ASPECT_PLANE_1_BIT_KHR | VK_IMAGE_ASPECT_PLANE_2_BIT_KHR, 0, 1, 0, 2};
    VkOffset3D zero_offset{0, 0, 0};
    VkOffset3D one_four_offset{32, 32, 0};
    VkExtent3D full_extent{128, 128, 1};    // <-- image type is 2D
    VkExtent3D half_extent{64, 64, 1};      // <-- image type is 2D
    VkExtent3D one_four_extent{32, 32, 1};  // <-- image type is 2D

    VkImageCopy region_all_plane0_to_all_plane0 = {layer_all_plane0, zero_offset, layer_all_plane0, zero_offset, full_extent};
    VkImageCopy region_layer0_plane0_to_layer0_plane0 = {layer0_plane0, zero_offset, layer0_plane0, zero_offset, full_extent};
    VkImageCopy region_layer0_plane0_to_layer0_plane1 = {layer0_plane0, zero_offset, layer0_plane1, zero_offset, half_extent};
    VkImageCopy region_layer1_plane1_to_layer1_plane1_front = {layer1_plane1, zero_offset, layer1_plane1, zero_offset,
                                                               one_four_extent};
    VkImageCopy region_layer1_plane1_to_layer1_plane1_back = {layer1_plane1, one_four_offset, layer1_plane1, one_four_offset,
                                                              one_four_extent};

    m_commandBuffer->begin();

    image_c.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    auto cb = m_commandBuffer->handle();

    vk::CmdCopyImage(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_all_plane0_to_all_plane0);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_all_plane0_to_all_plane0);
    m_errorMonitor->VerifyFound();

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto image_barrier = LvlInitStruct<VkImageMemoryBarrier>();
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.image = image_a.handle();
    image_barrier.subresourceRange = full_subresource_range;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &image_barrier);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_layer0_plane0_to_layer0_plane0);
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_layer0_plane0_to_layer0_plane1);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_layer0_plane0_to_layer0_plane1);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_all_plane0_to_all_plane0);
    m_errorMonitor->VerifyFound();

    // NOTE: Since the previous command skips in validation, the state update is never done, and the validation layer thus doesn't
    //       record the write operation to b.  So we'll need to repeat it successfully to set up for the *next* test.

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto mem_barrier = LvlInitStruct<VkMemoryBarrier>();
    mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mem_barrier, 0, nullptr, 0,
                           nullptr);
    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_all_plane0_to_all_plane0);
    m_errorMonitor->VerifyNotFound();

    // Use barrier to protect last reader, but not last writer...
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;  // Protects C but not B
    mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mem_barrier, 0, nullptr, 0,
                           nullptr);
    vk::CmdCopyImage(cb, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_all_plane0_to_all_plane0);
    m_errorMonitor->VerifyFound();

    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_layer1_plane1_to_layer1_plane1_front);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_layer1_plane1_to_layer1_plane1_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_layer1_plane1_to_layer1_plane1_back);
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->end();
}

TEST_F(VkSyncValTest, SyncCopyLinearImageHazards) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_a(m_device);
    const auto image_ci = VkImageObj::ImageCreateInfo2D(128, 128, 1, 1, format, usage, VK_IMAGE_TILING_LINEAR);
    image_a.Init(image_ci);
    VkImageObj image_b(m_device);
    image_b.Init(image_ci);
    VkImageObj image_c(m_device);
    image_c.Init(image_ci);

    VkImageSubresourceLayers layers_all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageSubresourceRange full_subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkOffset3D zero_offset{0, 0, 0};
    VkOffset3D half_offset{64, 64, 0};
    VkExtent3D full_extent{128, 128, 1};  // <-- image type is 2D
    VkExtent3D half_extent{64, 64, 1};    // <-- image type is 2D

    VkImageCopy full_region = {layers_all, zero_offset, layers_all, zero_offset, full_extent};
    VkImageCopy region_front = {layers_all, zero_offset, layers_all, zero_offset, half_extent};
    VkImageCopy region_back = {layers_all, half_offset, layers_all, half_offset, half_extent};

    m_commandBuffer->begin();

    image_c.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    auto cb = m_commandBuffer->handle();

    vk::CmdCopyImage(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyFound();

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto image_barrier = LvlInitStruct<VkImageMemoryBarrier>();
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.image = image_b.handle();
    image_barrier.subresourceRange = full_subresource_range;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &image_barrier);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyNotFound();

    // Use barrier to protect last reader, but not last writer...
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;  // Protects C but not B
    image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &image_barrier);
    vk::CmdCopyImage(cb, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);
    m_errorMonitor->VerifyFound();

    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_front);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_back);
    m_errorMonitor->VerifyNotFound();
}

TEST_F(VkSyncValTest, SyncCopyLinearMultiPlanarHazards) {
    // TODO: Add code to enable sync validation
    // Enable KHR multiplane req'd extensions
    bool mp_extensions = InstanceExtensionSupported(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                                                    VK_KHR_GET_MEMORY_REQUIREMENTS_2_SPEC_VERSION);
    if (mp_extensions) {
        m_instance_extension_names.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    mp_extensions = mp_extensions && DeviceExtensionSupported(gpu(), nullptr, VK_KHR_MAINTENANCE1_EXTENSION_NAME);
    mp_extensions = mp_extensions && DeviceExtensionSupported(gpu(), nullptr, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
    mp_extensions = mp_extensions && DeviceExtensionSupported(gpu(), nullptr, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
    mp_extensions = mp_extensions && DeviceExtensionSupported(gpu(), nullptr, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    if (mp_extensions) {
        m_device_extension_names.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
        m_device_extension_names.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
        m_device_extension_names.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
        m_device_extension_names.push_back(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    } else {
        printf("%s test requires KHR multiplane extensions, not available.  Skipping.\n", kSkipPrefix);
        return;
    }

    ASSERT_NO_FATAL_FAILURE(InitState());

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
    VkImageObj image_a(m_device);
    const auto image_ci = VkImageObj::ImageCreateInfo2D(128, 128, 1, 1, format, usage, VK_IMAGE_TILING_LINEAR);
    // Verify format
    bool supported = ImageFormatAndFeaturesSupported(instance(), gpu(), image_ci,
                                                     VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
    if (!supported) {
        printf("%s Multiplane image format not supported.  Skipping test.\n", kSkipPrefix);
        return;  // Assume there's low ROI on searching for different mp formats
    }

    image_a.Init(image_ci);
    VkImageObj image_b(m_device);
    image_b.Init(image_ci);
    VkImageObj image_c(m_device);
    image_c.Init(image_ci);

    VkImageSubresourceLayers layer_all_plane0{VK_IMAGE_ASPECT_PLANE_0_BIT_KHR, 0, 0, 1};
    VkImageSubresourceLayers layer_all_plane1{VK_IMAGE_ASPECT_PLANE_1_BIT_KHR, 0, 0, 1};
    VkImageSubresourceRange full_subresource_range{
        VK_IMAGE_ASPECT_PLANE_0_BIT_KHR | VK_IMAGE_ASPECT_PLANE_1_BIT_KHR | VK_IMAGE_ASPECT_PLANE_2_BIT_KHR, 0, 1, 0, 1};
    VkOffset3D zero_offset{0, 0, 0};
    VkOffset3D one_four_offset{32, 32, 0};
    VkExtent3D full_extent{128, 128, 1};    // <-- image type is 2D
    VkExtent3D half_extent{64, 64, 1};      // <-- image type is 2D
    VkExtent3D one_four_extent{32, 32, 1};  // <-- image type is 2D

    VkImageCopy region_plane0_to_plane0 = {layer_all_plane0, zero_offset, layer_all_plane0, zero_offset, full_extent};
    VkImageCopy region_plane0_to_plane1 = {layer_all_plane0, zero_offset, layer_all_plane1, zero_offset, half_extent};
    VkImageCopy region_plane1_to_plane1_front = {layer_all_plane1, zero_offset, layer_all_plane1, zero_offset, one_four_extent};
    VkImageCopy region_plane1_to_plane1_back = {layer_all_plane1, one_four_offset, layer_all_plane1, one_four_offset,
                                                one_four_extent};

    m_commandBuffer->begin();

    image_c.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    auto cb = m_commandBuffer->handle();

    vk::CmdCopyImage(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane0_to_plane0);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane0_to_plane0);
    m_errorMonitor->VerifyFound();

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto image_barrier = LvlInitStruct<VkImageMemoryBarrier>();
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.image = image_a.handle();
    image_barrier.subresourceRange = full_subresource_range;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &image_barrier);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane0_to_plane0);
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane0_to_plane1);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane0_to_plane1);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane0_to_plane0);
    m_errorMonitor->VerifyFound();

    // NOTE: Since the previous command skips in validation, the state update is never done, and the validation layer thus doesn't
    //       record the write operation to b.  So we'll need to repeat it successfully to set up for the *next* test.

    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    auto mem_barrier = LvlInitStruct<VkMemoryBarrier>();
    mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mem_barrier, 0, nullptr, 0,
                           nullptr);
    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane0_to_plane0);
    m_errorMonitor->VerifyNotFound();

    // Use barrier to protect last reader, but not last writer...
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;  // Protects C but not B
    mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mem_barrier, 0, nullptr, 0,
                           nullptr);
    vk::CmdCopyImage(cb, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane0_to_plane0);
    m_errorMonitor->VerifyFound();

    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane1_to_plane1_front);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane1_to_plane1_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImage(cb, image_c.handle(), VK_IMAGE_LAYOUT_GENERAL, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_plane1_to_plane1_back);
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->end();
}

TEST_F(VkSyncValTest, SyncCopyBufferImageHazards) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());

    VkBufferObj buffer_a, buffer_b;
    VkMemoryPropertyFlags mem_prop = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_a.init_as_src_and_dst(*m_device, 2048, mem_prop);
    buffer_b.init_as_src_and_dst(*m_device, 2048, mem_prop);

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_a(m_device), image_b(m_device);
    const auto image_ci = VkImageObj::ImageCreateInfo2D(32, 32, 1, 2, format, usage, VK_IMAGE_TILING_OPTIMAL);
    image_a.Init(image_ci);
    image_b.Init(image_ci);

    VkImageSubresourceLayers layers_0{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageSubresourceLayers layers_1{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1};
    VkOffset3D zero_offset{0, 0, 0};
    VkOffset3D half_offset{16, 16, 0};
    VkExtent3D half_extent{16, 16, 1};  // <-- image type is 2D

    VkBufferImageCopy region_buffer_front_image_0_front = {0, 16, 16, layers_0, zero_offset, half_extent};
    VkBufferImageCopy region_buffer_front_image_1_front = {0, 16, 16, layers_1, zero_offset, half_extent};
    VkBufferImageCopy region_buffer_front_image_1_back = {0, 16, 16, layers_1, half_offset, half_extent};
    VkBufferImageCopy region_buffer_back_image_0_front = {1024, 16, 16, layers_0, zero_offset, half_extent};
    VkBufferImageCopy region_buffer_back_image_0_back = {1024, 16, 16, layers_0, half_offset, half_extent};
    VkBufferImageCopy region_buffer_back_image_1_front = {1024, 16, 16, layers_1, zero_offset, half_extent};
    VkBufferImageCopy region_buffer_back_image_1_back = {1024, 16, 16, layers_1, half_offset, half_extent};

    m_commandBuffer->begin();
    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    auto cb = m_commandBuffer->handle();
    vk::CmdCopyBufferToImage(cb, buffer_a.handle(), image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &region_buffer_front_image_0_front);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyBufferToImage(cb, buffer_a.handle(), image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &region_buffer_front_image_0_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdCopyImageToBuffer(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_a.handle(), 1,
                             &region_buffer_front_image_0_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdCopyImageToBuffer(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_a.handle(), 1,
                             &region_buffer_back_image_0_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyImageToBuffer(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_a.handle(), 1,
                             &region_buffer_front_image_1_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyImageToBuffer(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_a.handle(), 1,
                             &region_buffer_front_image_1_back);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImageToBuffer(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_a.handle(), 1, &region_buffer_back_image_0_back);
    m_errorMonitor->VerifyNotFound();

    auto buffer_barrier = LvlInitStruct<VkBufferMemoryBarrier>();
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.buffer = buffer_a.handle();
    buffer_barrier.offset = 1024;
    buffer_barrier.size = 2048;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &buffer_barrier, 0,
                           nullptr);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImageToBuffer(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_a.handle(), 1,
                             &region_buffer_back_image_1_front);
    m_errorMonitor->VerifyNotFound();

    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &buffer_barrier, 0,
                           nullptr);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyImageToBuffer(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_a.handle(), 1, &region_buffer_back_image_1_back);
    m_errorMonitor->VerifyNotFound();

    vk::CmdCopyImageToBuffer(cb, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_b.handle(), 1,
                             &region_buffer_front_image_0_front);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyImageToBuffer(cb, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, buffer_b.handle(), 1,
                             &region_buffer_front_image_0_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdCopyBufferToImage(cb, buffer_b.handle(), image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &region_buffer_front_image_0_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyBufferToImage(cb, buffer_b.handle(), image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &region_buffer_back_image_0_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdCopyBufferToImage(cb, buffer_b.handle(), image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &region_buffer_front_image_1_front);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdCopyBufferToImage(cb, buffer_b.handle(), image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &region_buffer_front_image_1_back);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBufferToImage(cb, buffer_b.handle(), image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_buffer_back_image_0_back);
    m_errorMonitor->VerifyNotFound();

    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    buffer_barrier.buffer = buffer_b.handle();
    buffer_barrier.offset = 1024;
    buffer_barrier.size = 2048;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &buffer_barrier, 0,
                           nullptr);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBufferToImage(cb, buffer_b.handle(), image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                             &region_buffer_back_image_1_front);
    m_errorMonitor->VerifyNotFound();

    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &buffer_barrier, 0,
                           nullptr);

    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBufferToImage(cb, buffer_b.handle(), image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_buffer_back_image_1_back);
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->end();
}

TEST_F(VkSyncValTest, SyncBlitImageHazards) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_a(m_device), image_b(m_device);
    const auto image_ci = VkImageObj::ImageCreateInfo2D(32, 32, 1, 2, format, usage, VK_IMAGE_TILING_OPTIMAL);
    image_a.Init(image_ci);
    image_b.Init(image_ci);

    VkImageSubresourceLayers layers_0{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageSubresourceLayers layers_1{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1};
    VkOffset3D zero_offset{0, 0, 0};
    VkOffset3D half_0_offset{16, 16, 0};
    VkOffset3D half_1_offset{16, 16, 1};
    VkOffset3D full_offset{32, 32, 1};
    VkImageBlit region_0_front_1_front = {layers_0, {zero_offset, half_1_offset}, layers_1, {zero_offset, half_1_offset}};
    VkImageBlit region_1_front_0_front = {layers_1, {zero_offset, half_1_offset}, layers_0, {zero_offset, half_1_offset}};
    VkImageBlit region_1_back_0_back = {layers_1, {half_0_offset, full_offset}, layers_0, {half_0_offset, full_offset}};

    m_commandBuffer->begin();
    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    auto cb = m_commandBuffer->handle();

    vk::CmdBlitImage(cb, image_a.image(), VK_IMAGE_LAYOUT_GENERAL, image_b.image(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_0_front_1_front, VK_FILTER_NEAREST);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdBlitImage(cb, image_a.image(), VK_IMAGE_LAYOUT_GENERAL, image_b.image(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_0_front_1_front, VK_FILTER_NEAREST);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdBlitImage(cb, image_b.image(), VK_IMAGE_LAYOUT_GENERAL, image_a.image(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_1_front_0_front, VK_FILTER_NEAREST);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    vk::CmdBlitImage(cb, image_b.image(), VK_IMAGE_LAYOUT_GENERAL, image_a.image(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &region_1_back_0_back, VK_FILTER_NEAREST);
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->end();
}

TEST_F(VkSyncValTest, SyncRenderPassBeginTransitionHazard) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());
    ASSERT_NO_FATAL_FAILURE(InitRenderTarget(2));

    // Render Target Information
    auto width = static_cast<uint32_t>(m_width);
    auto height = static_cast<uint32_t>(m_height);
    auto *rt_0 = m_renderTargets[0].get();
    auto *rt_1 = m_renderTargets[1].get();

    // Other buffers with which to interact
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_a(m_device), image_b(m_device);
    const auto image_ci = VkImageObj::ImageCreateInfo2D(width, height, 1, 1, format, usage, VK_IMAGE_TILING_OPTIMAL);
    image_a.Init(image_ci);
    image_b.Init(image_ci);

    VkOffset3D zero_offset{0, 0, 0};
    VkExtent3D full_extent{width, height, 1};  // <-- image type is 2D
    VkImageSubresourceLayers layer_color{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageCopy region_to_copy = {layer_color, zero_offset, layer_color, zero_offset, full_extent};

    auto cb = m_commandBuffer->handle();

    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->begin();
    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rt_0->SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    rt_1->SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    rt_0->SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    vk::CmdCopyImage(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, rt_0->handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_to_copy);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);  // This fails so the driver call is skip and no end is valid
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    // Use the barrier to clean up the WAW, and try again. (and show that validation is accounting for the barrier effect too.)
    VkImageSubresourceRange rt_full_subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto image_barrier = LvlInitStruct<VkImageMemoryBarrier>();
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier.dstAccessMask = 0;
    image_barrier.image = rt_0->handle();
    image_barrier.subresourceRange = rt_full_subresource_range;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &image_barrier);
    vk::CmdCopyImage(cb, rt_1->handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region_to_copy);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);  // This fails so the driver call is skip and no end is valid
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    // A global execution barrier that the implict external dependency can chain with should work...
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 0,
                           nullptr);

    // With the barrier above, the layout transition has a chained execution sync operation, and the default
    // implict VkSubpassDependency safes the load op clear vs. the layout transition...
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    m_commandBuffer->EndRenderPass();
    m_errorMonitor->VerifyNotFound();
}

TEST_F(VkSyncValTest, SyncCmdDispatchDrawHazards) {
    // TODO: Add code to enable sync validation
    SetTargetApiVersion(VK_API_VERSION_1_2);

    // Enable VK_KHR_draw_indirect_count for KHR variants
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, nullptr};
    if (DeviceExtensionSupported(gpu(), nullptr, VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME)) {
        m_device_extension_names.push_back(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
        if (DeviceValidationVersion() >= VK_API_VERSION_1_2) {
            features12.drawIndirectCount = VK_TRUE;
        }
    }
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, &features12, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
    bool has_khr_indirect = DeviceExtensionEnabled(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
    ASSERT_NO_FATAL_FAILURE(InitRenderTarget());

    VkImageUsageFlags image_usage_combine = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_c_a(m_device), image_c_b(m_device);
    const auto image_c_ci = VkImageObj::ImageCreateInfo2D(16, 16, 1, 1, format, image_usage_combine, VK_IMAGE_TILING_OPTIMAL);
    image_c_a.Init(image_c_ci);
    image_c_b.Init(image_c_ci);

    VkImageView imageview_c = image_c_a.targetView(format);
    VkImageUsageFlags image_usage_storage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageObj image_s_a(m_device), image_s_b(m_device);
    const auto image_s_ci = VkImageObj::ImageCreateInfo2D(16, 16, 1, 1, format, image_usage_storage, VK_IMAGE_TILING_OPTIMAL);
    image_s_a.Init(image_s_ci);
    image_s_b.Init(image_s_ci);
    image_s_a.SetLayout(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_s_b.SetLayout(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    VkImageView imageview_s = image_s_a.targetView(format);

    VkSampler sampler_s, sampler_c;
    VkSamplerCreateInfo sampler_ci = SafeSaneSamplerCreateInfo();
    VkResult err = vk::CreateSampler(m_device->device(), &sampler_ci, nullptr, &sampler_s);
    ASSERT_VK_SUCCESS(err);
    err = vk::CreateSampler(m_device->device(), &sampler_ci, nullptr, &sampler_c);
    ASSERT_VK_SUCCESS(err);

    VkBufferObj buffer_a, buffer_b;
    VkMemoryPropertyFlags mem_prop = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_a.init(*m_device, buffer_a.create_info(2048, buffer_usage, nullptr), mem_prop);
    buffer_b.init(*m_device, buffer_b.create_info(2048, buffer_usage, nullptr), mem_prop);

    VkBufferView bufferview;
    auto bvci = LvlInitStruct<VkBufferViewCreateInfo>();
    bvci.buffer = buffer_a.handle();
    bvci.format = VK_FORMAT_R32_SFLOAT;
    bvci.offset = 0;
    bvci.range = VK_WHOLE_SIZE;

    err = vk::CreateBufferView(m_device->device(), &bvci, NULL, &bufferview);
    ASSERT_VK_SUCCESS(err);

    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                           {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                           {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
                                           {3, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                       });

    descriptor_set.WriteDescriptorBufferInfo(0, buffer_a.handle(), 0, 2048);
    descriptor_set.WriteDescriptorImageInfo(1, imageview_c, sampler_c, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                            VK_IMAGE_LAYOUT_GENERAL);
    descriptor_set.WriteDescriptorImageInfo(2, imageview_s, sampler_s, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    descriptor_set.WriteDescriptorBufferView(3, bufferview);
    descriptor_set.UpdateDescriptorSets();

    // Dispatch
    std::string csSource = R"glsl(
        #version 450
        layout(set=0, binding=0) uniform foo { float x; } ub0;
        layout(set=0, binding=1) uniform sampler2D cis1;
        layout(set=0, binding=2, rgba8) uniform readonly image2D si2;
        layout(set=0, binding=3, r32f) uniform readonly imageBuffer stb3;
        void main(){
            vec4 vColor4;
            vColor4.x = ub0.x;
            vColor4 = texture(cis1, vec2(0));
            vColor4 = imageLoad(si2, ivec2(0));
            vColor4 = imageLoad(stb3, 0);
        }
    )glsl";

    VkEventObj event;
    event.init(*m_device, VkEventObj::create_info(0));
    VkEvent event_handle = event.handle();

    CreateComputePipelineHelper pipe(*this);
    pipe.InitInfo();
    pipe.cs_.reset(new VkShaderObj(m_device, csSource.c_str(), VK_SHADER_STAGE_COMPUTE_BIT, this));
    pipe.InitState();
    pipe.pipeline_layout_ = VkPipelineLayoutObj(m_device, {&descriptor_set.layout_});
    pipe.CreateComputePipeline();

    m_commandBuffer->begin();

    VkBufferCopy buffer_region = {0, 0, 2048};
    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_b.handle(), buffer_a.handle(), 1, &buffer_region);

    VkImageSubresourceLayers layer{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkOffset3D zero_offset{0, 0, 0};
    VkExtent3D full_extent{16, 16, 1};
    VkImageCopy image_region = {layer, zero_offset, layer, zero_offset, full_extent};
    vk::CmdCopyImage(m_commandBuffer->handle(), image_c_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_c_a.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &image_region);
    vk::CmdCopyImage(m_commandBuffer->handle(), image_s_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_s_a.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &image_region);

    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdDispatch(m_commandBuffer->handle(), 1, 1, 1);
    m_errorMonitor->VerifyFound();

    m_commandBuffer->end();
    m_commandBuffer->reset();
    m_commandBuffer->begin();

    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);
    vk::CmdDispatch(m_commandBuffer->handle(), 1, 1, 1);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_b.handle(), buffer_a.handle(), 1, &buffer_region);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyImage(m_commandBuffer->handle(), image_c_b.handle(), VK_IMAGE_LAYOUT_GENERAL, image_c_a.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1, &image_region);
    m_errorMonitor->VerifyFound();

    m_commandBuffer->end();
    m_commandBuffer->reset();

    // DispatchIndirect
    m_errorMonitor->ExpectSuccess();
    VkBufferObj buffer_dispatchIndirect, buffer_dispatchIndirect2;
    buffer_usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_dispatchIndirect.init(
        *m_device, buffer_dispatchIndirect.create_info(sizeof(VkDispatchIndirectCommand), buffer_usage, nullptr), mem_prop);
    buffer_dispatchIndirect2.init(
        *m_device, buffer_dispatchIndirect2.create_info(sizeof(VkDispatchIndirectCommand), buffer_usage, nullptr), mem_prop);
    m_commandBuffer->begin();
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);
    vk::CmdDispatchIndirect(m_commandBuffer->handle(), buffer_dispatchIndirect.handle(), 0);
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();

    buffer_region = {0, 0, sizeof(VkDispatchIndirectCommand)};
    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_dispatchIndirect2.handle(), buffer_dispatchIndirect.handle(), 1,
                      &buffer_region);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdDispatchIndirect(m_commandBuffer->handle(), buffer_dispatchIndirect.handle(), 0);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();

    // Draw
    m_errorMonitor->ExpectSuccess();
    const float vbo_data[3] = {1.f, 0.f, 1.f};
    VkVertexInputAttributeDescription VertexInputAttributeDescription = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vbo_data)};
    VkVertexInputBindingDescription VertexInputBindingDescription = {0, sizeof(vbo_data), VK_VERTEX_INPUT_RATE_VERTEX};
    VkBufferObj vbo, vbo2;
    buffer_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vbo.init(*m_device, vbo.create_info(sizeof(vbo_data), buffer_usage, nullptr), mem_prop);
    vbo2.init(*m_device, vbo2.create_info(sizeof(vbo_data), buffer_usage, nullptr), mem_prop);

    VkShaderObj vs(m_device, bindStateVertShaderText, VK_SHADER_STAGE_VERTEX_BIT, this);
    VkShaderObj fs(m_device, csSource.c_str(), VK_SHADER_STAGE_FRAGMENT_BIT, this);

    CreatePipelineHelper g_pipe(*this);
    g_pipe.InitInfo();
    g_pipe.InitState();
    g_pipe.vi_ci_.pVertexBindingDescriptions = &VertexInputBindingDescription;
    g_pipe.vi_ci_.vertexBindingDescriptionCount = 1;
    g_pipe.vi_ci_.pVertexAttributeDescriptions = &VertexInputAttributeDescription;
    g_pipe.vi_ci_.vertexAttributeDescriptionCount = 1;
    g_pipe.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
    g_pipe.pipeline_layout_ = VkPipelineLayoutObj(m_device, {&descriptor_set.layout_});
    ASSERT_VK_SUCCESS(g_pipe.CreateGraphicsPipeline());

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    VkDeviceSize offset = 0;
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);

    VkViewport viewport = {0, 0, 16, 16, 0, 1};
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    VkRect2D scissor = {{0, 0}, {16, 16}};
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);

    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();

    buffer_region = {0, 0, sizeof(vbo_data)};
    vk::CmdCopyBuffer(m_commandBuffer->handle(), vbo2.handle(), vbo.handle(), 1, &buffer_region);

    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_errorMonitor->VerifyFound();

    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();

    // Repeat the draw test with a WaitEvent to protect it.
    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->reset();
    m_commandBuffer->begin();

    vk::CmdCopyBuffer(m_commandBuffer->handle(), vbo2.handle(), vbo.handle(), 1, &buffer_region);

    auto vbo_barrier = LvlInitStruct<VkBufferMemoryBarrier>();
    vbo_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vbo_barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vbo_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vbo_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vbo_barrier.buffer = vbo.handle();
    vbo_barrier.offset = buffer_region.dstOffset;
    vbo_barrier.size = buffer_region.size;

    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);

    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);

    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, nullptr, 1,
                                &vbo_barrier, 0, nullptr);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);

    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    // DrawIndexed
    m_errorMonitor->ExpectSuccess();
    const float ibo_data[3] = {0.f, 0.f, 0.f};
    VkBufferObj ibo, ibo2;
    buffer_usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ibo.init(*m_device, ibo.create_info(sizeof(ibo_data), buffer_usage, nullptr), mem_prop);
    ibo2.init(*m_device, ibo2.create_info(sizeof(ibo_data), buffer_usage, nullptr), mem_prop);

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
    vk::CmdBindIndexBuffer(m_commandBuffer->handle(), ibo.handle(), 0, VK_INDEX_TYPE_UINT16);
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);

    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);
    m_commandBuffer->DrawIndexed(3, 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();

    buffer_region = {0, 0, sizeof(ibo_data)};
    vk::CmdCopyBuffer(m_commandBuffer->handle(), ibo2.handle(), ibo.handle(), 1, &buffer_region);

    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
    vk::CmdBindIndexBuffer(m_commandBuffer->handle(), ibo.handle(), 0, VK_INDEX_TYPE_UINT16);
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    m_commandBuffer->DrawIndexed(3, 1, 0, 0, 0);
    m_errorMonitor->VerifyFound();

    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();

    // DrawIndirect
    m_errorMonitor->ExpectSuccess();
    VkBufferObj buffer_drawIndirect, buffer_drawIndirect2;
    buffer_usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_drawIndirect.init(*m_device, buffer_drawIndirect.create_info(sizeof(VkDrawIndirectCommand), buffer_usage, nullptr),
                             mem_prop);
    buffer_drawIndirect2.init(*m_device, buffer_drawIndirect2.create_info(sizeof(VkDrawIndirectCommand), buffer_usage, nullptr),
                              mem_prop);

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);

    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);
    vk::CmdDrawIndirect(m_commandBuffer->handle(), buffer_drawIndirect.handle(), 0, 1, sizeof(VkDrawIndirectCommand));
    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();

    buffer_region = {0, 0, sizeof(VkDrawIndirectCommand)};
    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_drawIndirect2.handle(), buffer_drawIndirect.handle(), 1, &buffer_region);

    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdDrawIndirect(m_commandBuffer->handle(), buffer_drawIndirect.handle(), 0, 1, sizeof(VkDrawIndirectCommand));
    m_errorMonitor->VerifyFound();

    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();

    // DrawIndexedIndirect
    m_errorMonitor->ExpectSuccess();
    VkBufferObj buffer_drawIndexedIndirect, buffer_drawIndexedIndirect2;
    buffer_usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_drawIndexedIndirect.init(
        *m_device, buffer_drawIndexedIndirect.create_info(sizeof(VkDrawIndexedIndirectCommand), buffer_usage, nullptr), mem_prop);
    buffer_drawIndexedIndirect2.init(
        *m_device, buffer_drawIndexedIndirect2.create_info(sizeof(VkDrawIndexedIndirectCommand), buffer_usage, nullptr), mem_prop);

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
    vk::CmdBindIndexBuffer(m_commandBuffer->handle(), ibo.handle(), 0, VK_INDEX_TYPE_UINT16);
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);

    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);
    vk::CmdDrawIndexedIndirect(m_commandBuffer->handle(), buffer_drawIndirect.handle(), 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();

    buffer_region = {0, 0, sizeof(VkDrawIndexedIndirectCommand)};
    vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_drawIndexedIndirect2.handle(), buffer_drawIndexedIndirect.handle(), 1,
                      &buffer_region);

    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
    vk::CmdBindIndexBuffer(m_commandBuffer->handle(), ibo.handle(), 0, VK_INDEX_TYPE_UINT16);
    vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
    vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &descriptor_set.set_, 0, nullptr);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    vk::CmdDrawIndexedIndirect(m_commandBuffer->handle(), buffer_drawIndexedIndirect.handle(), 0, 1,
                               sizeof(VkDrawIndexedIndirectCommand));
    m_errorMonitor->VerifyFound();

    m_commandBuffer->EndRenderPass();
    m_commandBuffer->end();

    if (has_khr_indirect) {
        // DrawIndirectCount
        auto fpCmdDrawIndirectCountKHR =
            (PFN_vkCmdDrawIndirectCount)vk::GetDeviceProcAddr(m_device->device(), "vkCmdDrawIndirectCountKHR");
        if (!fpCmdDrawIndirectCountKHR) {
            printf("%s Test requires unsupported vkCmdDrawIndirectCountKHR feature. Skipped.\n", kSkipPrefix);
        } else {
            m_errorMonitor->ExpectSuccess();
            VkBufferObj buffer_count, buffer_count2;
            buffer_usage =
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            buffer_count.init(*m_device, buffer_count.create_info(sizeof(uint32_t), buffer_usage, nullptr), mem_prop);
            buffer_count2.init(*m_device, buffer_count2.create_info(sizeof(uint32_t), buffer_usage, nullptr), mem_prop);

            m_commandBuffer->reset();
            m_commandBuffer->begin();
            m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
            vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
            vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
            vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);

            vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
            vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(),
                                      0, 1, &descriptor_set.set_, 0, nullptr);
            fpCmdDrawIndirectCountKHR(m_commandBuffer->handle(), buffer_drawIndirect.handle(), 0, buffer_count.handle(), 0, 1,
                                      sizeof(VkDrawIndirectCommand));
            m_commandBuffer->EndRenderPass();
            m_commandBuffer->end();
            m_errorMonitor->VerifyNotFound();

            m_commandBuffer->reset();
            m_commandBuffer->begin();

            buffer_region = {0, 0, sizeof(uint32_t)};
            vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_count2.handle(), buffer_count.handle(), 1, &buffer_region);

            m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
            vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
            vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
            vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);
            vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
            vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(),
                                      0, 1, &descriptor_set.set_, 0, nullptr);

            m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
            fpCmdDrawIndirectCountKHR(m_commandBuffer->handle(), buffer_drawIndirect.handle(), 0, buffer_count.handle(), 0, 1,
                                      sizeof(VkDrawIndirectCommand));
            m_errorMonitor->VerifyFound();

            m_commandBuffer->EndRenderPass();
            m_commandBuffer->end();
        }

        // DrawIndexedIndirectCount
        auto fpCmdDrawIndexIndirectCountKHR =
            (PFN_vkCmdDrawIndirectCount)vk::GetDeviceProcAddr(m_device->device(), "vkCmdDrawIndexedIndirectCountKHR");
        if (!fpCmdDrawIndexIndirectCountKHR) {
            printf("%s Test requires unsupported vkCmdDrawIndexedIndirectCountKHR feature. Skipped.\n", kSkipPrefix);
        } else {
            m_errorMonitor->ExpectSuccess();
            VkBufferObj buffer_count, buffer_count2;
            buffer_usage =
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            buffer_count.init(*m_device, buffer_count.create_info(sizeof(uint32_t), buffer_usage, nullptr), mem_prop);
            buffer_count2.init(*m_device, buffer_count2.create_info(sizeof(uint32_t), buffer_usage, nullptr), mem_prop);

            m_commandBuffer->reset();
            m_commandBuffer->begin();
            m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
            vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
            vk::CmdBindIndexBuffer(m_commandBuffer->handle(), ibo.handle(), 0, VK_INDEX_TYPE_UINT16);
            vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
            vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);

            vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
            vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(),
                                      0, 1, &descriptor_set.set_, 0, nullptr);
            fpCmdDrawIndexIndirectCountKHR(m_commandBuffer->handle(), buffer_drawIndexedIndirect.handle(), 0, buffer_count.handle(),
                                           0, 1, sizeof(VkDrawIndexedIndirectCommand));
            m_commandBuffer->EndRenderPass();
            m_commandBuffer->end();
            m_errorMonitor->VerifyNotFound();

            m_commandBuffer->reset();
            m_commandBuffer->begin();

            buffer_region = {0, 0, sizeof(uint32_t)};
            vk::CmdCopyBuffer(m_commandBuffer->handle(), buffer_count2.handle(), buffer_count.handle(), 1, &buffer_region);

            m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
            vk::CmdBindVertexBuffers(m_commandBuffer->handle(), 0, 1, &vbo.handle(), &offset);
            vk::CmdBindIndexBuffer(m_commandBuffer->handle(), ibo.handle(), 0, VK_INDEX_TYPE_UINT16);
            vk::CmdSetViewport(m_commandBuffer->handle(), 0, 1, &viewport);
            vk::CmdSetScissor(m_commandBuffer->handle(), 0, 1, &scissor);
            vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
            vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(),
                                      0, 1, &descriptor_set.set_, 0, nullptr);

            m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
            fpCmdDrawIndexIndirectCountKHR(m_commandBuffer->handle(), buffer_drawIndexedIndirect.handle(), 0, buffer_count.handle(),
                                           0, 1, sizeof(VkDrawIndexedIndirectCommand));
            m_errorMonitor->VerifyFound();

            m_commandBuffer->EndRenderPass();
            m_commandBuffer->end();
        }
    } else {
        printf("%s Test requires unsupported vkCmdDrawIndirectCountKHR & vkDrawIndexedIndirectCountKHR feature. Skipped.\n",
               kSkipPrefix);
    }
}

TEST_F(VkSyncValTest, SyncCmdClear) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
    // CmdClearColorImage
    m_errorMonitor->ExpectSuccess();
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_a(m_device), image_b(m_device);
    auto image_ci = VkImageObj::ImageCreateInfo2D(128, 128, 1, 1, format, usage, VK_IMAGE_TILING_OPTIMAL);
    image_a.Init(image_ci);
    image_b.Init(image_ci);

    VkImageSubresourceLayers layers_all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkOffset3D zero_offset{0, 0, 0};
    VkExtent3D full_extent{128, 128, 1};  // <-- image type is 2D
    VkImageSubresourceRange full_subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageCopy full_region = {layers_all, zero_offset, layers_all, zero_offset, full_extent};

    m_commandBuffer->begin();

    image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);

    auto cb = m_commandBuffer->handle();
    VkClearColorValue ccv = {};
    vk::CmdClearColorImage(m_commandBuffer->handle(), image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, &ccv, 1, &full_subresource_range);
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    vk::CmdCopyImage(cb, image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &full_region);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdClearColorImage(m_commandBuffer->handle(), image_a.handle(), VK_IMAGE_LAYOUT_GENERAL, &ccv, 1, &full_subresource_range);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdClearColorImage(m_commandBuffer->handle(), image_b.handle(), VK_IMAGE_LAYOUT_GENERAL, &ccv, 1, &full_subresource_range);
    m_errorMonitor->VerifyFound();

    m_commandBuffer->end();

    // CmdClearDepthStencilImage
    format = FindSupportedDepthStencilFormat(gpu());
    if (!format) {
        printf("%s No Depth + Stencil format found. Skipped.\n", kSkipPrefix);
        return;
    }
    m_errorMonitor->ExpectSuccess();
    VkImageObj image_ds_a(m_device), image_ds_b(m_device);
    image_ci = VkImageObj::ImageCreateInfo2D(128, 128, 1, 1, format, usage, VK_IMAGE_TILING_OPTIMAL);
    image_ds_a.Init(image_ci);
    image_ds_b.Init(image_ci);

    const VkImageAspectFlags ds_aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    image_ds_a.SetLayout(ds_aspect, VK_IMAGE_LAYOUT_GENERAL);
    image_ds_b.SetLayout(ds_aspect, VK_IMAGE_LAYOUT_GENERAL);

    m_commandBuffer->begin();
    const VkClearDepthStencilValue clear_value = {};
    VkImageSubresourceRange ds_range = {ds_aspect, 0, 1, 0, 1};

    vk::CmdClearDepthStencilImage(cb, image_ds_a.handle(), VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &ds_range);
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    VkImageSubresourceLayers ds_layers_all{ds_aspect, 0, 0, 1};
    VkImageCopy ds_full_region = {ds_layers_all, zero_offset, ds_layers_all, zero_offset, full_extent};

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    vk::CmdCopyImage(cb, image_ds_a.handle(), VK_IMAGE_LAYOUT_GENERAL, image_ds_b.handle(), VK_IMAGE_LAYOUT_GENERAL, 1,
                     &ds_full_region);

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdClearDepthStencilImage(m_commandBuffer->handle(), image_ds_a.handle(), VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1,
                                  &ds_range);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdClearDepthStencilImage(m_commandBuffer->handle(), image_ds_b.handle(), VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1,
                                  &ds_range);
    m_errorMonitor->VerifyFound();

    m_commandBuffer->end();
}

TEST_F(VkSyncValTest, SyncCmdQuery) {
    // CmdCopyQueryPoolResults
    m_errorMonitor->ExpectSuccess();
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
    if (IsPlatform(kNexusPlayer)) {
        printf("%s This test should not run on Nexus Player\n", kSkipPrefix);
        return;
    }
    if ((m_device->queue_props.empty()) || (m_device->queue_props[0].queueCount < 2)) {
        printf("%s Queue family needs to have multiple queues to run this test.\n", kSkipPrefix);
        return;
    }
    uint32_t queue_count;
    vk::GetPhysicalDeviceQueueFamilyProperties(gpu(), &queue_count, NULL);
    std::vector<VkQueueFamilyProperties> queue_props(queue_count);
    vk::GetPhysicalDeviceQueueFamilyProperties(gpu(), &queue_count, queue_props.data());
    if (queue_props[m_device->graphics_queue_node_index_].timestampValidBits == 0) {
        printf("%s Device graphic queue has timestampValidBits of 0, skipping.\n", kSkipPrefix);
        return;
    }

    VkQueryPool query_pool;
    VkQueryPoolCreateInfo query_pool_create_info{};
    query_pool_create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query_pool_create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_pool_create_info.queryCount = 1;
    vk::CreateQueryPool(m_device->device(), &query_pool_create_info, nullptr, &query_pool);

    VkBufferObj buffer_a, buffer_b;
    VkMemoryPropertyFlags mem_prop = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_a.init_as_src_and_dst(*m_device, 256, mem_prop);
    buffer_b.init_as_src_and_dst(*m_device, 256, mem_prop);

    VkBufferCopy region = {0, 0, 256};

    auto cb = m_commandBuffer->handle();
    m_commandBuffer->begin();
    vk::CmdResetQueryPool(cb, query_pool, 0, 1);
    vk::CmdWriteTimestamp(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, query_pool, 0);
    vk::CmdCopyQueryPoolResults(cb, query_pool, 0, 1, buffer_a.handle(), 0, 0, VK_QUERY_RESULT_WAIT_BIT);
    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();
    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &region);
    vk::CmdResetQueryPool(cb, query_pool, 0, 1);
    vk::CmdWriteTimestamp(cb, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, query_pool, 0);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyQueryPoolResults(cb, query_pool, 0, 1, buffer_a.handle(), 0, 256, VK_QUERY_RESULT_WAIT_BIT);
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyQueryPoolResults(cb, query_pool, 0, 1, buffer_b.handle(), 0, 256, VK_QUERY_RESULT_WAIT_BIT);
    m_commandBuffer->end();
    m_errorMonitor->VerifyFound();

    // TODO:Track VkQueryPool
    // TODO:CmdWriteTimestamp
    vk::DestroyQueryPool(m_device->device(), query_pool, nullptr);
}

TEST_F(VkSyncValTest, SyncCmdDrawDepthStencil) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
    m_errorMonitor->ExpectSuccess();

    const auto format_ds = FindSupportedDepthStencilFormat(gpu());
    if (!format_ds) {
        printf("%s No Depth + Stencil format found. Skipped.\n", kSkipPrefix);
        return;
    }
    const auto format_dp = FindSupportedDepthOnlyFormat(gpu());
    if (!format_dp) {
        printf("%s No only Depth format found. Skipped.\n", kSkipPrefix);
        return;
    }
    const auto format_st = FindSupportedStencilOnlyFormat(gpu());
    if (!format_st) {
        printf("%s No only Stencil format found. Skipped.\n", kSkipPrefix);
        return;
    }

    VkDepthStencilObj image_ds(m_device), image_dp(m_device), image_st(m_device);
    image_ds.Init(m_device, 16, 16, format_ds);
    image_dp.Init(m_device, 16, 16, format_dp);
    image_st.Init(m_device, 16, 16, format_st);

    VkRenderpassObj rp_ds(m_device, format_ds, true), rp_dp(m_device, format_dp, true), rp_st(m_device, format_st, true);

    VkFramebuffer fb_ds, fb_dp, fb_st;
    VkFramebufferCreateInfo fbci = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, rp_ds.handle(), 1, image_ds.BindInfo(), 16, 16, 1};
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb_ds));
    fbci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, rp_dp.handle(), 1, image_dp.BindInfo(), 16, 16, 1};
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb_dp));
    fbci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, rp_st.handle(), 1, image_st.BindInfo(), 16, 16, 1};
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb_st));

    VkStencilOpState stencil = {};
    stencil.failOp = VK_STENCIL_OP_KEEP;
    stencil.passOp = VK_STENCIL_OP_KEEP;
    stencil.depthFailOp = VK_STENCIL_OP_KEEP;
    stencil.compareOp = VK_COMPARE_OP_NEVER;

    auto ds_ci = LvlInitStruct<VkPipelineDepthStencilStateCreateInfo>();
    ds_ci.depthTestEnable = VK_TRUE;
    ds_ci.depthWriteEnable = VK_TRUE;
    ds_ci.depthCompareOp = VK_COMPARE_OP_NEVER;
    ds_ci.stencilTestEnable = VK_TRUE;
    ds_ci.front = stencil;
    ds_ci.back = stencil;

    CreatePipelineHelper g_pipe_ds(*this), g_pipe_dp(*this), g_pipe_st(*this);
    g_pipe_ds.InitInfo();
    g_pipe_ds.gp_ci_.renderPass = rp_ds.handle();
    g_pipe_ds.gp_ci_.pDepthStencilState = &ds_ci;
    g_pipe_ds.InitState();
    ASSERT_VK_SUCCESS(g_pipe_ds.CreateGraphicsPipeline());
    g_pipe_dp.InitInfo();
    g_pipe_dp.gp_ci_.renderPass = rp_dp.handle();
    ds_ci.stencilTestEnable = VK_FALSE;
    g_pipe_dp.gp_ci_.pDepthStencilState = &ds_ci;
    g_pipe_dp.InitState();
    ASSERT_VK_SUCCESS(g_pipe_dp.CreateGraphicsPipeline());
    g_pipe_st.InitInfo();
    g_pipe_st.gp_ci_.renderPass = rp_st.handle();
    ds_ci.depthTestEnable = VK_FALSE;
    ds_ci.stencilTestEnable = VK_TRUE;
    g_pipe_st.gp_ci_.pDepthStencilState = &ds_ci;
    g_pipe_st.InitState();
    ASSERT_VK_SUCCESS(g_pipe_st.CreateGraphicsPipeline());

    m_commandBuffer->begin();
    m_renderPassBeginInfo.renderArea = {{0, 0}, {16, 16}};
    m_renderPassBeginInfo.pClearValues = nullptr;
    m_renderPassBeginInfo.clearValueCount = 0;

    m_renderPassBeginInfo.renderPass = rp_ds.handle();
    m_renderPassBeginInfo.framebuffer = fb_ds;
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_ds.pipeline_);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();

    m_renderPassBeginInfo.renderPass = rp_dp.handle();
    m_renderPassBeginInfo.framebuffer = fb_dp;
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_dp.pipeline_);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();

    m_renderPassBeginInfo.renderPass = rp_st.handle();
    m_renderPassBeginInfo.framebuffer = fb_st;
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_st.pipeline_);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();

    m_commandBuffer->end();
    m_errorMonitor->VerifyNotFound();

    m_commandBuffer->reset();
    m_commandBuffer->begin();

    VkImageCopy copyRegion;
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = {0, 0, 0};
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = {0, 0, 0};
    copyRegion.extent = {16, 16, 1};

    m_commandBuffer->CopyImage(image_ds.handle(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, image_dp.handle(),
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, &copyRegion);

    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    m_commandBuffer->CopyImage(image_ds.handle(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, image_st.handle(),
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, &copyRegion);
    m_renderPassBeginInfo.renderPass = rp_ds.handle();
    m_renderPassBeginInfo.framebuffer = fb_ds;
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_ds.pipeline_);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();

    m_renderPassBeginInfo.renderPass = rp_dp.handle();
    m_renderPassBeginInfo.framebuffer = fb_dp;
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_dp.pipeline_);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();

    m_renderPassBeginInfo.renderPass = rp_st.handle();
    m_renderPassBeginInfo.framebuffer = fb_st;
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_st.pipeline_);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();

    m_commandBuffer->end();
    vk::DestroyFramebuffer(m_device->device(), fb_ds, nullptr);
    vk::DestroyFramebuffer(m_device->device(), fb_dp, nullptr);
    vk::DestroyFramebuffer(m_device->device(), fb_st, nullptr);
}

TEST_F(VkSyncValTest, RenderPassLoadHazardVsInitialLayout) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());
    ASSERT_NO_FATAL_FAILURE(InitRenderTarget());

    VkImageUsageFlags usage_color = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageUsageFlags usage_input = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_color(m_device), image_input(m_device);
    auto image_ci = VkImageObj::ImageCreateInfo2D(32, 32, 1, 1, format, usage_color, VK_IMAGE_TILING_OPTIMAL);
    image_color.Init(image_ci);
    image_ci.usage = usage_input;
    image_input.Init(image_ci);
    VkImageView attachments[] = {image_color.targetView(format), image_input.targetView(format)};

    const VkAttachmentDescription attachmentDescriptions[] = {
        // Result attachment
        {(VkAttachmentDescriptionFlags)0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
         VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_UNDEFINED,  // Here causes DesiredError that SYNC-HAZARD-NONE in BeginRenderPass.
                                     // It should be VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        // Input attachment
        {(VkAttachmentDescriptionFlags)0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD,
         VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};

    const VkAttachmentReference resultAttachmentRef = {0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference inputAttachmentRef = {1u, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    const VkSubpassDescription subpassDescription = {(VkSubpassDescriptionFlags)0,
                                                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                     1u,
                                                     &inputAttachmentRef,
                                                     1u,
                                                     &resultAttachmentRef,
                                                     0,
                                                     0,
                                                     0u,
                                                     0};

    const VkSubpassDependency subpassDependency = {VK_SUBPASS_EXTERNAL,
                                                   0,
                                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                   VK_ACCESS_TRANSFER_WRITE_BIT,
                                                   VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
                                                   VK_DEPENDENCY_BY_REGION_BIT};

    const VkRenderPassCreateInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                                   0,
                                                   (VkRenderPassCreateFlags)0,
                                                   2u,
                                                   attachmentDescriptions,
                                                   1u,
                                                   &subpassDescription,
                                                   1u,
                                                   &subpassDependency};
    VkRenderPass rp;
    ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &renderPassInfo, nullptr, &rp));

    VkFramebuffer fb;
    VkFramebufferCreateInfo fbci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, rp, 2, attachments, 32, 32, 1};
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb));

    image_input.SetLayout(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    m_commandBuffer->begin();

    m_renderPassBeginInfo.renderArea = {{0, 0}, {32, 32}};
    m_renderPassBeginInfo.renderPass = rp;
    m_renderPassBeginInfo.framebuffer = fb;

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ_AFTER_WRITE");
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    // Even though we have no accesses prior, the layout transition *is* an access, so load can be validated vs. layout transition
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    m_errorMonitor->VerifyFound();
}

TEST_F(VkSyncValTest, SyncRenderPassWithWrongDepthStencilInitialLayout) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());
    if (IsPlatform(kNexusPlayer)) {
        printf("%s This test should not run on Nexus Player\n", kSkipPrefix);
        return;
    }

    ASSERT_NO_FATAL_FAILURE(InitRenderTarget());

    VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat ds_format = FindSupportedDepthStencilFormat(gpu());
    if (!ds_format) {
        printf("%s No Depth + Stencil format found. Skipped.\n", kSkipPrefix);
        return;
    }
    VkImageUsageFlags usage_color = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageUsageFlags usage_ds = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImageObj image_color(m_device), image_color2(m_device);
    auto image_ci = VkImageObj::ImageCreateInfo2D(32, 32, 1, 1, color_format, usage_color, VK_IMAGE_TILING_OPTIMAL);
    image_color.Init(image_ci);
    image_color2.Init(image_ci);
    VkDepthStencilObj image_ds(m_device);
    image_ds.Init(m_device, 32, 32, ds_format, usage_ds);

    const VkAttachmentDescription colorAttachmentDescription = {(VkAttachmentDescriptionFlags)0,
                                                                color_format,
                                                                VK_SAMPLE_COUNT_1_BIT,
                                                                VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                                VK_ATTACHMENT_STORE_OP_STORE,
                                                                VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                                VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                                VK_IMAGE_LAYOUT_UNDEFINED,
                                                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    const VkAttachmentDescription depthStencilAttachmentDescription = {
        (VkAttachmentDescriptionFlags)0, ds_format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_UNDEFINED,  // Here causes DesiredError that SYNC-HAZARD-WRITE_AFTER_WRITE in BeginRenderPass.
                                    // It should be VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    std::vector<VkAttachmentDescription> attachmentDescriptions;
    attachmentDescriptions.push_back(colorAttachmentDescription);
    attachmentDescriptions.push_back(depthStencilAttachmentDescription);

    const VkAttachmentReference colorAttachmentRef = {0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    const VkAttachmentReference depthStencilAttachmentRef = {1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    const VkSubpassDescription subpassDescription = {(VkSubpassDescriptionFlags)0,
                                                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                     0u,
                                                     0,
                                                     1u,
                                                     &colorAttachmentRef,
                                                     0,
                                                     &depthStencilAttachmentRef,
                                                     0u,
                                                     0};

    const VkRenderPassCreateInfo renderPassInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                                   0,
                                                   (VkRenderPassCreateFlags)0,
                                                   (uint32_t)attachmentDescriptions.size(),
                                                   &attachmentDescriptions[0],
                                                   1u,
                                                   &subpassDescription,
                                                   0u,
                                                   0};
    VkRenderPass rp;
    ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &renderPassInfo, nullptr, &rp));

    VkImageView fb_attachments[] = {image_color.targetView(color_format),
                                    image_ds.targetView(ds_format, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)};
    const VkFramebufferCreateInfo fbci = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, 0, 0u, rp, 2u, fb_attachments, 32, 32, 1u,
    };
    VkFramebuffer fb;
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb));
    fb_attachments[0] = image_color2.targetView(color_format);
    VkFramebuffer fb1;
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb1));

    CreatePipelineHelper g_pipe(*this);
    g_pipe.InitInfo();
    g_pipe.gp_ci_.renderPass = rp;

    VkStencilOpState stencil = {};
    stencil.failOp = VK_STENCIL_OP_KEEP;
    stencil.passOp = VK_STENCIL_OP_KEEP;
    stencil.depthFailOp = VK_STENCIL_OP_KEEP;
    stencil.compareOp = VK_COMPARE_OP_NEVER;

    auto ds_ci = LvlInitStruct<VkPipelineDepthStencilStateCreateInfo>();
    ds_ci.depthTestEnable = VK_TRUE;
    ds_ci.depthWriteEnable = VK_TRUE;
    ds_ci.depthCompareOp = VK_COMPARE_OP_NEVER;
    ds_ci.stencilTestEnable = VK_TRUE;
    ds_ci.front = stencil;
    ds_ci.back = stencil;

    g_pipe.gp_ci_.pDepthStencilState = &ds_ci;
    g_pipe.InitState();
    ASSERT_VK_SUCCESS(g_pipe.CreateGraphicsPipeline());

    m_commandBuffer->begin();
    VkClearValue clear = {};
    std::array<VkClearValue, 2> clear_values = { {clear, clear} };
    m_renderPassBeginInfo.pClearValues = clear_values.data();
    m_renderPassBeginInfo.clearValueCount = clear_values.size();
    m_renderPassBeginInfo.renderArea = {{0, 0}, {32, 32}};
    m_renderPassBeginInfo.renderPass = rp;

    m_renderPassBeginInfo.framebuffer = fb;
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);
    m_commandBuffer->EndRenderPass();

    m_renderPassBeginInfo.framebuffer = fb1;

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    m_errorMonitor->VerifyFound();
}

TEST_F(VkSyncValTest, SyncLayoutTransition) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());
    if (IsPlatform(kNexusPlayer)) {
        printf("%s This test should not run on Nexus Player\n", kSkipPrefix);
        return;
    }

    ASSERT_NO_FATAL_FAILURE(InitRenderTarget());

    VkImageUsageFlags usage_color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImageUsageFlags usage_input =
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_color(m_device), image_input(m_device);
    auto image_ci = VkImageObj::ImageCreateInfo2D(64, 64, 1, 1, format, usage_input, VK_IMAGE_TILING_OPTIMAL);
    image_input.InitNoLayout(image_ci);
    image_ci.usage = usage_color;
    image_color.InitNoLayout(image_ci);
    VkImageView view_input = image_input.targetView(format);
    VkImageView view_color = image_color.targetView(format);
    VkImageView attachments[] = {view_color, view_input};

    const VkAttachmentDescription fbAttachment = {
        0u,
        format,
        VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    std::vector<VkAttachmentDescription> attachmentDescs;
    attachmentDescs.push_back(fbAttachment);

    // Add it as a frame buffer attachment.
    const VkAttachmentDescription inputAttachment = {
        0u,
        format,
        VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL,
    };
    attachmentDescs.push_back(inputAttachment);

    std::vector<VkAttachmentReference> inputAttachments;
    const VkAttachmentReference inputRef = {
        1u,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    inputAttachments.push_back(inputRef);

    const VkAttachmentReference colorRef = {
        0u,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const std::vector<VkAttachmentReference> colorAttachments(1u, colorRef);

    const VkSubpassDescription subpass = {
        0u,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        static_cast<uint32_t>(inputAttachments.size()),
        inputAttachments.data(),
        static_cast<uint32_t>(colorAttachments.size()),
        colorAttachments.data(),
        0u,
        nullptr,
        0u,
        nullptr,
    };
    const std::vector<VkSubpassDescription> subpasses(1u, subpass);

    const VkRenderPassCreateInfo renderPassInfo = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0u,
        static_cast<uint32_t>(attachmentDescs.size()),
        attachmentDescs.data(),
        static_cast<uint32_t>(subpasses.size()),
        subpasses.data(),
        0u,
        nullptr,
    };
    VkRenderPass rp;
    ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &renderPassInfo, nullptr, &rp));

    const VkFramebufferCreateInfo fbci = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, 0, 0u, rp, 2u, attachments, 64, 64, 1u,
    };
    VkFramebuffer fb;
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb));

    VkSampler sampler = VK_NULL_HANDLE;
    VkSamplerCreateInfo sampler_info = SafeSaneSamplerCreateInfo();
    vk::CreateSampler(m_device->device(), &sampler_info, NULL, &sampler);

    char const *fsSource = R"glsl(
        #version 450
        layout(input_attachment_index=0, set=0, binding=0) uniform subpassInput x;
        void main() {
           vec4 color = subpassLoad(x);
        }
    )glsl";

    VkShaderObj vs(m_device, bindStateVertShaderText, VK_SHADER_STAGE_VERTEX_BIT, this);
    VkShaderObj fs(m_device, fsSource, VK_SHADER_STAGE_FRAGMENT_BIT, this);

    CreatePipelineHelper g_pipe(*this);
    g_pipe.InitInfo();
    g_pipe.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
    g_pipe.dsl_bindings_ = {{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    g_pipe.gp_ci_.renderPass = rp;
    g_pipe.InitState();
    ASSERT_VK_SUCCESS(g_pipe.CreateGraphicsPipeline());

    g_pipe.descriptor_set_->WriteDescriptorImageInfo(0, view_input, sampler, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    g_pipe.descriptor_set_->UpdateDescriptorSets();

    m_commandBuffer->begin();
    auto cb = m_commandBuffer->handle();
    VkClearColorValue ccv = {};
    VkImageSubresourceRange full_subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    const VkImageMemoryBarrier preClearBarrier = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, 0, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,   0, 0, image_input.handle(),         full_subresource_range,
    };
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u,
                           &preClearBarrier);

    vk::CmdClearColorImage(m_commandBuffer->handle(), image_input.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ccv, 1,
                           &full_subresource_range);

    const VkImageMemoryBarrier postClearBarrier = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        0,
        0,
        image_input.handle(),
        full_subresource_range,
    };
    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr,
                           1u, &postClearBarrier);

    m_renderPassBeginInfo.renderArea = {{0, 0}, {64, 64}};
    m_renderPassBeginInfo.renderPass = rp;
    m_renderPassBeginInfo.framebuffer = fb;

    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &g_pipe.descriptor_set_->set_, 0, nullptr);

    // Positive test for ordering rules between load and input attachment usage
    m_errorMonitor->ExpectSuccess();
    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);

    // Positive test for store ordering vs. input attachment and dependency *to* external for layout transition
    m_commandBuffer->EndRenderPass();
    m_errorMonitor->VerifyNotFound();

    // Catch a conflict with the input attachment final layout transition
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdClearColorImage(m_commandBuffer->handle(), image_input.handle(), VK_IMAGE_LAYOUT_GENERAL, &ccv, 1,
                           &full_subresource_range);
    m_errorMonitor->VerifyFound();
}

TEST_F(VkSyncValTest, SyncSubpassMultiDep) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());
    if (IsPlatform(kNexusPlayer)) {
        printf("%s This test should not run on Nexus Player\n", kSkipPrefix);
        return;
    }

    ASSERT_NO_FATAL_FAILURE(InitRenderTarget());

    VkImageUsageFlags usage_color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImageUsageFlags usage_input =
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_color(m_device), image_input(m_device);
    auto image_ci = VkImageObj::ImageCreateInfo2D(64, 64, 1, 1, format, usage_input, VK_IMAGE_TILING_OPTIMAL);
    image_input.InitNoLayout(image_ci);
    image_ci.usage = usage_color;
    image_color.InitNoLayout(image_ci);
    VkImageView view_input = image_input.targetView(format);
    VkImageView view_color = image_color.targetView(format);
    VkImageView attachments[] = {view_color, view_input};
    VkImageSubresourceRange full_subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageSubresourceLayers mip_0_layer_0{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkOffset3D image_zero{0, 0, 0};
    VkExtent3D image_size{64, 64, 1};
    VkImageCopy full_region{mip_0_layer_0, image_zero, mip_0_layer_0, image_zero, image_size};

    const VkAttachmentDescription fbAttachment = {
        0u,
        format,
        VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL,
    };

    std::vector<VkAttachmentDescription> attachmentDescs;
    attachmentDescs.push_back(fbAttachment);

    // Add it as a frame buffer attachment.
    const VkAttachmentDescription inputAttachment = {
        0u,
        format,
        VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL,
    };
    attachmentDescs.push_back(inputAttachment);

    std::vector<VkAttachmentReference> inputAttachments;
    const VkAttachmentReference inputRef = {
        1u,
        VK_IMAGE_LAYOUT_GENERAL,
    };
    inputAttachments.push_back(inputRef);

    const VkAttachmentReference colorRef = {
        0u,
        VK_IMAGE_LAYOUT_GENERAL,
    };
    const std::vector<VkAttachmentReference> colorAttachments(1u, colorRef);

    const VkSubpassDescription subpass = {
        0u,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        static_cast<uint32_t>(inputAttachments.size()),
        inputAttachments.data(),
        static_cast<uint32_t>(colorAttachments.size()),
        colorAttachments.data(),
        0u,
        nullptr,
        0u,
        nullptr,
    };
    const std::vector<VkSubpassDescription> subpasses(1u, subpass);

    std::vector<VkSubpassDependency> subpass_dep_postive;
    subpass_dep_postive.push_back({VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_DEPENDENCY_VIEW_LOCAL_BIT});
    subpass_dep_postive.push_back({VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_DEPENDENCY_VIEW_LOCAL_BIT});
    subpass_dep_postive.push_back({0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT, VK_DEPENDENCY_VIEW_LOCAL_BIT});
    subpass_dep_postive.push_back({0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_DEPENDENCY_VIEW_LOCAL_BIT});

    VkRenderPassCreateInfo renderPassInfo = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0u,
        static_cast<uint32_t>(attachmentDescs.size()),
        attachmentDescs.data(),
        static_cast<uint32_t>(subpasses.size()),
        subpasses.data(),
        static_cast<uint32_t>(subpass_dep_postive.size()),
        subpass_dep_postive.data(),
    };
    VkRenderPass rp_positive;
    ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &renderPassInfo, nullptr, &rp_positive));

    std::vector<VkSubpassDependency> subpass_dep_negative;
    subpass_dep_negative.push_back({VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                    VK_DEPENDENCY_VIEW_LOCAL_BIT});
    // Show that the two barriers do *not* chain by breaking the positive barrier into two bits.
    subpass_dep_negative.push_back({VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                                    VK_DEPENDENCY_VIEW_LOCAL_BIT});
    subpass_dep_negative.push_back({VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                                    VK_DEPENDENCY_VIEW_LOCAL_BIT});

    renderPassInfo.dependencyCount = static_cast<uint32_t>(subpass_dep_negative.size());
    renderPassInfo.pDependencies = subpass_dep_negative.data();
    VkRenderPass rp_negative;
    ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &renderPassInfo, nullptr, &rp_negative));

    // rp_postive and rp_negative should be compatible for the same fb object
    const VkFramebufferCreateInfo fbci = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, 0, 0u, rp_positive, 2u, attachments, 64, 64, 1u,
    };
    VkFramebuffer fb;
    ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb));

    VkSampler sampler = VK_NULL_HANDLE;
    VkSamplerCreateInfo sampler_info = SafeSaneSamplerCreateInfo();
    vk::CreateSampler(m_device->device(), &sampler_info, NULL, &sampler);

    char const *fsSource = R"glsl(
        #version 450
        layout(input_attachment_index=0, set=0, binding=0) uniform subpassInput x;
        void main() {
           vec4 color = subpassLoad(x);
        }
    )glsl";

    VkShaderObj vs(m_device, bindStateVertShaderText, VK_SHADER_STAGE_VERTEX_BIT, this);
    VkShaderObj fs(m_device, fsSource, VK_SHADER_STAGE_FRAGMENT_BIT, this);

    CreatePipelineHelper g_pipe(*this);
    g_pipe.InitInfo();
    g_pipe.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
    g_pipe.dsl_bindings_ = {{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    g_pipe.gp_ci_.renderPass = rp_positive;
    g_pipe.InitState();
    ASSERT_VK_SUCCESS(g_pipe.CreateGraphicsPipeline());

    g_pipe.descriptor_set_->WriteDescriptorImageInfo(0, view_input, sampler, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
    g_pipe.descriptor_set_->UpdateDescriptorSets();

    m_commandBuffer->begin();
    auto cb = m_commandBuffer->handle();
    VkClearColorValue ccv = {};

    const VkImageMemoryBarrier xferDestBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                                  nullptr,
                                                  VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                                  VK_ACCESS_TRANSFER_WRITE_BIT,
                                                  VK_IMAGE_LAYOUT_GENERAL,
                                                  VK_IMAGE_LAYOUT_GENERAL,
                                                  VK_QUEUE_FAMILY_IGNORED,
                                                  VK_QUEUE_FAMILY_IGNORED,
                                                  VK_NULL_HANDLE,
                                                  full_subresource_range};
    const VkImageMemoryBarrier xferDestToSrcBarrier = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        nullptr,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        VK_NULL_HANDLE,
        full_subresource_range,
    };

    VkImageMemoryBarrier preClearBarrier = xferDestBarrier;
    preClearBarrier.image = image_color.handle();

    VkImageMemoryBarrier preCopyBarriers[2] = {xferDestToSrcBarrier, xferDestBarrier};
    preCopyBarriers[0].image = image_color.handle();
    preCopyBarriers[1].image = image_input.handle();
    // Positive test for ordering rules between load and input attachment usage
    m_errorMonitor->ExpectSuccess();

    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u,
                           &preClearBarrier);

    vk::CmdClearColorImage(m_commandBuffer->handle(), image_color.handle(), VK_IMAGE_LAYOUT_GENERAL, &ccv, 1,
                           &full_subresource_range);

    vk::CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 2u,
                           preCopyBarriers);

    vk::CmdCopyImage(m_commandBuffer->handle(), image_color.handle(), VK_IMAGE_LAYOUT_GENERAL, image_input.handle(),
                     VK_IMAGE_LAYOUT_GENERAL, 1u, &full_region);

    // No post copy image barrier, we are testing the subpass dependencies

    m_renderPassBeginInfo.renderArea = {{0, 0}, {64, 64}};
    m_renderPassBeginInfo.renderPass = rp_positive;
    m_renderPassBeginInfo.framebuffer = fb;

    // Postive renderpass multidependency test
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_);
    vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe.pipeline_layout_.handle(), 0, 1,
                              &g_pipe.descriptor_set_->set_, 0, nullptr);

    vk::CmdDraw(m_commandBuffer->handle(), 1, 0, 0, 0);

    // Positive test for store ordering vs. input attachment and dependency *to* external for layout transition
    m_commandBuffer->EndRenderPass();
    // m_errorMonitor->VerifyNotFound();

    vk::CmdCopyImage(m_commandBuffer->handle(), image_color.handle(), VK_IMAGE_LAYOUT_GENERAL, image_input.handle(),
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &full_region);
    m_errorMonitor->VerifyNotFound();

    m_renderPassBeginInfo.renderArea = {{0, 0}, {64, 64}};
    m_renderPassBeginInfo.renderPass = rp_negative;
    m_renderPassBeginInfo.framebuffer = fb;

    // Postive renderpass multidependency test, will fail IFF the dependencies are acting indepently.
    m_errorMonitor->SetDesiredFailureMsg(kErrorBit, "SYNC-HAZARD-READ_AFTER_WRITE");
    m_commandBuffer->BeginRenderPass(m_renderPassBeginInfo);
    m_errorMonitor->VerifyFound();
}

TEST_F(VkSyncValTest, RenderPassAsyncHazard) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState());

    // overall set up:
    // subpass 0:
    //   write image 0
    // subpass 1:
    //   read image 0
    //   write image 1
    // subpass 2:
    //   read image 0
    //   write image 2
    // subpass 3:
    //   read image 0
    //   write image 3
    //
    // subpasses 1 & 2 can run in parallel but both should depend on 0
    // subpass 3 must run after 1 & 2 because otherwise the store operation will
    // race with the reads in the other subpasses.

    constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
    constexpr uint32_t kWidth = 32, kHeight = 32;
    constexpr uint32_t kNumImages = 4;

    VkImageCreateInfo src_img_info = {};
    src_img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    src_img_info.pNext = NULL;
    src_img_info.flags = 0;
    src_img_info.imageType = VK_IMAGE_TYPE_2D;
    src_img_info.format = kFormat;
    src_img_info.extent = {kWidth, kHeight, 1};
    src_img_info.mipLevels = 1;
    src_img_info.arrayLayers = 1;
    src_img_info.samples = VK_SAMPLE_COUNT_2_BIT;
    src_img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    src_img_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    src_img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    src_img_info.queueFamilyIndexCount = 0;
    src_img_info.pQueueFamilyIndices = nullptr;
    src_img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageCreateInfo dst_img_info = {};
    dst_img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    dst_img_info.pNext = nullptr;
    dst_img_info.flags = 0;
    dst_img_info.imageType = VK_IMAGE_TYPE_2D;
    dst_img_info.format = kFormat;
    dst_img_info.extent = {kWidth, kHeight, 1};
    dst_img_info.mipLevels = 1;
    dst_img_info.arrayLayers = 1;
    dst_img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    dst_img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    dst_img_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    dst_img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    dst_img_info.queueFamilyIndexCount = 0;
    dst_img_info.pQueueFamilyIndices = nullptr;
    dst_img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    std::vector<std::unique_ptr<VkImageObj>> images;
    for (uint32_t i = 0; i < kNumImages; i++) {
        images.emplace_back(new VkImageObj(m_device));
    }
    images[0]->Init(src_img_info);
    for (uint32_t i = 1; i < images.size(); i++) {
        images[i]->Init(dst_img_info);
    }

    std::array<VkImageView, kNumImages> attachments{};
    std::array<VkAttachmentDescription, kNumImages> attachment_descriptions{};
    std::array<VkAttachmentReference, kNumImages> color_refs{};
    std::array<VkImageMemoryBarrier, kNumImages> img_barriers{};

    for (uint32_t i = 0; i < attachments.size(); i++) {
        attachments[i] = images[i]->targetView(kFormat);
        attachment_descriptions[i] = {};
        attachment_descriptions[i].flags = 0;
        attachment_descriptions[i].format = kFormat;
        attachment_descriptions[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachment_descriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_descriptions[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment_descriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_descriptions[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_descriptions[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment_descriptions[i].finalLayout =
            (i == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        color_refs[i] = {i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        img_barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        img_barriers[i].srcAccessMask = 0;
        img_barriers[i].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        img_barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img_barriers[i].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        img_barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_barriers[i].image = images[i]->handle();
        img_barriers[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    }

    const VkAttachmentReference input_ref{0u, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::array<std::array<uint32_t, 2>, kNumImages - 1> preserve_subpass{{{2, 3}, {1, 3}, {1, 2}}};

    std::array<VkSubpassDescription, kNumImages> subpasses{};

    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].inputAttachmentCount = 0;
    subpasses[0].pInputAttachments = nullptr;
    subpasses[0].colorAttachmentCount = 1;
    subpasses[0].pColorAttachments = &color_refs[0];

    for (uint32_t i = 1; i < subpasses.size(); i++) {
        subpasses[i].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpasses[i].inputAttachmentCount = 1;
        subpasses[i].pInputAttachments = &input_ref;
        subpasses[i].colorAttachmentCount = 1;
        subpasses[i].pColorAttachments = &color_refs[1];
        subpasses[i].preserveAttachmentCount = preserve_subpass[i - 1].size();
        subpasses[i].pPreserveAttachments = preserve_subpass[i - 1].data();
    }

    VkRenderPassCreateInfo renderpass_info = {};
    renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderpass_info.pNext = nullptr;
    renderpass_info.flags = 0;
    renderpass_info.attachmentCount = attachment_descriptions.size();
    renderpass_info.pAttachments = attachment_descriptions.data();
    renderpass_info.subpassCount = subpasses.size();
    renderpass_info.pSubpasses = subpasses.data();
    renderpass_info.dependencyCount = 0;
    renderpass_info.pDependencies = nullptr;

    VkFramebufferCreateInfo fbci = {};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.pNext = nullptr;
    fbci.flags = 0;
    fbci.attachmentCount = attachments.size();
    fbci.pAttachments = attachments.data();
    fbci.width = kWidth;
    fbci.height = kHeight;
    fbci.layers = 1;

    VkSampler sampler = VK_NULL_HANDLE;
    VkSamplerCreateInfo sampler_info = SafeSaneSamplerCreateInfo();
    vk::CreateSampler(m_device->device(), &sampler_info, NULL, &sampler);

    char const *fsSource = R"glsl(
        #version 450
        layout(input_attachment_index=0, set=0, binding=0) uniform subpassInput x;
        void main() {
           vec4 color = subpassLoad(x);
        }
    )glsl";

    VkShaderObj vs(m_device, bindStateVertShaderText, VK_SHADER_STAGE_VERTEX_BIT, this);
    VkShaderObj fs(m_device, fsSource, VK_SHADER_STAGE_FRAGMENT_BIT, this);

    VkClearValue clear = {};
    clear.color = m_clear_color;
    std::array<VkClearValue, 4> clear_values = {{clear, clear, clear, clear}};

    // run the renderpass with no dependencies
    {
        VkRenderPass rp;
        VkFramebuffer fb;
        ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &renderpass_info, nullptr, &rp));

        fbci.renderPass = rp;
        ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb));

        CreatePipelineHelper g_pipe_0(*this);
        g_pipe_0.InitInfo();
        g_pipe_0.gp_ci_.renderPass = rp;
        g_pipe_0.InitState();
        ASSERT_VK_SUCCESS(g_pipe_0.CreateGraphicsPipeline());

        CreatePipelineHelper g_pipe_12(*this);
        g_pipe_12.InitInfo();
        g_pipe_12.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
        g_pipe_12.dsl_bindings_ = {{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        g_pipe_12.gp_ci_.renderPass = rp;
        g_pipe_12.InitState();
        ASSERT_VK_SUCCESS(g_pipe_12.CreateGraphicsPipeline());

        g_pipe_12.descriptor_set_->WriteDescriptorImageInfo(0, attachments[0], sampler, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
        g_pipe_12.descriptor_set_->UpdateDescriptorSets();

        m_commandBuffer->begin();

        vk::CmdPipelineBarrier(m_commandBuffer->handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, img_barriers.size(),
                               img_barriers.data());

        m_renderPassBeginInfo.renderArea = {{0, 0}, {16, 16}};
        m_renderPassBeginInfo.pClearValues = clear_values.data();
        m_renderPassBeginInfo.clearValueCount = clear_values.size();

        m_renderPassBeginInfo.renderArea = {{0, 0}, {kWidth, kHeight}};
        m_renderPassBeginInfo.renderPass = rp;
        m_renderPassBeginInfo.framebuffer = fb;

        vk::CmdBeginRenderPass(m_commandBuffer->handle(), &m_renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_0.pipeline_);
        vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_0.pipeline_layout_.handle(), 0,
                                  1, &g_pipe_0.descriptor_set_->set_, 0, NULL);

        vk::CmdDraw(m_commandBuffer->handle(), 3, 1, 0, 0);

        for (uint32_t i = 1; i < subpasses.size(); i++) {
            vk::CmdNextSubpass(m_commandBuffer->handle(), VK_SUBPASS_CONTENTS_INLINE);
            vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_12.pipeline_);
            vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_pipe_12.pipeline_layout_.handle(), 0, 1, &g_pipe_12.descriptor_set_->set_, 0, NULL);

            // we're racing the writes from subpass 0 with our shader reads
            m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-READ-RACING-WRITE");
            vk::CmdDraw(m_commandBuffer->handle(), 3, 1, 0, 0);
            m_errorMonitor->VerifyFound();
        }

        // we should get an error from async checking in both subpasses 2 & 3
        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE-RACING-WRITE");
        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE-RACING-WRITE");
        vk::CmdEndRenderPass(m_commandBuffer->handle());
        m_errorMonitor->VerifyFound();

        m_commandBuffer->end();

        vk::DestroyFramebuffer(device(), fb, nullptr);
        vk::DestroyRenderPass(device(), rp, nullptr);
    }

    // add dependencies from subpass 0 to the others, which are necessary but not sufficient
    std::vector<VkSubpassDependency> subpass_dependencies;
    for (uint32_t i = 1; i < subpasses.size(); i++) {
        VkSubpassDependency dep{0,
                                i,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
                                0};
        subpass_dependencies.push_back(dep);
    }
    renderpass_info.dependencyCount = subpass_dependencies.size();
    renderpass_info.pDependencies = subpass_dependencies.data();

    {
        VkRenderPass rp;
        VkFramebuffer fb;
        ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &renderpass_info, nullptr, &rp));

        fbci.renderPass = rp;
        ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb));

        CreatePipelineHelper g_pipe_0(*this);
        g_pipe_0.InitInfo();
        g_pipe_0.gp_ci_.renderPass = rp;
        g_pipe_0.InitState();
        ASSERT_VK_SUCCESS(g_pipe_0.CreateGraphicsPipeline());

        CreatePipelineHelper g_pipe_12(*this);
        g_pipe_12.InitInfo();
        g_pipe_12.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
        g_pipe_12.dsl_bindings_ = {{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        g_pipe_12.gp_ci_.renderPass = rp;
        g_pipe_12.InitState();
        ASSERT_VK_SUCCESS(g_pipe_12.CreateGraphicsPipeline());

        g_pipe_12.descriptor_set_->WriteDescriptorImageInfo(0, attachments[0], sampler, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
        g_pipe_12.descriptor_set_->UpdateDescriptorSets();

        m_commandBuffer->begin();

        vk::CmdPipelineBarrier(m_commandBuffer->handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, img_barriers.size(),
                               img_barriers.data());

        m_renderPassBeginInfo.renderArea = {{0, 0}, {16, 16}};
        m_renderPassBeginInfo.pClearValues = clear_values.data();
        m_renderPassBeginInfo.clearValueCount = clear_values.size();

        m_renderPassBeginInfo.renderArea = {{0, 0}, {kWidth, kHeight}};
        m_renderPassBeginInfo.renderPass = rp;
        m_renderPassBeginInfo.framebuffer = fb;

        vk::CmdBeginRenderPass(m_commandBuffer->handle(), &m_renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_0.pipeline_);
        vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_0.pipeline_layout_.handle(), 0,
                                  1, &g_pipe_0.descriptor_set_->set_, 0, NULL);

        vk::CmdDraw(m_commandBuffer->handle(), 3, 1, 0, 0);

        m_errorMonitor->ExpectSuccess();
        for (uint32_t i = 1; i < subpasses.size(); i++) {
            vk::CmdNextSubpass(m_commandBuffer->handle(), VK_SUBPASS_CONTENTS_INLINE);
            vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_12.pipeline_);
            vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_pipe_12.pipeline_layout_.handle(), 0, 1, &g_pipe_12.descriptor_set_->set_, 0, NULL);
            vk::CmdDraw(m_commandBuffer->handle(), 3, 1, 0, 0);
        }
        m_errorMonitor->VerifyNotFound();
        // expect this error because 2 subpasses could try to do the store operation
        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE-RACING-WRITE");
        // ... and this one because the store could happen during a shader read from another subpass
        m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE-RACING-READ");
        vk::CmdEndRenderPass(m_commandBuffer->handle());
        m_errorMonitor->VerifyFound();

        m_commandBuffer->end();

        m_errorMonitor->VerifyFound();
        vk::DestroyFramebuffer(device(), fb, nullptr);
        vk::DestroyRenderPass(device(), rp, nullptr);
    }

    // try again with correct dependencies to make subpass 3 depend on 1 & 2
    for (uint32_t i = 1; i < (subpasses.size() - 1); i++) {
        VkSubpassDependency dep{i,
                                static_cast<uint32_t>(subpasses.size() - 1),
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
                                0};
        subpass_dependencies.push_back(dep);
    }
    renderpass_info.dependencyCount = subpass_dependencies.size();
    renderpass_info.pDependencies = subpass_dependencies.data();
    {
        VkRenderPass rp;
        VkFramebuffer fb;
        ASSERT_VK_SUCCESS(vk::CreateRenderPass(device(), &renderpass_info, nullptr, &rp));

        fbci.renderPass = rp;
        ASSERT_VK_SUCCESS(vk::CreateFramebuffer(device(), &fbci, nullptr, &fb));

        CreatePipelineHelper g_pipe_0(*this);
        g_pipe_0.InitInfo();
        g_pipe_0.gp_ci_.renderPass = rp;
        g_pipe_0.InitState();
        ASSERT_VK_SUCCESS(g_pipe_0.CreateGraphicsPipeline());

        CreatePipelineHelper g_pipe_12(*this);
        g_pipe_12.InitInfo();
        g_pipe_12.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
        g_pipe_12.dsl_bindings_ = {{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        g_pipe_12.gp_ci_.renderPass = rp;
        g_pipe_12.InitState();
        ASSERT_VK_SUCCESS(g_pipe_12.CreateGraphicsPipeline());

        g_pipe_12.descriptor_set_->WriteDescriptorImageInfo(0, attachments[0], sampler, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT);
        g_pipe_12.descriptor_set_->UpdateDescriptorSets();

        m_errorMonitor->ExpectSuccess();
        m_commandBuffer->begin();
        vk::CmdPipelineBarrier(m_commandBuffer->handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, img_barriers.size(),
                               img_barriers.data());

        m_renderPassBeginInfo.renderArea = {{0, 0}, {16, 16}};
        m_renderPassBeginInfo.pClearValues = clear_values.data();
        m_renderPassBeginInfo.clearValueCount = clear_values.size();

        m_renderPassBeginInfo.renderArea = {{0, 0}, {kWidth, kHeight}};
        m_renderPassBeginInfo.renderPass = rp;
        m_renderPassBeginInfo.framebuffer = fb;

        vk::CmdBeginRenderPass(m_commandBuffer->handle(), &m_renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_0.pipeline_);
        vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_0.pipeline_layout_.handle(), 0,
                                  1, &g_pipe_0.descriptor_set_->set_, 0, NULL);

        vk::CmdDraw(m_commandBuffer->handle(), 3, 1, 0, 0);

        for (uint32_t i = 1; i < subpasses.size(); i++) {
            vk::CmdNextSubpass(m_commandBuffer->handle(), VK_SUBPASS_CONTENTS_INLINE);
            vk::CmdBindPipeline(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe_12.pipeline_);
            vk::CmdBindDescriptorSets(m_commandBuffer->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      g_pipe_12.pipeline_layout_.handle(), 0, 1, &g_pipe_12.descriptor_set_->set_, 0, NULL);
            vk::CmdDraw(m_commandBuffer->handle(), 3, 1, 0, 0);
        }

        vk::CmdEndRenderPass(m_commandBuffer->handle());

        m_commandBuffer->end();

        m_errorMonitor->VerifyNotFound();
        vk::DestroyFramebuffer(device(), fb, nullptr);
        vk::DestroyRenderPass(device(), rp, nullptr);
    }
}

TEST_F(VkSyncValTest, SyncEventsBufferCopy) {
    TEST_DESCRIPTION("Check Set/Wait protection for a variety of use cases using buffer copies");
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));

    VkBufferObj buffer_a;
    VkBufferObj buffer_b;
    VkBufferObj buffer_c;
    VkMemoryPropertyFlags mem_prop = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer_a.init_as_src_and_dst(*m_device, 256, mem_prop);
    buffer_b.init_as_src_and_dst(*m_device, 256, mem_prop);
    buffer_c.init_as_src_and_dst(*m_device, 256, mem_prop);

    VkBufferCopy region = {0, 0, 256};
    VkBufferCopy front2front = {0, 0, 128};
    VkBufferCopy front2back = {0, 128, 128};
    VkBufferCopy back2back = {128, 128, 128};

    VkEventObj event;
    event.init(*m_device, VkEventObj::create_info(0));
    VkEvent event_handle = event.handle();

    auto cb = m_commandBuffer->handle();
    m_commandBuffer->begin();

    // Copy after set for WAR (note we are writing to the back half of c but only reading from the front
    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &region);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_c.handle(), 1, &back2back);
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0,
                                nullptr, 0, nullptr);
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &front2front);
    m_errorMonitor->VerifyNotFound();
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &front2back);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();

    // WAR prevented
    m_commandBuffer->reset();
    m_commandBuffer->begin();
    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &region);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    // Just protect against WAR, only need a sync barrier.
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0,
                                nullptr, 0, nullptr);
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &region);
    m_errorMonitor->VerifyNotFound();

    // Wait shouldn't prevent this WAW though, as it's only a synchronization barrier
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_b.handle(), 1, &region);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();

    // Prevent WAR and WAW
    m_commandBuffer->reset();
    m_commandBuffer->begin();
    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &region);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    auto mem_barrier_waw = LvlInitStruct<VkMemoryBarrier>();
    mem_barrier_waw.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mem_barrier_waw.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 1,
                                &mem_barrier_waw, 0, nullptr, 0, nullptr);
    // The WAW should be safe (on a memory barrier)
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_b.handle(), 1, &region);
    // The WAR should also be safe (on a sync barrier)
    vk::CmdCopyBuffer(cb, buffer_c.handle(), buffer_a.handle(), 1, &region);
    m_errorMonitor->VerifyNotFound();
    m_commandBuffer->end();

    // Barrier range check for WAW
    auto buffer_barrier_front_waw = LvlInitStruct<VkBufferMemoryBarrier>();
    buffer_barrier_front_waw.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier_front_waw.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier_front_waw.buffer = buffer_b.handle();
    buffer_barrier_front_waw.offset = front2front.dstOffset;
    buffer_barrier_front_waw.size = front2front.size;

    // Front safe, back WAW
    m_commandBuffer->reset();
    m_commandBuffer->begin();
    m_errorMonitor->ExpectSuccess();
    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &region);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 1,
                                &buffer_barrier_front_waw, 0, nullptr);
    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &front2front);
    m_errorMonitor->VerifyNotFound();
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    vk::CmdCopyBuffer(cb, buffer_a.handle(), buffer_b.handle(), 1, &back2back);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();
}

TEST_F(VkSyncValTest, SyncEventsCopyImageHazards) {
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_a(m_device);
    auto image_ci = VkImageObj::ImageCreateInfo2D(128, 128, 1, 2, format, usage, VK_IMAGE_TILING_OPTIMAL);
    image_a.Init(image_ci);
    ASSERT_TRUE(image_a.initialized());

    VkImageObj image_b(m_device);
    image_b.Init(image_ci);
    ASSERT_TRUE(image_b.initialized());

    VkImageObj image_c(m_device);
    image_c.Init(image_ci);
    ASSERT_TRUE(image_c.initialized());

    VkEventObj event;
    event.init(*m_device, VkEventObj::create_info(0));
    VkEvent event_handle = event.handle();

    VkImageSubresourceLayers layers_all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
    VkImageSubresourceLayers layers_0{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    VkImageSubresourceLayers layers_1{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1};
    VkImageSubresourceRange layers_0_subresource_range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkOffset3D zero_offset{0, 0, 0};
    VkOffset3D half_offset{64, 64, 0};
    VkExtent3D full_extent{128, 128, 1};  // <-- image type is 2D
    VkExtent3D half_extent{64, 64, 1};    // <-- image type is 2D

    VkImageCopy full_region = {layers_all, zero_offset, layers_all, zero_offset, full_extent};
    VkImageCopy region_0_to_0 = {layers_0, zero_offset, layers_0, zero_offset, full_extent};
    VkImageCopy region_1_to_1 = {layers_1, zero_offset, layers_1, zero_offset, full_extent};
    VkImageCopy region_0_q0toq0 = {layers_0, zero_offset, layers_0, zero_offset, half_extent};
    VkImageCopy region_0_q0toq3 = {layers_0, zero_offset, layers_0, half_offset, half_extent};
    VkImageCopy region_0_q3toq3 = {layers_0, half_offset, layers_0, half_offset, half_extent};

    auto cb = m_commandBuffer->handle();
    auto copy_general = [cb](const VkImageObj &from, const VkImageObj &to, const VkImageCopy &region) {
        vk::CmdCopyImage(cb, from.handle(), VK_IMAGE_LAYOUT_GENERAL, to.handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    };

    auto set_layouts = [this, &image_a, &image_b, &image_c]() {
        image_c.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
        image_b.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
        image_a.SetLayout(m_commandBuffer, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    };

    // Scope check.  One access in, one access not
    m_commandBuffer->begin();
    set_layouts();
    m_errorMonitor->ExpectSuccess();
    copy_general(image_a, image_b, full_region);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_general(image_a, image_c, region_0_q3toq3);
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0,
                                nullptr, 0, nullptr);
    copy_general(image_c, image_a, region_0_q0toq0);
    m_errorMonitor->VerifyNotFound();
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_READ");
    copy_general(image_c, image_a, region_0_q0toq3);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();

    // WAR prevented
    m_commandBuffer->reset();
    m_commandBuffer->begin();
    set_layouts();
    m_errorMonitor->ExpectSuccess();
    copy_general(image_a, image_b, full_region);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    // Just protect against WAR, only need a sync barrier.
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0,
                                nullptr, 0, nullptr);
    copy_general(image_c, image_a, full_region);
    m_errorMonitor->VerifyNotFound();

    // Wait shouldn't prevent this WAW though, as it's only a synchronization barrier
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    copy_general(image_c, image_b, full_region);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();

    // Prevent WAR and WAW
    m_commandBuffer->reset();
    m_commandBuffer->begin();
    m_errorMonitor->ExpectSuccess();
    set_layouts();
    copy_general(image_a, image_b, full_region);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    auto mem_barrier_waw = LvlInitStruct<VkMemoryBarrier>();
    mem_barrier_waw.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mem_barrier_waw.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 1,
                                &mem_barrier_waw, 0, nullptr, 0, nullptr);
    // The WAW should be safe (on a memory barrier)
    copy_general(image_c, image_b, full_region);
    // The WAR should also be safe (on a sync barrier)
    copy_general(image_c, image_a, full_region);
    m_errorMonitor->VerifyNotFound();
    m_commandBuffer->end();

    // Barrier range check for WAW
    auto image_barrier_region0_waw = LvlInitStruct<VkImageMemoryBarrier>();
    image_barrier_region0_waw.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_region0_waw.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier_region0_waw.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier_region0_waw.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier_region0_waw.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier_region0_waw.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier_region0_waw.image = image_b.handle();
    image_barrier_region0_waw.subresourceRange = layers_0_subresource_range;

    // Region 0 safe, back WAW
    m_commandBuffer->reset();
    m_commandBuffer->begin();
    set_layouts();
    m_errorMonitor->ExpectSuccess();
    copy_general(image_a, image_b, full_region);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0,
                                nullptr, 1, &image_barrier_region0_waw);
    copy_general(image_a, image_b, region_0_to_0);
    m_errorMonitor->VerifyNotFound();
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-HAZARD-WRITE_AFTER_WRITE");
    copy_general(image_a, image_b, region_1_to_1);
    m_errorMonitor->VerifyFound();
    m_commandBuffer->end();
}

TEST_F(VkSyncValTest, SyncEventsCommandHazards) {
    TEST_DESCRIPTION("Check Set/Reset/Wait command hazard checking");
    ASSERT_NO_FATAL_FAILURE(InitSyncValFramework());
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));

    VkEventObj event;
    event.init(*m_device, VkEventObj::create_info(0));

    const VkEvent event_handle = event.handle();

    m_commandBuffer->begin();
    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->ResetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_errorMonitor->VerifyNotFound();

    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "VUID-vkCmdResetEvent-event-03834");
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "VUID-vkCmdWaitEvents-srcStageMask-01158");
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0,
                                nullptr, 0, nullptr);
    m_errorMonitor->VerifyFound();
    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->end();

    m_commandBuffer->begin();
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, nullptr,
                                0, nullptr, 0, nullptr);
    m_errorMonitor->VerifyNotFound();
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-vkCmdResetEvent-missingbarrier-wait");
    m_commandBuffer->ResetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_errorMonitor->VerifyFound();
    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->end();

    m_commandBuffer->begin();
    m_commandBuffer->ResetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_errorMonitor->VerifyNotFound();
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-vkCmdSetEvent-missingbarrier-reset");
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_errorMonitor->VerifyFound();

    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0U, 0, nullptr, 0,
                                     nullptr, 0, nullptr);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_commandBuffer->WaitEvents(1, &event_handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0,
                                nullptr, 0, nullptr);
    m_commandBuffer->ResetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_commandBuffer->PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0U, 0, nullptr, 0,
                                     nullptr, 0, nullptr);
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_errorMonitor->VerifyNotFound();

    // Need a barrier between set and a reset
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-vkCmdResetEvent-missingbarrier-set");
    m_commandBuffer->ResetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_errorMonitor->VerifyFound();
    m_errorMonitor->ExpectSuccess();
    m_commandBuffer->end();

    m_commandBuffer->begin();
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_errorMonitor->VerifyNotFound();
    m_errorMonitor->SetDesiredFailureMsg(VK_DEBUG_REPORT_ERROR_BIT_EXT, "SYNC-vkCmdSetEvent-missingbarrier-set");
    m_commandBuffer->SetEvent(event, VK_PIPELINE_STAGE_TRANSFER_BIT);
    m_errorMonitor->VerifyFound();

    m_commandBuffer->end();
}
