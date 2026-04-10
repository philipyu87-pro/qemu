/*
 * VFIO container
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_CONTAINER_LEGACY_H
#define HW_VFIO_CONTAINER_LEGACY_H

#include "hw/vfio/vfio-container.h"
#include "hw/vfio/vfio-cpr.h"

OBJECT_DECLARE_SIMPLE_TYPE(VFIOLegacyContainer, VFIO_IOMMU_LEGACY);

typedef struct VFIODevice VFIODevice;

typedef struct VFIOGroup {
    int fd;
    int groupid;
    VFIOLegacyContainer *container;
    QLIST_HEAD(, VFIODevice) device_list;
    QLIST_ENTRY(VFIOGroup) next;
    QLIST_ENTRY(VFIOGroup) container_next;
    bool ram_block_discard_allowed;
    int pre_dma_map_count; /* reference count from pre-DMA-map objects */
} VFIOGroup;

struct VFIOLegacyContainer {
    VFIOContainer parent_obj;

    int fd; /* /dev/vfio/vfio, empowered by the attached groups */
    unsigned iommu_type;
    bool unmap_all_supported;
    QLIST_HEAD(, VFIOGroup) group_list;
    VFIOContainerCPR cpr;
};

/*
 * Pre-DMA-map support: create VFIO group/container and register memory
 * listener early (at VM startup), so DMA maps are already in place when
 * a VFIO device is later hot-plugged.
 */
VFIOGroup *vfio_legacy_group_get(int groupid, AddressSpace *as, Error **errp);
void vfio_legacy_group_put(VFIOGroup *group);

#endif /* HW_VFIO_CONTAINER_LEGACY_H */
