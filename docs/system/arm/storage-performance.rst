.. _arm-storage-performance:

Storage Performance Tuning on Arm
=================================

This document provides guidance for optimizing storage performance when
running QEMU on Arm platforms, particularly when using distributed storage
systems like Ceph/RBD.

Overview
--------

Storage performance in virtualized environments depends on multiple factors
including the host kernel version, QEMU configuration, storage backend
settings, and hardware capabilities. On Arm platforms such as HiSilicon
Kunpeng, specific optimizations may be necessary to achieve optimal
performance.

Performance Considerations for Kernel Upgrades
----------------------------------------------

When upgrading the Linux kernel (e.g., from 4.19 to 5.10 or later), you may
observe storage performance changes. This is often due to:

1. **I/O Scheduler Changes**: Newer kernels may use different default I/O
   schedulers. The ``mq-deadline`` or ``none`` schedulers are often preferred
   for virtualized storage workloads.

2. **IOMMU/SMMU Configuration**: ARM SMMU (System Memory Management Unit)
   behavior changes between kernel versions can affect DMA performance.

3. **Memory Management**: Changes in memory allocation, huge pages handling,
   and NUMA policies can impact I/O performance.

4. **virtio Optimizations**: Newer kernels may have different virtio-blk
   and virtio-scsi implementations with varying performance characteristics.

Kernel Parameters for Storage Performance
-----------------------------------------

The following kernel parameters can help optimize storage performance on
Arm platforms:

I/O Scheduler Configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^

For NVMe or high-performance storage backends::

   # Set I/O scheduler to none (bypass scheduling for fast storage)
   echo none > /sys/block/<device>/queue/scheduler

For Ceph/RBD workloads with rotational storage::

   # Use mq-deadline for better latency
   echo mq-deadline > /sys/block/<device>/queue/scheduler

IOMMU/SMMU Settings
^^^^^^^^^^^^^^^^^^^

On Kunpeng and other Arm platforms with SMMU, consider these boot parameters:

.. warning::
   The following parameters reduce security isolation between devices
   and the host system. They should only be used when the performance
   benefits are necessary and the security implications are understood.
   In multi-tenant or security-sensitive environments, these settings
   may not be appropriate.

``iommu.passthrough=1``
   Bypass IOMMU for all devices (improves performance but reduces isolation).
   Use with caution in production environments.

``arm-smmu.disable_bypass=0``
   Allow SMMU bypass for devices that don't require address translation.

``iommu.strict=0``
   Use non-strict mode for deferred TLB invalidation, which can improve
   I/O performance at the cost of slightly delayed memory reclamation.

Memory Configuration
^^^^^^^^^^^^^^^^^^^^

``transparent_hugepage=always``
   Enable transparent huge pages to reduce TLB misses.

``numa_balancing=0``
   Disable automatic NUMA balancing if the workload is already NUMA-aware.

Ceph/RBD Performance Optimization
---------------------------------

When using Ceph RBD as a storage backend with QEMU on Arm platforms,
consider the following optimizations:

QEMU RBD Cache Settings
^^^^^^^^^^^^^^^^^^^^^^^

Enable RBD caching for improved read performance::

   -drive file=rbd:pool/image,cache=writeback

Or configure RBD-specific caching via Ceph configuration options::

   rbd:pool/image:rbd_cache=true:rbd_cache_size=67108864

Ceph Client Configuration
^^^^^^^^^^^^^^^^^^^^^^^^^

In ``/etc/ceph/ceph.conf``, add client-side optimizations::

   [client]
   rbd_cache = true
   rbd_cache_size = 67108864
   rbd_cache_max_dirty = 50331648
   rbd_cache_target_dirty = 33554432
   rbd_cache_max_dirty_age = 2
   rbd_cache_writethrough_until_flush = true

   # For newer kernels with async I/O improvements
   rbd_concurrent_management_ops = 20

   # Optimize for ARM memory architecture
   ms_async_op_threads = 4
   ms_async_max_op_threads = 8

Network Tuning for Ceph
^^^^^^^^^^^^^^^^^^^^^^^

Ensure optimal network settings for Ceph traffic::

   # Increase socket buffer sizes
   sysctl -w net.core.rmem_max=67108864
   sysctl -w net.core.wmem_max=67108864
   sysctl -w net.core.rmem_default=33554432
   sysctl -w net.core.wmem_default=33554432

Kunpeng-Specific Optimizations
------------------------------

HiSilicon Kunpeng 920 and similar processors have specific features that
can be leveraged for improved storage performance:

UADK Acceleration
^^^^^^^^^^^^^^^^^

Kunpeng processors support hardware acceleration through the User Space
Accelerator Development Kit (UADK). For compression during migration or
storage operations, see :doc:`/devel/migration/uadk-compression`.

CPU Affinity
^^^^^^^^^^^^

For I/O intensive workloads, consider pinning QEMU I/O threads to specific
CPU cores::

   -object iothread,id=iothread0
   -device virtio-blk-pci,drive=drive0,iothread=iothread0

Memory Preallocation
^^^^^^^^^^^^^^^^^^^^

On Kunpeng platforms, preallocating guest memory can reduce page fault
overhead during I/O operations::

   -mem-prealloc

This is particularly important when using hardware acceleration features
that rely on Shared Virtual Addressing (SVA).

NUMA Configuration
^^^^^^^^^^^^^^^^^^

For multi-socket Kunpeng systems, align QEMU memory and I/O threads with
NUMA topology::

   -object memory-backend-ram,size=8G,id=ram0,host-nodes=0,policy=bind
   -numa node,nodeid=0,memdev=ram0

Troubleshooting Performance Degradation
---------------------------------------

If you experience performance degradation after a kernel upgrade:

1. **Compare I/O schedulers**: Check the active I/O scheduler before and
   after the upgrade::

      cat /sys/block/*/queue/scheduler

2. **Monitor SMMU overhead**: Use perf to check for SMMU-related overhead::

      perf stat -e arm_smmuv3/cycles/,arm_smmuv3/transaction/ -a sleep 10

3. **Check for IOMMU faults**: Monitor dmesg for IOMMU-related errors::

      dmesg | grep -i smmu

4. **Verify huge page allocation**: Ensure huge pages are properly
   allocated::

      cat /proc/meminfo | grep -i huge

5. **Profile storage latency**: Use ``fio`` to benchmark storage
   performance with consistent parameters before and after changes.

Recommended Test Methodology
----------------------------

When evaluating storage performance:

1. Use consistent test parameters (block size, queue depth, I/O pattern)
2. Test with both direct I/O (``cache=none``) and cached I/O
3. Measure both throughput (MB/s) and latency (IOPS, response time)
4. Test with realistic workload patterns for your use case
5. Consider the impact of other system components (network, CPU, memory)

Example ``fio`` test for Ceph/RBD::

   fio --name=randread --ioengine=rbd --pool=rbd --rbdname=testimage \
       --rw=randread --bs=4k --iodepth=32 --numjobs=4 --runtime=60 \
       --group_reporting

See Also
--------

- :doc:`virt` - ARM virt machine documentation
- :doc:`cpu-features` - ARM CPU feature configuration
- :doc:`/devel/migration/uadk-compression` - UADK acceleration for Kunpeng
