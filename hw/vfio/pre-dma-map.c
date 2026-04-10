/*
 * VFIO pre-DMA-map: create VFIO container and register memory listener
 * at VM startup, so DMA maps are already in place when a VFIO device is
 * later hot-plugged.
 *
 * Usage (legacy VFIO path):
 *   -object vfio-pre-dma-map,id=predma0,groupid=N
 *
 * Usage (iommufd path):
 *   -object iommufd,id=iommufd0
 *   -object vfio-pre-dma-map,id=predma0,iommufd=iommufd0
 *
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include CONFIG_DEVICES /* CONFIG_IOMMUFD */
#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "hw/vfio/vfio-container-legacy.h"
#ifdef CONFIG_IOMMUFD
#include "system/iommufd.h"
#include "vfio-iommufd.h"
#endif

#define TYPE_VFIO_PRE_DMA_MAP "vfio-pre-dma-map"

typedef struct VFIOPreDmaMap VFIOPreDmaMap;
DECLARE_INSTANCE_CHECKER(VFIOPreDmaMap, VFIO_PRE_DMA_MAP, TYPE_VFIO_PRE_DMA_MAP)

struct VFIOPreDmaMap {
    Object parent_obj;

    /* Legacy VFIO path */
    int32_t groupid;
    VFIOGroup *group;

#ifdef CONFIG_IOMMUFD
    /* iommufd path */
    IOMMUFDBackend *iommufd;
    VFIOIOMMUFDContainer *container;
#endif
};

static void vfio_pre_dma_map_complete(UserCreatable *uc, Error **errp)
{
    VFIOPreDmaMap *predma = VFIO_PRE_DMA_MAP(uc);

#ifdef CONFIG_IOMMUFD
    if (predma->iommufd) {
        if (predma->groupid >= 0) {
            error_setg(errp,
                       "Cannot specify both 'groupid' and 'iommufd'");
            return;
        }

        if (!iommufd_backend_connect(predma->iommufd, errp)) {
            return;
        }

        predma->container = vfio_iommufd_pre_dma_map(predma->iommufd,
                                                      &address_space_memory,
                                                      errp);
        if (!predma->container) {
            iommufd_backend_disconnect(predma->iommufd);
            return;
        }

        /* Hold a pre-DMA-map reference to prevent container destruction */
        VFIOContainer *bcontainer = VFIO_IOMMU(predma->container);
        bcontainer->pre_dma_map_count++;
        return;
    }
#endif

    if (predma->groupid < 0) {
        error_setg(errp,
                   "'groupid' (or 'iommufd') property is required");
        return;
    }

    predma->group = vfio_legacy_group_get(predma->groupid,
                                          &address_space_memory, errp);
    if (!predma->group) {
        return;
    }

    /* Hold a pre-DMA-map reference to prevent group/container destruction */
    predma->group->pre_dma_map_count++;
}

static bool vfio_pre_dma_map_can_be_deleted(UserCreatable *uc)
{
    return true;
}

static void vfio_pre_dma_map_init(Object *obj)
{
    VFIOPreDmaMap *predma = VFIO_PRE_DMA_MAP(obj);

    predma->groupid = -1;
    predma->group = NULL;
#ifdef CONFIG_IOMMUFD
    predma->iommufd = NULL;
    predma->container = NULL;
#endif
}

static void vfio_pre_dma_map_finalize(Object *obj)
{
    VFIOPreDmaMap *predma = VFIO_PRE_DMA_MAP(obj);

#ifdef CONFIG_IOMMUFD
    if (predma->container) {
        VFIOContainer *bcontainer = VFIO_IOMMU(predma->container);
        bcontainer->pre_dma_map_count--;
        vfio_iommufd_pre_dma_map_destroy(predma->container);
        iommufd_backend_disconnect(predma->iommufd);
        predma->container = NULL;
    }
#endif

    if (predma->group) {
        predma->group->pre_dma_map_count--;
        vfio_legacy_group_put(predma->group);
        predma->group = NULL;
    }
}

static void vfio_pre_dma_map_set_groupid(Object *obj, Visitor *v,
                                          const char *name, void *opaque,
                                          Error **errp)
{
    VFIOPreDmaMap *predma = VFIO_PRE_DMA_MAP(obj);
    int32_t value;

    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    if (value < 0) {
        error_setg(errp, "Property '%s' must be non-negative", name);
        return;
    }

    predma->groupid = value;
}

static void vfio_pre_dma_map_get_groupid(Object *obj, Visitor *v,
                                          const char *name, void *opaque,
                                          Error **errp)
{
    VFIOPreDmaMap *predma = VFIO_PRE_DMA_MAP(obj);

    visit_type_int32(v, name, &predma->groupid, errp);
}

static void vfio_pre_dma_map_class_init(ObjectClass *oc, const void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = vfio_pre_dma_map_complete;
    ucc->can_be_deleted = vfio_pre_dma_map_can_be_deleted;

    object_class_property_add(oc, "groupid", "int32",
                              vfio_pre_dma_map_get_groupid,
                              vfio_pre_dma_map_set_groupid,
                              NULL, NULL);
    object_class_property_set_description(oc, "groupid",
        "VFIO group ID for legacy VFIO path pre-DMA-map");

#ifdef CONFIG_IOMMUFD
    object_class_property_add_link(oc, "iommufd", TYPE_IOMMUFD_BACKEND,
                                   offsetof(VFIOPreDmaMap, iommufd),
                                   object_property_allow_set_link, 0);
    object_class_property_set_description(oc, "iommufd",
        "iommufd backend for iommufd path pre-DMA-map");
#endif
}

static const TypeInfo vfio_pre_dma_map_info = {
    .name = TYPE_VFIO_PRE_DMA_MAP,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(VFIOPreDmaMap),
    .instance_init = vfio_pre_dma_map_init,
    .instance_finalize = vfio_pre_dma_map_finalize,
    .class_init = vfio_pre_dma_map_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    },
};

static void vfio_pre_dma_map_register_types(void)
{
    type_register_static(&vfio_pre_dma_map_info);
}

type_init(vfio_pre_dma_map_register_types);
