#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <modesetting.h>

static int drmdev_lock(struct drmdev *drmdev) {
    return pthread_mutex_lock(&drmdev->mutex);
}

static int drmdev_unlock(struct drmdev *drmdev) {
    return pthread_mutex_unlock(&drmdev->mutex);
}

static int fetch_connectors(struct drmdev *drmdev, struct drm_connector **connectors_out, size_t *n_connectors_out) {
    struct drm_connector *connectors;
    int n_allocated_connectors;
    int ok;

    connectors = calloc(drmdev->res->count_connectors, sizeof *connectors);
    if (connectors == NULL) {
        *connectors_out = NULL;
        return ENOMEM;
    }

    n_allocated_connectors = 0;
    for (int i = 0; i < drmdev->res->count_connectors; i++, n_allocated_connectors++) {
        drmModeObjectProperties *props;
        drmModePropertyRes **props_info;
        drmModeConnector *connector;

        connector = drmModeGetConnector(drmdev->fd, drmdev->res->connectors[i]);
        if (connector == NULL) {
            ok = errno;
            perror("[modesetting] Could not get DRM device connector. drmModeGetConnector");
            goto fail_free_connectors;
        }

        props = drmModeObjectGetProperties(drmdev->fd, drmdev->res->connectors[i], DRM_MODE_OBJECT_CONNECTOR);
        if (props == NULL) {
            ok = errno;
            perror("[modesetting] Could not get DRM device connectors properties. drmModeObjectGetProperties");
            drmModeFreeConnector(connector);
            goto fail_free_connectors;
        }

        props_info = calloc(props->count_props, sizeof *props_info);
        if (props_info == NULL) {
            ok = ENOMEM;
            drmModeFreeObjectProperties(props);
            drmModeFreeConnector(connector);
            goto fail_free_connectors;
        }

        for (int j = 0; j < props->count_props; j++) {
            props_info[j] = drmModeGetProperty(drmdev->fd, props->props[j]);
            if (props_info[j] == NULL) {
                ok = errno;
                perror("[modesetting] Could not get DRM device connector properties' info. drmModeGetProperty");
                for (int k = 0; k < (j-1); k++)
                    drmModeFreeProperty(props_info[j]);
                free(props_info);
                drmModeFreeObjectProperties(props);
                drmModeFreeConnector(connector);
                goto fail_free_connectors;
            }
        }

        connectors[i].connector = connector;
        connectors[i].props = props;
        connectors[i].props_info = props_info;
    }

    *connectors_out = connectors;
    *n_connectors_out = drmdev->res->count_connectors;

    return 0;

    fail_free_connectors:
    for (int i = 0; i < n_allocated_connectors; i++) {
        for (int j = 0; j < connectors[i].props->count_props; j++)
            drmModeFreeProperty(connectors[i].props_info[j]);
        free(connectors[i].props_info);
        drmModeFreeObjectProperties(connectors[i].props);
        drmModeFreeConnector(connectors[i].connector);
    }

    fail_free_result:
    free(connectors);

    *connectors_out = NULL;
    *n_connectors_out = 0;
    return ok;
}

static int free_connectors(struct drm_connector *connectors, size_t n_connectors) {
    for (int i = 0; i < n_connectors; i++) {
        for (int j = 0; j < connectors[i].props->count_props; j++)
            drmModeFreeProperty(connectors[i].props_info[j]);
        free(connectors[i].props_info);
        drmModeFreeObjectProperties(connectors[i].props);
        drmModeFreeConnector(connectors[i].connector);
    }

    free(connectors);

    return 0;
}

static int fetch_encoders(struct drmdev *drmdev, struct drm_encoder **encoders_out, size_t *n_encoders_out) {
    struct drm_encoder *encoders;
    int n_allocated_encoders;
    int ok;

    encoders = calloc(drmdev->res->count_encoders, sizeof *encoders);
    if (encoders == NULL) {
        *encoders_out = NULL;
        *n_encoders_out = 0;
        return ENOMEM;
    }

    n_allocated_encoders = 0;
    for (int i = 0; i < drmdev->res->count_encoders; i++, n_allocated_encoders++) {
        drmModeEncoder *encoder;

        encoder = drmModeGetEncoder(drmdev->fd, drmdev->res->encoders[i]);
        if (encoder == NULL) {
            ok = errno;
            perror("[modesetting] Could not get DRM device encoder. drmModeGetEncoder");
            goto fail_free_encoders;
        }

        encoders[i].encoder = encoder;
    }

    *encoders_out = encoders;
    *n_encoders_out = drmdev->res->count_encoders;

    return 0;

    fail_free_encoders:
    for (int i = 0; i < n_allocated_encoders; i++) {
        drmModeFreeEncoder(encoders[i].encoder);
    }

    fail_free_result:
    free(encoders);

    *encoders_out = NULL;
    *n_encoders_out = 0;
    return ok;
}

