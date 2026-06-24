.. _arm-storage-performance-zh:

Arm 平台存储性能调优
====================

本文档提供在 Arm 平台上运行 QEMU 时优化存储性能的指南，特别是在使用
Ceph/RBD 等分布式存储系统时。

概述
----

虚拟化环境中的存储性能取决于多个因素，包括主机内核版本、QEMU 配置、
存储后端设置和硬件能力。在华为鲲鹏等 Arm 平台上，可能需要进行特定的
优化才能获得最佳性能。

内核升级对性能的影响
--------------------

当升级 Linux 内核（例如从 4.19 升级到 5.10 或更高版本）时，您可能会
观察到存储性能的变化。这通常是由于以下原因：

1. **I/O 调度器变更**：较新的内核可能使用不同的默认 I/O 调度器。
   对于虚拟化存储工作负载，通常推荐使用 ``mq-deadline`` 或 ``none`` 调度器。

2. **IOMMU/SMMU 配置**：ARM SMMU（系统内存管理单元）在不同内核版本之间
   的行为变化可能会影响 DMA 性能。

3. **内存管理**：内存分配、大页处理和 NUMA 策略的变化可能会影响 I/O 性能。

4. **virtio 优化**：较新的内核可能具有不同的 virtio-blk 和 virtio-scsi
   实现，具有不同的性能特征。

存储性能内核参数
----------------

以下内核参数可以帮助优化 Arm 平台上的存储性能：

I/O 调度器配置
^^^^^^^^^^^^^^

对于 NVMe 或高性能存储后端::

   # 将 I/O 调度器设置为 none（绕过调度以获得快速存储）
   echo none > /sys/block/<device>/queue/scheduler

对于使用旋转存储的 Ceph/RBD 工作负载::

   # 使用 mq-deadline 以获得更好的延迟
   echo mq-deadline > /sys/block/<device>/queue/scheduler

IOMMU/SMMU 设置
^^^^^^^^^^^^^^^

在鲲鹏和其他带有 SMMU 的 Arm 平台上，考虑以下启动参数：

.. warning::
   以下参数会降低设备与主机系统之间的安全隔离。只有在确实需要性能优势
   且了解安全影响时才应使用这些参数。在多租户或安全敏感的环境中，
   这些设置可能不适用。

``iommu.passthrough=1``
   为所有设备绕过 IOMMU（提高性能但降低隔离性）。
   在生产环境中谨慎使用。

``arm-smmu.disable_bypass=0``
   允许不需要地址转换的设备绕过 SMMU。

``iommu.strict=0``
   使用非严格模式进行延迟 TLB 失效，这可以提高 I/O 性能，
   但代价是略微延迟的内存回收。

内存配置
^^^^^^^^

``transparent_hugepage=always``
   启用透明大页以减少 TLB 缺失。

``numa_balancing=0``
   如果工作负载已经是 NUMA 感知的，则禁用自动 NUMA 平衡。

Ceph/RBD 性能优化
-----------------

在 Arm 平台上使用 Ceph RBD 作为 QEMU 的存储后端时，
请考虑以下优化：

QEMU RBD 缓存设置
^^^^^^^^^^^^^^^^^

启用 RBD 缓存以提高读取性能::

   -drive file=rbd:pool/image,cache=writeback

或通过 Ceph 配置选项配置 RBD 特定的缓存::

   rbd:pool/image:rbd_cache=true:rbd_cache_size=67108864

Ceph 客户端配置
^^^^^^^^^^^^^^^

在 ``/etc/ceph/ceph.conf`` 中添加客户端优化::

   [client]
   rbd_cache = true
   rbd_cache_size = 67108864
   rbd_cache_max_dirty = 50331648
   rbd_cache_target_dirty = 33554432
   rbd_cache_max_dirty_age = 2
   rbd_cache_writethrough_until_flush = true

   # 对于具有异步 I/O 改进的较新内核
   rbd_concurrent_management_ops = 20

   # 针对 ARM 内存架构进行优化
   ms_async_op_threads = 4
   ms_async_max_op_threads = 8

Ceph 网络调优
^^^^^^^^^^^^^

确保 Ceph 流量的最佳网络设置::

   # 增加套接字缓冲区大小
   sysctl -w net.core.rmem_max=67108864
   sysctl -w net.core.wmem_max=67108864
   sysctl -w net.core.rmem_default=33554432
   sysctl -w net.core.wmem_default=33554432

鲲鹏专用优化
------------

华为鲲鹏 920 及类似处理器具有可用于提高存储性能的特定功能：

UADK 加速
^^^^^^^^^

鲲鹏处理器通过用户空间加速器开发套件（UADK）支持硬件加速。
有关迁移或存储操作期间的压缩，请参阅 :doc:`/devel/migration/uadk-compression`。

CPU 亲和性
^^^^^^^^^^

对于 I/O 密集型工作负载，考虑将 QEMU I/O 线程固定到特定的 CPU 核心::

   -object iothread,id=iothread0
   -device virtio-blk-pci,drive=drive0,iothread=iothread0

内存预分配
^^^^^^^^^^

在鲲鹏平台上，预分配客户机内存可以减少 I/O 操作期间的缺页开销::

   -mem-prealloc

当使用依赖共享虚拟地址（SVA）的硬件加速功能时，这一点尤为重要。

NUMA 配置
^^^^^^^^^

对于多插槽鲲鹏系统，将 QEMU 内存和 I/O 线程与 NUMA 拓扑对齐::

   -object memory-backend-ram,size=8G,id=ram0,host-nodes=0,policy=bind
   -numa node,nodeid=0,memdev=ram0

性能下降故障排除
----------------

如果在内核升级后遇到性能下降：

1. **比较 I/O 调度器**：检查升级前后的活动 I/O 调度器::

      cat /sys/block/*/queue/scheduler

2. **监控 SMMU 开销**：使用 perf 检查与 SMMU 相关的开销::

      perf stat -e arm_smmuv3/cycles/,arm_smmuv3/transaction/ -a sleep 10

3. **检查 IOMMU 故障**：监控 dmesg 中与 IOMMU 相关的错误::

      dmesg | grep -i smmu

4. **验证大页分配**：确保正确分配了大页::

      cat /proc/meminfo | grep -i huge

5. **分析存储延迟**：使用 ``fio`` 在更改前后使用一致的参数
   对存储性能进行基准测试。

推荐的测试方法
--------------

评估存储性能时：

1. 使用一致的测试参数（块大小、队列深度、I/O 模式）
2. 同时测试直接 I/O（``cache=none``）和缓存 I/O
3. 测量吞吐量（MB/s）和延迟（IOPS、响应时间）
4. 使用符合您用例的真实工作负载模式进行测试
5. 考虑其他系统组件（网络、CPU、内存）的影响

Ceph/RBD 的 ``fio`` 测试示例::

   fio --name=randread --ioengine=rbd --pool=rbd --rbdname=testimage \
       --rw=randread --bs=4k --iodepth=32 --numjobs=4 --runtime=60 \
       --group_reporting

另请参阅
--------

- :doc:`virt` - ARM virt 机器文档
- :doc:`cpu-features` - ARM CPU 功能配置
- :doc:`/devel/migration/uadk-compression` - 鲲鹏 UADK 加速
- :doc:`storage-performance` - 英文版本