static int free_encoders(struct drm_encoder *encoders, size_t n_encoders) {
    for (int i = 0; i < n_encoders; i++) {
        drmModeFreeEncoder(encoders[i].encoder);
    }

    free(encoders);

    return 0;
}

static int fetch_crtcs(struct drmdev *drmdev, struct drm_crtc **crtcs_out, size_t *n_crtcs_out) {
    struct drm_crtc *crtcs;
    int n_allocated_crtcs;
    int ok;

    crtcs = calloc(drmdev->res->count_crtcs, sizeof *crtcs);
    if (crtcs == NULL) {
        *crtcs_out = NULL;
        return ENOMEM;
    }

    n_allocated_crtcs = 0;
    for (int i = 0; i < drmdev->res->count_crtcs; i++, n_allocated_crtcs++) {
        drmModeObjectProperties *props;
        drmModePropertyRes **props_info;
        drmModeCrtc *crtc;

        crtc = drmModeGetCrtc(drmdev->fd, drmdev->res->crtcs[i]);
        if (crtc == NULL) {
            ok = errno;
            perror("[modesetting] Could not get DRM device CRTC. drmModeGetCrtc");
            goto fail_free_crtcs;
        }

        props = drmModeObjectGetProperties(drmdev->fd, drmdev->res->crtcs[i], DRM_MODE_OBJECT_CRTC);
        if (props == NULL) {
            ok = errno;
            perror("[modesetting] Could not get DRM device CRTCs properties. drmModeObjectGetProperties");
            drmModeFreeCrtc(crtc);
            goto fail_free_crtcs;
        }

        props_info = calloc(props->count_props, sizeof *props_info);
        if (props_info == NULL) {
            ok = ENOMEM;
            drmModeFreeObjectProperties(props);
            drmModeFreeCrtc(crtc);
            goto fail_free_crtcs;
        }

        for (int j = 0; j < props->count_props; j++) {
            props_info[j] = drmModeGetProperty(drmdev->fd, props->props[j]);
            if (props_info[j] == NULL) {
                ok = errno;
                perror("[modesetting] Could not get DRM device CRTCs properties' info. drmModeGetProperty");
                for (int k = 0; k < (j-1); k++)
                    drmModeFreeProperty(props_info[j]);
                free(props_info);
                drmModeFreeObjectProperties(props);
                drmModeFreeCrtc(crtc);
                goto fail_free_crtcs;
            }
        }

        crtcs[i].crtc = crtc;
        crtcs[i].props = props;
        crtcs[i].props_info = props_info;
    }

    *crtcs_out = crtcs;
    *n_crtcs_out = drmdev->res->count_crtcs;

    return 0;


    fail_free_crtcs:
    for (int i = 0; i < n_allocated_crtcs; i++) {
        for (int j = 0; j < crtcs[i].props->count_props; j++)
            drmModeFreeProperty(crtcs[i].props_info[j]);
        free(crtcs[i].props_info);
        drmModeFreeObjectProperties(crtcs[i].props);
        drmModeFreeCrtc(crtcs[i].crtc);
    }

    fail_free_result:
    free(crtcs);

    *crtcs_out = NULL;
    *n_crtcs_out = 0;
    return ok;
}

static int free_crtcs(struct drm_crtc *crtcs, size_t n_crtcs) {
    for (int i = 0; i < n_crtcs; i++) {
        for (int j = 0; j < crtcs[i].props->count_props; j++)
            drmModeFreeProperty(crtcs[i].props_info[j]);
        free(crtcs[i].props_info);
        drmModeFreeObjectProperties(crtcs[i].props);
        drmModeFreeCrtc(crtcs[i].crtc);
    }

    free(crtcs);

    return 0;
}

static int fetch_planes(struct drmdev *drmdev, struct drm_plane **planes_out, size_t *n_planes_out) {
    struct drm_plane *planes;
    int n_allocated_planes;
    int ok;

    planes = calloc(drmdev->plane_res->count_planes, sizeof *planes);
    if (planes == NULL) {
        *planes_out = NULL;
        return ENOMEM;
    }

    n_allocated_planes = 0;
    for (int i = 0; i < drmdev->plane_res->count_planes; i++, n_allocated_planes++) {
        drmModeObjectProperties *props;
        drmModePropertyRes **props_info;
        drmModePlane *plane;

        plane = drmModeGetPlane(drmdev->fd, drmdev->plane_res->planes[i]);
        if (plane == NULL) {
            ok = errno;
            perror("[modesetting] Could not get DRM device plane. drmModeGetPlane");
            goto fail_free_planes;
        }

        props = drmModeObjectGetProperties(drmdev->fd, drmdev->plane_res->planes[i], DRM_MODE_OBJECT_PLANE);
        if (props == NULL) {
            ok = errno;
            perror("[modesetting] Could not get DRM device planes' properties. drmModeObjectGetProperties");
            drmModeFreePlane(plane);
            goto fail_free_planes;
        }

        props_info = calloc(props->count_props, sizeof *props_info);
        if (props_info == NULL) {
            ok = ENOMEM;
            drmModeFreeObjectProperties(props);
            drmModeFreePlane(plane);
            goto fail_free_planes;
        }

        for (int j = 0; j < props->count_props; j++) {
            props_info[j] = drmModeGetProperty(drmdev->fd, props->props[j]);
            if (props_info[j] == NULL) {
                ok = errno;
                perror("[modesetting] Could not get DRM device planes' properties' info. drmModeGetProperty");
                for (int k = 0; k < (j-1); k++)
                    drmModeFreeProperty(props_info[j]);
                free(props_info);
                drmModeFreeObjectProperties(props);
                drmModeFreePlane(plane);
                goto fail_free_planes;
            }
        }

        planes[i].plane = plane;
        planes[i].props = props;
        planes[i].props_info = props_info;
    }

    *planes_out = planes;
    *n_planes_out = drmdev->plane_res->count_planes;

    return 0;


    fail_free_planes:
    for (int i = 0; i < n_allocated_planes; i++) {
        for (int j = 0; j < planes[i].props->count_props; j++)
            drmModeFreeProperty(planes[i].props_info[j]);
        free(planes[i].props_info);
        drmModeFreeObjectProperties(planes[i].props);
        drmModeFreePlane(planes[i].plane);
    }

    fail_free_result:
    free(planes);

    *planes_out = NULL;
    *n_planes_out = 0;
    return ok;
}

static int free_planes(struct drm_plane *planes, size_t n_planes) {
    for (int i = 0; i < n_planes; i++) {
        for (int j = 0; j < planes[i].props->count_props; j++)
            drmModeFreeProperty(planes[i].props_info[j]);
        free(planes[i].props_info);
        drmModeFreeObjectProperties(planes[i].props);
        drmModeFreePlane(planes[i].plane);
    }

    free(planes);

    return 0;
}


int drmdev_new_from_fd(
    struct drmdev **drmdev_out,
    int fd
) {
    struct drmdev *drmdev;
    int ok;

    drmdev = calloc(1, sizeof *drmdev);
    if (drmdev == NULL) {
        return ENOMEM;
    }

    drmdev->fd = fd;

    ok = drmSetClientCap(drmdev->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (ok < 0) {
        ok = errno;
        perror("[modesetting] Could not set DRM client universal planes capable. drmSetClientCap");
        goto fail_free_drmdev;
    }
    
    ok = drmSetClientCap(drmdev->fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ok < 0) {
        ok = errno;
        perror("[modesetting] Could not set DRM client atomic capable. drmSetClientCap");
        goto fail_free_drmdev;
    }

    drmdev->res = drmModeGetResources(drmdev->fd);
    if (drmdev->res == NULL) {
        ok = errno;
        perror("[modesetting] Could not get DRM device resources. drmModeGetResources");
        goto fail_free_drmdev;
    }

    drmdev->plane_res = drmModeGetPlaneResources(drmdev->fd);
    if (drmdev->plane_res == NULL) {
        ok = errno;
        perror("[modesetting] Could not get DRM device planes resources. drmModeGetPlaneResources");
        goto fail_free_resources;
    }

    ok = fetch_connectors(drmdev, &drmdev->connectors, &drmdev->n_connectors);
    if (ok != 0) {
        goto fail_free_plane_resources;
    }

    ok = fetch_encoders(drmdev, &drmdev->encoders, &drmdev->n_encoders);
    if (ok != 0) {
        goto fail_free_connectors;
    }

    ok = fetch_crtcs(drmdev, &drmdev->crtcs, &drmdev->n_crtcs);
    if (ok != 0) {
        goto fail_free_encoders;
    }

    ok = fetch_planes(drmdev, &drmdev->planes, &drmdev->n_planes);
    if (ok != 0) {
        goto fail_free_crtcs;
    }

    *drmdev_out = drmdev;

    return 0;


    fail_free_crtcs:
    free_crtcs(drmdev->crtcs, drmdev->n_crtcs);

    fail_free_encoders:
    free_encoders(drmdev->encoders, drmdev->n_encoders);

    fail_free_connectors:
    free_connectors(drmdev->connectors, drmdev->n_connectors);

    fail_free_plane_resources:
    drmModeFreePlaneResources(drmdev->plane_res);

    fail_free_resources:
    drmModeFreeResources(drmdev->res);

    fail_free_drmdev:
    free(drmdev);

    return ok;
}

int drmdev_new_from_path(
    struct drmdev **drmdev_out,
    const char *path
) {
    int ok, fd;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("[modesetting] Could not open DRM device. open");
        return errno;
    }

    ok = drmdev_new_from_fd(drmdev_out, fd);
    if (ok != 0) {
        close(fd);
        return ok;
    }

    return 0;
}

int drmdev_configure(
    struct drmdev *drmdev,
    uint32_t connector_id,
    uint32_t encoder_id,
    uint32_t crtc_id,
    const drmModeModeInfo *mode
) {
    struct drm_connector *connector;
    struct drm_encoder *encoder;
    struct drm_crtc *crtc;
    uint32_t mode_id;
    int ok;

    drmdev_lock(drmdev);

    for_each_connector_in_drmdev(drmdev, connector) {
        if (connector->connector->connector_id == connector_id) {
            break;
        }
    }

    if (connector == NULL) {
        drmdev_unlock(drmdev);
        return EINVAL;
    }

    for_each_encoder_in_drmdev(drmdev, encoder) {
        if (encoder->encoder->encoder_id == encoder_id) {
            break;
        }
    }

    if (encoder == NULL) {
        drmdev_unlock(drmdev);
        return EINVAL;
    }

    for_each_crtc_in_drmdev(drmdev, crtc) {
        if (crtc->crtc->crtc_id == crtc_id) {
            break;
        }
    }

    if (crtc == NULL) {
        drmdev_unlock(drmdev);
        return EINVAL;
    }

    ok = drmModeCreatePropertyBlob(drmdev->fd, mode, sizeof(*mode), &mode_id);
    if (ok < 0) {
        perror("[modesetting] Could not create property blob for DRM mode. drmModeCreatePropertyBlob");
        drmdev_unlock(drmdev);
        return errno;
    }

    if (drmdev->selected_mode != NULL) {
        ok = drmModeDestroyPropertyBlob(drmdev->fd, drmdev->selected_mode_blob_id);
        if (ok < 0) {
            ok = errno;
            perror("[modesetting] Could not destroy old DRM mode property blob. drmModeDestroyPropertyBlob");
            drmModeDestroyPropertyBlob(drmdev->fd, mode_id);
            drmdev_unlock(drmdev);
            return ok;
        }
    }

    drmdev->selected_connector = connector;
    drmdev->selected_encoder = encoder;
    drmdev->selected_crtc = crtc;
    drmdev->selected_mode = mode;
    drmdev->selected_mode_blob_id = mode_id;

    drmdev->is_configured = true;

    drmdev_unlock(drmdev);

    return 0;
}

int drmdev_new_atomic_req(
    struct drmdev *drmdev,
    struct drmdev_atomic_req **req_out
) {
    struct drmdev_atomic_req *req;
    int ok;

    req = calloc(1, sizeof *req);
    if (req == NULL) {
        return ENOMEM;
    }

    req->drmdev = drmdev;

    req->atomic_req = drmModeAtomicAlloc();
    if (req->atomic_req == NULL) {
        free(req);
        return ENOMEM;
    }

    *req_out = req;
    
    return 0;
}

void drmdev_destroy_atomic_req(
    struct drmdev_atomic_req *req
) {
    drmModeAtomicFree(req->atomic_req);
    free(req);
}

int drmdev_atomic_req_put_connector_property(
    struct drmdev_atomic_req *req,
    const char *name,
    uint64_t value
) {
    int ok;

    drmdev_lock(req->drmdev);

    for (int i = 0; i < req->drmdev->selected_connector->props->count_props; i++) {
        drmModePropertyRes *prop = req->drmdev->selected_connector->props_info[i];
        if (strcmp(prop->name, name) == 0) {
            ok = drmModeAtomicAddProperty(
                req->atomic_req,
                req->drmdev->selected_connector->connector->connector_id,
                prop->prop_id, value
            );
            if (ok < 0) {
                ok = errno;
                perror("[modesetting] Could not add connector property to atomic request. drmModeAtomicAddProperty");
                drmdev_unlock(req->drmdev);
                return ok;
            }

            drmdev_unlock(req->drmdev);
            return 0;
        }
    }

    drmdev_unlock(req->drmdev);
    return EINVAL;
}

int drmdev_atomic_req_put_crtc_property(
    struct drmdev_atomic_req *req,
    const char *name,
    uint64_t value
) {
    int ok;

    drmdev_lock(req->drmdev);

    for (int i = 0; i < req->drmdev->selected_crtc->props->count_props; i++) {
        drmModePropertyRes *prop = req->drmdev->selected_crtc->props_info[i];
        if (strcmp(prop->name, name) == 0) {
            ok = drmModeAtomicAddProperty(
                req->atomic_req,
                req->drmdev->selected_crtc->crtc->crtc_id,
                prop->prop_id,
                value
            );
            if (ok < 0) {
                ok = errno;
                perror("[modesetting] Could not add crtc property to atomic request. drmModeAtomicAddProperty");
                drmdev_unlock(req->drmdev);
                return ok;
            }
            
            drmdev_unlock(req->drmdev);
            return 0;
        }
    }

    drmdev_unlock(req->drmdev);
    return EINVAL;
}

int drmdev_atomic_req_put_plane_property(
    struct drmdev_atomic_req *req,
    uint32_t plane_id,
    const char *name,
    uint64_t value
) {
    struct drm_plane *plane;
    int ok;

    drmdev_lock(req->drmdev);

    plane = NULL;
    for (int i = 0; i < req->drmdev->n_planes; i++) {
        if (req->drmdev->planes[i].plane->plane_id == plane_id) {
            plane = req->drmdev->planes + i;
            break;
        }
    }

    if (plane == NULL) {
        drmdev_unlock(req->drmdev);
        return EINVAL;
    }

    for (int i = 0; i < plane->props->count_props; i++) {
        drmModePropertyRes *prop;
        
        prop = plane->props_info[i];
        
        if (strcmp(prop->name, name) == 0) {
            ok = drmModeAtomicAddProperty(
                req->atomic_req,
                plane_id,
                prop->prop_id,
                value
            );
            if (ok < 0) {
                ok = errno;
                perror("[modesetting] Could not add plane property to atomic request. drmModeAtomicAddProperty");
                drmdev_unlock(req->drmdev);
                return ok;
            }
            
            drmdev_unlock(req->drmdev);
            return 0;
        }
    }

    drmdev_unlock(req->drmdev);
    return EINVAL;
}

int drmdev_atomic_req_put_modeset_props(
    struct drmdev_atomic_req *req,
    uint32_t *flags
) {
    struct drmdev_atomic_req *augment;
    int ok;

    ok = drmdev_new_atomic_req(req->drmdev, &augment);
    if (ok != 0) {
        return ok;
    }

    ok = drmdev_atomic_req_put_connector_property(req, "CRTC_ID", req->drmdev->selected_crtc->crtc->crtc_id);
    if (ok != 0) {
        drmdev_destroy_atomic_req(augment);
        return ok;
    }

    ok = drmdev_atomic_req_put_crtc_property(req, "MODE_ID", req->drmdev->selected_mode_blob_id);
    if (ok != 0) {
        drmdev_destroy_atomic_req(augment);
        return ok;
    }

    ok = drmdev_atomic_req_put_crtc_property(req, "ACTIVE", 1);
    if (ok != 0) {
        drmdev_destroy_atomic_req(augment);
        return ok;
    }

    ok = drmModeAtomicMerge(req->atomic_req, augment->atomic_req);
    if (ok < 0) {
        ok = errno;
        perror("[modesetting] Could not apply modesetting properties to atomic request. drmModeAtomicMerge");
        drmdev_destroy_atomic_req(augment);
        return ok;
    }

    drmdev_destroy_atomic_req(augment);

    if (flags != NULL) {
        *flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
    }

    return 0;
}

int drmdev_atomic_req_commit(
    struct drmdev_atomic_req *req,
    uint32_t flags,
    void *userdata
) {
    int ok;

    drmdev_lock(req->drmdev);

    ok = drmModeAtomicCommit(req->drmdev->fd, req->atomic_req, flags, userdata);
    if (ok < 0) {
        ok = errno;
        perror("[modesetting] Could not commit atomic request. drmModeAtomicCommit");
        drmdev_unlock(req->drmdev);
        return ok;
    }

    drmdev_unlock(req->drmdev);
    return 0;
}